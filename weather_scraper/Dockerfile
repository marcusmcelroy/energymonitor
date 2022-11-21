FROM arm64v8/python:3.10-slim-bullseye

RUN pip3 --no-cache-dir install influxdb_client pandas feedparser lxml

COPY ./scrape_dtgweather.py /app/scrape_dtgweather.py

