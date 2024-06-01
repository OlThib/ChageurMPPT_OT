
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