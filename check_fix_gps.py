#!/usr/bin/env python3
# Monitor the GPS and time sync of the slow antenna
# Created 25 January 2024 by Sam Gardner <samuel.gardner@ttu.edu>

from time import sleep
import datetime
from datetime import UTC
import RPi.GPIO as GPIO
import subprocess
import gpsd
import atexit
from os import remove, rename, chmod, path

def exit_handler():
    GPIO.output(26, GPIO.LOW)
    GPIO.cleanup()

atexit.register(exit_handler)

# We need to wait a few seconds after a fresh boot for gpsd to figure its life out
sleep(6)
current_time = datetime.datetime.now(UTC).strftime('%Y-%m-%d %H:%M:%S.%f')
print(f'[{current_time}] Starting scheduled GPS fix...')


def write_gps_atomically(gps_str):
    if path.exists('/home/pi/Desktop/this_gps.txt'):
        remove('/home/pi/Desktop/this_gps.txt')
    with open('/home/pi/Desktop/this_gps.txt', 'w') as f:
        f.write(gps_str)
    chmod('/home/pi/Desktop/this_gps.txt', 0o666)
    rename('/home/pi/Desktop/this_gps.txt', '/home/pi/Desktop/last_gps.txt')

write_gps_atomically('NO_FIX_2Donly_NaT')

GPIO.setmode(GPIO.BCM)
GPIO.setup(26, GPIO.OUT)
GPIO.output(26, GPIO.LOW)

# Connect to gpsd instance, init variables, turn off status LED during check
gpsd.connect()
last_sentence = None
alt = '2Donly'

# Define some commands that we'll run later
chrony_cmd = ['chronyc', 'sources']
check_test_cmd = ['systemctl', 'status', 'adc_test_startup']
check_data_cmd = ['systemctl', 'status', 'adc_data_collect']
start_data_cmd = ['systemctl', 'start', 'adc_data_collect']

# Loop indefinitely
while True:
    current_time = datetime.datetime.now(UTC).strftime('%Y-%m-%d %H:%M:%S.%f')
    # Get latest NMEA sentence from gpsd
    sentence = gpsd.get_current()
    # Sometimes there can be sentences with empty time data -- filter these out:
    if sentence.time == '':
        continue
    # If the last sentence exists, check to see if the sentence we just grabbed matches the previous
    if last_sentence is not None:
        if last_sentence.time == sentence.time:
            # If the time of the last sentence as over one minute ago, flash the GPS status light at once per 1/4 sec
            if (datetime.datetime.now(UTC) - datetime.datetime.strptime(sentence.time[:-5], '%Y-%m-%dT%H:%M:%S').replace(tzinfo=UTC)).total_seconds() >= 120:
                GPIO.output(26, not GPIO.input(26))
                alt = '2Donly'
            # Wait 1/4 sec before trying again for a new packet
            sleep(0.25)
            continue
    last_sentence = sentence
    # If this is is a new packet, make sure it's not a 'NO FIX packet'
    if sentence.mode >= 2:
        # Grab the longitude and latitude (and altitude, if available)
        lon = sentence.lon
        lat = sentence.lat
        if sentence.mode == 3:
            alt = sentence.alt
        # Grab the received time and convert to a python datetime object
        sentence_time_str = sentence.time[:-5]
        sentence_time = datetime.datetime.strptime(sentence_time_str, '%Y-%m-%dT%H:%M:%S').replace(tzinfo=UTC)
        current_time = datetime.datetime.now(UTC).strftime('%Y-%m-%d %H:%M:%S.%f')
        print(f'[{current_time}] GPS packet: {lat}, {lon}, {alt}, {sentence_time_str}, updating clock')
        # This file needs to be written atomically since data_collect might be reading it at the same time
        # See https://github.com/wx4stg/Bruning_Slow_Antenna_Software/issues/3 for more details
        write_gps_atomically(f'{lat:.6f}_{lon:.6f}_{alt}_{sentence_time_str}')
        # Check to make sure chrony is using PPS as a source for time updates
        while True:
            chrony_task = subprocess.Popen(chrony_cmd, stdout=subprocess.PIPE)
            chrony_task.wait()
            chrony_out = [l for l in chrony_task.stdout.read().decode('utf-8').split('\n') if 'PPS' in l][0]
            if chrony_out.startswith('#*'):
                break
            else:
                GPIO.output(26, not GPIO.input(26))
                sleep(1)
        # Now check to see if adc_data_collect needs to be started.
        if subprocess.run(check_data_cmd, stdout=subprocess.DEVNULL).returncode != 0:
            # Non-zero exit, adc_data_collect needs to be started
            while True:
                # Since it takes a moment to grab the info from the GPS packet, the system clock should always be AHEAD of the received packet time
                # ...unless the system just rebooted and the time hasn't updated yet, in which case, we want to wait for the system clock to update before
                # starting data collect. This makes sure that the system clock is up to date before proceeding.
                if (sentence_time - datetime.datetime.now(UTC)).total_seconds() <= 10:
                    current_time = datetime.datetime.now(UTC).strftime('%Y-%m-%d %H:%M:%S.%f')
                    print(f'[{current_time}] Clock is reasonable, proceeding')
                    while True:
                        if subprocess.run(check_test_cmd, stdout=subprocess.DEVNULL).returncode != 0:
                            current_time = datetime.datetime.now(UTC).strftime('%Y-%m-%d %H:%M:%S.%f')
                            print(f'[{current_time}] ADC test startup not active, starting data collect!')
                            subprocess.run(start_data_cmd, stdout=subprocess.DEVNULL)
                            GPIO.output(26, GPIO.HIGH)
                            break
                        else:
                            current_time = datetime.datetime.now(UTC).strftime('%Y-%m-%d %H:%M:%S.%f')
                            print(f'[{current_time}] ADC test startup active, waiting')
                            sleep(1)
                            continue
                    break
                else:
                    current_time = datetime.datetime.now(UTC).strftime('%Y-%m-%d %H:%M:%S.%f')
                    print(f'[{current_time}] GPS is ahead of system clock by {(sentence_time - datetime.datetime.now(UTC)).total_seconds()} s')
                    sleep(0.25)
        else:
            # ADC data collect already running
            GPIO.output(26, GPIO.HIGH)
    sleep(5)
