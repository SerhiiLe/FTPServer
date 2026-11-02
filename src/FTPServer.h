/*
 * FTPServer - Lightweight, robust FTP Server for ESP8266 and ESP32
 *
 * Copyright (c) 2026 Serhii Lebedenko
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef FTP_SERVER_H
#define FTP_SERVER_H

#include <Arduino.h>
#include <FS.h>

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
#endif

class FTPServer {
public:
    /**
     * @brief Construct a new FTPServer instance.
     * @param fs Reference to the filesystem (e.g. LittleFS, SPIFFS, SD, FFat).
     * @param ctrlPort Port for FTP control connection (default: 21).
     * @param dataPort Port for FTP passive data connection (default: 50009).
     */
    explicit FTPServer(fs::FS &fs, uint16_t ctrlPort = 21, uint16_t dataPort = 50009);

    /**
     * @brief Destructor. Automatically stops server and closes open connections.
     */
    ~FTPServer();

    /**
     * @brief Start listening for incoming FTP connections.
     * @param user Username required for login. If empty, allows anonymous access.
     * @param pass Password required for login.
     */
    void begin(const String &user = "", const String &pass = "");

    /**
     * @brief Stop the FTP server and disconnect any connected clients.
     */
    void stop();

    /**
     * @brief Process incoming FTP traffic and data transfers.
     *        Must be called frequently from Arduino loop().
     */
    void handleFTP();

    /**
     * @brief Check whether a client is currently connected.
     * @return true if connected, false otherwise.
     */
    bool isConnected();

    /**
     * @brief Set connection inactivity timeout in milliseconds.
     * @param timeoutMs Inactivity timeout (default: 300,000 ms = 5 min).
     */
    void setTimeout(uint32_t timeoutMs);

private:
    enum ClientState {
        STATE_IDLE,
        STATE_WAIT_USER,
        STATE_WAIT_PASS,
        STATE_AUTHENTICATED
    };

    enum TransferState {
        TRANSFER_IDLE,
        TRANSFER_RETR,
        TRANSFER_STOR
    };

    fs::FS &_fs;
    uint16_t _ctrlPort;
    uint16_t _dataPort;

    WiFiServer _ctrlServer;
    WiFiServer _dataServer;

    WiFiClient _ctrlClient;
    WiFiClient _dataClient;

    File _file;

    String _user;
    String _pass;
    String _cwd;
    String _rnFrom;

    // Active mode (PORT command) parameters
    IPAddress _portIP;
    uint16_t _portPort;
    bool _isPassive;

    ClientState _clientState;
    TransferState _transferState;

    uint32_t _timeoutMs;
    uint32_t _lastActivity;

    // Transfer stats & state
    uint32_t _transferStartMs;
    size_t _fileSize;
    size_t _bytesTransferred;

    // Command line buffer (avoids per-byte String reallocation)
    static constexpr size_t CMD_BUF_SIZE = 256;
    char _cmdBuf[CMD_BUF_SIZE];
    size_t _cmdLen;

    // Transfer chunk buffer
    static constexpr size_t TRANSFER_BUF_SIZE = 1460;
    uint8_t *_transferBuffer;
    size_t _transferBufLen;

    // Private helper methods
    void resetSession();
    void closeDataConnection();
    void abortTransfer();
    void finishTransfer();

    int8_t openDataConnection();
    bool parsePortParam(const char *param, IPAddress &ip, uint16_t &port);

    void sendResponse(int code, const char *fmt = nullptr, ...);

    String resolvePath(const String &param);
    void createParentDirs(const String &path);
    void listDirectory(const String &targetPath, bool mlsd, bool nlst);
    String formatDateTime(time_t t, bool forMlsd);

    void processCommandLine(char *line);
    void executeCommand(const char *cmd, const char *param);

    bool transferFileToNetwork();
    bool transferNetworkToFile();

    void allocateTransferBuffer();
    void freeTransferBuffer();
};

#endif // FTP_SERVER_H

