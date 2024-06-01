//-----------------------------------------------------------------------
//
// 1. void myPrint(char* buf)
// 2. bool readSensors(unsigned long sensor_reading_timer)
// 3. float calibratedValues(float rawValue, float linCoeff, float linOffset)
// 4. bool initSD(void)
// 5. unsigned long getLineCount(File file)
// 6. void saveSDdata(telemetryUnion telemetry)
// 7. String getTimeString(void)
// 8. void saveSDlog(char* buf)
//
//-----------------------------------------------------------------------
// This funtion take a char array as argument and depending on the definitions
// in the header it will print it on the serial monitor and/or save it on the SD card

void myPrint(char* buf) {
#ifdef SERIAL_DEBUG
  Serial.print(buf);
#endif
#ifdef SD_LOG
  saveSDlog(buf);
#endif
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function applies a numeric filtering to the sensor reading values by taking a percentage
// of the new value and adding it to (1 minus that percentage) of the latest value
float num_fltr(float new_value, float last_value) {
  if (isnan(new_value) || isnan(last_value)) {
    return 0.0F;
  } else {
    return new_value * alpha + last_value * (1 - alpha);
  }
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function is called as often as possible in the loop. It uses static variables to keep track of last readings and is in charge of
// keeping track of the present sensor values. Take in parameter the last time the uC woke up and compares to a time 5 seconds ahead.
// Returns true if the sensors have been read for at least 5 seconds since wake up.
bool readSensors(unsigned long sensor_reading_timer) {
  bool retVal = false;
  static float temp = 0.0F, hum = 0.0F, lum = 0.0F, pres = 0.0F, pv_v = 0.0F, pv_i = 0.0F, pv_w = 0.0F, bat_v = 0.0F, bat_i = 0.0F, bat_w = 0.0F;
  // Read sensors
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
  // True if 5 seconds spent since last wake up
  if (millis() >= sensor_reading_timer) {
    retVal = true;
    ave_temp.push(calibratedValues(temp, temp_coeff, temp_offset));
    ave_hum.push(hum);
    ave_lum.push(lum);
    ave_pres.push(pres);
    ave_pv_v.push(calibratedValues(pv_v, pv_v_coeff, pv_v_offset));
    ave_pv_i.push(calibratedValues(pv_i, pv_i_coeff, pv_i_offset));
    ave_pv_w.push(pv_w);
    ave_bat_v.push(calibratedValues(bat_v, bat_v_coeff, bat_v_offset));
    ave_bat_i.push(calibratedValues(bat_i, bat_i_coeff, bat_i_offset));
    ave_bat_w.push(bat_w);
#ifdef SERIAL_DEBUG
    Serial.println("Pushing data into averaging array : ");
    Serial.printf("\tT: %.2f°C\tH: %.2f%%\tL: %.2flux\n", temp, hum, lum);
    Serial.printf("\tPV_V: %.2fV\tPV_I: %.2fmA\tPV_W: %.2fmW\n", pv_v, pv_i, pv_w);
    Serial.printf("\tBAT_V: %.2fV\tBAT_I: %.2fmA\tBAT_W: %.2fmW\n", bat_v, bat_i, bat_w);
#endif
  }
  return retVal;
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function is used to apply a linear correction the sensors that have been calibrated
// Takes in parameter the value read by the sensor and the correction constants
float calibratedValues(float rawValue, float linCoeff, float linOffset) {
  // Y = mx + b ...
  return linCoeff * rawValue + linOffset;
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function inits the SD module. If every is OK, it tries to open the data and log files.
// If they are not existant, the files will be created with their respective headers.
// Returns true if the module is initialised succesfully (functionnality not used yet)

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
      // If the SD module is initialized and a card is present, atempt to read data file
      File file1 = SD.open(dataFile, FILE_READ);
      unsigned long dataLineCount = getLineCount(file1);
      file1.close();
      // If no data file is present, create one with header
      if (dataLineCount == 0) {
        File file2 = SD.open(dataFile, FILE_WRITE);
        file2.print(header);
        file2.close();
      }
#ifdef SERIAL_DEBUG
      Serial.printf("Data file line count : %u\n", dataLineCount);
#endif
      // Attemp to read log file
      File file2 = SD.open(logFile, FILE_READ);
      unsigned long logLineCount = getLineCount(file2);
      file2.close();
      // If no log file present, create one with header
      if (logLineCount == 0) {
        SD.open(logFile, FILE_WRITE);
        file2.print(logHeader);
        file2.close();
      }
#ifdef SERIAL_DEBUG
      Serial.printf("Log file line count : %u\n", logLineCount);
#endif
      retVal = true;
    }
  }
  return retVal;
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function opens a given file passed in parameter and tries to open it.
// If successful it returns the number lines (\n)

unsigned long getLineCount(File file) {
  unsigned long linecount = 0;
  char inputChar;
  // If the file can be opened
  if (file) {
    // While no EOF
    while (file.available()) {
      // Read a char, if its a line return, add one
      inputChar = file.read();
      if (inputChar == '\n') {
        linecount++;
      }
    }
  }
  return linecount;
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function takes an union as parameter and formats it in a char array
// respecting a CSV format
void saveSDdata(telemetryUnion telemetry) {
  char buf[256];
  // FORMAT: TS, TEMP, HUM, LUM, PRES, PV_V, PV_I, PV_W, BAT_V, BAT_I, BAT_W
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
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function a concatenated sting with the RTC values for date and time
// RTClib has methods that are way simpler and cleaner than this function. Needs to
// be changed.

String getTimeString(void) {
  bool century = false;
  bool h12Flag;
  bool pmFlag;
  return String(Clock.getMonth(century)) + "/" + String(Clock.getDate()) + "/" + String(Clock.getYear()) + " " + String(Clock.getHour(h12Flag, pmFlag)) + ":" + String(Clock.getMinute()) + ":" + String(Clock.getSecond());
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function takes a char array as parameter and prints it in the log file
// on the SD card. It puts the date and time in front of it

void saveSDlog(char* buf) {
  File file = SD.open(logFile, FILE_APPEND);
  // Get the time stamp in format : [Y/M/D H/m/S]
  String tstamp = "[" + getTimeString() + "] ";
  // Print tstamp
  file.print(tstamp);
  // Print log
  file.print(buf);
  file.close();
}
//-----------------------------------------------------------------------