#include <Arduino.h>
#include <ModbusMaster.h>

// put function declarations here:
#define MAX485_DE 32
#define MAX485_RE 32
#define RS485_RX  16   // ESP32 Serial2 RX
#define RS485_TX  17   // ESP32 Serial2 TX

//int myFunction(int, int);

ModbusMaster node;

void preTransmission() 
{
  digitalWrite(MAX485_RE, 1);
  digitalWrite(MAX485_DE, 1);
}

void postTransmission() 
{
  digitalWrite(MAX485_RE, 0);
  digitalWrite(MAX485_DE, 0);
}

float floatModbusReading(uint16_t addr, uint8_t id)
{
  Serial.print(addr); Serial.print(" : ");

  uint8_t result = 0;
  float y = 0;
  uint32_t dec;

  node.begin(id, Serial2);
  result = node.readHoldingRegisters(addr, 2);
  delay(100);

  if (result == node.ku8MBSuccess)
  {
    uint32_t x = (uint32_t)node.getResponseBuffer(1) * 65536 + node.getResponseBuffer(0);

    if (x == 0) return 0;

    int16_t exponent = ((x & 0x7F800000) >> 23) - 127;
    uint32_t a = (uint32_t)(pow(2, exponent) - 1);

    if (exponent <= 23 && exponent >= 0)
    {
      dec = (x & (a << (23 - exponent))) >> (23 - exponent);
      dec = (uint32_t)pow(2, exponent) + dec;
      uint8_t count = 23 - exponent;
      uint16_t count_value = (uint16_t)pow(2, count) - 1;

      if (count >= 6)
      {
        uint8_t mantissa = (x >> (count - 6)) & 0x3F;
        y =  ((mantissa >> 5) & 1) * 0.5f
           + ((mantissa >> 4) & 1) * 0.25f
           + ((mantissa >> 3) & 1) * 0.125f
           + ((mantissa >> 2) & 1) * 0.0625f
           + ((mantissa >> 1) & 1) * 0.03125f
           + ( mantissa       & 1) * 0.015625f;
      }
      else
      {
        uint8_t mantissa = (x & count_value);
        int count_copy = count;
        int count_power = 0;
        while (count_copy > 0)
        {
          count_power++;
          count_copy--;
          int r = (int)pow(2, count_copy);
          float s = pow(2, -count_power);
          y += ((mantissa & r) >> count_copy) * s;
        }
      }
      y += dec;
    }
    else if (exponent < 0)
    {
      uint8_t mantissa = (x >> 17) & 0x3F;
      y =  ((mantissa >> 5) & 1) * 0.5f
         + ((mantissa >> 4) & 1) * 0.25f
         + ((mantissa >> 3) & 1) * 0.125f
         + ((mantissa >> 2) & 1) * 0.0625f
         + ((mantissa >> 1) & 1) * 0.03125f
         + ( mantissa       & 1) * 0.015625f;
      y = (1.0f + y) * pow(2, exponent);
      return y;
    }
    else
    {
      return -1;
    }
  }
  else
  {
    Serial.print("RS485 ERROR ");
    return -1;
  }

  return y;
}

void setup() {
  // put your setup code here, to run once:
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

void loop() {
  // put your main code here, to run repeatedly:
  Serial.println("Reading from Elite 300 device...");
  
  //Voltage
  Serial.print("V_R            : "); Serial.print(floatModbusReading(99,  1)); Serial.println(" V");
  Serial.print("V_Y            : "); Serial.print(floatModbusReading(101, 1)); Serial.println(" V");
  Serial.print("V_B            : "); Serial.print(floatModbusReading(103, 1)); Serial.println(" V");

  //Current
  Serial.print("I_R            : "); Serial.print(floatModbusReading(113, 1)); Serial.println(" A");
  Serial.print("I_Y            : "); Serial.print(floatModbusReading(115, 1)); Serial.println(" A");
  Serial.print("I_B            : "); Serial.print(floatModbusReading(117, 1)); Serial.println(" A");

  //Power Factor
  Serial.print("PF_R           : "); Serial.println(floatModbusReading(133, 1));
  Serial.print("PF_Y           : "); Serial.println(floatModbusReading(135, 1));
  Serial.print("PF_B           : "); Serial.println(floatModbusReading(137, 1));

  Serial.println();
  delay(1000);
}

// put function definitions here:
//int myFunction(int x, int y) {
//  return x + y;
//}