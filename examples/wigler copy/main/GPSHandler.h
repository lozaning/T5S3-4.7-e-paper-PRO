/*
 * GPS Handler
 * Helper class for GPS functionality
 */

#include <Arduino.h>
#include <TinyGPS++.h>

class GPSHandler {
private:
  TinyGPSPlus gps;
  HardwareSerial *gpsSerial;
  unsigned long lastValidFix = 0;
  const unsigned long GPS_MAX_AGE_MS = 10000; // Consider GPS stale after 10 seconds

public:
  float latitude = 0;
  float longitude = 0;
  float altitude = 0;
  float speed = 0;
  float course = 0;
  int satellites = 0;
  int hdop = 0;
  bool fixValid = false;
  String dateTime = "";
  
  GPSHandler(HardwareSerial &serial) {
    gpsSerial = &serial;
  }
  
  void begin(unsigned long baud, int8_t rxPin, int8_t txPin) {
    gpsSerial->begin(baud, SERIAL_8N1, rxPin, txPin);
  }
  
  void update() {
    while (gpsSerial->available() > 0) {
      char c = gpsSerial->read();
      if (gps.encode(c)) {
        updateGPSData();
      }
    }
    
    // Check if fix is stale
    if (millis() - lastValidFix > GPS_MAX_AGE_MS && fixValid) {
      fixValid = false;
    }
  }
  
  void updateGPSData() {
    // Update location if valid
    if (gps.location.isValid()) {
      latitude = gps.location.lat();
      longitude = gps.location.lng();
      fixValid = true;
      lastValidFix = millis();
    }
    
    // Update other GPS data
    if (gps.altitude.isValid()) {
      altitude = gps.altitude.meters();
    }
    
    if (gps.speed.isValid()) {
      speed = gps.speed.kmph();
    }
    
    if (gps.course.isValid()) {
      course = gps.course.deg();
    }
    
    if (gps.satellites.isValid()) {
      satellites = gps.satellites.value();
    }
    
    if (gps.hdop.isValid()) {
      hdop = gps.hdop.value();
    }
    
    // Update date/time
    if (gps.date.isValid() && gps.time.isValid()) {
      char dateTimeStr[32];
      sprintf(dateTimeStr, "%04d-%02d-%02d %02d:%02d:%02d", 
              gps.date.year(), gps.date.month(), gps.date.day(),
              gps.time.hour(), gps.time.minute(), gps.time.second());
      dateTime = String(dateTimeStr);
    }
  }
  
  String getLatitudeStr() {
    if (fixValid) {
      return String(latitude, 6);
    }
    return "No Fix";
  }
  
  String getLongitudeStr() {
    if (fixValid) {
      return String(longitude, 6);
    }
    return "No Fix";
  }
  
  String getFormattedLocation() {
    if (fixValid) {
      return getLatitudeStr() + ", " + getLongitudeStr();
    }
    return "No GPS Fix";
  }
  
  String getStatusStr() {
    if (fixValid) {
      return "Fix OK";
    } else if (satellites > 0) {
      return "Searching...";
    }
    return "No Signal";
  }
  
  // Check if GPS data is fresh enough to be used
  bool isFixFresh() {
    return fixValid && (millis() - lastValidFix <= GPS_MAX_AGE_MS);
  }
  
  // Get number of satellites as a string (with prefix)
  String getSatellitesStr() {
    return "Sats: " + String(satellites);
  }
  
  // Simple validity check for coordinates
  bool hasValidCoordinates() {
    return fixValid && latitude != 0.0 && longitude != 0.0;
  }
};