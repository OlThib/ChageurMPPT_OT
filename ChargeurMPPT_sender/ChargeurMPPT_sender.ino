//-----------------------------------------------------------------------
// PROGRAM  :     ChargeurMPPT_sender.ino
// PLATFORM :     DOIT ESP32 DEVKIT V1
// DESCRIPTIOM :  Reads sensors and data on a SD card before sending over radio to the receiver
//-----------------------------------------------------------------------

//--- Libraries ---------------------------------------------------------
#include <Average.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <Adafruit_BME280.h>
#include <DS3231.h>
//#include <WiFi.h>   // Commented out to save compilation time
#include <Max44009.h>
#include <SPI.h>
#include <SD.h>
#include <LoRa.h>
//-----------------------------------------------------------------------

//--- Definitions -------------------------------------------------------
#define BAUDRATE 115200
#undef SERIAL_DEBUG  // define/undef to turn on/off serial debugging
#define SD_LOG       // define/undef to turn on/off SD log of serial debugging
// Lora config
#define LORA_BRIDGE_ADDRESS 1
#define LORA_STATION_ADDRESS 2  // Change this line if you have more than 2 nodes
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
#define BME_ADDRESS 0x76
#define GY_ADDRESS 0x4A
#define INA_PV_ADDRESS 0x40
#define INA_BAT_ADDRESS 0x41  // Solder A0 and/or A1 on the board to get custom different addresses : https://learn.adafruit.com/adafruit-ina219-current-sensor-breakout/assembly
//-----------------------------------------------------------------------

//--- Constants ---------------------------------------------------------
const uint8_t LORA_BUFFER_SIZE = HEADER_SIZE + PAYLOAD_SIZE;
const char *dataFile = "/data.csv";
const char *logFile = "/log.txt";
const char *header = "TIMESTAMP,TEMP,HUM,LUM,PRES,PV_V,PV_I,PV_W,BAT_V,BAT_I,BAT_w\n";
const char *logHeader = "--- ESP32 LOG FILE ---\n";
// Modify these 2 constants to change the data sampling frequency
const uint8_t recordings_per_minute = 4;
const uint8_t minutes_per_send = 1;
const uint8_t sensor_reading_time_seconds = 5;
// Do not modify these
const uint16_t num_points = recordings_per_minute * minutes_per_send;
const uint16_t sensor_reading_delay = sensor_reading_time_seconds * 1000;
const unsigned long averaging_delay = (60 / recordings_per_minute) * 1000;        // Delay in milliseconds between sensor readings
const unsigned long telemetry_transmission_delay = minutes_per_send * 60 * 1000;  // Delay in microseconds between transmissions
// Alpha factor for numeric filtering. Must be between 0 and 1.
const float alpha = 0.1F;
// Calibration factors
const float temp_coeff = 0.9766538F, temp_offset = -0.1886671F;
const float pv_v_coeff = 1.0F, pv_v_offset = -0.006F;
const float pv_i_coeff = 0.98588F, pv_i_offset = 0.3977F;
const float bat_v_coeff = 1.0F, bat_v_offset = -0.006F;
const float bat_i_coeff = 0.98498F, bat_i_offset = 0.2904F;
//-----------------------------------------------------------------------

//--- Objets / Globals --------------------------------------------------
// Sensors
Adafruit_INA219 ina_PV(INA_PV_ADDRESS);
Adafruit_INA219 ina_BAT(INA_BAT_ADDRESS);
Adafruit_BME280 bme;
Max44009 light(GY_ADDRESS);
// Averaging arrays
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
// RTC
DS3231 Clock;
RTClib myRTC;
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
void myPrint(char *buf);
float num_fltr(float new_value, float last_value);
bool readSensors(unsigned long sensor_reading_timer);
float calibratedValues(float rawValue, float linCoeff, float linOffset);
void push_average(void);
bool initSD(void);
unsigned long getLineCount(File file);
void saveSDlog(char *buf);
void saveSDdata(telemetryUnion payload);
String getTimeString(void);
// LoRa
void lora_send_payload(void);
//-----------------------------------------------------------------------

//-------- Setup --------------------------------------------------------
void setup() {
#ifdef SERIAL_DEBUG
  Serial.begin(BAUDRATE);
  delay(1000);
  Serial.printf("Booting up\n");
#endif
  delay(1000);
  // Turn off wifi/bt antena
  btStop();
  //WiFi.mode(WIFI_MODE_NULL);
  // Init SPI modules
  initSD();
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) { myPrint("Failed to init LoRa\n"); }
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SPREAD_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWITH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  // Init I2C modules (note: add error detection here)
  Wire.begin();
  Clock.setClockMode(false);
  light.setContinuousMode();
  bme.begin(BME_ADDRESS);
  ina_PV.begin();
  ina_BAT.begin();
  myPrint("Setup done\n");
}
//-----------------------------------------------------------------------

//-------- Loop ---------------------------------------------------------
void loop() {
  static unsigned long sleep_timer = millis();
  // This funtion has a static timer and is triggered every minute, depending on settings
  lora_send_telemetry();
  // readSensors returns true if it's been 5 second since last wake up
  if (readSensors(sleep_timer + sensor_reading_delay)) {
    // Calculate the time to sleep based on the time spent since last wake up ( = delay between measures minus time spent since last wake up )
    unsigned long sleep_duration = averaging_delay - (millis() - sleep_timer);
    // Multiply by 1000 to specify sleep time in microseconds
    esp_sleep_enable_timer_wakeup(sleep_duration * 1000);
    esp_light_sleep_start();
    // Reset timer once the uC wakes up
    sleep_timer = millis();
  }
}
//------------------------------------------------------------------------