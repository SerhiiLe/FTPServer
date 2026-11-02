# FTPServer for ESP8266, ESP32 and ESP32-C3

[Русская версия](README.md)

Lightweight, robust, and fast pure FTP Server library for **ESP8266**, **ESP32** (Xtensa), and **ESP32-C3** (RISC-V) microcontrollers with **LittleFS**, **SPIFFS**, **SD**, and **FFat** support.

Designed as an uncompromising replacement for legacy and unmaintained libraries, optimized for constrained RAM environments, maximum transfer throughput, and flash memory endurance.

---

## Features and Highlights

1. **Server-Only Design**:
   - Zero client code overhead, free of monolithic `FTPCommon` bloat.
   - Saves substantial flash memory (over 7 KB on ESP8266).
2. **Zero Heap Fragmentation**:
   - Command reception uses a fixed-size `char` buffer without per-byte dynamic string reallocations (`String += c`).
   - Protocol responses are formatted on the stack via `vsnprintf` without repeated `malloc` / `free` calls.
   - The data transfer buffer (1460 bytes = TCP MSS) is allocated dynamically **only during active file transfers** and freed immediately upon completion.
3. **RISC-V Safe (ESP32-C3 / C6)**:
   - Eliminates dangerous unaligned 32-bit pointer casts (`*(uint32_t*)`) that caused hardware `Load Alignment Fault` crashes on RISC-V architectures.
   - Command parsing uses safe `strcasecmp`.
4. **Hyphenated File Name Bug Fix**:
   - Fixed a critical legacy bug (`path.lastIndexOf("-")`) that corrupted filenames containing dashes, such as `firmware-v1.bin` or `wifi-config.json`.
   - Command flags (`LIST -la`, `LIST -a`) are parsed cleanly without mutilating target file paths.
5. **Block-Buffered Flash Writes (LittleFS)**:
   - Incoming file uploads (`STOR`) are buffered into 1460-byte blocks before writing to flash. This eliminates small 10–50 byte random writes, drastically reducing flash wear (write amplification) and boosting upload speed.
6. **Automatic Directory Creation**:
   - When uploading files to nested paths (e.g. `/data/logs/test.txt`), the server automatically creates missing parent directories.
7. **Arduino Core 2.x and 3.x Compatibility**:
   - Seamlessly uses `accept()` on ESP32/ESP8266 Core 3.x and falls back to `available()` on older cores.
   - Zero external dependency on the obsolete `PolledTimeout`.
8. **Encapsulated Sockets**:
   - `WiFiServer` instances are member variables of the class rather than static globals. Ports can be customized in the constructor (defaults: `21` and `50009`).

---

## Library Architecture

The library is designed around a single-threaded, non-blocking finite state machine (FSM):

```
+-----------------------------------------------------------+
|                        handleFTP()                        |
|                                                           |
| 1. _ctrlServer.accept()    -> Accept new client           |
| 2. Inactivity Timeout      -> Check timeout (millis)      |
| 3. State: RETR / STOR      -> Stream chunk (1460 B)       |
| 4. Command Parser          -> Read line and execute       |
+-----------------------------------------------------------+
```

* **Separation of Control and Data Channels**: The command channel (`Control`, port 21) manages authentication and state, while the data channel (`Data`, passive port 50009 or active port) opens strictly for directory listings and file transfers.
* **Filesystem Polymorphism**: The class operates over the abstract `fs::FS &` interface, providing drop-in compatibility with LittleFS, SPIFFS, SD, SD_MMC, and FFat.
* **Non-Blocking Streaming**: File read/write calls process fixed-size chunks (1460 bytes) per `handleFTP()` iteration, maintaining device responsiveness without triggering watchdog timer (WDT) resets.

---

## API Reference

### Constructor
```cpp
FTPServer(fs::FS &fs, uint16_t ctrlPort = 21, uint16_t dataPort = 50009);
```
* `fs`: Reference to the filesystem instance (e.g. `LittleFS`).
* `ctrlPort`: Control port (default: `21`).
* `dataPort`: Passive data port (default: `50009`).

### `begin`
```cpp
void begin(const String &user = "", const String &pass = "");
```
Starts listening on configured ports. Passing empty strings or omitting arguments enables anonymous access.

### `stop`
```cpp
void stop();
```
Stops the server, disconnects active clients, and releases open sockets and buffers.

### `handleFTP`
```cpp
void handleFTP();
```
Main processing loop. Handles connections, commands, and streaming file transfers. Must be called regularly within the Arduino `loop()`.

### `isConnected`
```cpp
bool isConnected();
```
Returns `true` if a client is currently connected.

### `setTimeout`
```cpp
void setTimeout(uint32_t timeoutMs);
```
Configures client inactivity timeout in milliseconds (default: `300000` ms = 5 minutes).

---

## Server Lifecycle Management

The server can be activated and deactivated dynamically based on runtime conditions or web-UI flags (pattern from the `Clock_Mini` project):

```cpp
#include <LittleFS.h>
#include <FTPServer.h>

FTPServer ftpSrv(LittleFS);
bool ftp_isStarted = false;
bool ftp_isAllow = false; // Enabled/disabled via user settings or web UI

void ftp_process() {
    if (ftp_isStarted) {
        if (ftp_isAllow) {
            ftpSrv.handleFTP();
        } else {
            ftpSrv.stop();
            ftp_isStarted = false;
        }
    } else {
        if (ftp_isAllow) {
            ftpSrv.begin("admin", "password123");
            ftp_isStarted = true;
        }
    }
}
```

---

## Supported FTP Commands

| Command | Description |
| :--- | :--- |
| `USER`, `PASS` | User authentication (or anonymous login) |
| `QUIT` | Terminate session and disconnect |
| `PWD`, `XPWD` | Print working directory |
| `CWD`, `XCWD` | Change working directory (supports `.`, `..`, relative and absolute paths) |
| `CDUP`, `XCUP` | Change to parent directory |
| `PASV` | Enter passive data mode (recommended for modern clients) |
| `PORT` | Enter active data mode |
| `TYPE` | Set representation type (`A` — ASCII, `I` / `L 8` — Binary) |
| `STRU` | File structure (`F` — File) |
| `MODE` | Transfer mode (`S` — Stream) |
| `LIST` | Unix-style directory listing (supports flags like `-la`, `-a`, `-l`) |
| `MLSD` | Machine-readable listing conforming to RFC 3659 |
| `NLST` | Name list |
| `RETR` | Retrieve (download) file |
| `STOR` | Store (upload) file (with automatic parent folder creation) |
| `DELE` | Delete file |
| `MKD`, `XMKD` | Make directory |
| `RMD`, `XRMD` | Remove directory |
| `RNFR`, `RNTO` | Rename / move file |
| `SIZE` | Return file size |
| `MDTM` | Return file last modification time |
| `FEAT` | List supported protocol extensions |
| `SYST` | Return system type (`UNIX Type: L8`) |
| `NOOP` | No-op keepalive |
| `ABOR` | Abort active transfer |

---

## Basic Example

A complete ready-to-run sketch is available at [`examples/FTPServerBasic/FTPServerBasic.ino`](examples/FTPServerBasic/FTPServerBasic.ino).

```cpp
#include <Arduino.h>
#include <LittleFS.h>
#include <FTPServer.h>

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
#endif

FTPServer ftpServer(LittleFS);

void setup() {
    Serial.begin(115200);
    LittleFS.begin();

    WiFi.begin("WiFi_SSID", "WiFi_PASSWORD");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    // Start server with credentials
    ftpServer.begin("esp", "esp");
}

void loop() {
    ftpServer.handleFTP();
}
```

> [!TIP]
> When using multi-threaded FTP clients (such as FileZilla or WinSCP), configure connection settings to limit the maximum number of simultaneous connections to **1**.

---

## License

This library is licensed under the **GNU Lesser General Public License v3.0 (LGPL-3.0-or-later)**.
See [LICENSE](LICENSE) for details.

