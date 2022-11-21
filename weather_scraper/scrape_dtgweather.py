#!/usr/bin/env python
import feedparser
import re
import pandas as pd
from datetime import datetime 

from influxdb_client import InfluxDBClient
from influxdb_client.client.write_api import SYNCHRONOUS

d = feedparser.parse('http://www.cl.cam.ac.uk/research/dtg/weather/rss.xml',
                     request_headers={'Cache-control': 'max-age=0'});
wlist = d['entries'][0]['description']

df = pd.read_html(wlist)[0]
df.columns = ['measurement', 'value_unit']
time = df.loc[df['measurement'] == "Time"].value_unit[1]
df = df.loc[df['measurement'] != "Time"].copy()
df['measurement'] = df['measurement'].str.replace(' ','_');
df['measurement'] = df['measurement'].str.lower();
df[['value','unit']] = df['value_unit'].str.split(' ',expand=True)
df["value"] = pd.to_numeric(df["value"])

measurements_dict = dict(zip(df.measurement,df.value))

date_time_obj = datetime.strptime(time, '%Y-%m-%d %H:%M:%S').timestamp()

measurement_time = int(date_time_obj * 1000000000)

tags_dict = {
  "location": "cambridge"
}

json_body = [
        {
          "measurement": "weather",
          "tags": tags_dict,
          "time": measurement_time,
          "fields": measurements_dict
         }
       ]

bucket = "weather"

client = InfluxDBClient.from_env_properties()

write_api = client.write_api(write_options=SYNCHRONOUS)
write_api.write(bucket=bucket, record=json_body)

client.close()

print("Wrote record with timestamp ",time)
