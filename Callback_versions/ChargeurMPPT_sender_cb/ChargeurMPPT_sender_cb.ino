//--- Definitions -------------------------------------------------------
#define SD_LOG
#define SERIAL_DEBUG
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
#define BME_ADDRESS 0x76
#define GY_ADDRESS 0x4A
#define INA_PV_ADDRESS 0x40
#define INA_BAT_ADDRESS 0x41  // Solder A0 and/or A1 on the board to get custom addresses : https://learn.adafruit.com/adafruit-ina219-current-sensor-breakout/assembly

//-----------------------------------------------------------------------

//--- Déclaration des librairies (en ordre alpha) -----------------------
#include <Average.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <Adafruit_BME280.h>
#include <DS3231.h>
#include <WiFi.h>
#include <Max44009.h>
#include <SPI.h>
#include <SD.h>
#include <LoRa.h>

//-----------------------------------------------------------------------

//--- Constants ---------------------------------------------------------
const uint8_t LORA_BUFFER_SIZE = MSGTYPE_SIZE + PAYLOAD_SIZE + CHECKSUM_SIZE;
const uint8_t MESSAGE = 'M';
const uint8_t DATA = 'D';
const uint8_t TIME = 'T';
const uint8_t msgTypeList[NUM_MSGTYPE] = { MESSAGE, DATA, TIME };
const uint8_t ACK = '*';
const uint8_t NAK = '?';
const char *dataFile = "/data.csv";
const char *logFile = "/log.txt";
const uint8_t recordings_per_minute = 4;
const uint8_t minutes_per_send = 1;
const uint16_t num_points = recordings_per_minute * minutes_per_send;
const uint16_t sensor_reading_delay = 5 * 1000;
const unsigned long averaging_delay = (60 / recordings_per_minute) * 1000;  // Delay in milliseconds between sensor readings
const unsigned long transmission_delay = minutes_per_send * 60 * 1000;      // Delay in microseconds between transmissions
const float alpha = 0.1F;                                                   // Alpha factor that defines how aggressive the numeric filtering is

// Calibration factors
const float temp_coeff = 1.0F, temp_offset = 0.0F;
const float pv_v_coeff = 1.0F, pv_v_offset = -0.006F;
const float pv_i_coeff = 0.98588F, pv_i_offset = 0.3977F;
const float bat_v_coeff = 1.0F, bat_v_offset = -0.006F;
const float bat_i_coeff = 0.98498F, bat_i_offset = 0.2904F;
//-----------------------------------------------------------------------

//--- Typedefs ----------------------------------------------------------
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

//--- Objects -----------------------------------------------------------
Adafruit_INA219 ina_PV(INA_PV_ADDRESS);
Adafruit_INA219 ina_BAT(INA_BAT_ADDRESS);
Adafruit_BME280 bme;
Max44009 light(GY_ADDRESS);
Average<float> ave_temp(num_points);
Average<float> ave_hum(num_points);
Average<float> ave_lum(num_points);
Average<float> ave_pres(num_points);
Average<float> ave_pv_v(num_points);
Average<float> ave_pv_i(num_points);
Average<float> ave_pv_w(num_points);
Average<float> ave_bat_v(num_points);
Average<float> ave_bat_i(num_points);
Average<float> ave_bat_w(num_points);
DS3231 Clock;
RTClib myRTC;
//-----------------------------------------------------------------------

//--- Globals -----------------------------------------------------------
volatile bool loraTx = false;
volatile int loraPacketSize = 0;
bool msgRcv = false, isTransmitting = false;
uint8_t msgQindex = 0;
uint8_t loraBuf[LORA_BUFFER_SIZE];
uint8_t msgQbuf[MAX_QUEUED_MESSAGES * LORA_BUFFER_SIZE];
bool boolSD = false;
//-----------------------------------------------------------------------

//--- Prototypes --------------------------------------------------------
// Core functions
void myPrint(char* buf);
tm get_ds3231_time(void);
float num_fltr(float new_value, float last_value);
void read_sensors(void);
float calibrated_values(float rawValue, float linCoeff, float linOffset);
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
void lora_send_buffer(uint8_t *buf, uint8_t len, char msgType);
bool lora_validate_payload(void);
void lora_read_payload(void);
void lora_send_telemetry(void);
void lora_send_reply(uint8_t reply, uint8_t msgType);
void lora_say_hello(void);
//-----------------------------------------------------------------------

// tm local;
// if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) { // if reset or power loss
//   struct timeval val;
//   loadStruct(&local); // e.g. load time from eeprom
//   const time_t sec = mktime(&local); // make time_t
//   localtime(&sec); //set time
// } else {
//   getLocalTime(&local);
// }


void setup() {
#ifdef SERIAL_DEBUG
  Serial.begin(BAUDRATE);
#endif
  delay(1000);
  // Close WiFi and Bluetooth antenas
  btStop();
  WiFi.mode(WIFI_MODE_NULL);
  // Init sensors
  Wire.begin();
  light.setContinuousMode();
  bme.begin(BME_ADDRESS);
  ina_PV.begin();
  ina_BAT.begin();
  Clock.setClockMode(false);
  initSD();
  // Init RFM95 LoRa module
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(FREQ)) { myPrint("Failed to init LoRa\n"); }
  LoRa.setSyncWord(SYNC_WORD);
  LoRa.setSpreadingFactor(SPREAD_FACTOR);
  LoRa.setSignalBandwidth(BANDWITH);
  LoRa.setCodingRate4(CODING_RATE);
  LoRa.onTxDone(lora_tx_cb);
  LoRa.onReceive(lora_rx_cb);
  myPrint("Setup done\n");
  lora_say_hello();
}

void loop() {
  read_sensors();
  lora_send_telemetry();
  lora_tx_manager();
  lora_rx_manager();
  if (msgQindex) { lora_msgQ_manager(); }
  // Then go to sleep until its time for next recording
  //     unsigned long sleep_time = (recordings_delay - (millis() - recordings_timer)) * 1000;
  // #ifdef SERIAL_DEBUG
  //     Serial.printf("Going to sleep for %u milliseconds\n", sleep_time / 1000);
  //     delay(100);
  // #endif
  //esp_sleep_enable_timer_wakeup(sleep_time);
  //esp_light_sleep_start();
  //recordings_timer = millis();
  //readings_timer = millis();
}
