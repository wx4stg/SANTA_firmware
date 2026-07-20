#include <SPI.h>
#include <CircularBuffer.hpp>
#include <Adafruit_GPS.h>
#include <time.h>

typedef struct
{
  byte sb;
  byte adc_b1;
  byte adc_b2;
  byte adc_b3;
  uint32_t adc_pps_time;
  byte eb;
} datapacket;

CircularBuffer<datapacket, 10000> datapackets;

const uint8_t PIN_ADC_INT = 10;
const uint8_t PIN_ADC_CS = 5;
const uint8_t PIN_BUFFER_FULL = A2;
const uint8_t PIN_SERIAL_FULL = A3;
const uint8_t PIN_GPS_PPS = 6;
const uint8_t PIN_ONBOARD_LED = 13;
volatile bool GPS_PPS;
volatile uint64_t epoch = 0;
uint8_t thing_to_transmit[29]; // 8 bytes epoch, 4 bytes lat, 4 bytes lon, 4 bytes alt, 1 byte use_relay, 8 bytes cpu_id
volatile int32_t lat = 0.0;
volatile int32_t lon = 0.0;
volatile float alt = 0.0;
volatile uint8_t use_relay = 0;
volatile uint64_t cpu_id = 0;
volatile uint16_t packets_since_last_pps = 0;
volatile uint8_t nmea_transmit_bit = 200;
volatile bool config_received = false;
byte b1;
byte b2;
byte b3;
Adafruit_GPS GPS(&Serial1);

void setAllRegisters()
{
  // Sets all registers to what we think we need
  digitalWrite(PIN_ADC_CS, LOW);
  delay(5);
  SPI.transfer(0x46); // incremental write starting at 0x01
  SPI.transfer(0b01000011); // CONFIG0
  //SPI.transfer(0b00000000); // CONFIG1 for 38.4kHZ
  //SPI.transfer(0b00000100); // CONFIG1 for 19.2kHz
  SPI.transfer(0b00001000); // CONFIG1 for 9.6kHz
  SPI.transfer(0b10001011); // CONFIG2
  SPI.transfer(0b11000000); // CONFIG3
  SPI.transfer(0b01110011); // IRQ
  SPI.transfer(0b00000001); // MUX
  SPI.transfer(0x00); SPI.transfer(0x00); SPI.transfer(0x00); // SCAN
  SPI.transfer(0x00); SPI.transfer(0x00); SPI.transfer(0x00); // TIMER
  SPI.transfer(0x00); SPI.transfer(0x00); SPI.transfer(0x00); // OFFSETCAL
  SPI.transfer(0x80); SPI.transfer(0x00); SPI.transfer(0x00); // GAINCAL
  SPI.transfer(0x90); SPI.transfer(0x00); SPI.transfer(0x00); // RESERVED
  SPI.transfer(0x50); // RESERVED
  SPI.transfer(0xA5); // LOCK
  SPI.transfer(0x000F); // RESERVED
  delay(5);
  digitalWrite(PIN_ADC_CS, HIGH);
}

void setup()
{
  Serial.begin(2000000);
  GPS.begin(9600);
  SPI.begin();
  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  pinMode(PIN_ADC_CS, OUTPUT);
  pinMode(PIN_BUFFER_FULL, OUTPUT);
  pinMode(PIN_SERIAL_FULL, OUTPUT);
  pinMode(PIN_ADC_INT, INPUT_PULLUP);
  pinMode(PIN_GPS_PPS, INPUT);
  pinMode(PIN_ONBOARD_LED, OUTPUT);
  digitalWrite(PIN_ADC_CS, HIGH);
  digitalWrite(PIN_BUFFER_FULL, LOW);
  digitalWrite(PIN_SERIAL_FULL, LOW);
  digitalWrite(PIN_ONBOARD_LED, LOW);
  
  delay(100);
  setAllRegisters();
  delay(100);
  attachInterrupt(digitalPinToInterrupt(PIN_ADC_INT), adcisr, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), gpsPpsChg, CHANGE);
}

void gpsPpsChg() {
  // set global flag when GPS PPS is active
  if (digitalRead(PIN_GPS_PPS) == HIGH) {
    GPS_PPS = true;
    if (config_received) {
      digitalWrite(PIN_ONBOARD_LED, HIGH);
    }
  } else {
    GPS_PPS = false;
    digitalWrite(PIN_ONBOARD_LED, LOW);
  }
}

void adcisr()
{
  uint32_t adcus = micros();
  digitalWrite(PIN_ADC_CS, LOW);
  SPI.transfer(0x41); // Read ADC DATA
  b1 = SPI.transfer(0x00);
  b2 = SPI.transfer(0x00);
  b3 = SPI.transfer(0x00);
  b3 = bitWrite(b3, 0, GPS_PPS); // Set the LSB of b3 to GPS_PPS
  if (GPS_PPS) {
    packets_since_last_pps = 0; // Reset the counter on PPS
  } else {
    if (packets_since_last_pps < (sizeof(thing_to_transmit)*8 + 10 + 1)) { // Only transmit if we haven't transmitted all bits yet
      if (packets_since_last_pps == 0) {
        uint64_t tx_epoch = epoch+1; // Update the transmit epoch if we haven't seen a PPS in a while. Add 1 second because the PPS will have already passed.
        int32_t tx_lat = lat;
        int32_t tx_lon = lon;
        float tx_alt = alt;
        int8_t tx_use_relay = use_relay;
        uint64_t tx_cpu_id = cpu_id;
        memcpy(thing_to_transmit, &tx_epoch, sizeof(tx_epoch));
        memcpy(thing_to_transmit+8, &tx_lat, sizeof(tx_lat));
        memcpy(thing_to_transmit+12, &tx_lon, sizeof(tx_lon));
        memcpy(thing_to_transmit+16, &tx_alt, sizeof(tx_alt));
        memcpy(thing_to_transmit+20, &tx_use_relay, sizeof(tx_use_relay));
        memcpy(thing_to_transmit+21, &tx_cpu_id, sizeof(tx_cpu_id));
        nmea_transmit_bit = 0; // Reset the bit index for transmitting epoch
      }
      if (packets_since_last_pps > 10) {
        // it's probably safe to start transmitting NMEA now
        if (nmea_transmit_bit < sizeof(thing_to_transmit) * 8) {
          uint8_t nmea_byte_idx = nmea_transmit_bit / 8;
          uint8_t nmea_bit_idx = 7 - (nmea_transmit_bit % 8); // Transmit MSB first
          b3 = bitWrite(b3, 0, bitRead(thing_to_transmit[nmea_byte_idx], nmea_bit_idx)); // Set the LSB of b3 to the next bit of epoch
          nmea_transmit_bit++;
        } else {
          // Reset after transmitting all bits
          epoch = 0; 
          lat = 0.0;
          lon = 0.0;
          alt = 0.0;
          thing_to_transmit[0] = 0;
        }
      }
      
    }
    packets_since_last_pps++;
  }
  datapackets.push(datapacket{0xBE, b1, b2, b3, adcus, 0xEF});
  digitalWrite(PIN_ADC_CS, HIGH);
}

void loop()
{
  if ((config_received) && (!datapackets.isEmpty()) && (Serial.availableForWrite()> 10))
  {
    // We have things to write and the place to write them
    datapacket dp = datapackets.pop();
    Serial.write((byte*)&dp.sb, 1);
    Serial.write((byte*)&dp.adc_b1, 1);
    Serial.write((byte*)&dp.adc_b2, 1);
    Serial.write((byte*)&dp.adc_b3, 1);
    Serial.write((byte*)&dp.adc_pps_time, 4);
    Serial.write((byte*)&dp.eb, 1);
  }

  if (datapackets.isFull())
  {
    digitalWrite(PIN_BUFFER_FULL, HIGH);
  }
  if (Serial.availableForWrite() < 10)
  {
    digitalWrite(PIN_SERIAL_FULL, HIGH);
  }
  while (Serial1.available()) GPS.read();
  if (GPS.newNMEAreceived()) {
    if (GPS.parse(GPS.lastNMEA()) && GPS.fix) {
      struct tm timeinfo = {};
      timeinfo.tm_year = GPS.year + 100; // tm_year is years since 1900
      timeinfo.tm_mon = GPS.month - 1; // tm_mon is 0-11
      timeinfo.tm_mday = GPS.day;
      timeinfo.tm_hour = GPS.hour;
      timeinfo.tm_min = GPS.minute;
      timeinfo.tm_sec = GPS.seconds;
      timeinfo.tm_isdst = 0;
      uint64_t gps_epoch = mktime(&timeinfo); // calculate this before storing to minimize interrupt disabling time
      noInterrupts();
      epoch = gps_epoch;
      lat = GPS.latitude_fixed;
      lon = GPS.longitude_fixed;
      alt = GPS.altitude;
      interrupts();
    }
  }
  uint8_t rx_from_pi_buffer[9];
  if (Serial.available() >= 9) {
    // the first byte is the relay in use, the next 4 bytes are the cpu_id
    Serial.readBytes((char*)rx_from_pi_buffer, 9);
    use_relay = rx_from_pi_buffer[0];
    uint64_t rx_cpu_id = 0;
    memcpy(&rx_cpu_id, rx_from_pi_buffer+1, sizeof(rx_cpu_id));
    cpu_id = rx_cpu_id;
    config_received = true;
  };
}
