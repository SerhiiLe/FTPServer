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

#include "FTPServer.h"
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdarg.h>

#if (defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)) || (defined(ARDUINO_ESP8266_MAJOR) && (ARDUINO_ESP8266_MAJOR >= 3))
  #define FTPSRV_ACCEPT(srv) srv.accept()
#else
  #define FTPSRV_ACCEPT(srv) srv.available()
#endif

FTPServer::FTPServer(fs::FS &fs, uint16_t ctrlPort, uint16_t dataPort)
    : _fs(fs),
      _ctrlPort(ctrlPort),
      _dataPort(dataPort),
      _ctrlServer(ctrlPort),
      _dataServer(dataPort),
      _isPassive(true),
      _clientState(STATE_IDLE),
      _transferState(TRANSFER_IDLE),
      _timeoutMs(5 * 60 * 1000),
      _lastActivity(0),
      _transferStartMs(0),
      _fileSize(0),
      _bytesTransferred(0),
      _cmdLen(0),
      _transferBuffer(nullptr),
      _transferBufLen(0)
{
    _cmdBuf[0] = '\0';
}

FTPServer::~FTPServer() {
    stop();
}

void FTPServer::begin(const String &user, const String &pass) {
    _user = user;
    _pass = pass;
    resetSession();
    _ctrlServer.begin();
    _dataServer.begin();
}

void FTPServer::stop() {
    if (_ctrlClient.connected()) {
        sendResponse(221, "Service closing control connection.");
        _ctrlClient.stop();
    }
    resetSession();
    _ctrlServer.stop();
    _dataServer.stop();
}

bool FTPServer::isConnected() {
    return _ctrlClient.connected();
}

void FTPServer::setTimeout(uint32_t timeoutMs) {
    _timeoutMs = timeoutMs;
}

void FTPServer::resetSession() {
    if (_file) {
        _file.close();
    }
    closeDataConnection();
    freeTransferBuffer();

    _clientState = STATE_IDLE;
    _transferState = TRANSFER_IDLE;
    _cwd = "/";
    _rnFrom = "";
    _cmdLen = 0;
    _isPassive = true;
    _bytesTransferred = 0;
    _fileSize = 0;
}

void FTPServer::closeDataConnection() {
    if (_dataClient.connected()) {
        _dataClient.stop();
    }
}

void FTPServer::allocateTransferBuffer() {
    if (!_transferBuffer) {
        _transferBuffer = (uint8_t *)malloc(TRANSFER_BUF_SIZE);
    }
    _transferBufLen = 0;
}

void FTPServer::freeTransferBuffer() {
    if (_transferBuffer) {
        free(_transferBuffer);
        _transferBuffer = nullptr;
    }
    _transferBufLen = 0;
}

void FTPServer::sendResponse(int code, const char *fmt, ...) {
    char buf[192];
    int len = snprintf(buf, sizeof(buf), "%d", code);
    if (fmt && *fmt && len < (int)sizeof(buf) - 2) {
        buf[len++] = ' ';
        va_list args;
        va_start(args, fmt);
        len += vsnprintf(buf + len, sizeof(buf) - len - 2, fmt, args);
        va_end(args);
    }
    if (len > (int)sizeof(buf) - 3) {
        len = sizeof(buf) - 3;
    }
    buf[len++] = '\r';
    buf[len++] = '\n';
    buf[len] = '\0';
    if (_ctrlClient.connected()) {
        _ctrlClient.write((const uint8_t *)buf, len);
    }
}

String FTPServer::resolvePath(const String &param) {
    String p = param;
    p.trim();
    if (p.length() == 0) return _cwd;

    String full;
    if (p[0] == '/') {
        full = p;
    } else {
        if (_cwd == "/") full = "/" + p;
        else full = _cwd + "/" + p;
    }

    String result = "";
    int start = 0;
    int len = full.length();
    while (start < len) {
        while (start < len && full[start] == '/') start++;
        if (start >= len) break;
        int end = full.indexOf('/', start);
        if (end < 0) end = len;
        String token = full.substring(start, end);
        start = end;

        if (token == ".") {
            continue;
        } else if (token == "..") {
            int lastSlash = result.lastIndexOf('/');
            if (lastSlash > 0) {
                result = result.substring(0, lastSlash);
            } else {
                result = "";
            }
        } else {
            result += "/" + token;
        }
    }

    if (result.length() == 0) result = "/";
    return result;
}

void FTPServer::createParentDirs(const String &path) {
    int slash = path.indexOf('/', 1);
    while (slash > 0) {
        String sub = path.substring(0, slash);
        if (!_fs.exists(sub)) {
            _fs.mkdir(sub);
        }
        slash = path.indexOf('/', slash + 1);
    }
}

int8_t FTPServer::openDataConnection() {
    if (_isPassive) {
        uint32_t startMs = millis();
        while (!_dataClient.connected() && (millis() - startMs < 2000)) {
            if (_dataServer.hasClient()) {
                _dataClient.stop();
                _dataClient = FTPSRV_ACCEPT(_dataServer);
                break;
            }
            delay(5);
            yield();
        }
        return _dataClient.connected() ? 1 : -1;
    } else {
        _dataClient.stop();
        if (_dataClient.connect(_portIP, _portPort)) {
            return 1;
        }
        return -1;
    }
}

bool FTPServer::parsePortParam(const char *param, IPAddress &ip, uint16_t &port) {
    int parts[6];
    int parsed = sscanf(param, "%d,%d,%d,%d,%d,%d",
                        &parts[0], &parts[1], &parts[2], &parts[3], &parts[4], &parts[5]);
    if (parsed == 6) {
        ip = IPAddress(parts[0], parts[1], parts[2], parts[3]);
        port = (parts[4] << 8) | parts[5];
        return true;
    }
    return false;
}

String FTPServer::formatDateTime(time_t t, bool forMlsd) {
    if (t == 0) {
        if (forMlsd) return "19700101000000";
        return "Jan 01  1970";
    }

    struct tm timeinfo;
    gmtime_r(&t, &timeinfo);

    char buf[32];
    if (forMlsd) {
        strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &timeinfo);
    } else {
        time_t now = time(nullptr);
        struct tm nowInfo;
        gmtime_r(&now, &nowInfo);
        if (timeinfo.tm_year == nowInfo.tm_year && now > 100000) {
            strftime(buf, sizeof(buf), "%b %d %H:%M", &timeinfo);
        } else {
            strftime(buf, sizeof(buf), "%b %d  %Y", &timeinfo);
        }
    }
    return String(buf);
}

void FTPServer::listDirectory(const String &targetPath, bool mlsd, bool nlst) {
    uint32_t count = 0;

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
    Dir dir = _fs.openDir(targetPath);
    while (dir.next()) {
        bool isDir = dir.isDirectory();
        String name = dir.fileName();
        size_t size = dir.fileSize();
        time_t t = dir.fileTime();

        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);

        if (nlst) {
            _dataClient.printf("%s\r\n", name.c_str());
        } else if (mlsd) {
            String dt = formatDateTime(t, true);
            if (isDir) {
                _dataClient.printf("type=dir;modify=%s; %s\r\n", dt.c_str(), name.c_str());
            } else {
                _dataClient.printf("type=file;size=%lu;modify=%s; %s\r\n", (unsigned long)size, dt.c_str(), name.c_str());
            }
        } else {
            String dt = formatDateTime(t, false);
            _dataClient.printf("%crw-r--r-- 1 owner group %8lu %s %s\r\n",
                isDir ? 'd' : '-',
                (unsigned long)(isDir ? 0 : size),
                dt.c_str(),
                name.c_str());
        }
        count++;
        if ((count % 16) == 0) yield();
    }
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    File dir = _fs.open(targetPath);
    if (dir && dir.isDirectory()) {
        File f = dir.openNextFile();
        while (f) {
            bool isDir = f.isDirectory();
            const char *rawName = f.name();
            const char *slash = strrchr(rawName, '/');
            const char *name = slash ? (slash + 1) : rawName;
            size_t size = f.size();
            time_t t = f.getLastWrite();

            if (nlst) {
                _dataClient.printf("%s\r\n", name);
            } else if (mlsd) {
                String dt = formatDateTime(t, true);
                if (isDir) {
                    _dataClient.printf("type=dir;modify=%s; %s\r\n", dt.c_str(), name);
                } else {
                    _dataClient.printf("type=file;size=%lu;modify=%s; %s\r\n", (unsigned long)size, dt.c_str(), name);
                }
            } else {
                String dt = formatDateTime(t, false);
                _dataClient.printf("%crw-r--r-- 1 owner group %8lu %s %s\r\n",
                    isDir ? 'd' : '-',
                    (unsigned long)(isDir ? 0 : size),
                    dt.c_str(),
                    name);
            }
            count++;
            if ((count % 16) == 0) yield();
            f = dir.openNextFile();
        }
        dir.close();
    }
#endif
    (void)count;
}

void FTPServer::finishTransfer() {
    if (_file) {
        _file.close();
    }
    closeDataConnection();

    uint32_t durationMs = millis() - _transferStartMs;
    if (durationMs == 0) durationMs = 1;
    float kbSec = ((float)_bytesTransferred / 1024.0f) / ((float)durationMs / 1000.0f);

    sendResponse(226, "Transfer complete. %lu bytes in %lu ms (%.2f KB/s).",
                 (unsigned long)_bytesTransferred, (unsigned long)durationMs, kbSec);

    _transferState = TRANSFER_IDLE;
    freeTransferBuffer();
}

void FTPServer::abortTransfer() {
    if (_file) {
        _file.close();
    }
    closeDataConnection();
    _transferState = TRANSFER_IDLE;
    freeTransferBuffer();
    sendResponse(426, "Transfer aborted.");
}

bool FTPServer::transferFileToNetwork() {
    if (!_dataClient.connected()) {
        abortTransfer();
        return false;
    }

    if (_bytesTransferred >= _fileSize) {
        finishTransfer();
        return false;
    }

    size_t toRead = _fileSize - _bytesTransferred;
    if (toRead > TRANSFER_BUF_SIZE) toRead = TRANSFER_BUF_SIZE;

    size_t n = _file.read(_transferBuffer, toRead);
    if (n > 0) {
        size_t written = _dataClient.write(_transferBuffer, n);
        _bytesTransferred += written;
        _lastActivity = millis();
    }

    if (_bytesTransferred >= _fileSize) {
        finishTransfer();
        return false;
    }
    return true;
}

bool FTPServer::transferNetworkToFile() {
    if (_dataClient.available() > 0) {
        int avail = _dataClient.available();
        int space = TRANSFER_BUF_SIZE - _transferBufLen;
        int toRead = (avail < space) ? avail : space;

        int n = _dataClient.read(_transferBuffer + _transferBufLen, toRead);
        if (n > 0) {
            _transferBufLen += n;
            _bytesTransferred += n;
            _lastActivity = millis();

            if (_transferBufLen >= TRANSFER_BUF_SIZE) {
                _file.write(_transferBuffer, _transferBufLen);
                _transferBufLen = 0;
            }
        }
    }

    if (!_dataClient.connected() && _dataClient.available() == 0) {
        if (_transferBufLen > 0) {
            _file.write(_transferBuffer, _transferBufLen);
            _transferBufLen = 0;
        }
        finishTransfer();
        return false;
    }
    return true;
}

void FTPServer::processCommandLine(char *line) {
    while (*line == ' ') line++;
    if (*line == '\0') return;

    char *space = strchr(line, ' ');
    char *cmd = line;
    char *param = nullptr;
    if (space) {
        *space = '\0';
        param = space + 1;
        while (*param == ' ') param++;
    } else {
        param = line + strlen(line);
    }

    executeCommand(cmd, param);
}

void FTPServer::executeCommand(const char *cmd, const char *param) {
    // Check commands allowed before authentication
    if (_clientState != STATE_AUTHENTICATED) {
        if (strcasecmp(cmd, "USER") == 0) {
            if (_user.length() == 0) {
                sendResponse(230, "Anonymous user logged in.");
                _clientState = STATE_AUTHENTICATED;
            } else if (strcmp(param, _user.c_str()) == 0) {
                if (_pass.length() == 0) {
                    sendResponse(230, "User logged in.");
                    _clientState = STATE_AUTHENTICATED;
                } else {
                    sendResponse(331, "Password required.");
                    _clientState = STATE_WAIT_PASS;
                }
            } else {
                sendResponse(530, "User not found.");
            }
            return;
        } else if (strcasecmp(cmd, "PASS") == 0) {
            if (_clientState != STATE_WAIT_PASS) {
                sendResponse(503, "Bad sequence of commands.");
            } else if (_pass.length() == 0 || strcmp(param, _pass.c_str()) == 0) {
                sendResponse(230, "Password accepted, user logged in.");
                _clientState = STATE_AUTHENTICATED;
            } else {
                sendResponse(530, "Password incorrect.");
                _clientState = STATE_WAIT_USER;
            }
            return;
        } else if (strcasecmp(cmd, "QUIT") == 0) {
            sendResponse(221, "Goodbye.");
            _ctrlClient.stop();
            resetSession();
            return;
        } else if (strcasecmp(cmd, "FEAT") == 0) {
            _ctrlClient.print("211-Features:\r\n MLSD\r\n MDTM\r\n SIZE\r\n PASV\r\n UTF8\r\n211 End\r\n");
            return;
        } else if (strcasecmp(cmd, "NOOP") == 0) {
            sendResponse(200, "OK.");
            return;
        } else {
            sendResponse(530, "Please login with USER and PASS.");
            return;
        }
    }

    // Authenticated commands
    if (strcasecmp(cmd, "USER") == 0) {
        sendResponse(230, "Already logged in.");
    } else if (strcasecmp(cmd, "PASS") == 0) {
        sendResponse(230, "Already logged in.");
    } else if (strcasecmp(cmd, "QUIT") == 0) {
        sendResponse(221, "Goodbye.");
        _ctrlClient.stop();
        resetSession();
    } else if (strcasecmp(cmd, "NOOP") == 0) {
        sendResponse(200, "OK.");
    } else if (strcasecmp(cmd, "PWD") == 0 || strcasecmp(cmd, "XPWD") == 0) {
        sendResponse(257, "\"%s\" is current directory.", _cwd.c_str());
    } else if (strcasecmp(cmd, "CDUP") == 0 || strcasecmp(cmd, "XCUP") == 0) {
        if (_cwd == "/" || _cwd.length() == 0) {
            _cwd = "/";
        } else {
            int lastSlash = _cwd.lastIndexOf('/');
            if (lastSlash <= 0) {
                _cwd = "/";
            } else {
                _cwd = _cwd.substring(0, lastSlash);
            }
        }
        sendResponse(250, "Directory changed to \"%s\".", _cwd.c_str());
    } else if (strcasecmp(cmd, "CWD") == 0 || strcasecmp(cmd, "XCWD") == 0) {
        if (strcmp(param, ".") == 0) {
            sendResponse(250, "Directory unchanged.");
        } else if (strcmp(param, "..") == 0) {
            executeCommand("CDUP", "");
        } else {
            String target = resolvePath(param);
            if (target == "/") {
                _cwd = target;
                sendResponse(250, "Directory changed to \"/\".");
            } else {
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
                Dir d = _fs.openDir(target);
                if (d.isDirectory() || _fs.exists(target)) {
                    _cwd = target;
                    sendResponse(250, "Directory changed to \"%s\".", _cwd.c_str());
                } else {
                    sendResponse(550, "Directory not found.");
                }
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
                File f = _fs.open(target);
                if (f && f.isDirectory()) {
                    _cwd = target;
                    sendResponse(250, "Directory changed to \"%s\".", _cwd.c_str());
                } else {
                    sendResponse(550, "Directory not found.");
                }
                if (f) f.close();
#endif
            }
        }
    } else if (strcasecmp(cmd, "TYPE") == 0) {
        if (strcasecmp(param, "A") == 0) {
            sendResponse(200, "Type set to ASCII.");
        } else if (strcasecmp(param, "I") == 0 || strcasecmp(param, "L 8") == 0) {
            sendResponse(200, "Type set to binary.");
        } else {
            sendResponse(504, "Unsupported TYPE parameter.");
        }
    } else if (strcasecmp(cmd, "STRU") == 0) {
        if (strcasecmp(param, "F") == 0) {
            sendResponse(200, "Structure set to F.");
        } else {
            sendResponse(504, "Only F structure supported.");
        }
    } else if (strcasecmp(cmd, "MODE") == 0) {
        if (strcasecmp(param, "S") == 0) {
            sendResponse(200, "Mode set to S.");
        } else {
            sendResponse(504, "Only S mode supported.");
        }
    } else if (strcasecmp(cmd, "PASV") == 0) {
        closeDataConnection();
        _isPassive = true;
        _dataServer.begin();

        IPAddress ip = _ctrlClient.localIP();
        sendResponse(227, "Entering Passive Mode (%d,%d,%d,%d,%d,%d).",
                     ip[0], ip[1], ip[2], ip[3], _dataPort >> 8, _dataPort & 0xFF);
    } else if (strcasecmp(cmd, "PORT") == 0) {
        closeDataConnection();
        if (parsePortParam(param, _portIP, _portPort)) {
            _isPassive = false;
            sendResponse(200, "PORT command successful.");
        } else {
            sendResponse(501, "Syntax error in parameters.");
        }
    } else if (strcasecmp(cmd, "LIST") == 0 || strcasecmp(cmd, "NLST") == 0 || strcasecmp(cmd, "MLSD") == 0) {
        int8_t rc = openDataConnection();
        if (rc < 0) {
            sendResponse(425, "Can't open data connection.");
            return;
        }

        sendResponse(150, "Accepted data connection.");

        // Parse optional flags and target directory without damaging names with '-'
        const char *p = param;
        while (*p == ' ') p++;
        while (*p == '-') {
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
        }
        String targetPath = (*p != '\0') ? resolvePath(p) : _cwd;

        bool mlsd = (strcasecmp(cmd, "MLSD") == 0);
        bool nlst = (strcasecmp(cmd, "NLST") == 0);
        listDirectory(targetPath, mlsd, nlst);

        closeDataConnection();
        sendResponse(226, "Directory listing complete.");
    } else if (strcasecmp(cmd, "RETR") == 0) {
        if (strlen(param) == 0) {
            sendResponse(501, "No file name provided.");
            return;
        }

        String target = resolvePath(param);
        if (!_fs.exists(target)) {
            sendResponse(550, "File not found.");
            return;
        }

        _file = _fs.open(target, "r");
        if (!_file || _file.isDirectory()) {
            if (_file) _file.close();
            sendResponse(450, "Cannot open file.");
            return;
        }

        int8_t rc = openDataConnection();
        if (rc < 0) {
            _file.close();
            sendResponse(425, "Can't open data connection.");
            return;
        }

        _fileSize = _file.size();
        _bytesTransferred = 0;
        _transferStartMs = millis();
        _transferState = TRANSFER_RETR;
        allocateTransferBuffer();

        sendResponse(150, "Opening data connection for %s (%lu bytes).", param, (unsigned long)_fileSize);
    } else if (strcasecmp(cmd, "STOR") == 0) {
        if (strlen(param) == 0) {
            sendResponse(501, "No file name provided.");
            return;
        }

        String target = resolvePath(param);
        createParentDirs(target);

        if (_fs.exists(target)) {
            _fs.remove(target);
        }

        _file = _fs.open(target, "w");
        if (!_file) {
            sendResponse(451, "Cannot create file.");
            return;
        }

        int8_t rc = openDataConnection();
        if (rc < 0) {
            _file.close();
            sendResponse(425, "Can't open data connection.");
            return;
        }

        _bytesTransferred = 0;
        _transferBufLen = 0;
        _transferStartMs = millis();
        _transferState = TRANSFER_STOR;
        allocateTransferBuffer();

        sendResponse(150, "Opening data connection for %s.", param);
    } else if (strcasecmp(cmd, "DELE") == 0) {
        if (strlen(param) == 0) {
            sendResponse(501, "No file name provided.");
            return;
        }
        String target = resolvePath(param);
        if (!_fs.exists(target)) {
            sendResponse(550, "File not found.");
        } else if (_fs.remove(target)) {
            sendResponse(250, "File deleted.");
        } else {
            sendResponse(450, "Delete operation failed.");
        }
    } else if (strcasecmp(cmd, "MKD") == 0 || strcasecmp(cmd, "XMKD") == 0) {
        if (strlen(param) == 0) {
            sendResponse(501, "No directory name provided.");
            return;
        }
        String target = resolvePath(param);
        if (_fs.mkdir(target)) {
            sendResponse(257, "\"%s\" created.", target.c_str());
        } else {
            sendResponse(550, "Create directory failed.");
        }
    } else if (strcasecmp(cmd, "RMD") == 0 || strcasecmp(cmd, "XRMD") == 0) {
        if (strlen(param) == 0) {
            sendResponse(501, "No directory name provided.");
            return;
        }
        String target = resolvePath(param);
        if (_fs.rmdir(target)) {
            sendResponse(250, "Directory removed.");
        } else {
            sendResponse(550, "Remove directory failed.");
        }
    } else if (strcasecmp(cmd, "RNFR") == 0) {
        if (strlen(param) == 0) {
            sendResponse(501, "No file name provided.");
            return;
        }
        String target = resolvePath(param);
        if (!_fs.exists(target)) {
            sendResponse(550, "File not found.");
        } else {
            _rnFrom = target;
            sendResponse(350, "Ready for RNTO.");
        }
    } else if (strcasecmp(cmd, "RNTO") == 0) {
        if (_rnFrom.length() == 0) {
            sendResponse(503, "Bad sequence of commands (RNFR first).");
            return;
        }
        if (strlen(param) == 0) {
            sendResponse(501, "No destination name provided.");
            _rnFrom = "";
            return;
        }
        String target = resolvePath(param);
        if (_fs.exists(target)) {
            sendResponse(553, "File already exists.");
        } else if (_fs.rename(_rnFrom, target)) {
            sendResponse(250, "Rename successful.");
        } else {
            sendResponse(451, "Rename failed.");
        }
        _rnFrom = "";
    } else if (strcasecmp(cmd, "SIZE") == 0) {
        String target = resolvePath(param);
        File f = _fs.open(target, "r");
        if (!f || f.isDirectory()) {
            sendResponse(550, "Cannot get file size.");
        } else {
            sendResponse(213, "%lu", (unsigned long)f.size());
        }
        if (f) f.close();
    } else if (strcasecmp(cmd, "MDTM") == 0) {
        String target = resolvePath(param);
        File f = _fs.open(target, "r");
        if (!f) {
            sendResponse(550, "File not found.");
        } else {
#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
            Dir d = _fs.openDir(target);
            time_t t = d.fileTime();
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
            time_t t = f.getLastWrite();
#endif
            sendResponse(213, "%s", formatDateTime(t, true).c_str());
            f.close();
        }
    } else if (strcasecmp(cmd, "FEAT") == 0) {
        _ctrlClient.print("211-Features:\r\n MLSD\r\n MDTM\r\n SIZE\r\n PASV\r\n UTF8\r\n211 End\r\n");
    } else if (strcasecmp(cmd, "SYST") == 0) {
        sendResponse(215, "UNIX Type: L8");
    } else if (strcasecmp(cmd, "ABOR") == 0) {
        abortTransfer();
        sendResponse(226, "Transfer aborted.");
    } else if (strcasecmp(cmd, "SITE") == 0) {
        sendResponse(502, "SITE command not implemented.");
    } else {
        sendResponse(500, "Unknown command \"%s\".", cmd);
    }
}

void FTPServer::handleFTP() {
    // 1. Accept new incoming control connections
    if (_ctrlServer.hasClient()) {
        WiFiClient newClient = FTPSRV_ACCEPT(_ctrlServer);
        if (newClient) {
            if (_ctrlClient.connected()) {
                _ctrlClient.stop();
            }
            _ctrlClient = newClient;
            resetSession();
            _clientState = STATE_WAIT_USER;
            _lastActivity = millis();
            sendResponse(220, "ESP FTPServer Ready.");
        }
    }

    // 2. Check if client dropped connection
    if (!_ctrlClient.connected()) {
        if (_clientState != STATE_IDLE) {
            resetSession();
        }
        return;
    }

    // 3. Inactivity timeout check
    if (millis() - _lastActivity > _timeoutMs) {
        sendResponse(530, "Timeout due to inactivity.");
        _ctrlClient.stop();
        resetSession();
        return;
    }

    // 4. Handle ongoing data transfer
    if (_transferState == TRANSFER_RETR) {
        transferFileToNetwork();
        return;
    } else if (_transferState == TRANSFER_STOR) {
        transferNetworkToFile();
        return;
    }

    // 5. Read incoming commands
    while (_ctrlClient.available() > 0) {
        char c = (char)_ctrlClient.read();
        if (c == '\r') continue;
        if (c == '\n') {
            _cmdBuf[_cmdLen] = '\0';
            if (_cmdLen > 0) {
                processCommandLine(_cmdBuf);
                _cmdLen = 0;
                _lastActivity = millis();
            }
            break;
        } else {
            if (_cmdLen < CMD_BUF_SIZE - 1) {
                _cmdBuf[_cmdLen++] = c;
            } else {
                _cmdLen = 0;
                sendResponse(500, "Command line too long.");
                break;
            }
        }
    }
}

