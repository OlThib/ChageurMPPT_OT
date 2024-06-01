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

bool ntpReq(void) {
  bool retVal = false;
  uint8_t counter = 0;
  myPrint("Sending NTP request\n");
  setenv("TZ", "EST", 1);
  tzset();
  configTime(-5 * 3600, 0, "0.ca.pool.ntp.org");
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && counter < 30) {
    counter++;
    delay(500);
  }
  tm now;
  getLocalTime(&now);

  if (!now.tm_isdst) {
    myPrint("Adjusting clock for DST\n");
    configTime(-5 * 3600, 3600, "0.ca.pool.ntp.org");
    delay(500);
    getLocalTime(&now);
  }
  if (counter < 30) {
    retVal = true;
    Clock.setEpoch(mktime(&now));
  }
  return retVal;
}

void wifi_cnx(void) {
  myPrint("Connecting to WIFI\n");
  WiFi.begin(SSID_1, PWD_1);
  delay(5000);
  uint8_t counter = 0;
  while (WiFi.status() != WL_CONNECTED && counter < TIMEOUT_DELAY) {
    counter++;
    delay(500);
  }
  if (WiFi.status() != WL_CONNECTED) {
    // sprintf(debug, "Failed to connect to %s network\n", SSID_1);
    // myPrint(debug);
    // myPrint("Connecting to second network\n");
    WiFi.begin(SSID_2, PWD_2);
    counter = 0;
    while (WiFi.status() != WL_CONNECTED && counter < TIMEOUT_DELAY) {
      counter++;
      delay(500);
    }
    if (WiFi.status() != WL_CONNECTED) {
      // char debug[128];
      // sprintf(debug, "Failed to connect to %s network, rebooting\n", SSID_2);
      // myPrint(debug);
      delay(1000);
      ESP.restart();
    } else {
      // char debug[128];
      // sprintf(debug, "Connected to %s network\n", SSID_2);
      // myPrint(debug);
    }
  } else {
    // char debug[128];
    // sprintf("Connected to %s network\n", SSID_1);
    // myPrint(debug);
  }
}

bool checkConnexion(void) {
  bool retVal = true;
  if (WiFi.status() != WL_CONNECTED) {
    myPrint("Wifi connexion lost, reconnecting\n");
    wifi_cnx();
    ntpReq();
  }
  if (!tb.connected()) {
    myPrint("Disconnected from TB, reconnecting...\n");
    if (!tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) {
      myPrint("Failed to connect to TB\n");
      retVal = false;
    }
  }
  return retVal;
}



void tb_post(DynamicJsonDocument jsonDoc) {
#ifdef TB_POST
  String jsonString = "";
  serializeJson(jsonDoc, jsonString);
  uint16_t jsonLenght = jsonString.length() + 1;
  char jsonBuf[jsonLenght];
  jsonString.toCharArray(jsonBuf, jsonLenght);
  if (tb.sendTelemetryJson(jsonBuf)) {
    myPrint("Posted successfully on TB\n");
  } else {
    myPrint("Failed to post data on TB\n");
  }
#endif
}

void tb_sendTelemetry(telemetryUnion telemetry) {
  int lora_rssi = LoRa.packetRssi();
  int wifi_rssi = WiFi.RSSI();
  tm now = get_ds3231_time();
  int drift = mktime(&now) - telemetry.ts;
#ifdef SERIAL_DEBUG
  Serial.printf("\tT: %.2f°C\tH: %.2f%%\tL: %.2flux\tP: %.2fkPa\n", telemetry.temp, telemetry.hum, telemetry.lum, telemetry.pres);
  Serial.printf("\tPV_V: %.2fV\tPV_I: %.2fmA\tPV_W: %.2fmW\tLORA_RSSI: %ddB\n", telemetry.pv_v, telemetry.pv_i, telemetry.pv_w, lora_rssi);
  Serial.printf("\tBAT_V: %.2fV\tBAT_I: %.2fmA\tBAT_W: %.2fmW\tWIFI_RSSI: %ddB\n", telemetry.bat_v, telemetry.bat_i, telemetry.bat_w, wifi_rssi);
#endif
  if (checkConnexion()) {
    DynamicJsonDocument doc(512);
    doc["temp"] = String(telemetry.temp, 2);
    doc["hum"] = String(telemetry.hum, 2);
    doc["lum"] = String(telemetry.lum, 2);
    doc["pres"] = String(telemetry.pres, 2);
    doc["pv_v"] = String(telemetry.pv_v, 2);
    doc["pv_i"] = String(telemetry.pv_i, 2);
    doc["pv_w"] = String(telemetry.pv_w, 2);
    doc["bat_v"] = String(telemetry.bat_v, 2);
    doc["bat_i"] = String(telemetry.bat_i, 2);
    doc["bat_w"] = String(telemetry.bat_w, 2);
    doc["lora_rssi"] = String(lora_rssi);
    doc["wifi_rssi"] = String(wifi_rssi);
    doc["drift"] = String(drift);
    doc["node_logFileSize"] = String(telemetry.logFileSize);
    doc["node_dataFileSize"] = String(telemetry.dataFileSize);
    File log_file = SD.open(logFile, FILE_READ);
    unsigned long logFileSize = log_file.size();
    log_file.close();
    doc["bridge_logFileSize"] = String(logFileSize);
    tb_post(doc);
    //if (drift > MAX_TS_DRIFT) { updateNodeTime = true; }
  }
}

void tb_sendLog(String log) {
  Serial.println("Sending log info to TB : ");
  Serial.println(log);
  if (checkConnexion()) {
    DynamicJsonDocument doc(512);
    doc["log"] = log;
    tb_post(doc);
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
#ifdef SERIAL_DEBUG
      Serial.printf("Log file line count : %u\n", logLineCount);
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