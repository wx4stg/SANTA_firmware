from collections import deque
from datetime import datetime as dt, UTC
from time import sleep
import csv

import board
from adafruit_bme280 import basic as adafruit_bme280

sampling_time = 15
duration_to_collect = 86400

if __name__ == '__main__':
    i2c = board.I2C()
    bme280 = adafruit_bme280.Adafruit_BME280_I2C(i2c, address=118)
    buffer_len = int(duration_to_collect/sampling_time)
    dt_buf = deque(maxlen=buffer_len)
    temp_buf = deque(maxlen=buffer_len)
    rh_buf = deque(maxlen=buffer_len)
    pressure_buf = deque(maxlen=buffer_len)
    while True:
        dt_buf.append(dt.now(UTC))
        temp_buf.append(bme280.temperature)
        rh_buf.append(bme280.humidity)
        pressure_buf.append(bme280.pressure)

        with open('temp_data.csv', 'w', newline='') as csvfile:
            writer = csv.writer(csvfile)
            writer.writerow(['dt', 'temp', 'rh', 'pressure'])
            for t, temp, rh, p in zip(dt_buf, temp_buf, rh_buf, pressure_buf):
                writer.writerow([t.isoformat(), temp, rh, p])

        sleep(sampling_time)
