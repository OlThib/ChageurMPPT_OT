// Program used to see sensor readings in real time duing calibration procedures
// Uses Average.h library to stabilize readings

#define BAUDRATE 115200
#define INA_PV_ADDRESS 0x40
#define INA_BAT_ADDRESS 0x41

#include <Adafruit_INA219.h>
#include <Average.h>
#include <Wire.h>
#include <DS3231.h>


const char* SSID_1 = "Network name";
const char* PWD_1 = "Network password";

Adafruit_INA219 ina_pv(INA_PV_ADDRESS);
Adafruit_INA219 ina_bat(INA_BAT_ADDRESS);

Average<float> ave_pv_v(100);
Average<float> ave_pv_i(100);
Average<float> ave_bat_v(10);
Average<float> ave_bat_i(10);
DS3231 Clock;
RTClib myRtc;



void setup() {
  Serial.begin(BAUDRATE);
  delay(1000);
  ina_pv.begin();
  ina_bat.begin();
  Wire.begin();
  Clock.setClockMode(false);
}

void loop() {
  static unsigned long readingsTimer = 1000;
  ave_pv_v.push(ina_pv.getBusVoltage_V() + ina_pv.getShuntVoltage_mV() / 1000);
  ave_pv_i.push(ina_pv.getCurrent_mA());
  ave_bat_v.push(ina_bat.getBusVoltage_V() + ina_bat.getShuntVoltage_mV() / 1000);
  ave_bat_i.push(ina_bat.getCurrent_mA());

  if (millis() >= readingsTimer) {
    readingsTimer += 1000;
    Serial.println("------------------------------");
    Serial.printf("PV_V = %.2f, PV_I = %.2f\n", ave_pv_v.mean(), ave_pv_i.mean());
    Serial.printf("BAT_V = %.2f, BAT_I = %.2f\n", ave_bat_v.mean(), ave_bat_i.mean());
  }
}
