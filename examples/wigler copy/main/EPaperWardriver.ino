/* 
 * Wardriver for ESP32-S3 with E-Paper Display (With Battery Monitoring)
 * For LilyGO T5S3-4.7-e-paper-PRO
 */

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <epdiy.h>

#include "sdkconfig.h"

#include "firasans_12.h"
#include "firasans_20.h"

#ifdef ARDUINO_ARCH_ESP32
// Arduino
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <TinyGPS++.h>
// Battery monitoring
#include "bq27220.h"
#endif

#define WAVEFORM EPD_BUILTIN_WAVEFORM

// choose the default demo board depending on the architecture
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define DEMO_BOARD epd_board_v7
#endif

// Pin definitions
#define GPS_RX 44
#define GPS_TX 43
#define BOARD_I2C_SDA 39
#define BOARD_I2C_SCL 40

// WiFi scanning variables
int totalNetworks = 0;
int newNetworks = 0;

// BLE scanning variables
int totalBLEDevices = 0;
int newBLEDevices = 0;

// GPS variables
float latitude = 0.0;
float longitude = 0.0;
float altitude = 0.0;
int satellites = 0;
float speed = 0.0;
bool gpsValid = false;

// Battery variables
int batteryPercentage = 0;
int batteryVoltage = 0;
int batteryCurrent = 0;
float batteryTemperature = 0.0;
bool isCharging = false;

// BLE Scanner
BLEScan* pBLEScan;

// GPS
HardwareSerial GPSSerial(1); // Use UART1 for GPS
TinyGPSPlus gps;

// Battery Monitor
BQ27220 bq;
BQ27220BatteryStatus batt;

// For tracking networks and devices (in-memory storage)
#define MAX_NETWORKS 100
String foundBSSIDs[MAX_NETWORKS];
String foundSSIDs[MAX_NETWORKS];
int foundRSSIs[MAX_NETWORKS];
int foundChannels[MAX_NETWORKS];

#define MAX_BLE_DEVICES 100
String foundBLEAddresses[MAX_BLE_DEVICES];
String foundBLENames[MAX_BLE_DEVICES];
int foundBLERSSIs[MAX_BLE_DEVICES];

// EPD setup
EpdiyHighlevelState hl;

// Utility for checking draw errors
static inline void checkError(enum EpdDrawError err) {
    if (err != EPD_DRAW_SUCCESS) {
        Serial.printf("EPD draw error: %X\n", err);
    }
}

// Helper to safely write text with consistent alignment
void writeTextLine(const EpdFont* font, const char* text, int x, int y, uint8_t* fb) {
    EpdFontProperties font_props = epd_font_properties_default();
    font_props.flags = EPD_DRAW_ALIGN_LEFT;
    int cursor_x = x;
    int cursor_y = y;
    epd_write_string(font, text, &cursor_x, &cursor_y, fb, &font_props);
}

// BLE scan callback
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Feed the watchdog during long BLE callbacks
    vTaskDelay(1);
    
    String address = advertisedDevice.getAddress().toString().c_str();
    int rssi = advertisedDevice.getRSSI();
    String name = advertisedDevice.haveName() ? advertisedDevice.getName().c_str() : "Unknown";
    
    // Log the device to Serial
    Serial.print("BLE Device found: ");
    Serial.print(address);
    Serial.print(" Name: ");
    Serial.print(name);
    Serial.print(" RSSI: ");
    Serial.println(rssi);
    
    // Check if this is a new device
    bool isNew = true;
    for (int i = 0; i < totalBLEDevices && i < MAX_BLE_DEVICES; i++) {
      if (foundBLEAddresses[i] == address) {
        // Update the RSSI if we've seen this device before
        foundBLERSSIs[i] = rssi;
        isNew = false;
        break;
      }
    }
    
    if (isNew && totalBLEDevices < MAX_BLE_DEVICES) {
      // Add to our tracked devices
      foundBLEAddresses[totalBLEDevices] = address;
      foundBLENames[totalBLEDevices] = name;
      foundBLERSSIs[totalBLEDevices] = rssi;
      newBLEDevices++;
      totalBLEDevices++;
    }
  }
};

void updateBattery() {
  // Update battery information
  bq.getBatteryStatus(&batt);
  
  // Get battery status
  isCharging = bq.getIsCharging();
  batteryVoltage = bq.getVoltage();
  batteryCurrent = bq.getCurrent();
  batteryTemperature = (float)(bq.getTemperature() / 10.0);
  batteryPercentage = bq.getStateOfCharge();
  
  // Log battery status
  Serial.printf("Battery Status: %s\n", isCharging ? "Charging" : "Discharging");
  Serial.printf("Battery: %d%% (%d mV)\n", batteryPercentage, batteryVoltage);
  Serial.printf("Current: %d mA\n", batteryCurrent);
  Serial.printf("Temperature: %.1f K\n", batteryTemperature);
}

void setup() {
  // Initialize Serial
  Serial.begin(115200);
  Serial.println("ESP32-S3 E-Paper Wardriver Starting...");
  
  // Initialize I2C for display and battery monitor
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
  
  // Initialize battery monitor
  bq.init();
  updateBattery();
  Serial.println("Battery monitor initialized");
  
  // Initialize GPS
  GPSSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS initialized");
  
  // Initialize e-paper display
  epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_64K);
  // Set VCOM
  epd_set_vcom(1560);
  
  // Initialize high-level interface
  hl = epd_hl_init(WAVEFORM);
  
  // Set rotation to portrait
  epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
  
  Serial.printf("Display dimensions: %d x %d\n", 
                epd_rotated_display_width(), epd_rotated_display_height());
  
  // Get framebuffer
  uint8_t* fb = epd_hl_get_framebuffer(&hl);
  
  // Clear screen
  epd_poweron();
  epd_clear();
  int temperature = epd_ambient_temperature();
  epd_poweroff();
  
  // Select appropriate font
  const EpdFont* font;
  if (epd_width() < 1000) {
    font = &FiraSans_12;
  } else {
    font = &FiraSans_20;
  }
  
  // Display welcome message using a centered alignment
  EpdFontProperties font_props = epd_font_properties_default();
  font_props.flags = EPD_DRAW_ALIGN_CENTER;
  
  int cursor_x = epd_rotated_display_width() / 2;
  int cursor_y = 50;
  epd_write_string(font, "ESP32-S3 Wardriver", &cursor_x, &cursor_y, fb, &font_props);
  
  cursor_x = epd_rotated_display_width() / 2; // Reset x position
  cursor_y += 40;
  epd_write_string(font, "Initializing...", &cursor_x, &cursor_y, fb, &font_props);
  
  // Display battery status
  cursor_x = epd_rotated_display_width() / 2; // Reset x position
  cursor_y += 40;
  char batteryInfo[50];
  sprintf(batteryInfo, "Battery: %d%% (%s)", 
          batteryPercentage, 
          isCharging ? "Charging" : "Discharging");
  epd_write_string(font, batteryInfo, &cursor_x, &cursor_y, fb, &font_props);
  
  epd_poweron();
  checkError(epd_hl_update_screen(&hl, MODE_GL16, temperature));
  epd_poweroff();
  
  // Setup WiFi for scanning
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  // Initialize BLE scanner
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  
  // Ready message
  cursor_x = epd_rotated_display_width() / 2; // Reset x position
  cursor_y += 40;
  epd_write_string(font, "Ready to scan!", &cursor_x, &cursor_y, fb, &font_props);
  
  epd_poweron();
  checkError(epd_hl_update_screen(&hl, MODE_GL16, temperature));
  epd_poweroff();
  
  Serial.println("Setup complete. Starting scan loop...");
}

void updateGPS() {
  // Process GPS data
  while (GPSSerial.available() > 0) {
    if (gps.encode(GPSSerial.read())) {
      // Feed watchdog during potentially long GPS processing
      vTaskDelay(1);
      
      if (gps.location.isValid()) {
        latitude = gps.location.lat();
        longitude = gps.location.lng();
        gpsValid = true;
      } else {
        gpsValid = false;
      }
      
      if (gps.altitude.isValid()) {
        altitude = gps.altitude.meters();
      }
      
      if (gps.satellites.isValid()) {
        satellites = gps.satellites.value();
      }
      
      if (gps.speed.isValid()) {
        speed = gps.speed.kmph();
      }
    }
  }
  
  // Log GPS status
  if (gpsValid) {
    Serial.print("GPS Fix: ");
    Serial.print(latitude, 6);
    Serial.print(", ");
    Serial.print(longitude, 6);
    Serial.print(" Altitude: ");
    Serial.print(altitude);
    Serial.print("m Satellites: ");
    Serial.print(satellites);
    Serial.print(" Speed: ");
    Serial.print(speed);
    Serial.println(" km/h");
  } else {
    Serial.println("No GPS Fix");
  }
}

void scanWiFiNetworks() {
  Serial.println("Scanning WiFi networks...");
  
  // Start the WiFi scan
  WiFi.scanNetworks(true); // Async scan
  
  // Wait for scan to complete with periodic yield
  while (WiFi.scanComplete() < 0) {
    vTaskDelay(10); // Brief delay to feed watchdog
    updateGPS(); // Process GPS while waiting
  }
  
  int n = WiFi.scanComplete();
  if (n == 0) {
    Serial.println("No WiFi networks found.");
  } else if (n > 0) {
    Serial.print(n);
    Serial.println(" WiFi networks found.");
    
    for (int i = 0; i < n; ++i) {
      // Yield periodically during processing
      if (i % 3 == 0) vTaskDelay(1);
      
      String bssid = WiFi.BSSIDstr(i);
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      int channel = WiFi.channel(i);
      
      // Determine encryption type
      String encType;
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN:
          encType = "Open";
          break;
        case WIFI_AUTH_WEP:
          encType = "WEP";
          break;
        case WIFI_AUTH_WPA_PSK:
          encType = "WPA";
          break;
        case WIFI_AUTH_WPA2_PSK:
          encType = "WPA2";
          break;
        case WIFI_AUTH_WPA_WPA2_PSK:
          encType = "WPA/WPA2";
          break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
          encType = "WPA2-EAP";
          break;
        default:
          encType = "Unknown";
      }
      
      // Log to serial
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(ssid);
      Serial.print(" (");
      Serial.print(rssi);
      Serial.print("dBm) CH:");
      Serial.print(channel);
      Serial.print(" ");
      Serial.println(encType);
      
      // Check if this is a new network
      bool isNew = true;
      for (int j = 0; j < totalNetworks && j < MAX_NETWORKS; j++) {
        if (foundBSSIDs[j] == bssid) {
          // Update data for this network
          foundRSSIs[j] = rssi;
          foundChannels[j] = channel;
          isNew = false;
          break;
        }
      }
      
      if (isNew && totalNetworks < MAX_NETWORKS) {
        // Add to our tracked networks
        foundBSSIDs[totalNetworks] = bssid;
        foundSSIDs[totalNetworks] = ssid;
        foundRSSIs[totalNetworks] = rssi;
        foundChannels[totalNetworks] = channel;
        newNetworks++;
        totalNetworks++;
      }
    }
  }
  
  // Delete scan results to free memory
  WiFi.scanDelete();
}

void scanBLEDevices() {
  Serial.println("Scanning BLE devices...");
  
  // Reduced scan time to avoid watchdog timeout
  BLEScanResults foundDevices = pBLEScan->start(1, false);
  vTaskDelay(10); // Brief delay to feed watchdog
  
  Serial.print("BLE Scan completed. Devices found in this scan: ");
  Serial.println(foundDevices.getCount());
  
  pBLEScan->clearResults();
}

void updateDisplay() {
  // Get framebuffer
  uint8_t* fb = epd_hl_get_framebuffer(&hl);
  
  // Clear the framebuffer to white
  epd_hl_set_all_white(&hl);
  
  // Select font based on display width
  const EpdFont* font;
  if (epd_width() < 1000) {
    font = &FiraSans_12;
  } else {
    font = &FiraSans_20;
  }
  
  // Draw border around display for alignment reference
  EpdRect border = {
    .x = 5,
    .y = 5,
    .width = epd_rotated_display_width() - 10,
    .height = epd_rotated_display_height() - 10
  };
  epd_draw_rect(border, 0, fb);
  
  // Display title and stats with proper alignment
  int y_pos = 30; // Increased top margin
  char buffer[100];
  
  // Title
  writeTextLine(font, "ESP32-S3 Wardriver", 15, y_pos, fb);
  y_pos += 25; // Increased line spacing
  
  // Stats at top
  sprintf(buffer, "WiFi: %d (%d new) | BLE: %d (%d new)", 
          totalNetworks, newNetworks, totalBLEDevices, newBLEDevices);
  writeTextLine(font, buffer, 15, y_pos, fb);
  
  // GPS Section
  y_pos += 35; // More space before GPS section
  writeTextLine(font, "--- GPS Status ---", 15, y_pos, fb);
  y_pos += 25;
  
  if (gpsValid) {
    sprintf(buffer, "Location: %.6f, %.6f", latitude, longitude);
    writeTextLine(font, buffer, 15, y_pos, fb);
    y_pos += 25;
    
    sprintf(buffer, "Alt: %.1fm | Sats: %d | Speed: %.1f km/h", 
            altitude, satellites, speed);
    writeTextLine(font, buffer, 15, y_pos, fb);
  } else {
    writeTextLine(font, "No GPS Fix", 15, y_pos, fb);
  }
  
  // Line separator
  y_pos += 35; // More space before separator
  EpdRect line = {
    .x = 15,
    .y = y_pos,
    .width = epd_rotated_display_width() - 30,
    .height = 1
  };
  epd_fill_rect(line, 0, fb);
  
  // WiFi Networks Section
  y_pos += 20; // More space after separator
  writeTextLine(font, "--- Recent WiFi Networks ---", 15, y_pos, fb);
  y_pos += 25;
  
  // Display last 8 WiFi networks (or fewer if not enough found)
  int maxToShow = 8;
  int startIdx = (totalNetworks > maxToShow) ? (totalNetworks - maxToShow) : 0;
  
  for (int i = startIdx; i < totalNetworks; i++) {
    char wifi_info[100];
    if (foundSSIDs[i].length() > 13) {
      // Truncate long SSIDs
      sprintf(wifi_info, "%s... (-%ddBm) Ch:%d", 
             foundSSIDs[i].substring(0, 10).c_str(),
             -foundRSSIs[i], // Convert to positive for display
             foundChannels[i]);
    } else {
      sprintf(wifi_info, "%s (-%ddBm) Ch:%d", 
             foundSSIDs[i].c_str(),
             -foundRSSIs[i], // Convert to positive for display
             foundChannels[i]);
    }
    writeTextLine(font, wifi_info, 15, y_pos, fb);
    y_pos += 22; // Slightly more space between items
  }
  
  // Line separator
  y_pos += 15; // More space before separator
  line.y = y_pos;
  epd_fill_rect(line, 0, fb);
  
  // BLE Devices Section
  y_pos += 20; // More space after separator
  writeTextLine(font, "--- Recent BLE Devices ---", 15, y_pos, fb);
  y_pos += 25;
  
  // Display last 5 BLE devices (reduced from 6 to make space for battery info)
  maxToShow = 5;
  startIdx = (totalBLEDevices > maxToShow) ? (totalBLEDevices - maxToShow) : 0;
  
  for (int i = startIdx; i < totalBLEDevices; i++) {
    char ble_info[100];
    if (foundBLENames[i] != "Unknown" && foundBLENames[i].length() > 0) {
      if (foundBLENames[i].length() > 13) {
        // Truncate long names
        sprintf(ble_info, "%s... (-%ddBm)", 
               foundBLENames[i].substring(0, 10).c_str(),
               -foundBLERSSIs[i]); // Convert to positive for display
      } else {
        sprintf(ble_info, "%s (-%ddBm)", 
               foundBLENames[i].c_str(),
               -foundBLERSSIs[i]); // Convert to positive for display
      }
    } else {
      // Just show address if no name
      sprintf(ble_info, "%s (-%ddBm)", 
             foundBLEAddresses[i].c_str(),
             -foundBLERSSIs[i]); // Convert to positive for display
    }
    writeTextLine(font, ble_info, 15, y_pos, fb);
    y_pos += 22; // Slightly more space between items
  }
  
  // Add battery status at the bottom
  // Battery line separator
  y_pos += 15;
  line.y = y_pos;
  epd_fill_rect(line, 0, fb);
  
  // Update battery info
  updateBattery();
  
  // Battery status
  y_pos += 20;
  char batteryStatus[100];
  
  // Create battery status string with charging indicator
  sprintf(batteryStatus, "Battery: %d%% (%d mV) [%s]", 
          batteryPercentage, 
          batteryVoltage,
          isCharging ? "Charging" : "Discharging");
  
  writeTextLine(font, batteryStatus, 15, y_pos, fb);
  
  // Update the display with the new content
  epd_poweron();
  int temperature = epd_ambient_temperature();
  checkError(epd_hl_update_screen(&hl, MODE_GL16, temperature));
  epd_poweroff();
}

void loop() {
  // Reset counters for this loop
  newNetworks = 0;
  newBLEDevices = 0;
  
  // Update battery data
  updateBattery();
  vTaskDelay(1); // Feed watchdog
  
  // Update GPS data
  updateGPS();
  vTaskDelay(1); // Feed watchdog
  
  // Scan for WiFi networks with watchdog protection
  scanWiFiNetworks();
  vTaskDelay(1); // Feed watchdog
  
  // Scan for BLE devices with watchdog protection
  scanBLEDevices();
  vTaskDelay(1); // Feed watchdog
  
  // Update the display
  updateDisplay();
  vTaskDelay(1); // Feed watchdog
  
  // Print stats to serial
  Serial.println("=== Scan Summary ===");
  Serial.print("Total WiFi networks found: ");
  Serial.println(totalNetworks);
  Serial.print("New WiFi networks: ");
  Serial.println(newNetworks);
  Serial.print("Total BLE devices found: ");
  Serial.println(totalBLEDevices);
  Serial.print("New BLE devices: ");
  Serial.println(newBLEDevices);
  Serial.println("==================");
  
  // Delay before next scan
  Serial.println("Waiting before next scan...");
  
  // Use a loop with yield to prevent watchdog timeouts during the long delay
  for (int i = 0; i < 50; i++) {
    updateGPS(); // Continue updating GPS during delay
    
    // Update battery every 10 iterations (approximately once per second)
    if (i % 10 == 0) {
      updateBattery();
    }
    
    vTaskDelay(100); // 100ms delay with yield
  }
}