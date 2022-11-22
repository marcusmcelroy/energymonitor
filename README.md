# Firebeetle ESP32 energy monitor

## Overview

There are 2 parts to this project..

1) ESP32 application - counts LED pulses on consumer electricity/ gas meter and sends these periodically over wifi via mqtt.
2) Docker stack - runs mqtt broker (mosquitto), collection agent (telegraf), time series database (influxdb2) and data visualisation (grafana).

### ESP32 application

Uses an ESP32, in my case a [DFRobot Firebeetle ESP32](https://www.dfrobot.com/product-1590.html), in conjunction with an [inexpensive photodiode-based sensor](https://www.amazon.co.uk/sourcing-map-Photosensitive-Detection-Photodiode/dp/B07WLTG1KL) to count pulses on a consumer electricity/ gas meter. 

In my case, the sensor is attached to a Landis + Gyr E470 Type 5424, which pulses its integrated LED for 20ms for every 1Wh. 

I modified the sensor slightly to reduce power (removed the LEDs) and make it easier to stick to the meter (reversed the photodiode on the pcb).

ESP32 code uses the ULP co-processor to count pulses with very low power consumption, which enables the Firebeetle to run for long periods on 18650 Li-Ion batteries. The application periodically (default 1m) sends pulse counts via mqtt over wifi. 

Current whilst counting pulses is just less than 1mA, whilst average current over a 1m period, including the connect to wifi/ publish mqtt routine, is ~2.5mA. There is undoubtebly a lower power sensor for this purpose- I simply went for cheap and convenient.

TODO: Insert current waveform

ESP32 code is developed in vscode using platformio plugin, and is heavilly based on the [mqtt](https://github.com/espressif/esp-idf/tree/master/examples/protocols/mqtt/tcp), [wifi](https://github.com/espressif/esp-idf/tree/master/examples/wifi/getting_started) and [ulp_fsm](https://github.com/espressif/esp-idf/tree/master/examples/system/ulp_fsm/ulp) [ESP32 IDF](https://github.com/espressif/esp-idf) [examples](https://github.com/espressif/esp-idf/tree/master/examples)..

### Docker stack

TODO: links

The docker stack is there to consume data produced by the ESP32. It stores the time-series sensor data (pulse counts) delivered by mqtt to mosquitto, via the telegraf collection agent into an influxdb2 database .

Data visualisation is using Grafana. At this time, Grafana must be manually configured to connect to the influxdb2 database, perform queries in flux, and visualise the data.

Also included is a python-based scraper which periodically scrapes weather conditions from a nearby (to me) [weather station hosted by University of Cambridge](https://www.cl.cam.ac.uk/research/dtg/weather), storing measurements into influxdb2. Implementation is ugly, but it does the job.

For me this docker stack runs using compose on a raspberrypi4.

## Configuration

Both ESP32 application and docker stack can be (at least partially) configured using variables.

### ESP32 configuration

ESP32 application is configured by modifying the #define entries in the `esp32/include/config.h` file.

| #define     | Default | Description |
| ----------- | ------- |----------- |
| `WIFI_SSID` | `myssid` | SSID of the Wifi network ESP32 connects to before publishing counter using mqtt |
| `WIFI_PASS` | `mypassword` | Password of Wifi network |
| `MQTT_URL` | `mqtt://mqtt.eclipseprojects.io:1883` | URL of the MQTT broker that the ESP32 will publish to. This will most likely need to be the IP address of the server running the docker stack. |
| `MQTT_USER` | `mqttuser` | Username that ESP32 uses to authenticate with MQTT broker |
| `MQTT_PASS` | `mqttpassword` | Password that ESP32 uses to authenticate with MQTT broker |
| `MQTT_CLID` | `mqttclientid` | ClientID of the ESP32 |
| `MQTT_PUB_TOPIC` | `/home/energy/electric` | The topic that the ESP32 publishes with. Shouldn't need to change this. | 
| `TIME_TO_SLEEP` | `58` | Time, in seconds, between each publish |

### Docker stack configuration

Docker stack is configured by modifying envionment files in the `.env` file.

| Environment Variable | Default | Description |
| ------------ | ------- | ----------- |
| `INFLUXDB_V2_URL` | `http://influxdb:8086` | URL that telegraf and weather scraper connect to influxdb on. Shouldn't need to change this. |
| `INFLUXDB_V2_ORG` | `my_org` | influxdb organisation used in connections to influxdb |
| `INFLUXDB_V2_TOKEN` | `my_token` | influxdb API token used in connections to influxdb |
| `INFLUXDB_V2_BUCKET_ENERGY` | `sensors` | name of influxdb bucket used to store pulse counts |
| `INFLUXDB_V2_BUCKET_WEATHER` | `weather` | name of influxdb bucket used to store weather meaurements |
| `MQTT_URL` | `tcp://mosquitto:1883` | URL that telegraf uses to connect to mosquitto broker. Shouldn't need to change this. |
| `MQTT_USER` | `mqttuser` | Username that telegraf uses to authenticate with MQTT broker
| `MQTT_PASS` | `mqttpassword` | Password that telegraf uses to authenticate with MQTT broker
| `MQTT_CLID` | `mqttclientid` | ClientID of telegraf |
| `MQTT_SUB_TOPIC` | `home/energy/#` | The topic(s) that telegraf subscribes to, and outputs to influxdb2. Shouldn't need to change this. | 

## Recommended Usage

1) Setup the docker stack and bring up the services therein
2) Configure and build the ESP32 application
3) Log some measurements
4) Write some queries to get useful things out of influxdb2
5) Make pretty visualisations in Grafana

## Setup docker stack

### Prerequisits

A server accessible on your network, with physical or remote access, with docker and compose installed. There are loads of guides about how to install docker. Personally I go for...

    curl -fsSL https://get.docker.com -o get-docker.sh
    sh get-docker.sh

Alternatively visit [Get Docker](https://docs.docker.com/get-docker/) for more info.

### Setup influxdb

Bring up the influxdb service..

    docker compose up -d influxdb

Visit `http://<ip of server running docker>:8086` and setup initial username, password, organisation, bucket, etc. 

Suggest you use *energy* as the initial bucket name. If you want to use the weather scraper, then you'll need to setup another bucket to store weather data. Suggest you call this one *weather*

Create a new API token, with at least read/ write access to the buckets you created above.

Once you've done this, you can bring down the influxdb service for now..

    docker compose down
    
Note you'll need to provide the details of your influxdb configuration when you configure telegraf, grafana and the weather scraper below.

### Setup mosquitto authentication

Bring up the mosquitto service..
    
    docker compose up -d mosquitto

Run an interactive shell in the mosquitto container and create username/ password for clients to authenticate with..    
    
    docker exec -it mosquitto sh
    mosquitto_passwd -c /mosquitto/data/pwfile <username>

Optionally add more users with..
    
    mosquitto_passwd -b /mosquitto/data/pwfile <username> <password>

..before leaving the interactive shell..
    
    exit

Then you can bring down the mosquitto service for now..

    docker compose down

Note you'll need to provide a mosquitto username and password when configuring telegraf below.

### Setup telegraf

telegraf requires authenticated access to both mosquitto and influxdb2.

Open up `.env` in your favorite editor and update the `MQTT_*` and `INFLUXDB_V2_*` variables to match what you configured, or obtained above. 

### Setup weather scraper (python)

By default, this uses the same influxdb2 credentials as telegraf (as set by `INFLUXDB_V2_*`). You need to configure the bucket used to store weather data using the `INFLUXDB_V2_BUCKET_ENERGY`. This is `weather` if you followed the suggestions above.

### Bring up the configured docker stack

Now that you've configured all of the services, you can bring up the full docker stack with..

    docker compose up -d

If you issue..

    docker ps

..you should see 4 containers running. Now's the time to debug if any of them don't work.

### Setup grafana

At present, grafana configuration is manual, but it's pretty simple.

Open up grafana on `http://<ip of server running docker>:3000` and set a username/ password of your choosing.

Add an InfluxDB datasource with url `http://influxdb:8086`, set the query language to flux, and add yout organisation, token, default bucket details.

Create a dashboard, add a panel, and configure the panel to use an influxdb query. If you stuck with the defaults and if you have some data in the influx database, you could use a query like..

    from(bucket: "sensors")
        |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
        |> filter(fn: (r) => r["topic"] == "home/energy/electric")
        |> filter(fn: (r) => r["_field"] == "value")
        |> aggregateWindow(every: v.windowPeriod, fn: sum, createEmpty: false)
        |> yield(name: "electricityinwh")

..to show some of your readings.

## Setup ESP32

### Prerequisits

vscode, with platformIO extension.

### Configure, build and upload ESP32 application

Edit `config.h`, set `WIFI_*` and `MQTT_*` as required. Build, upload to firebeetle. 

