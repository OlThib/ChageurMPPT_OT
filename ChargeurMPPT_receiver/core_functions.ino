//-----------------------------------------------------------------------
//
// 1. void myPrint(char* buf)
// 2. bool initSD(void)
// 3. unsigned long getLineCount(File file)
// 4. String getTimeString(void)
// 5. void saveSDlog(char* buf)
// 6. void saveSDdata(telemetryUnion telemetry)
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
      // If the SD module is initialized and a card is present, atempt to read log file
      File file = SD.open(logFile, FILE_READ);
      unsigned long logLineCount = getLineCount(file);
      file.close();
      // If no log file present, create one with header
      if (logLineCount == 0) {
        SD.open(logFile, FILE_WRITE);
        file.print(logHeader);
        file.close();
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
// This function a concatenated sting with the RTC values for date and time
// RTClib has methods that are way simpler and cleaner than this function. Needs to
// be changed.

String getTimeString(void) {
  DateTime now = myRtc.now();
  //String myString = now.toString();
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