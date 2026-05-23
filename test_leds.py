import RPi.GPIO as GPIO
from time import sleep

PIN_BLUE = 19
PIN_WHITE = 26

if __name__ == '__main__':
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(PIN_BLUE, GPIO.OUT)
    GPIO.setup(PIN_WHITE, GPIO.OUT)

    while True:
        GPIO.output(PIN_BLUE, GPIO.HIGH)
        GPIO.output(PIN_WHITE, GPIO.LOW)
        sleep(0.5)
        GPIO.output(PIN_BLUE, GPIO.LOW)
        GPIO.output(PIN_WHITE, GPIO.HIGH)
        sleep(1)