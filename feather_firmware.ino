#include <SPI.h>
#include <CircularBuffer.h>

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
const uint8_t PIN_ONBOARD_LED = 13;
volatile bool GPS_PPS;
byte b1;
byte b2;
byte b3;

void setAllRegisters()
{
  // Sets all registers to what we think we need
  digitalWrite(PIN_ADC_CS, LOW);
  delay(5);
  SPI.transfer(0x46); // incremental write starting at 0x01
  SPI.transfer(0b01000011); // CONFIG0
  SPI.transfer(0b00001000); // CONFIG1 for 9.6kHz
  SPI.transfer(0b10001011); // CONFIG2
  SPI.transfer(0b11110000); // CONFIG3
  SPI.transfer(0b01110011); // IRQ
  SPI.transfer(0b00000000); // MUX
  SPI.transfer(0x00); SPI.transfer(0x03); SPI.transfer(0x00); // SCAN
  SPI.transfer(0x00); SPI.transfer(0x00); SPI.transfer(0x00); // TIMER
  SPI.transfer(0x00); SPI.transfer(0x00); SPI.transfer(0x00); // OFFSETCAL
  SPI.transfer(0x80); SPI.transfer(0x00); SPI.transfer(0x00); // GAINCAL
  SPI.transfer(0x90); SPI.transfer(0x00); SPI.transfer(0x00); // RESERVED
  SPI.transfer(0x50); // RESERVED
  SPI.transfer(0xA5); // LOCK
  delay(5);
  digitalWrite(PIN_ADC_CS, HIGH);
}

void setup()
{
  Serial.begin(2000000);
  SPI.begin();
  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  pinMode(PIN_ADC_CS, OUTPUT);
  pinMode(PIN_BUFFER_FULL, OUTPUT);
  pinMode(PIN_SERIAL_FULL, OUTPUT);
  pinMode(PIN_ADC_INT, INPUT_PULLUP);
  pinMode(PIN_ONBOARD_LED, OUTPUT);
  digitalWrite(PIN_ADC_CS, HIGH);
  digitalWrite(PIN_BUFFER_FULL, LOW);
  digitalWrite(PIN_SERIAL_FULL, LOW);
  digitalWrite(PIN_ONBOARD_LED, LOW);
  
  delay(100);
  setAllRegisters();
  delay(100);
  attachInterrupt(digitalPinToInterrupt(PIN_ADC_INT), adcisr, FALLING);
}


void fetchData() {
  SPI.transfer(0x41); // Read ADC DATA
  byte channel_and_sgn = SPI.transfer(0x00); // 0x80 (aka 128 aka 0b10000000) or 0x90 (aka 144 aka 0b10010000)
  byte channel = channel_and_sgn & 0xF0;
  bool isNegative = (channel_and_sgn & 0x0F) != 0;
  byte dataB1 = SPI.transfer(0x00);
  byte dataB2 = SPI.transfer(0x00);
  byte dataB3 = SPI.transfer(0x00);

  uint32_t raw24 = ((uint32_t)dataB1 << 16) | ((uint32_t)dataB2 << 8) | (uint32_t)dataB3;
  // if data is negative, convert to signed 32-bit integer by sign-extending the 24-bit value
  int32_t data = isNegative ? (int32_t)(raw24 | 0xFF000000UL) : (int32_t)raw24;
  // if data is outside the range of a signed 24-bit integer, clip it
  if (data > ((int32_t)0x7FFFFF)) {
    data = 0x7FFFFF;
  }
  if (data < (int32_t)(-0x800000)) {
    data = -0x800000;
  }
  if (channel == 0x80) { // 0x80 is 1000 (diff channel A aka lightning)
    // set global variables for transfer to serial for storage.
    b1 = (data >> 16) & 0xFF;
    b2 = (data >> 8) & 0xFF;
    b3 = data & 0xFF;
  }
  } elif (channel == 0x90) { // 0x90 is 1001 (diff channel B aka PPS)
    // check if PPS is over half of VRef, set global flag.
    if (data > 0x400000) {
      // PPS is high
      GPS_PPS = true;
    } else {
      // PPS is low
      GPS_PPS = false;
    }
  }
}

void adcisr()
{
  uint32_t adcus = micros();
  digitalWrite(PIN_ADC_CS, LOW);
  b1 = 0x00;
  b2 = 0x00;
  b3 = 0x00;
  // Fetch data twice, once for lightning and once for GPS.
  // We don't know what order these will occur, so just set globals.
  fetchData();
  fetchData();
  
  // interleave GPS with data by replacing least significant bit of b3 with GPS_PPS flag
  b3 = bitWrite(b3, 0, GPS_PPS);
  datapackets.push(datapacket{0xBE, b1, b2, b3, adcus, 0xEF});
  digitalWrite(PIN_ADC_CS, HIGH);
}

void loop()
{
  if ((!datapackets.isEmpty()) && (Serial.availableForWrite()> 10))
  {
    // We have things to write and the place to write them
    datapacket dp = datapackets.pop();
    Serial.write((byte*)&dp.sb, 1);
    Serial.write((byte*)&dp.ch_sgn, 1);
    Serial.write((byte*)&dp.adc_b1, 1);
    Serial.write((byte*)&dp.adc_b2, 1);
    Serial.write((byte*)&dp.adc_b3, 1);
    Serial.write((byte*)&dp.adc_pps_time, 4);
    Serial.write((byte*)&dp.eb, 1);
    digitalWrite(PIN_ONBOARD_LED, HIGH);
  }

  if (datapackets.isFull())
  {
    digitalWrite(PIN_BUFFER_FULL, HIGH);
  }
  if (Serial.availableForWrite() < 10)
  {
    digitalWrite(PIN_SERIAL_FULL, HIGH);
  }
}
