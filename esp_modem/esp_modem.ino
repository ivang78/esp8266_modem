/*
 * ESP8266 based virtual modem improved
 * Copyright (C) 2016 Jussi Salin <salinjus@gmail.com>
 * Modified 2023 ivang78 
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ESP8266WiFi.h>
#include <algorithm>
#include <EEPROM.h>

// Global variables
String cmd = "";            // Gather a new AT command to this string from serial
bool cmdMode = true;        // Are we in AT command mode or connected mode
bool telnet = true;         // Is telnet control code handling enabled
#define SWITCH_PIN 0        // GPIO0 (programmind mode pin)
#define DEFAULT_BPS 115200  // 2400 safe for all old computers including C64
//#define DEBUG 1          // Print additional debug information to serial channel
#undef DEBUG
#define LISTEN_PORT 23      // Listen to this if not connected. Set to zero to disable.
#define RING_INTERVAL 3000  // How often to print RING when having a new incoming connection (ms)
WiFiClient tcpClient;
WiFiClientSecure tcpClientSecure;
bool isSecure;
WiFiServer tcpServer(LISTEN_PORT);
unsigned long lastRingMs = 0;  // Time of last "RING" message (millis())
long myBps;                    // What is the current BPS setting
#define MAX_CMD_LENGTH 256     // Maximum length for AT command
char plusCount = 0;            // Go to AT mode at "+++" sequence, that has to be counted
unsigned long plusTime = 0;    // When did we last receive a "+++" sequence
#define TX_BUF_SIZE 256        // Buffer where to read from serial before writing to TCP \
                               // (that direction is very blocking by the ESP TCP stack, \
                               // so we can't do one byte a time.)
uint8_t txBuf[TX_BUF_SIZE];

// Telnet codes
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe

String s1;
char ch1[255];
#define RX_BUFF_SIZE 16384
uint8_t rxBuff[RX_BUFF_SIZE];
uint16_t rxPos;
bool isHtml = false;
bool httpHeaderProcessed = false;
bool isStartTag = false;
bool isGetTagName = false;
bool isHiddenTag = false;
bool wasSpace = false;
char tagName[255];
uint8_t tagPos = 0;
char uartDelay = 0;
char config[64];

// -------------------------------------------
// print functions with delays
void dlprintln(char *str) {
  for (int i = 0; i < strlen(str); i++) {
    Serial.write(str[i]);
    delay(uartDelay);
  }
  Serial.write(13);
  delay(uartDelay);
  Serial.write(10);
  delay(uartDelay);
}

void dlprint(char *str) {
  for (int i = 0; i < strlen(str); i++) {
    Serial.write(str[i]);
    delay(uartDelay);
  }
}

void dlprintchar(char ch) {
  Serial.write(ch);
  delay(uartDelay);
}

void dlprintempty(void) {
  Serial.write(13);
  delay(uartDelay);
  Serial.write(10);
  delay(uartDelay);
}
// -------------------------------------------

/**
 * Arduino main init function
 */
void setup() {
  Serial.begin(DEFAULT_BPS);
  myBps = DEFAULT_BPS;

  dlprintln("Virtual modem");
  dlprintln("=============");
  dlprintempty();
  dlprintln("Connect to WIFI: ATWIFI<ssid>,<key>");
  dlprintln("Change terminal baud rate: AT<baud>");
  dlprintln("Connect by TCP: ATDT<host>:<port>");
  dlprintln("See my IP address: ATIP");
  dlprintln("Disable telnet command handling: ATNET0");
  dlprintln("HTTP/HTTPS GET: ATGET<URL>");
  dlprintln("Set delay after each byte send to UART from 0 to 9ms: ATX<delay>");
  dlprintempty();
  if (LISTEN_PORT > 0) {
    dlprint("Listening to connections at port ");
    itoa(LISTEN_PORT, ch1, 10);
    dlprint(ch1);
    dlprintln(", which result in RING and you can answer with ATA.");
    tcpServer.begin();
  } else {
    dlprintln("Incoming connections are disabled.");
  }
  dlprintempty();
  dlprintln("OK");

  // Work with EEPROM config
  memset(config, 0, sizeof(config));
  EEPROM.begin(64);
  EEPROM.get(0, config);
  if (config[63] != 0) {
    uartDelay = config[63];
  }
}

/**
 * Perform a command given in command mode
 */
void command() {
  cmd.trim();
  if (cmd == "") return;
  dlprintempty();
  String upCmd = cmd;
  upCmd.toUpperCase();

  long newBps = 0;

  /**** Just AT ****/
  if (upCmd == "AT") {
    dlprintln("OK");
  }
  else if (upCmd.indexOf("ATX") == 0) {
  /**** Set UART delay ****/
    if (cmd.length() == 4) {
      uartDelay = cmd[3] - 48;
      config[63] = uartDelay;
      EEPROM.write(63, uartDelay);
      EEPROM.commit();
      dlprintln("OK");
    } else {
      dlprintln("ERROR");
    }
  }
  /**** Dial to host ****/
  else if ((upCmd.indexOf("ATDT") == 0) || (upCmd.indexOf("ATDP") == 0) || (upCmd.indexOf("ATDI") == 0)) {
    isHtml = false;
    int portIndex = cmd.indexOf(":");
    String host, port;
    if (portIndex != -1) {
      host = cmd.substring(4, portIndex);
      port = cmd.substring(portIndex + 1, cmd.length());
    } else {
      host = cmd.substring(4, cmd.length());
      port = "23";  // Telnet default
    }
    dlprint("Connecting to ");
    char *hostChr = new char[host.length() + 1];
    host.toCharArray(hostChr, host.length() + 1);
    dlprint(hostChr);
    dlprint(":");
    port.toCharArray(ch1, port.length() + 1);
    dlprintln(ch1);
    int portInt = port.toInt();
    isSecure = false;
    tcpClient.setNoDelay(true);  // Try to disable naggle
    if (tcpClient.connect(hostChr, portInt)) {
      tcpClient.setNoDelay(true);  // Try to disable naggle
      dlprint("CONNECT ");
      itoa(myBps, ch1, 10);
      dlprintln(ch1);
      cmdMode = false;
      Serial.flush();
      if (LISTEN_PORT > 0) tcpServer.stop();
    } else {
      dlprintln("NO CARRIER");
    }
    delete hostChr;
  }

  /**** Connect to WIFI ****/
  else if (upCmd.indexOf("ATWIFI") == 0) {
    // WiFi store at config?
    uint8_t p = 0;
    if (cmd.length() == 6 && config[0] != 0) {
      while (config[p] != 0) {
        cmd += config[p];
        p++;
      }
    }
    int keyIndex = cmd.indexOf(",");
    String ssid, key;
    if (keyIndex != -1) {
      ssid = cmd.substring(6, keyIndex);
      key = cmd.substring(keyIndex + 1, cmd.length());
    } else {
      ssid = cmd.substring(6, cmd.length());
      key = "";
    }
    char *ssidChr = new char[ssid.length() + 1];
    ssid.toCharArray(ssidChr, ssid.length() + 1);
    char *keyChr = new char[key.length() + 1];
    key.toCharArray(keyChr, key.length() + 1);
    dlprint("Connecting to ");

    dlprint(ssidChr);
    dlprint("/");
    dlprintln(keyChr);
    WiFi.begin(ssidChr, keyChr);
    for (int i = 0; i < 100; i++) {
      delay(100);
      if (WiFi.status() == WL_CONNECTED) {
        dlprintln("OK");
        // Type-in WiFi and connect OK
        if (p == 0) {
          // Work with EEPROM
          memset(config, 0, sizeof(config) - 1);
          s1 = cmd.substring(6, cmd.length());
          s1.toCharArray(config, s1.length() + 1);
          EEPROM.put(0, config);
          EEPROM.commit();
        }
        break;
      }
    }
    if (WiFi.status() != WL_CONNECTED) {
      dlprintln("ERROR");
    }
    delete ssidChr;
    delete keyChr;
  }

  /**** Change baud rate from default ****/
  else if (upCmd == "AT300")
    newBps = 300;
  else if (upCmd == "AT1200") newBps = 1200;
  else if (upCmd == "AT2400") newBps = 2400;
  else if (upCmd == "AT9600") newBps = 9600;
  else if (upCmd == "AT19200") newBps = 19200;
  else if (upCmd == "AT28800") newBps = 28800;
  else if (upCmd == "AT38400") newBps = 38400;
  else if (upCmd == "AT57600") newBps = 57600;
  else if (upCmd == "AT115200") newBps = 115200;

  /**** Change telnet mode ****/
  else if (upCmd == "ATNET0") {
    telnet = false;
    dlprintln("OK");
  } else if (upCmd == "ATNET1") {
    telnet = true;
    dlprintln("OK");
  }

  /**** Answer to incoming connection ****/
  else if ((upCmd == "ATA") && tcpServer.hasClient()) {
    tcpClient = tcpServer.available();
    tcpClient.setNoDelay(true);  // try to disable naggle
    tcpServer.stop();
    dlprint("CONNECT ");
    itoa(myBps, ch1, 10);
    dlprintln(ch1);
    cmdMode = false;
    Serial.flush();
  }

  /**** See my IP address ****/
  else if (upCmd == "ATIP") {
    IPAddress ip1 = WiFi.localIP();
    s1 = String(ip1[0]) + String(".") + String(ip1[1]) + String(".") + String(ip1[2]) + String(".") + String(ip1[3]);
    s1.toCharArray(ch1, s1.length() + 1);
    dlprintln(ch1);
    dlprintln("OK");
  }

  /**** HTTP GET request ****/
  else if (upCmd.indexOf("ATGET") == 0) {
    // From the URL, aquire required variables
    // (12 = "ATGEThttp://")
    uint8_t startPos = 12;
    int isHttps = cmd.indexOf("https:");
    if (isHttps < 0) {
      isSecure = false;
    } else {
      startPos = 13;
      isSecure = true;
      tcpClientSecure.setNoDelay(true);
      tcpClientSecure.setInsecure();
    }
    int portIndex = cmd.indexOf(":", startPos);  // Index where port number might begin
    int pathIndex = cmd.indexOf("/", startPos);  // Index first host name and possible port ends and path begins
    int port;
    String path, host;
    if (pathIndex < 0) {
      pathIndex = cmd.length();
    }
    if (portIndex < 0) {
      if (isHttps < 0) {
        port = 80;
      } else {
        port = 443;
      }
      portIndex = pathIndex;
    } else {
      port = cmd.substring(portIndex + 1, pathIndex).toInt();
    }
    host = cmd.substring(startPos, portIndex);
    path = cmd.substring(pathIndex, cmd.length());
    if (path == "") path = "/";
    char *hostChr = new char[host.length() + 1];
    host.toCharArray(hostChr, host.length() + 1);

    // Debug
    dlprint("Getting path ");
    path.toCharArray(ch1, path.length() + 1);
    dlprint(ch1);
    dlprint(" from port ");
    itoa(port, ch1, 10);
    dlprint(ch1);
    dlprint(" of host ");
    host.toCharArray(ch1, host.length() + 1);
    dlprint(ch1);
    dlprintln("...");

    // Establish connection
    bool res;
    if (isSecure == true) {
      res = tcpClientSecure.connect(hostChr, port);
    } else {
      res = tcpClient.connect(hostChr, port);
    }
    if (!res) {
      if (isSecure == true) {
        tcpClientSecure.getLastSSLError(ch1, sizeof(ch1));
        dlprint("ERROR: "); dlprintln(ch1);
      }
      dlprintln("NO CARRIER");
    } else {
      dlprint("CONNECT ");
      itoa(myBps, ch1, 10);
      dlprintln(ch1);
      cmdMode = false;
      isHtml = true;
      httpHeaderProcessed = false;
      rxPos = 0;

      // Send a HTTP request before continuing the connection as usual
      String request = "GET ";
      request += path;
      request += " HTTP/1.1\r\nHost: ";
      request += host;
      request += "\r\nConnection: close\r\n\r\n";
      if (isSecure == true) {
        tcpClientSecure.print(request);
      } else {
        tcpClient.print(request);
      }
    }
    delete hostChr;
  }

  /**** Unknown command ****/
  else
    dlprintln("ERROR");

  /**** Tasks to do after command has been parsed ****/
  if (newBps) {
    dlprintln("OK");
    delay(150);  // Sleep enough for 4 bytes at any previous baud rate to finish ("\nOK\n")
    Serial.begin(newBps);
    myBps = newBps;
  }

  cmd = "";
}

/**
 * Convert ASCII upper to lower case for a-z
 */
char lowerCase(uint8_t c) {
  if ((c > 64) && (c < 91)) return c += 32;
  else return c;
}

/**
 * Convert ASCII lower to upper case for A-Z
 */
char upperCase(uint8_t c) {
  if ((c >= 97) && (c <= 122)) return c -= 32;
  else return c;
}

/**
 * Process byte of received data

ATWIFIwolfs-logovo,wolf1978
ATGEThttp://www.columbia.edu/~fdc/sample.html
ATGEThttps://home.web.cern.ch/about

 */
void processHtmlByte(uint8_t rxByte) {
  uint8_t lc;

  // Receive buffer overloaded?
  if (rxPos >= RX_BUFF_SIZE) {
    return;
  }

  // Lowercase byte for tag name
  lc = lowerCase(rxByte);

  // Find end of HTTP header
  if (rxByte == 13 && rxPos > 3 && (rxBuff[rxPos - 1] == 13 || (rxBuff[rxPos - 1] == 10 && rxBuff[rxPos - 2] == 13)) && httpHeaderProcessed == false) {
    httpHeaderProcessed = true;
    isStartTag = false;
    isGetTagName = false;
    isHiddenTag = false;
    wasSpace = false;
    rxPos = 0;
    return;
  }

  if (httpHeaderProcessed) {

    // Start of HTML tag "<" ?
    if (rxByte == 60) {
      isStartTag = true;
      isGetTagName = false;
      tagPos = 0;
      return;
    }
    // End of HTML tag ">" ?
    if (rxByte == 62 && isStartTag == true) {
      if (isGetTagName == false && tagPos > 0) {
        isGetTagName = true;
      }
      if (isGetTagName == true) {
        tagName[tagPos] = 0;

        if (strcmp(tagName, "b") == 0 || strcmp(tagName, "strong") == 0) {
          // Text bold on                    
          rxBuff[rxPos] = 27; rxBuff[rxPos + 1] = 91; rxBuff[rxPos + 2] = 49; rxBuff[rxPos + 3] = 109;   rxPos = rxPos + 4;       
        } else if (strcmp(tagName, "/b") == 0 || strcmp(tagName, "/strong") == 0) {
          // Text bold off
          rxBuff[rxPos] = 27; rxBuff[rxPos + 1] = 91; rxBuff[rxPos + 2] = 48; rxBuff[rxPos + 3] = 109;   rxPos = rxPos + 4;       
        } else if (strcmp(tagName, "h1") == 0) {
          // H1 header on
          rxBuff[rxPos] = 27; rxBuff[rxPos + 1] = 91; rxBuff[rxPos + 2] = 51; rxBuff[rxPos + 3] = 49; rxBuff[rxPos + 4] = 109; rxBuff[rxPos + 5] = 13; rxBuff[rxPos + 6] = 10;  rxPos = rxPos + 7;
        } else if (strcmp(tagName, "h2") == 0) {
          // H2 header on
          rxBuff[rxPos] = 27; rxBuff[rxPos + 1] = 91; rxBuff[rxPos + 2] = 51; rxBuff[rxPos + 3] = 50; rxBuff[rxPos + 4] = 109; rxBuff[rxPos + 5] = 13; rxBuff[rxPos + 6] = 10;  rxPos = rxPos + 7;       
        } else if (strcmp(tagName, "h3") == 0 || strcmp(tagName, "h4") == 0) {
          // H3 or H4 header on
          rxBuff[rxPos] = 27; rxBuff[rxPos + 1] = 91; rxBuff[rxPos + 2] = 51; rxBuff[rxPos + 3] = 54; rxBuff[rxPos + 4] = 109; rxBuff[rxPos + 5] = 13; rxBuff[rxPos + 6] = 10;  rxPos = rxPos + 7;
        } else if (strcmp(tagName, "/h1") == 0 || strcmp(tagName, "/h2") == 0 || strcmp(tagName, "/h3") == 0 || strcmp(tagName, "/h4") == 0) {
          // H1-H4 header off
          rxBuff[rxPos] = 27; rxBuff[rxPos + 1] = 91; rxBuff[rxPos + 2] = 48; rxBuff[rxPos + 3] = 109; rxPos = rxPos + 4;
        } else if (strcmp(tagName, "p") == 0 || strcmp(tagName, "div") == 0 || strcmp(tagName, "br") == 0 || strcmp(tagName, "br/") == 0) {
          // Block tag <p>, <div>, <br> start
          rxBuff[rxPos] = 13; rxBuff[rxPos + 1] = 10; rxPos = rxPos + 2;
        } else if (strcmp(tagName, "li") == 0) {
          // <li> start 
          rxBuff[rxPos] = 13; rxBuff[rxPos + 1] = 10; rxBuff[rxPos + 2] = 42; rxBuff[rxPos + 3] = 32; rxPos = rxPos + 4;       
        } else if (strcmp(tagName, "style") == 0 || strcmp(tagName, "script") == 0) {
          // Hidden tag <style>, <script> start 
          isHiddenTag = true;
        } else if (strcmp(tagName, "/style") == 0 || strcmp(tagName, "/script") == 0) {
          // Hidden tag <style>, <script> end 
          isHiddenTag = false;
        } 
      }
      isStartTag = false;
      return;
    }   
    // Lowercase letter and slash with HTML tag started means tag name part
    if (((lc >= 97 && lc <= 122) || (lc >= 47 && lc <= 57)) && isStartTag == true) {
      if (isGetTagName == false) {
        tagName[tagPos] = lc;
        tagPos++;
      }
      return;
    }
    // Space or "/" found, means end of tag name
    if (lc == 32 && isStartTag == true && isGetTagName == false) {
      isGetTagName = true;
      return;
    }
  }

  // Part of HTTP header
  if (httpHeaderProcessed == false) {
    rxBuff[rxPos] = rxByte;
    rxPos++;
  }
  
  // All other means printable character
  if (httpHeaderProcessed == true && isStartTag == false && isHiddenTag == false && rxByte > 31) {
    if ((wasSpace == false && rxByte == 32) || rxByte != 32) { // Ignore multi-spaces
      if (rxByte == 32) {
        wasSpace = true;
      } else {
        wasSpace = false;
      }
      rxBuff[rxPos] = rxByte;
      rxPos++;
    }
    for (uint16_t j = 0; j < rxPos; j++) {
      dlprintchar(rxBuff[j]);
    }
    rxPos = 0;
  }
}

/**
 * Print processed HTML
 */
void printOutHtml() {
  isHtml = false;
  isStartTag = false;
  isGetTagName = false;
  isHiddenTag = false;
  wasSpace = false;
  Serial.flush();
}

/**
 * Arduino main loop function
 */
void loop() {
  uint8_t tmpBuf[3];

  /**** AT command mode ****/
  if (cmdMode == true) {
    // In command mode but new unanswered incoming connection on server listen socket
    if ((LISTEN_PORT > 0) && (tcpServer.hasClient())) {
      // Print RING every now and then while the new incoming connection exists
      if ((millis() - lastRingMs) > RING_INTERVAL) {
        dlprintln("RING");
        lastRingMs = millis();
      }
    }

    // In command mode - don't exchange with TCP but gather characters to a string
    if (Serial.available()) {
      char chr = Serial.read();

      // Return, enter, new line, carriage return.. anything goes to end the command
      if ((chr == '\n') || (chr == '\r')) {
        command();
      }
      // Backspace or delete deletes previous character
      else if ((chr == 8) || (chr == 127)) {
        cmd.remove(cmd.length() - 1);
        // We don't assume that backspace is destructive
        // Clear with a space
        dlprintchar(8);
        dlprintchar(' ');
        dlprintchar(8);
      } else {
        if (cmd.length() < MAX_CMD_LENGTH) cmd.concat(chr);
        dlprintchar(chr);
      }
    }
  }
  /**** Connected mode ****/
  else {
    // Transmit from terminal to TCP
    if (Serial.available()) {
      // In telnet in worst case we have to escape every byte
      // so leave half of the buffer always free
      int max_buf_size;
      if (telnet == true)
        max_buf_size = TX_BUF_SIZE / 2;
      else
        max_buf_size = TX_BUF_SIZE;

      // Read from serial, the amount available up to
      // maximum size of the buffer
      size_t len = std::min(Serial.available(), max_buf_size);
      Serial.readBytes(&txBuf[0], len);

      // Disconnect if going to AT mode with "+++" sequence
      for (int i = 0; i < (int)len; i++) {
        if (txBuf[i] == '+') plusCount++;
        else plusCount = 0;
        if (plusCount >= 3) {
          plusTime = millis();
        }
        if (txBuf[i] != '+') {
          plusCount = 0;
        }
      }

      // Double (escape) every 0xff for telnet, shifting the following bytes
      // towards the end of the buffer from that point
      if (telnet == true) {
        for (int i = len - 1; i >= 0; i--) {
          if (txBuf[i] == 0xff) {
            for (int j = TX_BUF_SIZE - 1; j > i; j--) {
              txBuf[j] = txBuf[j - 1];
            }
            len++;
          }
        }
      }

      // Write the buffer to TCP finally
      if (isSecure == true) {
        tcpClientSecure.write(&txBuf[0], len);
      } else {
        tcpClient.write(&txBuf[0], len);
      }
      yield();
    }

    // Transmit from TCP to terminal
    while ((isSecure == true && tcpClientSecure.available()) || (isSecure == false && tcpClient.available())) {
      uint8_t rxByte;
      if (isSecure == true) {
        rxByte = tcpClientSecure.read();
      } else {
        rxByte = tcpClient.read();
      }

      // Is a telnet control code starting?
      if ((telnet == true) && (rxByte == 0xff)) {
        if (isSecure == true) {
          rxByte = tcpClientSecure.read();
        } else {
          rxByte = tcpClient.read();
        }
        if (rxByte == 0xff) {
          // 2 times 0xff is just an escaped real 0xff
          dlprintchar(0xff);
          Serial.flush();
        } else {
          // rxByte has now the first byte of the actual non-escaped control code
          uint8_t cmdByte1 = rxByte;
          if (isSecure == true) {
            rxByte = tcpClientSecure.read();
          } else {
            rxByte = tcpClient.read();
          }
          uint8_t cmdByte2 = rxByte;
          // rxByte has now the second byte of the actual non-escaped control code
          // We are asked to do some option, respond we won't
          if (cmdByte1 == DO) {
            tmpBuf[0] = 255;
            tmpBuf[1] = (uint8_t)WONT;
            tmpBuf[2] = cmdByte2;
            if (isSecure == true) {
              tcpClientSecure.write(tmpBuf, 3);
            } else {
              tcpClient.write(tmpBuf, 3);
            }
          }
          // Server wants to do any option, allow it
          else if (cmdByte1 == WILL) {
            tmpBuf[0] = 255;
            tmpBuf[1] = (uint8_t)DO;
            tmpBuf[2] = cmdByte2;
            if (isSecure == true) {
              tcpClientSecure.write(tmpBuf, 3);
            } else {
              tcpClient.write(tmpBuf, 3);
            }
          }
        }
      } else {
        if (isHtml == true) {  // HTML bytes store to buffer
          processHtmlByte(rxByte);
        } else {  // Non-control codes pass through freely
          dlprintchar(rxByte);
          Serial.flush();
        }
      }
    }
  }

  // If we have received "+++" as last bytes from serial port and there
  // has been over a second without any more bytes, disconnect
  if (plusCount >= 3) {
    if (millis() - plusTime > 1000) {
      if (isSecure == true) {
        tcpClientSecure.stop();
      } else {
        tcpClient.stop();
      }
      plusCount = 0;
      if (isHtml == true) {  // HTML fully loader, parse and print it
        printOutHtml();
      }
    }
  }

  // Go to command mode if TCP disconnected and not in command mode
  if (isSecure == true) {
    if ((!tcpClientSecure.connected()) && (cmdMode == false)) {
      if (isHtml == true) {  // HTML fully loader, parse and print it
        printOutHtml();
      } else {
        dlprintln("NO CARRIER");
      }
      cmdMode = true;
      if (LISTEN_PORT > 0) {
        tcpServer.begin();
      }
    }
  } else {
    if ((!tcpClient.connected()) && (cmdMode == false)) {
      if (isHtml == true) {  // HTML fully loaded, finish
        printOutHtml();
      } else {
        dlprintln("NO CARRIER");
      }
      cmdMode = true;
      if (LISTEN_PORT > 0) {
        tcpServer.begin();
      }
    }
  }
}