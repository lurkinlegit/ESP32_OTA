#include <Arduino.h>
#include <ModbusMaster.h>

// ---------------- Pins (classic ESP32, esp32doit-devkit-v1) ----------------
#define MAX485_DE 32
#define MAX485_RE 32
#define RS485_RX  16   // ESP32 Serial2 RX
#define RS485_TX  17   // ESP32 Serial2 TX

ModbusMaster node;

// ---------------- MAX485 direction control ----------------
void preTransmission()
{
  digitalWrite(MAX485_RE, HIGH);   // transmit mode
  digitalWrite(MAX485_DE, HIGH);
}

void postTransmission()
{
  digitalWrite(MAX485_RE, LOW);    // receive mode
  digitalWrite(MAX485_DE, LOW);
}

// ---------------- Read one 32-bit float (2 registers, low word first) ----------------
float floatModbusReading(uint16_t addr, uint8_t id)
{
  node.begin(id, Serial2);
  uint8_t result = node.readHoldingRegisters(addr, 2);
  delay(100);

  if (result != node.ku8MBSuccess)
  {
    Serial.print("RS485 ERROR ");
    return -1;
  }

  uint32_t x = ((uint32_t)node.getResponseBuffer(1) << 16) | node.getResponseBuffer(0);
  float f;
  memcpy(&f, &x, sizeof(f));       // reinterpret the 32 bits as an IEEE-754 float
  return f;
}

void setup()
{
  pinMode(MAX485_RE, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_RE, LOW);
  digitalWrite(MAX485_DE, LOW);

  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);

  node.begin(1, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("Welcome to Elite 300");
}

void loop()
{
  Serial.println("Reading from Elite 300 device...");

  // Voltage
  Serial.print("V_R            : "); Serial.print(floatModbusReading(99,  1)); Serial.println(" V");
  Serial.print("V_Y            : "); Serial.print(floatModbusReading(101, 1)); Serial.println(" V");
  Serial.print("V_B            : "); Serial.print(floatModbusReading(103, 1)); Serial.println(" V");

  // Current
  Serial.print("I_R            : "); Serial.print(floatModbusReading(113, 1)); Serial.println(" A");
  Serial.print("I_Y            : "); Serial.print(floatModbusReading(115, 1)); Serial.println(" A");
  Serial.print("I_B            : "); Serial.print(floatModbusReading(117, 1)); Serial.println(" A");

  // Power factor
  Serial.print("PF_R           : "); Serial.println(floatModbusReading(133, 1));
  Serial.print("PF_Y           : "); Serial.println(floatModbusReading(135, 1));
  Serial.print("PF_B           : "); Serial.println(floatModbusReading(137, 1));

  // Frequency and power
  Serial.print("Frequency      : "); Serial.print(floatModbusReading(171, 1)); Serial.println(" Hz");
  Serial.print("3Ph Active Pwr : "); Serial.print(floatModbusReading(147, 1)); Serial.println(" W");
  Serial.print("3Ph Apparent   : "); Serial.print(floatModbusReading(163, 1)); Serial.println(" VA");

  Serial.println();
  delay(1000);
}