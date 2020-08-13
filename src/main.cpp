

// TODO дописать

#include <Arduino.h>

#define BLYNK_PRINT Serial

#define BlynkVP_varCurrent 1
#define BlynkVP_varVoltage 2
#define BlynkVP_varPower 3
#define BlynkVP_varEnergySUM 4
#define BlynkVP_varBattery 5
#define BlynkVP_varMoneySpent 6
#define BlynkVP_varPowerDifBlynk 7
#define BlynkVP_varTemperature 8
#define BlynkVP_varMoneyStart 9  // виртуальный порт для BLYNK для ввода начальных значений рассчета расхода в рублях
#define BlynkVP_mode_Button 10   // виртуальный порт для BLYNK виджета кнопка переключения в тестовый режим и обратно
#define BlynkVP_Update_Button 13 // виртуальный порт для BLYNK виджета кнопка обновления по воздуху
#define BlynkVP_Terminal 39      // виртуальный порт для BLYNK виджета терминал

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <OneWire.h>           // Для возможности подключить датчика температуры
#include <DallasTemperature.h> // Для возможности подключить датчика температуры
#include <SoftwareSerial.h>
#include <ESP8266mDNS.h>      //  OTA w/ ESP8266
#include <ESP8266WebServer.h> //  OTA
#include <WiFiClient.h>       //  OTA

WidgetTerminal terminal(BlynkVP_Terminal);

#include <credentials.h>
const char *auth = BLYNK_AUTH;
const char *ssid = WIFI_SSID;
const char *pass = WIFI_PASSWD;

BlynkTimer timer; // запускает выполнение функций по таймеру

//-------- порты для rs 485
#define SSerialRx D1     // RO
#define SSerialTx D2     // DI
#define SerialControl D5 // RS485 для выбора направления
#define RS485Transmit HIGH
#define RS485Receive LOW

#define ONE_WIRE_BUS D6 // датчик температуры DS18b20

SoftwareSerial RS485Serial(SSerialRx, SSerialTx); // Rx, Tx
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

//    команды Меркурий 200
//    |Адрес счетчика 4 байта | Запрос 1 байт|

byte address[] = {250, 0, 2, 11}; // адрес 4194304523
// byte address[] = {00, 13, 32, 206}; адрес860366

byte test_cmd[] = {39}; // тестовая команда для счетчика
byte Current[] = {};    // команда сила тока
byte Voltage[] = {};    // команда напряжение
byte Power[] = {};      // команда мощность
byte energy[] = {39};   // команда энергия
byte battery[] = {41};  // напряжение батарейки

int netAdr2 = 4194304523; // адрес тест счетчика (не используется в коде)|  | {250, 0, 2, 11, | | FA00020B |
int netAdr = 860366;      // адрес счетчика (не используется в коде)|  | {00, 13, 32, 206, | | D20CE |

float tarif = 6;  // тариф рублей кВт⋅ч
int firstRun = 1; // знать что первый запуск
int rssi;
float varCurrent;
int varVoltage;
float varenergyT1;
float varenergyT2;
float varenergy_sum;
float varMoneySpent;
float var_battery;
int varPower;
int varPower1;
int varPower2;
int varPower3;
int varPowerDif;
int varPowerDifBlynk;
int power_change = 0;
int power_hold = 0;
int sensor_error = 0;

byte response[24]; // длина массива входящего сообщения
int byteReceived;
int byteSend;

float varMoneyStart;
BLYNK_WRITE(BlynkVP_varMoneyStart) // обновляет стартовую переменную для расчета расхода в рублях на основе разницы с текущими показаниями в квтч
{
  int pinValue = param.asInt();
  varMoneyStart = pinValue;
}

int test_mode = 0;               // тестовый режим работы
BLYNK_WRITE(BlynkVP_mode_Button) // Переключение режимов работы в проекте BLYNK (тест/обычный)
{
  test_mode = param.asInt();
}

//****************** OTA ************************

ESP8266WebServer server(80);
const char *serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";
const char *host = "esp8266-webupdate";
unsigned long update_timer;
unsigned long update_period = 120000;
const char *firmware = "mercury 0.1";

void serv()
{
  Blynk.virtualWrite(BlynkVP_Terminal, "\nBooting Sketch...");
  if (WiFi.waitForConnectResult() == WL_CONNECTED)
  {
    MDNS.begin(host);
    server.on("/", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/html", serverIndex);
    });
    server.on(
        "/update", HTTP_POST, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      ESP.restart(); }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.setDebugOutput(true);
        WiFiUDP::stopAll();
        Blynk.virtualWrite(BlynkVP_Terminal, "\nUpdate: %s\n", upload.filename.c_str());
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace)) { //start with max available size
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Blynk.virtualWrite(BlynkVP_Terminal, "\nUpdate Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      }
      yield(); });
    server.begin();
    MDNS.addService("http", "tcp", 80);
  }
  else
  {
    Blynk.virtualWrite(BlynkVP_Terminal, "\nWiFi Failed");
  }
}

BLYNK_WRITE(BlynkVP_Update_Button) // При нажатии в проекте BLYNK кнопки Update открывается временная возможность для прошивки по воздуху
{
  if (param.asInt() == 1)
  {
    Blynk.virtualWrite(BlynkVP_Terminal, "clr");
    Blynk.virtualWrite(BlynkVP_Terminal, "\nupdate");
    digitalWrite(SerialControl, RS485Receive);
    IPAddress myip = WiFi.localIP();                                                 // узнаем ip
    String fullip = String(myip[0]) + "." + myip[1] + "." + myip[2] + "." + myip[3]; // для вывода на дисплей
    Blynk.virtualWrite(BlynkVP_Terminal, "\nReady! Open http://", fullip);
    serv();
    update_timer = millis(); // "сбросить" таймер
    while (millis() - update_timer < update_period)
    {
      server.handleClient();
      MDNS.update();
      delay(1);
      int x = (update_period - (millis() - update_timer)) / 1000;
      Blynk.setProperty(BlynkVP_Update_Button, "offLabel", x);
    }
    Blynk.setProperty(BlynkVP_Update_Button, "offLabel", firmware);
  }
}

//****************** OTA END************************

unsigned int crc16MODBUS(byte *s, int count) // Расчет контрольной суммы для запроса
{
  unsigned int crcTable[] = {
      0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
      0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
      0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
      0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
      0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
      0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
      0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
      0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
      0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
      0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
      0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
      0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
      0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
      0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
      0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
      0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
      0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
      0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
      0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
      0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
      0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
      0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
      0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
      0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
      0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
      0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
      0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
      0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
      0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
      0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
      0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
      0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040};

  unsigned int crc = 0xFFFF;

  for (int i = 0; i < count; i++)
  {
    crc = ((crc >> 8) ^ crcTable[(crc ^ s[i]) & 0xFF]);
  }
  return crc;
}

void test_send(byte *cmd, int s_cmd) // для тестовых запросов {Адрес счетчика 4, 	Запрос 1, 	CRC16 (Modbus) 2}
{

  int s_address = sizeof(address);
  int s_address_cmd = s_address + s_cmd;
  int s_address_cmd_crc = s_address_cmd + 2;

  byte address_cmd[s_address_cmd];
  byte address_cmd_crc[s_address_cmd_crc];

  int pos = 0;
  for (int i = 0; i < s_address; i++)
  {
    address_cmd[pos] = address[i];
    address_cmd_crc[pos] = address[i];
    pos++;
  }

  for (int i = 0; i < s_cmd; i++)
  {
    address_cmd[pos] = cmd[i];
    address_cmd_crc[pos] = cmd[i];
    pos++;
  }

  Serial.println(" ");
  Serial.println(" ");
  Serial.print("Sending test mode");
  Serial.println(" ");
  Blynk.virtualWrite(BlynkVP_Terminal, "\nTest sending: ");

  unsigned int crc = crc16MODBUS(address_cmd, s_address_cmd);
  unsigned int crc1 = crc & 0xFF;
  unsigned int crc2 = (crc >> 8) & 0xFF;
  digitalWrite(SerialControl, RS485Transmit); // Переключает RS485 на передачу
  delay(100);

  address_cmd_crc[pos] = crc1;
  pos++;
  address_cmd_crc[pos] = crc2;

  String temp_term1 = "";
  String temp_term2 = "";

  for (int i = 0; i < s_address_cmd_crc; i++)
  {
    temp_term1 += String(address_cmd_crc[i]);
    temp_term1 += " ";
    temp_term2 += String(address_cmd_crc[i], HEX);
    temp_term2 += " ";
  }

  Serial.println("");
  Serial.print("RS485Serial.write test:  ");
  Serial.print(temp_term1);
  Serial.println("");
  Serial.print("RS485Serial.write test HEX:  ");
  Serial.print(temp_term2);

  Blynk.virtualWrite(BlynkVP_Terminal, "\nTest write:  ", temp_term1);
  Blynk.virtualWrite(BlynkVP_Terminal, "\nTest write HEX:  ", temp_term2);

  for (int i = 0; i < s_address_cmd_crc; i++)
  {
    RS485Serial.write(address_cmd_crc[i]);
  }

  digitalWrite(SerialControl, RS485Receive); // Переключает RS485 на прием
  delay(100);

  if (RS485Serial.available()) // получаем: |Адрес счетчика | Запрос на который отвечаем | Ответ |	CRC16 (Modbus)|
  {
    byte i = 0;
    while (RS485Serial.available())
    {
      byteReceived = RS485Serial.read(); //
      delay(10);
      response[i++] = byteReceived;
    }

    String temp_term1 = "";
    String temp_term2 = "";
    for (unsigned int i = 0; i < (sizeof(response)); i++) //
    {
      temp_term1 += String(response[i]);
      temp_term1 += " ";
      temp_term2 += String(response[i], HEX);
      temp_term2 += " ";
    }
    Blynk.virtualWrite(BlynkVP_Terminal, "\nRespond:  ", temp_term1);
    Blynk.virtualWrite(BlynkVP_Terminal, "\nRespond HEX:  ", temp_term2);

    Serial.println("");
    Serial.println("");
    Serial.print("Respond:  ");
    Serial.print(temp_term1);
    Serial.println("");
    Serial.print("Respond HEX  ");
    Serial.print(temp_term2);

    for (unsigned int i = 0; i < (sizeof(response)); i++) //
    {
      response[i] = 0; // обнуляет массив
    }
  }
}

void test_receive() // для тестовых входящих {Адрес счетчика 4, 	Запрос 1, 	CRC16 (Modbus) 2}
{
  Serial.println(" ");
  Serial.println(" ");
  Serial.print("Receiving test mode - listening 5 seconds");
  Serial.println(" ");
  Blynk.virtualWrite(BlynkVP_Terminal, "\nTest receiving 5 seconds: ");
  digitalWrite(SerialControl, RS485Receive); // Переключает RS485 на прием
  delay(100);
  unsigned long test_receive_timer = millis(); // "сбросить" таймер
  int response_status = 0;
  while (millis() - test_receive_timer < 5000)
  {
    if (RS485Serial.available()) // получаем: |Адрес счетчика | Запрос на который отвечаем | Ответ |	CRC16 (Modbus)|
    {
      response_status = 1;
      byte i = 0;
      Serial.println(" ");
      while (RS485Serial.available())
      {
        byteReceived = RS485Serial.read(); //
        delay(10);
        response[i++] = byteReceived;
      }

      String temp_term1 = "";
      String temp_term2 = "";
      for (unsigned int i = 0; i < (sizeof(response)); i++) //
      {
        Serial.print(response[i]);
        Serial.print("");
        temp_term1 += String(response[i]);
        temp_term1 += " ";
        temp_term2 += String(response[i], HEX);
        temp_term2 += " ";
      }
      Blynk.virtualWrite(BlynkVP_Terminal, "\nReceived:  ", temp_term1);
      Blynk.virtualWrite(BlynkVP_Terminal, "\nReceived HEX:  ", temp_term2);

      Serial.println("");
      Serial.print("Received:  ");
      Serial.print(temp_term1);
      Serial.println(" ");
      Serial.print("Received HEX  ");
      Serial.print(temp_term2);

      for (unsigned int i = 0; i < (sizeof(response)); i++) //
      {
        response[i] = 0; // обнуляет массив
      }
      test_receive_timer = millis(); // "сбросить" таймер
    }
  }
  if (response_status == 0)
  {
    Serial.println("");
    Serial.println("quiet");
    Blynk.virtualWrite(BlynkVP_Terminal, "\nquiet");
    Serial.println("");
  }
}

void test() // тесовый режим
{

  test_send(test_cmd, sizeof(test_cmd));
  test_receive();
}

void send(byte *cmd, int s_cmd) // отправка-получение в обычном режиме {Адрес счетчика 4, 	Запрос 1, 	CRC16 (Modbus) 2}
{

  int s_address = sizeof(address);
  int s_address_cmd = s_address + s_cmd;
  int s_address_cmd_crc = s_address_cmd + 2;

  byte address_cmd[s_address_cmd];
  byte address_cmd_crc[s_address_cmd_crc];

  int pos = 0;
  for (int i = 0; i < s_address; i++)
  {
    address_cmd[pos] = address[i];
    address_cmd_crc[pos] = address[i];
    pos++;
  }

  for (int i = 0; i < s_cmd; i++)
  {
    address_cmd[pos] = cmd[i];
    address_cmd_crc[pos] = cmd[i];
    pos++;
  }

  unsigned int crc = crc16MODBUS(address_cmd, s_address_cmd);
  unsigned int crc1 = crc & 0xFF;
  unsigned int crc2 = (crc >> 8) & 0xFF;

  address_cmd_crc[pos] = crc1;
  pos++;
  address_cmd_crc[pos] = crc2;

  digitalWrite(SerialControl, RS485Transmit); // Переключает RS485 на передачу
  delay(100);

  for (int i = 0; i < s_address_cmd_crc; i++)
  {
    RS485Serial.write(address_cmd_crc[i]);
  }

  digitalWrite(SerialControl, RS485Receive); // Переключает RS485 на прием
  delay(100);

  for (unsigned int i = 0; i < (sizeof(response)); i++) //
  {
    response[i] = 0; // обнуляет массив
  }

  if (RS485Serial.available()) // получаем: |Адрес счетчика | Запрос на который отвечаем | Ответ |	CRC16 (Modbus)|
  {

    byte i = 0;
    while (RS485Serial.available())
    {
      byteReceived = RS485Serial.read(); //
      delay(10);
      response[i++] = byteReceived;
    }
  }
}

float getEnergyT1() // расшифровает полученное в данные энергии
{
  send(energy, sizeof(energy));
  Serial.println("");
  Serial.print("getEnergyT1 response[i] (HEX): ");
  String x123 = "";
  for (int i = 5; i < 8; i++) // выбирает в массиве показатели энергии T1
  {
    Serial.print(response[i], HEX);
    Serial.print(" ");
    String temp_x = String(response[i], HEX);
    int x = temp_x.toInt();
    if (x < 10)
    {
      x123 += "0";
      x123 += String(response[i], HEX);
    }
    else
    {
      x123 += String(response[i], HEX);
    }
  }

  float y123 = x123.toFloat();
  String temp_y4 = String(response[8], HEX);
  float y4 = (temp_y4.toFloat()) / 100;
  float thenumber = y123 + y4;

  return thenumber;
}

float getEnergyT2() // расшифровает полученное в данные энергии
{
  send(energy, sizeof(energy));
  Serial.println("");
  Serial.print("getEnergyT2 response[i] (HEX): ");
  String x123 = "";
  for (int i = 9; i < 12; i++) // выбирает в массиве показатели энергии T2
  {
    Serial.print(response[i], HEX);
    Serial.print(" ");
    String temp_x = String(response[i], HEX);
    int x = temp_x.toInt();
    if (x < 10)
    {
      x123 += "0";
      x123 += String(response[i], HEX);
    }
    else
    {
      x123 += String(response[i], HEX);
    }
  }

  float y123 = x123.toFloat();
  String temp_y4 = String(response[12], HEX);
  float y4 = (temp_y4.toFloat()) / 100;
  float thenumber = y123 + y4;

  return thenumber;
}

String get_battery() // расшифровает полученное в данные батарейки
{
  send(battery, sizeof(battery));
  String battery_return = "";

  Serial.println("");
  Serial.print("battery response[i], HEX  ");
  for (int i = 5; i < 7; i++) // выбирает в массиве показатели батарейки
  {
    Serial.print(response[i], HEX);
    Serial.print(" ");
    battery_return += String(response[i], HEX);
  }

  return battery_return;
}

String get_Power()
{
  // дописать по аналогии
  return String("");
}

String get_Voltage()
{
  // дописать по аналогии
  return String("");
}

// остальные комманды дописать по аналогии

void Mercury_Slow_Data() // для запросов не требующих частый сбор
{
  String temp_battery = get_battery();
  var_battery = (temp_battery.toFloat()) / 100;
  Serial.println(" ");
  Serial.print("var_battery (float): ");
  Serial.print(var_battery);
  Serial.println(" ");

  varenergyT1 = getEnergyT1();
  Serial.println(" ");
  Serial.print("varenergyT1 (float): ");
  Serial.print(varenergyT1);
  Serial.println(" ");

  varenergyT2 = getEnergyT2();
  Serial.println(" ");
  Serial.print("varenergyT2 (float): ");
  Serial.print(varenergyT2);
  Serial.println(" ");

  varenergy_sum = varenergyT1 + varenergyT2;
  Serial.println(" ");
  Serial.print("varenergy SUM (float): ");
  Serial.print(varenergy_sum);
  Serial.println(" ");
}

void Mercury_Fast_Data() // для запросов и действий требующих частый сбор
{

  // тут частые запросы
  String temp_Power = get_Power();
  varPower = temp_Power.toInt();
  String temp_Voltage = get_Voltage();
  varVoltage = temp_Voltage.toInt();

  varMoneySpent = (varenergy_sum - varMoneyStart) * tarif; // расчет расхода от начального периода в рублях

  //Далее расчет разницы для показателя моментальной мощности ватт, чтобы наглядно видеть сколько какой прибор расходует при включении или отключении
  // нужно сначала написать команду чтобы запросить счетчик

  varPowerDif = varPower - varPower1;
  varPowerDif = abs(varPowerDif);
  if (firstRun == 1)
  {
    varPowerDif = 0;
    Mercury_Slow_Data();
  }

  if (varPowerDif < 5)
  {
    varPower2 = varPower1;
    power_hold++;
    // Serial.println("(varPower2)");
    // Serial.println(varPower2);
  }

  if (varPowerDif > 50 && power_hold > 5)
  {
    varPower3 = varPower2;
    power_change = 1;
    power_hold = 0;
    //Serial.println("(varPower3)");
    //Serial.println(varPower3);
  }

  if (power_change == 1 && power_hold > 5 && (abs(varPower2 - varPower3)) > 100)
  {
    varPowerDifBlynk = varPower2 - varPower3;
    // Serial.println("(varPowerDifBlynk)");
    // Serial.println(varPowerDifBlynk);
    Blynk.virtualWrite(BlynkVP_varPowerDifBlynk, varPowerDifBlynk);
    power_change = 0;
  }
  firstRun = 0;
  varPower1 = varPower;

  Blynk.virtualWrite(BlynkVP_varCurrent, varCurrent);       // отправляет в блинк силу тока
  Blynk.virtualWrite(BlynkVP_varVoltage, varVoltage);       // отправляет напряжение в блинк
  Blynk.virtualWrite(BlynkVP_varPower, varPower);           // отправляет в блинк мощность тока
  Blynk.virtualWrite(BlynkVP_varEnergySUM, varenergy_sum);  // отправляет в блинк расход электроэнергии по Т1
  Blynk.virtualWrite(BlynkVP_varMoneySpent, varMoneySpent); // отправляет в блинк расхд в рублях
  Blynk.virtualWrite(BlynkVP_varBattery, var_battery);      // отправляет в блинк сколько вольт осталось в батарейке
}

void sensor_polling() // опрос датчика температуры
{
  sensors.requestTemperatures();                // Polls the sensors
  float tempRoom1 = sensors.getTempCByIndex(0); //

  // if (tempRoom1 < 0 || tempRoom1 > 40)
  // {                                                         // Проверка инициализации датчика
  //   Blynk.virtualWrite(BlynkVP_Terminal, "sensor error\n"); // Печать, об ошибки инициализации.
  //   sensor_error = 1;
  //   return;
  // }

  float t = round(tempRoom1 * 10) / 10;
  Blynk.virtualWrite(BlynkVP_varTemperature, t);
  // sensor_error = 0;
}

void wifi_reconnection_ON() // включает режим автопереподключения
{
  if (WiFi.getAutoConnect() != true) //
  {
    WiFi.setAutoConnect(true);   //
    WiFi.setAutoReconnect(true); //
  }
}

void connection_control()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Not connected to WiFi");
  }
  else
  {
    rssi = WiFi.RSSI();
    //Serial.println(rssi);
    //Serial.println(WiFi.localIP());
    if (!Blynk.connected())
    {
      Serial.println("Not connected to Blynk server");
      Blynk.connect(); // try to connect to server with default timeout
    }
    else
    {
      //Serial.println("Connected to Blynk server");
    }
  }
  if (Blynk.connected())
  {
    Blynk.run();
  }
}

void setup()
{
  RS485Serial.begin(9600);
  Serial.begin(9600);
  Serial.println("setup ");
  WiFi.begin(ssid, pass);
  delay(1000);
  wifi_reconnection_ON();
  Blynk.config(auth, IPAddress(192, 168, 1, 64), 8080); // Blynk.config(auth, server, port); адрес и порт локального сервера
  Blynk.connect();
  Blynk.virtualWrite(BlynkVP_Terminal, "clr");
  Blynk.virtualWrite(BlynkVP_Terminal, "\nStart");

  sensors.begin(); // для датчика температуры

  timer.setInterval(1000L, Mercury_Fast_Data); // интервал для частых  опросов (1000L = 1 секунда)
  timer.setInterval(3000L, Mercury_Slow_Data); // интервал для  редких опросов чтобы освободить время для моментальных показателей (обычно 60000L раз в минуту опрос)
  //timer.setInterval(50000L, sensor_polling);    // Интервал опроса температуры (1000L = 1 секунда)

  pinMode(SerialControl, OUTPUT);

  Blynk.syncAll();                                                // синхронизирует все состояния с приложением BLYNK
  Blynk.setProperty(BlynkVP_Update_Button, "offLabel", firmware); // меняет название кнопки в приложении BLYNK на то что в переменной firmware
}

void loop()
{
  if (test_mode == 1) // если режим тест то выполняется только тест
  {
    test();
  }
  else
  {
    timer.run();
  }
  connection_control();
}
