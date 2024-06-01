void lora_tx_cb(void) {
  loraTx = true;
}

void lora_rx_cb(int packetSize) {
  loraPacketSize = packetSize;
}

void lora_empty_buffer(void) {
  for (byte i = 0; i < LORA_BUFFER_SIZE; i++) { loraBuf[i] = 0; }
}

void lora_tx_manager(void) {
  if (loraTx) {
    loraTx = isTransmitting = false;
    LoRa.receive(LORA_BUFFER_SIZE);
    myPrint("Transmission done, going back to listening\n");
  }
}

void lora_send_buffer(uint8_t* buf, uint8_t len, uint8_t msgType) {
  if (isTransmitting) {
    myPrint("Antenna busy sending, adding message to queue\n");

    if (msgQindex == MAX_QUEUED_MESSAGES) {
      myPrint("Error: message queue is full, flushing message\n");
      return;
    }
    for (byte i = 0; i < LORA_BUFFER_SIZE; i++) {
      if (i < len) {
        msgQbuf[msgQindex * LORA_BUFFER_SIZE + i] = buf[i];
      } else {
        msgQbuf[i] = 0;
      }
    }
    msgQindex++;
  }
  // Set every value in the buffer to 0
  lora_empty_buffer();
  // Put the message type in the first byte
  loraBuf[0] = msgType;
  checkSumUnion checkSum;
  // Continue writing with payload data and calculate checkSum
  for (byte i = 0; i < len; i++) {
    loraBuf[MSGTYPE_SIZE + i] = buf[i];
    checkSum.value += buf[i];
  }
  // Skip to the end of the buffer to write checkSum size
  for (byte i = 0; i < CHECKSUM_SIZE; i++) { loraBuf[MSGTYPE_SIZE + PAYLOAD_SIZE + i] = checkSum.byteArray[i]; }
  char debug[128];
  sprintf(debug, "Sending %u bytes over radio, checkSum: %u\n", len + MSGTYPE_SIZE, checkSum.value);
  myPrint(debug);
  // Start writing into radio buffer
  LoRa.beginPacket();
  // Print payload
  for (byte i = 0; i < LORA_BUFFER_SIZE; i++) { LoRa.write(loraBuf[i]); }
  // Begin transmission in ASYNC mode
  LoRa.endPacket(false);
  LoRa.receive(LORA_BUFFER_SIZE);
  //isTransmitting = true;
}

void lora_rx_manager(void) {
  if (loraPacketSize == LORA_BUFFER_SIZE) {
    loraPacketSize = 0;
    myPrint("Received LoRa transmission from node\n");
    lora_empty_buffer();
    LoRa.readBytes(loraBuf, LORA_BUFFER_SIZE);
    if (lora_validate_payload()) {
      lora_read_payload();
    } else {
      //lora_send_reply(NAK, loraBuf[0]);
    }
  }
}

bool lora_validate_payload(void) {
  bool retVal = false;
  checkSumUnion sent_checkSum;
  uint16_t actual_checkSum = 0;

  // Retreive message type and check if its valid
  uint8_t msgType = loraBuf[0];
  for (byte i = 0; i < NUM_MSGTYPE; i++) {
    if (msgType == msgTypeList[i]) {
      i = NUM_MSGTYPE;
      // If msgType is valid, retreive the sent checkSum
      for (byte j = 0; j < CHECKSUM_SIZE; j++) { sent_checkSum.byteArray[j] = loraBuf[MSGTYPE_SIZE + PAYLOAD_SIZE + j]; }
      // Then count the actual checkSum
      for (byte k = 0; k < PAYLOAD_SIZE; k++) { actual_checkSum += loraBuf[MSGTYPE_SIZE + k]; }
      if (actual_checkSum == sent_checkSum.value) {
        retVal = true;
      } else {
        char debug[128];
        sprintf(debug, "Wrong checkSum : Expected %d but counted %d\n", sent_checkSum, actual_checkSum);
        myPrint(debug);
        //lora_send_reply(NAK, msgType);
      }
    } else {
      if (i == NUM_MSGTYPE - 1) {
        char debug[128];
        sprintf(debug, "Wrong msgType received : %d, discarding message\n", msgType);
        myPrint(debug);
      }
    }
  }
  return retVal;
}

void lora_msgQ_manager(void) {
  if (isTransmitting) { return; }
  char debug[128];
  sprintf(debug, "Sending queued message, total of %d messages in queue\n", msgQindex);
  myPrint(debug);
  LoRa.beginPacket();
  for (byte i = 0; i < LORA_BUFFER_SIZE; i++) { LoRa.write(msgQbuf[i]); }
  LoRa.endPacket(false);
  //isTransmitting = true;
  for (byte i = 0; i < (MAX_QUEUED_MESSAGES * LORA_BUFFER_SIZE); i++) {
    if (i < ((MAX_QUEUED_MESSAGES - 1) * LORA_BUFFER_SIZE)) {
      msgQbuf[i] = msgQbuf[i + LORA_BUFFER_SIZE];
    } else {
      msgQbuf[i] = 0;
    }
  }
  msgQindex--;
  if (msgQindex < 0) { msgQindex = 0; }
}

void lora_send_reply(uint8_t reply, uint8_t msgType) {
  char debug[128];
  sprintf(debug, "Sending %c as reply to msg type %c\n", (char)reply, (char)msgType);
  myPrint(debug);
  uint8_t buf[1];
  buf[0] = reply;
  lora_send_buffer(buf, sizeof(buf), msgType);
}

void lora_send_ts(void) {
  updateNodeTime = false;
  tm now = get_ds3231_time();
  tsUnion loraTs;
  loraTs.ts = mktime(&now);
  lora_send_buffer(loraTs.byteArray, TIME_SIZE, TIME);
}

void lora_read_payload(void) {
  telemetryUnion telemetry;
  String msg = "";
  uint8_t msgType = loraBuf[0];
  uint8_t reply = 0;

  switch (msgType) {
    case TIME:
      
      reply = loraBuf[1];
      if (reply == ACK) { myPrint("Received ACK reply to time update\n"); }
      else if (reply == NAK) { 
        myPrint("Received NAK reply to time update\n");
        // RETRY FLAG
      } else {
        char debug[128];
        sprintf(debug, "THIS IS NOT SUPPOSED TO HAPPEN, SUSPICIOUS CHAR : %c\n", (char)reply);
        myPrint(debug);
      }
      break;

    case MESSAGE:
      for (byte i = 0; i < PAYLOAD_SIZE; i++) { msg += (char)loraBuf[MSGTYPE_SIZE + i]; }
      if (msg.startsWith("*")) { myPrint("Received ACK reply to message text\n"); }
      else if (msg.startsWith("?")) {
        myPrint("Received NAK reply to text message\n");
        // RETRY FLAG
      } else if (msg.startsWith("Node just rebooted")) {
        //updateNodeTime = true;
      } else {
        myPrint("Something went wrong...\n");
      }
      break;

    case DATA:
      Serial.printf("Received sensor data from node\n");
      for (byte i = 0; i < TELEMETRY_SIZE; i++) { telemetry.byteArray[i] = loraBuf[MSGTYPE_SIZE + i]; }
      tb_sendTelemetry(telemetry);
      //lora_send_reply(ACK, DATA);
      break;

    default:
      Serial.printf("THIS SHOULD NEVER APPEAR, SUSPICIOUS CHAR : %c\n", (char)msgType);
      break;
  }
}

