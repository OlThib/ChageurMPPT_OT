//-----------------------------------------------------------------------
// PROGRAM  :     ChargeurMPPT_reveiver.ino
// PLATFORM :     DOIT ESP32 DEVKIT V1
// DESCRIPTIOM :  Waits for LoRa transmissions to come in and posts telemetry on thingsboard
//-----------------------------------------------------------------------

//--- Libraries ---------------------------------------------------------
#include <ArduinoJson.h>
#include <LoRa.h>
#include <SPI.h>
#include <ThingsBoard.h>
#include <esp_sntp.h>
#include <time.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <DS3231.h>
#include <SD.h>
//-----------------------------------------------------------------------

//--- Definitions -------------------------------------------------------
#define BAUDRATE 115200
#undef SERIAL_DEBUG      // define/undef to turn on/off serial debugging
#define SD_LOG           // define/undef to turn on/off SD log of serial debugging
#define TB_POST_ENABLED  // define/undef to turn on/off posting on TB
// Lora config
#define LORA_STATION_ADDRESS 1  // Leave this line at 1 if this station is the bridge
#define LORA_SS 5
#define LORA_RST 13
#define LORA_DIO0 12
#define LORA_FREQ 902500000
#define LORA_SYNC_WORD 0xB6
#define LORA_SPREAD_FACTOR 7
#define LORA_BANDWITH 125000
#define LORA_CODING_RATE 4
// Lora communication protocol definitions
#define HEADER_SIZE 4
#define NUM_SUBJECTS 3  // Telemtry = 'T', Message = 'M', Date/time = 'H'
#define TELEMETRY_SIZE 52
#define TIME_SIZE 6
#define PAYLOAD_SIZE 128
// Hardware pins and I2C addresses
#define SD_CS_PIN 16
#define TIMEOUT_DELAY 30
#define INFO_SIZE 1
// Offset for NTP requests
#define GMT_OFFSET -5
//-----------------------------------------------------------------------

//--- Constants ---------------------------------------------------------
const uint8_t LORA_BUFFER_SIZE = HEADER_SIZE + PAYLOAD_SIZE;
constexpr char THINGSBOARD_SERVER[] = "thingsboard.cloud";
constexpr uint16_t THINGSBOARD_PORT = 1883U;
constexpr uint32_t MAX_MESSAGE_SIZE = 512U;
const char* dataFile = "/data.csv";
const char* logFile = "/log.txt";
const char* dataHeader = "TIMESTAMP,TEMP,HUM,LUM,PRES,PV_V,PV_I,PV_W,BAT_V,BAT_I,BAT_w\n";
const char* logHeader = "--- ESP32 LOG FILE ---\n";
//-----------------------------------------------------------------------

//--- Objets / Globals --------------------------------------------------
// Internet
WiFiClient wifiClient;
ThingsBoard tb(wifiClient, MAX_MESSAGE_SIZE);
// RTC
DS3231 Clock;
RTClib myRtc;
// WiFi mac adress. Change to the mac address given by the network admin, if needed
byte mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0 };
//-----------------------------------------------------------------------

//--- Structures / Unions -----------------------------------------------
typedef union headerUnion {
  struct __attribute__((packed)) {
    uint8_t sender = 0;
    uint8_t receiver = 0;
    char subject = NULL;
    uint8_t palyoadSize = 0;
  };
  uint8_t byteArray[HEADER_SIZE];
};

typedef union telemetryUnion {
  struct __attribute__((packed)) {
    float temp = 0.0F;
    float hum = 0.0F;
    float lum = 0.0F;
    float pres = 0.0F;
    float pv_v = 0.0F;
    float pv_i = 0.0F;
    float pv_w = 0.0F;
    float bat_v = 0.0F;
    float bat_i = 0.0F;
    float bat_w = 0.0F;
    time_t ts = 0;
    unsigned long logFileSize = 0;
    unsigned long dataFileSize = 0;
  };
  uint8_t byteArray[TELEMETRY_SIZE];
};
//-----------------------------------------------------------------------

//------ Prototypes -----------------------------------------------------
// Core
void myPrint(char* buf);
bool initSD(void);
unsigned long getLineCount(File file);
String getTimeString(void);
void saveSDlog(char* buf);
// Internet
bool wifi_cnx(void);
bool connect_network(const char* ssid, const char* pswd);
bool checkConnexion(void);
bool ntpReq(void);
void tb_post(DynamicJsonDocument jsonDoc);
void tb_sendTelemetry(telemetryUnion telemetry);
// LoRa
void lora_rx_manager(int packetSize);
//-----------------------------------------------------------------------

//-------- Setup --------------------------------------------------------
void setup() {
#ifdef SERIAL_DEBUG
  Serial.begin(BAUDRATE);
  delay(1000);
  Serial.printf("Booting up\n");
#endif
  // Stop bluetooth services
  btStop();
  // Init SPI modules
  initSD();
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) { myPrint("Failed to init LoRa\n"); }
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SPREAD_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWITH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  // Init I2C modules
  Wire.begin();
  Clock.setClockMode(false);
  // Set wifi mode, mac address, connect to wifi and update RTC.
  WiFi.mode(WIFI_STA);
  esp_wifi_set_mac(WIFI_IF_STA, &mac[0]); // Comment out if mac address is attributed by network
  if (checkConnexion()) { ntpReq(); }
  myPrint("Setup done\n");
}
//-----------------------------------------------------------------------

//-------- Loop ---------------------------------------------------------
void loop() {
  // Refresh TB object, see if messages came in
  tb.loop();
  // Listen for LoRa packets
  int loraPacketSize = LoRa.parsePacket();
  if (loraPacketSize) { lora_rx_manager(loraPacketSize); }
}
//-----------------------------------------------------------------------