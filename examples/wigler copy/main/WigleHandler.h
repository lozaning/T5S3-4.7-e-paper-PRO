/*
 * Wigle Format Handler
 * Helper class for managing WiGLE CSV format
 */

#include <Arduino.h>
#include <SD.h>
#include <time.h>

class WigleHandler {
private:
  String currentFilename;
  File logFile;
  
public:
  WigleHandler() {
    currentFilename = "";
  }
  
  bool begin(fs::FS &fs, String deviceName = "LilyGOEPaper") {
    // Generate a filename based on current date and time (or use millis if no RTC)
    String timestamp = String(millis());
    currentFilename = "/wigle_" + timestamp + ".csv";
    
    // Create the header file
    logFile = fs.open(currentFilename, FILE_WRITE);
    if (!logFile) {
      Serial.println("Failed to create Wigle log file!");
      return false;
    }
    
    // Write Wigle CSV header
    logFile.println("WigleWifi-1.4,appRelease=1.0,model=ESP32-S3,release=1.0,device=" + deviceName + ",display=true,board=ESP32S3,brand=LilyGO");
    logFile.println("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type");
    logFile.close();
    
    return true;
  }
  
  String getFilename() {
    return currentFilename;
  }
  
  bool logWiFiNetwork(fs::FS &fs, String mac, String ssid, String authMode, 
                      int channel, int rssi, float lat, float lon, float alt, 
                      bool hasGPS = true) {
    logFile = fs.open(currentFilename, FILE_APPEND);
    if (!logFile) {
      Serial.println("Failed to open Wigle log file for append!");
      return false;
    }
    
    // Generate timestamp
    String timestamp = String(millis());
    
    // MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type
    logFile.print(mac); logFile.print(",");
    
    // Escape any commas in the SSID to maintain CSV format
    ssid.replace(",", "\\,");
    logFile.print(ssid); logFile.print(",");
    
    logFile.print(authMode); logFile.print(",");
    logFile.print(timestamp); logFile.print(",");
    logFile.print(channel); logFile.print(",");
    logFile.print(rssi); logFile.print(",");
    
    // GPS data
    if (hasGPS) {
      logFile.print(lat, 6); logFile.print(",");
      logFile.print(lon, 6); logFile.print(",");
      logFile.print(alt, 1); logFile.print(",");
      logFile.print("0,"); // Accuracy - we don't have this value
    } else {
      logFile.print("0,0,0,0,"); // No GPS data
    }
    
    logFile.println("WIFI");
    logFile.close();
    
    return true;
  }
  
  bool logBLEDevice(fs::FS &fs, String mac, String name, int rssi, 
                    float lat, float lon, float alt, bool hasGPS = true) {
    logFile = fs.open(currentFilename, FILE_APPEND);
    if (!logFile) {
      Serial.println("Failed to open Wigle log file for append!");
      return false;
    }
    
    // Generate timestamp
    String timestamp = String(millis());
    
    // MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type
    logFile.print(mac); logFile.print(",");
    
    // Escape any commas in the name to maintain CSV format
    name.replace(",", "\\,");
    logFile.print(name); logFile.print(",");
    
    logFile.print("Unknown,"); // AuthMode for BLE
    logFile.print(timestamp); logFile.print(",");
    logFile.print("0,"); // Channel for BLE (not applicable)
    logFile.print(rssi); logFile.print(",");
    
    // GPS data
    if (hasGPS) {
      logFile.print(lat, 6); logFile.print(",");
      logFile.print(lon, 6); logFile.print(",");
      logFile.print(alt, 1); logFile.print(",");
      logFile.print("0,"); // Accuracy - we don't have this value
    } else {
      logFile.print("0,0,0,0,"); // No GPS data
    }
    
    logFile.println("BLE");
    logFile.close();
    
    return true;
  }
  
  uint32_t getRecordCount(fs::FS &fs) {
    uint32_t count = 0;
    
    logFile = fs.open(currentFilename, FILE_READ);
    if (!logFile) {
      Serial.println("Failed to open Wigle log file for reading!");
      return 0;
    }
    
    // Skip the first two lines (headers)
    String line = logFile.readStringUntil('\n');
    line = logFile.readStringUntil('\n');
    
    // Count remaining lines
    while (logFile.available()) {
      line = logFile.readStringUntil('\n');
      if (line.length() > 0) {
        count++;
      }
    }
    
    logFile.close();
    return count;
  }
  
  bool createNewFile(fs::FS &fs, String deviceName = "LilyGOEPaper") {
    // Close current file if open
    if (logFile) {
      logFile.close();
    }
    
    // Generate a new filename
    String timestamp = String(millis());
    currentFilename = "/wigle_" + timestamp + ".csv";
    
    // Create the header file
    logFile = fs.open(currentFilename, FILE_WRITE);
    if (!logFile) {
      Serial.println("Failed to create new Wigle log file!");
      return false;
    }
    
    // Write Wigle CSV header
    logFile.println("WigleWifi-1.4,appRelease=1.0,model=ESP32-S3,release=1.0,device=" + deviceName + ",display=true,board=ESP32S3,brand=LilyGO");
    logFile.println("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type");
    logFile.close();
    
    return true;
  }
};