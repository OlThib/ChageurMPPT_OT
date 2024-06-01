//-----------------------------------------------------------------------
//
//  1. checkConnexion(void)
//  2. bool wifi_cnx(void)
//  3. bool connect_network(const char* ssid, const char* pswd)
//  4. bool ntpReq(void)
//  5. void tb_post(DynamicJsonDocument jsonDoc)
//  6. void tb_sendTelemetry(telemetryUnion telemetry)
// 
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function is used to very internet connexion before sending NTP request or posting on TB
// Checks wifi first, and reboots the ESP32 if it is unable to connect to a network
// Then it attemps to connect to TB
// Returns false if either of these operations fail 
// TO DO : Make it 2 separate funtions so that if WiFi is OK but TB isn't, backup telemetry data on SD to post later on

bool checkConnexion(void) {
  bool retVal = false;
  if (WiFi.status() != WL_CONNECTED) {
    myPrint("Connecting to WiFi\n");
    if (!wifi_cnx()) {
      myPrint("Failed to connect to a wifi network, rebooting.\n");
      delay(1000);
      ESP.restart();
    }
  }
  if (!tb.connected()) {
    myPrint("Connecting to Thingsboard\n");
    if (!tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) {
      myPrint("Failed to connect to TB\n");
    } else {
      retVal = true;
    }
  }
  return retVal;
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function goes through the list of known networks, defined in the credentials.h file
// If the connexion is successful for one of the networks, the index is set to be equal to the number of elements
// in the networks array of pointers, which ends the for loop immediatly
// Returns true only if the connexion was successful with one the networks

bool wifi_cnx(void) {
  bool retVal = false;
  for (byte i = 0; i < NUM_NETWORKS; i++) {
    if (connect_network(SSIDs[i], PWDs[i])) {
      retVal = true;
      i = NUM_NETWORKS;
    }
  }
  return retVal;
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function tries to connect to a WiFi access point, with the SSID and the pasword given in parameter
// Waits for 15 seconds (30 half seconds) before giving up and returning false
// Returns true if the connexion is established

bool connect_network(const char* ssid, const char* pswd) {
  bool retVal = false;
  WiFi.begin(ssid, pswd);
  uint8_t counter = 0;
  while (WiFi.status() != WL_CONNECTED && counter < TIMEOUT_DELAY) {
    counter++;
    delay(500);
  }
  if (WiFi.status() != WL_CONNECTED) {
    char debugBuf[64];
    sprintf(debugBuf, "Failed to connect to %s network\n", ssid);
    myPrint(debugBuf);
  } else {
    retVal = true;
    char debugBuf[64];
    sprintf(debugBuf, "Connected to %s network\n", ssid);
    myPrint(debugBuf);
  }
  return retVal;
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function sends an NTP request to the nearest canadian ntp server (hard coded).
// This updates the ESP32's "RTC", which is then used to update the DS3231 RTC module
// Returns false after the timeout delay of 15 seconds
// Note : uses <esp_sntp.h> for the sntp_get_sync_status() function

bool ntpReq(void) {
  bool retVal = false;
  uint8_t counter = 0;
  myPrint("Sending NTP request\n");
  setenv("TZ", "EST", 1);
  tzset();
  configTime(GMT_OFFSET * 3600, 0, "0.ca.pool.ntp.org");
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && counter < 30) {
    counter++;
    delay(500);
  }
  tm now;
  getLocalTime(&now);
  // If its not daylight saving time, add 1 hour (yeah, the logic is inverted here) 
  if (!now.tm_isdst) {
    myPrint("Adjusting clock for DST\n");
    configTime(GMT_OFFSET * 3600, 3600, "0.ca.pool.ntp.org");
    delay(500);
    getLocalTime(&now);
  }
  // Only true if NTP request was valid
  if (counter < 30) {
    retVal = true;
    // Sync the DS3231 RTC
    Clock.setEpoch(mktime(&now));
  } else {
    myPrint("Error: NTP request timed out\n");
  }
  return retVal;
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function takes a json document as parameter and serializes it into a string
// before posting in on TB
// TO DO : make it so it returns a bool

void tb_post(DynamicJsonDocument jsonDoc) {
#ifdef TB_POST_ENABLED
  if (!checkConnexion) { return; }
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
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function takes a telemetry union and formats it into a json document
// Change key names to fit your fields on TB. Fields on TB do not need to be
// predefined, TB will create a new key itelf when it sees one

void tb_sendTelemetry(telemetryUnion telemetry) {
#ifdef SERIAL_DEBUG
  myPrint("Received telemetry\n");
  Serial.printf("\tT: %.2f°C\tH: %.2f%%\tL: %.2flux\n", telemetry.temp, telemetry.hum, telemetry.lum);
  Serial.printf("\tPV_V: %.2fV\tPV_I: %.2fmA\tPV_W: %.2fmW\n", telemetry.pv_v, telemetry.pv_i, telemetry.pv_w);
  Serial.printf("\tBAT_V: %.2fV\tBAT_I: %.2fmA\tBAT_W: %.2fmW\n", telemetry.bat_v, telemetry.bat_i, telemetry.bat_w);
#endif
  File log_file = SD.open(logFile, FILE_READ);
  unsigned long bridge_logSize = log_file.size();
  log_file.close();
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
  doc["lora_rssi"] = String(LoRa.packetRssi());
  doc["wifi_rssi"] = String(WiFi.RSSI());
  doc["node_logSize"] = String(telemetry.logFileSize);
  doc["node_dataSize"] = String(telemetry.dataFileSize);
  doc["bridge_logSize"] = String(bridge_logSize);
  tb_post(doc);
}
//-----------------------------------------------------------------------