void myPrint(char* buf) {
#ifdef SERIAL_DEBUG
  Serial.print(buf);
#endif
#ifdef SD_LOG
  saveSDlog(buf);
#endif
}

tm get_ds3231_time(void) {
  bool century = false;
  bool h12Flag;
  bool pmFlag;
  tm now;
  now.tm_hour = Clock.getHour(h12Flag, pmFlag);
  now.tm_min = Clock.getMinute();
  now.tm_sec = Clock.getSecond();
  now.tm_year = Clock.getYear();
  now.tm_mon = Clock.getMonth(century);
  now.tm_mday = Clock.getDate();
  return now;
}

String getTimeString(void) {
  bool century = false;
  bool h12Flag;
  bool pmFlag;
  return String(Clock.getMonth(century)) + "/" + String(Clock.getDate()) + "/" + String(Clock.getYear()) + " " + String(Clock.getHour(h12Flag, pmFlag)) + ":" + String(Clock.getMinute()) + ":" + String(Clock.getSecond());
}

float num_fltr(float new_value, float last_value) {
  if (isnan(new_value) || isnan(last_value)) {
    return 0.0F;
  } else {
    return new_value * alpha + last_value * (1 - alpha);
  }
}

float calibrated_values(float rawValue, float linCoeff, float linOffset) {
  if (abs(rawValue / linOffset) <= 1.0F) {
    linOffset = 0.0F;
  }
  return linCoeff * rawValue + linOffset;
}

void read_sensors(void) {
  static float temp = 0.0F, hum = 0.0F, lum = 0.0F, pres = 0.0F, pv_v = 0.0F, pv_i = 0.0F, pv_w = 0.0F, bat_v = 0.0F, bat_i = 0.0F, bat_w = 0.0F;
  static unsigned long averaging_timer = averaging_delay;

  temp = num_fltr(bme.readTemperature(), temp);
  hum = num_fltr(bme.readHumidity(), hum);
  pres = num_fltr(bme.readPressure() / 1000, pres);
  lum = num_fltr(light.getLux(), lum);
  pv_v = num_fltr(ina_PV.getBusVoltage_V() + (ina_PV.getShuntVoltage_mV() / 1000), pv_v);
  pv_i = num_fltr(ina_PV.getCurrent_mA(), pv_i);
  pv_w = num_fltr(ina_PV.getPower_mW(), pv_w);
  bat_v = num_fltr(ina_BAT.getBusVoltage_V() + (ina_BAT.getShuntVoltage_mV() / 1000), bat_v);
  bat_i = num_fltr(ina_BAT.getCurrent_mA(), bat_i);
  bat_w = num_fltr(ina_BAT.getPower_mW(), bat_w);

  if (millis() >= averaging_timer) {
    averaging_timer += averaging_delay;
    ave_temp.push(calibrated_values(temp, temp_coeff, temp_offset));
    ave_hum.push(hum);
    ave_lum.push(lum);
    ave_pres.push(pres);
    ave_pv_v.push(calibrated_values(pv_v, pv_v_coeff, pv_v_offset));
    ave_pv_i.push(calibrated_values(pv_i, pv_i_coeff, pv_i_offset));
    ave_pv_w.push(pv_w);
    ave_bat_v.push(calibrated_values(bat_v, bat_v_coeff, bat_v_offset));
    ave_bat_i.push(calibrated_values(bat_i, bat_i_coeff, bat_i_offset));
    ave_bat_w.push(bat_w);
#ifdef SERIAL_DEBUG
    Serial.println("Recording data : ");
    Serial.printf("\tT: %.2f°C\tH: %.2f%%\tL: %.2flux\tP: %.2fkPa\n", temp, hum, lum, pres);
    Serial.printf("\tPV_V: %.2fV\tPV_I: %.2fmA\tPV_W: %.2fmW\n", pv_v, pv_i, pv_w);
    Serial.printf("\tBAT_V: %.2fV\tBAT_I: %.2fmA\tBAT_W: %.2fmW\n", bat_v, bat_i, bat_w);
#endif
  }
}

bool initSD(void) {
  bool retVal = false;
  if (!SD.begin(SD_CS_PIN)) {
#ifdef SERIAL_DEBUG
    Serial.printf("Failed to init SD module\n");
#endif
  } else {
    uint8_t card_type = SD.cardType();
    if (card_type == CARD_NONE) {
#ifdef SERIAL_DEBUG
      Serial.printf("No SD card present in module\n");
#endif
    } else {
      File file = SD.open(logFile);
      unsigned long logLineCount = getLineCount(file);
      file.close();
      File file2 = SD.open(dataFile);
      unsigned long dataLineCount = getLineCount(file2);
      file2.close();
#ifdef SERIAL_DEBUG
      Serial.printf("Log file line count : %u\nData file line count : %u\n", logLineCount, dataLineCount);
#endif
      retVal = true;
    }
  }
  return retVal;
}

unsigned long getLineCount(File file) {
  unsigned long linecount = 0;
  char inputChar;

  if (file) {
    while (file.available()) {
      inputChar = file.read();
      if (inputChar == '\n') {
        linecount++;
      }
    }
  }
  return linecount;
}

void saveSDlog(char* buf) {
  File file = SD.open(logFile, FILE_APPEND);
  String tstamp = "[" + getTimeString() + "] ";
  file.print(tstamp);
  file.print(buf);
  file.close();
}

void saveSDdata(telemetryUnion telemetry) {
  char buf[256];
  sprintf(buf, "%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", telemetry.ts,
          telemetry.temp,
          telemetry.hum,
          telemetry.lum,
          telemetry.pres,
          telemetry.pv_v,
          telemetry.pv_i,
          telemetry.pv_w,
          telemetry.bat_v,
          telemetry.bat_i,
          telemetry.bat_w);
  File file = SD.open(dataFile, FILE_APPEND);
  file.print(buf);
  file.close();
}
