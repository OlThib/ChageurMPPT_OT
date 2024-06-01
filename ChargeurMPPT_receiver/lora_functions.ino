//-----------------------------------------------------------------------
//
//  1. void lora_rx_manager(int packetSize)
// 
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// This function is called if the size of the packet parsed by the RFM95 is greater than 0
// Receives the packet size in parameter, which the number of bytes available on the radio buffer
// If the number of bytes in the radio buffer is the same size as the expected buffer size, the header is read first
// Then a switch calls the needed function depending on the header content

void lora_rx_manager(int packetSize) {
  char debugBuf1[64];
  sprintf(debugBuf1, "Received LoRa transmission: %d bytes with RSSI %d\n", packetSize, LoRa.packetRssi());
  myPrint(debugBuf1);
  // Only treat packets that have the right size
  if (packetSize == LORA_BUFFER_SIZE) {
    // Get header
    headerUnion header;
    LoRa.readBytes(header.byteArray, HEADER_SIZE);
    char debugBuf2[172];
    sprintf(debugBuf2, "Reading header: Sender = %d, Receiver = %d, Subject = %c, Payload size = %d bytes\n", header.sender, header.receiver, header.subject, header.palyoadSize);
    myPrint(debugBuf2);
    telemetryUnion telemetry;
    // First switch, if the transmission is destined to the present station
    if (header.receiver == LORA_STATION_ADDRESS) {
      // Call functions based on subject
      switch (header.subject) {
        case 'T':  // Telemetry
          LoRa.readBytes(telemetry.byteArray, TELEMETRY_SIZE);
          tb_sendTelemetry(telemetry);
          break;

        case 'M': // Text message
          break;

        default: // In case of a random transmission caught by the radio
          myPrint("Error : Wrong subject char received...");
          break;
      }
    } else {
      // Next switch, if message is destined to another station
    }
  }
}
//-----------------------------------------------------------------------
