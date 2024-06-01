//-----------------------------------------------------------------------
#define SD_LOG
#define SERIAL_DEBUG
#define TB_POST
#define BAUDRATE 115200

#define LORA_SS 5
#define LORA_RST 13
#define LORA_DIO0 12
#define FREQ 902500000
#define SYNC_WORD 0xF3
#define SPREAD_FACTOR 7
#define BANDWITH 125000
#define CODING_RATE 4

#define NUM_MSGTYPE 3
#define MSGTYPE_SIZE 1
#define TELEMETRY_SIZE 52
#define TIME_SIZE 6
#define PAYLOAD_SIZE 128
#define CHECKSUM_SIZE 2
#define MAX_QUEUED_MESSAGES 5

#define SD_CS_PIN 16
#define TIMEOUT_DELAY 30
#define MAX_TS_DRIFT 5
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
#include <ArduinoJson.h>
#include <LoRa.h>
#include <SPI.h>
#include <SD.h>
#include <ThingsBoard.h>
#include <esp_wifi.h>
#include <esp_sntp.h>
#include <Wire.h>
#include <DS3231.h>
#include <WiFi.h>
#include "credentials.h"
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
constexpr char THINGSBOARD_SERVER[] = "thingsboard.cloud";
constexpr uint16_t THINGSBOARD_PORT = 1883U;
constexpr uint32_t MAX_MESSAGE_SIZE = 512U;

const uint8_t LORA_BUFFER_SIZE = MSGTYPE_SIZE + PAYLOAD_SIZE + CHECKSUM_SIZE;
const uint8_t MESSAGE = 'M';
const uint8_t DATA = 'D';
const uint8_t TIME = 'T';
const uint8_t msgTypeList[NUM_MSGTYPE] = { MESSAGE, DATA, TIME };
const char ACK = '*';
const char NAK = '?';
const char* logFile = "/log.txt";
//-----------------------------------------------------------------------

//--- Structures / Unions -----------------------------------------------
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

typedef union tsUnion {
  struct __attribute__((packed)) {
    time_t ts = 0;
    int offset = 0;
  };
  uint8_t byteArray[TIME_SIZE];
};

typedef union checkSumUnion {
  uint16_t value = 0;
  uint8_t byteArray[CHECKSUM_SIZE];
};

//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
WiFiClient wifiClient;
ThingsBoard tb(wifiClient, MAX_MESSAGE_SIZE);
DS3231 Clock;
RTClib myRtc;
//-----------------------------------------------------------------------

//--- Globals -----------------------------------------------------------
volatile bool loraTx = false;
volatile int loraPacketSize = 0;
bool msgRcv = false, isTransmitting = false, updateNodeTime = false;
uint8_t msgQindex = 0;
uint8_t loraBuf[LORA_BUFFER_SIZE];
uint8_t msgQbuf[MAX_QUEUED_MESSAGES * LORA_BUFFER_SIZE];
bool boolSD = false;
//-----------------------------------------------------------------------


//-----------------------------------------------------------------------
// Core functions
void myPrint(char* buf);
tm get_ds3231_time(void);
bool ntpReq(void);
void wifi_cnx_(void);
bool checkConnexion(void);
void tb_post(DynamicJsonDocument jsonDoc);
void tb_sendTelemetry(telemetryUnion telemetry);
void tb_sendLog(String log);
bool initSD(void);
void saveSDlog(char* buf);
unsigned long getLineCount(File file);
void saveSDdata(telemetryUnion telemetry);
// LoRa functions
void lora_rx_cb(int packetSize);
void lora_tx_cb(void);
void lora_rx_manager(void);
void lora_tx_manager(void);
void lora_msgQ_manager(void);
void lora_empty_buffer(void);
void lora_send_buffer(uint8_t* buf, uint8_t len, char msgType);
bool lora_validate_payload(void);
void lora_read_payload(void);
void lora_send_reply(uint8_t reply, uint8_t msgType);
void lora_send_ts(void);
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
void setup() {
#ifdef SERIAL_DEBUG
  Serial.begin(BAUDRATE);
#endif
  delay(1000);
  btStop();
  Wire.begin();
  Clock.setClockMode(false);
  pinMode(SD_CS_PIN, OUTPUT);
  boolSD = initSD();
  WiFi.mode(WIFI_STA);
  wifi_cnx();
  ntpReq();
  if (tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) { myPrint("Connected to TB\n"); }
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(FREQ)) { myPrint("Failed to init LoRa\n"); }
  LoRa.setSyncWord(SYNC_WORD);
  LoRa.setSpreadingFactor(SPREAD_FACTOR);
  LoRa.setSignalBandwidth(BANDWITH);
  LoRa.setCodingRate4(CODING_RATE);
  LoRa.onTxDone(lora_tx_cb);
  LoRa.onReceive(lora_rx_cb);
  LoRa.receive(LORA_BUFFER_SIZE);
  myPrint("Setup done\n");
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
void loop() {
  tb.loop();
  lora_tx_manager();
  lora_rx_manager();
  if (updateNodeTime) { lora_send_ts(); }
}
//-----------------------------------------------------------------------