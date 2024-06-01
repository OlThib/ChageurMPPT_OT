//-----------------------------------------------------------------------
//
//  1. lora_send_buffer(uint8_t* byte_array, uint8_t expeditor, uint8_t destination, char subject, uint8_t array_size)
//  2. void lora_send_telemetry(void)
// 
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function takes a byte array in parameter as payload. Depending of the other paramaters specified, it fills another
// byte array representing the header before the payload. It then fills a buffer which is defined to be the size of the 
// expected transmited packet the receiver is waiting for. By using a fixed transmission size, a syncword and a header with subjects, it
// is easy to filter out false positives and errors. By having a sender and destination in the header allows for future developpement, where
// multiple stations can talk to each other.

void lora_send_buffer(uint8_t* byte_array, uint8_t expeditor, uint8_t destination, char subject, uint8_t array_size) {
  char debugBuf[128];
  sprintf(debugBuf, "Sending %d bytes over LoRa to station number %d. Subject: %c\n", array_size, destination, subject);
  myPrint(debugBuf);
  // Fill header
  headerUnion header;
  header.sender = expeditor;
  header.receiver = destination;
  header.subject = subject;
  header.palyoadSize = array_size;
  // Fill transmission buffer
    uint8_t loraBuf[LORA_BUFFER_SIZE];
  // First copy header
  for (byte i = 0; i < HEADER_SIZE; i++) { loraBuf[i] = header.byteArray[i]; }
  // Then copy the payload next to it
  for (byte i = 0; i < array_size; i++) { loraBuf[HEADER_SIZE + i] = byte_array[i]; }
  // Then for the rest of the buffer write zeros
  for (byte i = 0; i < LORA_BUFFER_SIZE - HEADER_SIZE - array_size; i++) { loraBuf[HEADER_SIZE + array_size + i] = 0; }
  // Start sending the buffer to the radio module
  LoRa.beginPacket();
  for (byte i = 0; i < LORA_BUFFER_SIZE; i++) { LoRa.write(loraBuf[i]); }
  // Start transmission in blocking mode. Giving true as param makes it async 
  LoRa.endPacket(false);
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// When the time is triggered, this function fills a union with the average of the values stored
// in the global averaging arrays. Then it saves it on the SD card before sending it over radio to
// the receiver.

void lora_send_telemetry(void) {
  static unsigned long telemetry_transmission_timer = telemetry_transmission_delay;
  if (millis() >= telemetry_transmission_timer) {
    telemetry_transmission_timer += telemetry_transmission_delay;
    telemetryUnion telemetry;
    // Fill union with average of global arrays
    telemetry.temp = ave_temp.mean();
    telemetry.hum = ave_hum.mean();
    telemetry.lum = ave_lum.mean();
    telemetry.pres = ave_pres.mean();
    telemetry.pv_v = ave_pv_v.mean();
    telemetry.pv_i = ave_pv_i.mean();
    telemetry.pv_w = ave_pv_w.mean();
    telemetry.bat_v = ave_bat_v.mean();
    telemetry.bat_i = ave_bat_i.mean();
    telemetry.bat_w = ave_bat_w.mean();
    // Get timestamp
    DateTime now = myRTC.now();
    telemetry.ts = now.unixtime();
    // Get file sizes
    File log_file = SD.open(logFile, FILE_READ);
    telemetry.logFileSize = log_file.size();
    log_file.close();
    File data_file = SD.open(dataFile, FILE_READ);
    telemetry.dataFileSize = data_file.size();
    data_file.close();
    // Save telemetry on SD card
    saveSDdata(telemetry);
    // Send telemetry over radio to receiver
    lora_send_buffer(telemetry.byteArray, LORA_STATION_ADDRESS, LORA_BRIDGE_ADDRESS, 'T', TELEMETRY_SIZE);
  }
}