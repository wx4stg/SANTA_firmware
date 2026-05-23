import serial
import RPi.GPIO as GPIO
from time import sleep
import datetime
from datetime import UTC
import os.path
from os import system
import threading
import atexit

pin_relay_a = 5
pin_relay_b = 6
pin_relay_c = 13
pin_LED = 19
pin_overflow_buffer = 10
pin_overflow_serial = 11
use_relay = 'b'
mins_before_write = 1
save_path ='/home/pi/Desktop/DATA/'
write_success = 0
bytes_before_write = 5400000*mins_before_write
SERIAL_SPEED = 2000000
cpu_id = ''
with open('/proc/cpuinfo', 'r') as f:
    cpu_id = f.readlines()[-2].replace('\n', '')[-8:]

def exit_handler():
    global pin_relay_a
    global pin_relay_b
    global pin_relay_c
    global pin_LED
    GPIO.output(pin_relay_a, GPIO.LOW)
    GPIO.output(pin_relay_b, GPIO.LOW)
    GPIO.output(pin_relay_c, GPIO.LOW)
    GPIO.output(pin_LED, GPIO.LOW)
    GPIO.cleanup()

atexit.register(exit_handler)


def write_file(start_time, bytes_data):
    global save_path
    global cpu_id
    global use_relay
    global write_success
    last_gps = 'NO_FIX_2Donly_NaT'
    if os.path.exists('/home/pi/Desktop/last_gps.txt'):
        with open('/home/pi/Desktop/last_gps.txt', 'r') as f:
            last_gps = f.read()
        last_gps_split = last_gps.split('_')
        try:
            last_gps_time_offset = (datetime.datetime.now(UTC) - datetime.datetime.strptime(last_gps_split[-1], '%Y-%m-%dT%H:%M:%S').replace(tzinfo=UTC)).total_seconds()
        except ValueError:
            last_gps_time_offset = 0
        last_gps_split[-1] = f'{last_gps_time_offset:.2f}'
        last_gps = '_'.join(last_gps_split)
    name = os.path.join(save_path, f'{start_time.strftime("%Y%m%d_%H%M%S_%f")}_{last_gps}_{cpu_id}_{use_relay}.raw')
    try:
        with open(name, mode='wb') as file:
            file.write(bytes_data)
    except OSError as e:
        if 'No space left on device' in str(e):
            system('sudo shutdown -h now')
        else:
            raise e
    write_success += 1
    if write_success == 5:
        GPIO.output(pin_LED, GPIO.LOW)
    print(f'[{datetime.datetime.now(UTC).strftime("%Y-%m-%d %H:%M:%S.%f")}] Data collect wrote file: {name}')


def do_run():
    global write_success
    global pin_LED_status
    start_time = datetime.datetime.now(UTC)
    print(f'[{start_time.strftime("%Y-%m-%d %H:%M:%S.%f")}] data_collect do_run()!')
    try:
        ser = serial.Serial('/dev/ttyACM0', SERIAL_SPEED, timeout=1)
    except Exception as e:
        # Feather has disconnected from the pi
        # This is usually water intrusion, so shut down the system to prevent damage
        if 'No such file or directory' in str(e):
            system('sudo shutdown -h now')
    byte_count_since_last_write = 0
    bytes_data = bytearray()
    ser.flush()
    while True:
        bytes_available = ser.in_waiting
        s = ser.read(bytes_available)
        bytes_data += s
        if (write_success > 0) and (write_success <= 5):
            if pin_LED_status == 1000:
                GPIO.output(pin_LED, GPIO.LOW)
                pin_LED_status = -1
            elif pin_LED_status == 500:
                GPIO.output(pin_LED, GPIO.HIGH)
            pin_LED_status += 1
        if (byte_count_since_last_write >= bytes_before_write): #27000000):
            threading.Thread(target=write_file, args=(start_time, bytes_data)).start()
            bytes_data = bytearray()
            byte_count_since_last_write = 0
            print(f'[{datetime.datetime.now(UTC).strftime("%Y-%m-%d %H:%M:%S.%f")}] Bytes in input buffer: {ser.in_waiting}')
            start_time = datetime.datetime.now(UTC)
        byte_count_since_last_write += bytes_available


if __name__ == "__main__":
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(pin_relay_a, GPIO.OUT)
    GPIO.setup(pin_relay_b, GPIO.OUT)
    GPIO.setup(pin_relay_c, GPIO.OUT)
    GPIO.setup(pin_LED, GPIO.OUT)

    GPIO.output(pin_relay_a, GPIO.HIGH) if use_relay == 'a' else GPIO.output(pin_relay_a, GPIO.LOW)
    GPIO.output(pin_relay_b, GPIO.HIGH) if use_relay == 'b' else GPIO.output(pin_relay_b, GPIO.LOW)
    GPIO.output(pin_relay_c, GPIO.HIGH) if use_relay == 'c' else GPIO.output(pin_relay_c, GPIO.LOW)
    GPIO.output(pin_LED, GPIO.LOW)
    pin_LED_status = 0

    sleep(2)
    do_run()

