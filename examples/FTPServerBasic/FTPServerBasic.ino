/*
 * FTPServerBasic.ino - Basic FTPServer example for ESP8266, ESP32, and ESP32-C3
 *
 * This example initializes LittleFS, connects to an existing Wi-Fi network
 * (or creates a SoftAP access point if credentials are empty), starts the
 * FTP server, and creates a default welcome file.
 *
 * Compatible with FileZilla, Midnight Commander, Total Commander, WinSCP, etc.
 * Note for FTP clients: configure connection limit to 1 concurrent connection.
 *
 * License: GNU Lesser General Public License v3.0 (LGPL-3.0-or-later)
 */

#include <Arduino.h>

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  #define CHIP_NAME "ESP8266"
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
  #define CHIP_NAME "ESP32"
#endif

#include <LittleFS.h>
#include <FTPServer.h>

// Wi-Fi Configuration:
// Leave empty ("") to start as a SoftAP Access Point.
const char *WIFI_SSID = "";        // Your Wi-Fi network SSID
const char *WIFI_PASS = "";        // Your Wi-Fi network password

// SoftAP fallback configuration (used if WIFI_SSID is empty or connection fails)
const char *AP_SSID = "ESP-FTP-Server";
const char *AP_PASS = "12345678";

// FTP Credentials (leave empty "" for anonymous access)
const char *FTP_USER = "esp";
const char *FTP_PASS = "esp";

// Create FTPServer instance bound to LittleFS
FTPServer ftpServer(LittleFS);

// Helper: create a default welcome file on LittleFS
void createWelcomeFile() {
    if (!LittleFS.exists("/welcome.txt")) {
        File f = LittleFS.open("/welcome.txt", "w");
        if (f) {
            f.println("==========================================");
            f.printf(" Welcome to %s FTP Server!\n", CHIP_NAME);
            f.println(" File transfer via LittleFS is active.");
            f.println("==========================================");
            f.close();
            Serial.println("[FS] Created /welcome.txt");
        }
    }
}

// Helper: print directory listing
void printDirectory(const char *dirName = "/") {
    Serial.printf("\n--- Directory content: %s ---\n", dirName);
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
    Dir dir = LittleFS.openDir(dirName);
    while (dir.next()) {
        if (dir.isDirectory()) {
            Serial.printf("  [DIR]  %s\n", dir.fileName().c_str());
        } else {
            Serial.printf("  [FILE] %-20s  %8u bytes\n", dir.fileName().c_str(), (unsigned int)dir.fileSize());
        }
    }
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    File root = LittleFS.open(dirName);
    if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f) {
            if (f.isDirectory()) {
                Serial.printf("  [DIR]  %s\n", f.name());
            } else {
                Serial.printf("  [FILE] %-20s  %8u bytes\n", f.name(), (unsigned int)f.size());
            }
            f = root.openNextFile();
        }
        root.close();
    }
#endif
    Serial.println("----------------------------------------\n");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n==========================================");
    Serial.printf("    %s FTPServer Basic Example\n", CHIP_NAME);
    Serial.println("==========================================");

    // 1. Mount LittleFS
    Serial.print("[FS] Mounting LittleFS... ");
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
    if (!LittleFS.begin()) {
        Serial.println("Format needed, formatting...");
        LittleFS.format();
        LittleFS.begin();
    }
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    if (!LittleFS.begin(true)) { // true = formatOnFail
        Serial.println("Mount failed!");
    }
#endif
    Serial.println("OK");

    createWelcomeFile();

    // 2. Setup Wi-Fi
    if (strlen(WIFI_SSID) > 0) {
        Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASS);

        unsigned long startMs = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startMs < 10000) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println(" CONNECTED!");
        } else {
            Serial.println("\n[WiFi] Connection failed, switching to SoftAP...");
            WiFi.mode(WIFI_AP);
            WiFi.softAP(AP_SSID, AP_PASS);
        }
    } else {
        Serial.println("[WiFi] No SSID provided, starting SoftAP...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
    }

    IPAddress ip = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();

    // 3. Start FTP Server
    ftpServer.begin(FTP_USER, FTP_PASS);

    Serial.println("==========================================");
    Serial.printf(" FTP Server started!\n");
    Serial.printf(" IP Address: %s\n", ip.toString().c_str());
    Serial.printf(" Port:       21 (PASV: 50009)\n");
    Serial.printf(" User:       %s\n", FTP_USER);
    Serial.printf(" Password:   %s\n", FTP_PASS);
    Serial.println("==========================================");
    Serial.printf("Connect via terminal:  ftp %s\n", ip.toString().c_str());
    Serial.println("Or open FileZilla / WinSCP / Midnight Commander");
    Serial.println("==========================================\n");

    printDirectory("/");
}

void loop() {
    // Process incoming FTP requests and transfers
    ftpServer.handleFTP();

    // Serial console helper commands
    if (Serial.available()) {
        char c = Serial.read();
        while (Serial.available()) Serial.read();

        if (c == 'l' || c == 'L') {
            printDirectory("/");
        } else if (c == 's' || c == 'S') {
            Serial.printf("FTP status: %s\n", ftpServer.isConnected() ? "Client connected" : "Idle");
        }
    }

    yield();
}

