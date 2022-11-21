# Firebeetle ESP32 energy monitor

## Overview

Uses a DFRobot Firebeetle ESP32 in conjunction with an inexpensive photo-diode based sensor to count pulses on a consumer electricity/ gas meter. Uses the ULP co-processor to count pulses with very low power consumption, which enables the Firebeetle to run for long periods on 18650 Li-Ion batteries.

The ESP32 application periodically (default 1m) sends pulse counts via mqtt over wifi. 

Data collection and analysis is provided by a docker stack comprising mosquitto, telegraf, influxdb2 and grafana. For me this runs on a raspberrypi4. 

ESP32 code is platformio, and is heavilly based on these ESP32 IDF examples..

- https://github.com/espressif/esp-idf/tree/master/examples/protocols/mqtt/tcp
- https://github.com/espressif/esp-idf/tree/master/examples/wifi/getting_started
- https://github.com/espressif/esp-idf/tree/master/examples/system/ulp_fsm/ulp

## Usage

### Setup influxdb buckets:

> docker compose up -d influxdb

visit http://127.0.0.1:8086 and setup initial username, password, organisation, bucket, etc. 

Suggest you use "energy" as the initial bucket name. If you want to use the weather scraper, then you'll need to setup another bucket to store weather data. Suggest you call this one "weather"

Create a new API token, with read/ write access to the buckets you created above.

You'll need to provide organisation, bucket and token when you configure telegraf.

> docker compose down

### Setup mosquitto authentication:

> docker compose up -d mosquitto

> docker exec -it mosquitto sh

> mosquitto_passwd -c /mosquitto/data/pwfile <username>
> mosquitto_passwd -b /mosquitto/data/pwfile <username> <password>
>exit

You'll need to provide a mosquitto username and password use when configuring telegraf.

> docker compose down

### Setup telegraf:

telegraf requires authenticated access to both mosquitto and influxdb2

In .env, update the MQTT_USER and MQTT_PASS variables with a username/ password you setup above. 
Optionally set the client ID that telegraf uses with the MQTT_CLID variable.
Optionally set the MQTT topic that telegraf subscribes to using the MQTT_SUB_TOPIC variable. If you're publishing to MQTT with the esp32, you'll want to set this variable to ensure telegraf subscribes to events being published by the ESP32 program (configured with MQTT_PUB_TOPIC in config.h)

Similarly, provide details about the infuxdb2 in INFLUXDB_V2_* variables in .env. Set INFLUXDB_V2_BUCKET_ENERGY to the name of the bucket that contains your energy data (energy if you followed my suggestions above).

## Setup weather scraper (python):

By default, this uses the same influxdb2 credentials as telegraf (as set by INFLUXDB_V2_*). 
You need to configure the bucket used to store weather data using the INFLUXDB_V2_BUCKET_ENERGY. This is weather if you followed the suggestions above.

### Setup firebeetle esp32:

Edit config.h, complete WIFI_* and MQTT_* as required. Build, upload to firebeetle. 

### Setup grafana:

#### Bring up the complete docker stack:

> docker compose up -d

visit grafana at http://127.0.0.1:3000

#### Provision grafana

Add a datasource (InfluxDB, with Query Language = Flux, with url of http://influxdb:8086, add organization, token, bucket details)

Create a dashboard using flux queries

TODO - Grafana provisioning
