#include <WiFi.h>
#include <Update.h>
#include <base64.h>
#include "webserver.h"
#include "hardware.h"
#include "schedule.h"
#include "ntp.h"
#include "config.h"
#include "secrets.h"

extern Valve insideValve;
extern Valve outsideValve;
extern Valve tankValve;
extern bool pumpActive;

extern Switch insideSwitch;
extern Switch outsideSwitch;
extern Switch insideAutoSwitch;
extern Switch outsideAutoSwitch;

static WiFiServer server(80);

void webserverBegin() {
  server.begin();
}

void printWiFiStatus() {
  Serial.print("SSID: "); Serial.println(WiFi.SSID());
  Serial.print("IP Address: "); Serial.println(WiFi.localIP());
}

static void printHTMLHeader(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  client.println("<style>body{font-family:Arial; padding: 20px;} table, th, td {border: 1px solid black; border-collapse: collapse; padding: 5px;}</style></head><body>");
  client.println("<h2>Sprinky Controller - Merida</h2>");
}

static void printHTMLFooter(WiFiClient& client) {
  client.println("</body></html>");
}

static void sendAuth401(WiFiClient& client) {
  client.println("HTTP/1.1 401 Unauthorized");
  client.println("WWW-Authenticate: Basic realm=\"Sprinky OTA\"");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE html><html><body><h3>401 Unauthorized</h3></body></html>");
}

static bool checkOTAAuth(const String& authHeader) {
  if (!authHeader.startsWith("Basic ")) return false;
  String expected = base64::encode(String(OTA_USER) + ":" + OTA_PASS);
  return authHeader.substring(6) == expected;
}

static void sendOTAResponse(WiFiClient& client, bool success) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  if (success) {
    client.println("<!DOCTYPE html><html><body>");
    client.println("<h3>Update Successful!</h3>");
    client.println("<p>Rebooting... page will reload in 10 seconds.</p>");
    client.println("<script>setTimeout(function(){window.location='/';},10000);</script>");
    client.println("</body></html>");
  } else {
    client.println("<!DOCTYPE html><html><body><h3>Update Failed</h3>");
    client.print("<p>Error: ");
    Update.printError(client);
    client.println("</p><p><a href='/update'>Try Again</a></p></body></html>");
  }
}

static const unsigned long OTA_READ_TIMEOUT_MS = 10000;

static bool waitForByte(WiFiClient& client) {
  unsigned long start = millis();
  while (!client.available()) {
    if (!client.connected() || millis() - start > OTA_READ_TIMEOUT_MS) return false;
    delay(1);
  }
  return true;
}

static void shutdownIrrigation() {
  pumpActive = false;
  digitalWrite(PIN_PUMP, LOW);
  insideValve.turnOff();
  outsideValve.turnOff();
  tankValve.turnOff();
  scheduleRunning = false;
  Serial.println("OTA: irrigation shut down");
}

static void handleOTAUpload(WiFiClient& client, int contentLength) {
  shutdownIrrigation();
  client.setTimeout(OTA_READ_TIMEOUT_MS / 1000);

  // Extract the multipart boundary from the first line of the body
  // The body starts with: --boundary\r\n
  String boundary = "";
  while (client.connected()) {
    if (!waitForByte(client)) { sendOTAResponse(client, false); return; }
    char c = client.read();
    contentLength--;
    if (c == '\n') break;
    if (c != '\r') boundary += c;
  }

  // Skip part headers (Content-Disposition, Content-Type, etc.) until blank line
  String line = "";
  while (client.connected()) {
    if (!waitForByte(client)) { sendOTAResponse(client, false); return; }
    char c = client.read();
    contentLength--;
    if (c == '\n') {
      if (line.length() == 0 || line == "\r") break;
      line = "";
    } else {
      line += c;
    }
  }

  // Remaining contentLength = binary data + \r\n + boundary footer
  // boundary already contains the leading "--", so footer "\r\n--boundary--\r\n" = boundary.length() + 6
  size_t footerSize = boundary.length() + 6;
  size_t firmwareSize = (contentLength > (int)footerSize) ? contentLength - footerSize : 0;

  if (firmwareSize == 0 || !Update.begin(firmwareSize)) {
    sendOTAResponse(client, false);
    return;
  }

  Serial.printf("OTA: receiving %u bytes...\n", firmwareSize);

  uint8_t buf[1024];
  size_t remaining = firmwareSize;
  while (remaining > 0 && client.connected()) {
    size_t toRead = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
    size_t bytesRead = client.readBytes(buf, toRead);
    if (bytesRead == 0) {
      Serial.println("OTA: read timeout, aborting");
      Update.abort();
      sendOTAResponse(client, false);
      return;
    }
    if (Update.write(buf, bytesRead) != bytesRead) {
      Update.abort();
      sendOTAResponse(client, false);
      return;
    }
    remaining -= bytesRead;
  }

  if (remaining > 0) {
    Serial.printf("OTA: incomplete upload (%u bytes missing), aborting\n", remaining);
    Update.abort();
    sendOTAResponse(client, false);
    return;
  }

  // Drain remaining body (boundary footer)
  unsigned long drainStart = millis();
  while (contentLength > (int)firmwareSize && client.connected()) {
    if (client.available()) {
      client.read();
    } else if (millis() - drainStart > OTA_READ_TIMEOUT_MS) {
      break;
    } else {
      delay(1);
    }
  }

  bool success = Update.end();
  sendOTAResponse(client, success);

  if (success) {
    Serial.println("OTA web update successful, rebooting...");
    client.flush();
    delay(500);
    ESP.restart();
  }
}

void handleClient() {
  WiFiClient client = server.available();
  if (!client) return;

  String reqLine = "";
  String firstLine = "";
  String authHeader = "";
  bool formSubmitted = false;
  bool pwmSubmitted  = false;
  int contentLength = 0;

  while (client.connected()) {
    if (client.available()) {
      char c = client.read();

      if (c != '\n' && c != '\r') {
        reqLine += c;
      } else if (c == '\n') {
        if (firstLine.length() == 0 && reqLine.length() > 0) {
          firstLine = reqLine;
        }
        if (reqLine.startsWith("Content-Length: ")) {
          contentLength = reqLine.substring(16).toInt();
        }
        if (reqLine.startsWith("Authorization: ")) {
          authHeader = reqLine.substring(15);
        }

        if (reqLine.length() == 0) {
          // Handle /update routes — require Basic Auth
          if (firstLine.startsWith("GET /update") || firstLine.startsWith("POST /update")) {
            if (!checkOTAAuth(authHeader)) {
              sendAuth401(client);
              break;
            }

            if (firstLine.startsWith("GET /update")) {
              printHTMLHeader(client);
              client.println("<h3>Firmware Update</h3>");
              client.println("<form method='POST' action='/update' enctype='multipart/form-data'>");
              client.println("<input type='file' name='firmware' accept='.bin'><br><br>");
              client.println("<input type='submit' value='Upload &amp; Flash'>");
              client.println("</form>");
              client.println("<p><a href='/'>Back to Status</a></p>");
              printHTMLFooter(client);
              break;
            }

            if (contentLength > 0) {
              handleOTAUpload(client, contentLength);
              break;
            }
          }

          printHTMLHeader(client);

          unsigned long e = currentEpoch + ((millis() - lastEpochUpdateMillis) / 1000);
          int eh = (e % 86400L) / 3600;
          int em = (e % 3600) / 60;
          char timeStrDisp[6];
          sprintf(timeStrDisp, "%02d:%02d", eh, em);
          client.print("<p>Current System Time: ");
          client.print(timeStrDisp);
          client.println(" (UTC-6)</p>");
          client.println("<p>Firmware: v" FIRMWARE_VERSION "</p>");

          if (formSubmitted) client.println("<p style='color:green'>Schedules Updated &amp; Saved to EEPROM!</p>");

          client.println("<h3>Schedules</h3><p>Automatically alternates Tank -> Inside (5min) -> Outside (5min).</p>");
          client.println("<form action='/save' method='GET'>");
          client.println("<table><tr><th>ID</th><th>Active</th><th>Start Time (HH:MM)</th><th>Duration (Mins)</th></tr>");

          for (int i = 0; i < MAX_SCHEDULES; i++) {
            client.print("<tr><td>"); client.print(i); client.print("</td>");
            client.print("<td><input type='checkbox' name='act"); client.print(i); client.print("' value='1' ");
            if (schedules[i].active) client.print("checked");
            client.print("></td>");
            char timeStr[6];
            sprintf(timeStr, "%02d:%02d", schedules[i].startHour, schedules[i].startMinute);
            client.print("<td><input type='time' name='time"); client.print(i); client.print("' value='"); client.print(timeStr); client.print("'></td>");
            client.print("<td><input type='number' name='dur"); client.print(i); client.print("' value='"); client.print(schedules[i].durationMinutes); client.print("' min='0' max='300'></td>");
            client.println("</tr>");
          }
          client.println("</table><br><input type='submit' value='Save'></form>");

          client.println("<h3>Manual Zone Control</h3>");
          client.println("<p style='margin-bottom:8px'>");
          // Inside toggle button
          if (webReqInside) {
            client.println("<a href='/zone?inside=0' style='display:inline-block;padding:12px 24px;margin-right:12px;background:#4CAF50;color:white;text-decoration:none;border-radius:6px;font-size:16px'>Inside: ON</a>");
          } else {
            client.println("<a href='/zone?inside=1' style='display:inline-block;padding:12px 24px;margin-right:12px;background:#aaa;color:white;text-decoration:none;border-radius:6px;font-size:16px'>Inside: OFF</a>");
          }
          // Outside toggle button
          if (webReqOutside) {
            client.println("<a href='/zone?outside=0' style='display:inline-block;padding:12px 24px;background:#4CAF50;color:white;text-decoration:none;border-radius:6px;font-size:16px'>Outside: ON</a>");
          } else {
            client.println("<a href='/zone?outside=1' style='display:inline-block;padding:12px 24px;background:#aaa;color:white;text-decoration:none;border-radius:6px;font-size:16px'>Outside: OFF</a>");
          }
          client.println("</p>");

          client.println("<h3>Status</h3>");
          client.println("<table><tr><th>Component</th><th>State</th></tr>");

          auto stateCell = [&](WiFiClient& c, const char* label, bool on, const char* onText, const char* offText) {
            c.print("<tr><td>"); c.print(label); c.print("</td>");
            c.print("<td style='color:"); c.print(on ? "green" : "gray"); c.print("'>");
            c.print(on ? onText : offText);
            c.println("</td></tr>");
          };

          stateCell(client, "Pump",              pumpActive,                   "ON",   "OFF");
          stateCell(client, "Tank Valve",        tankValve.isActive(),         "OPEN", "CLOSED");
          stateCell(client, "Inside Valve",      insideValve.isActive(),       "OPEN", "CLOSED");
          stateCell(client, "Outside Valve",     outsideValve.isActive(),      "OPEN", "CLOSED");
          stateCell(client, "Inside Switch",     insideSwitch.getState(),      "ON",   "OFF");
          stateCell(client, "Outside Switch",    outsideSwitch.getState(),     "ON",   "OFF");
          stateCell(client, "Inside Auto SW",    insideAutoSwitch.getState(),  "ON",   "OFF");
          stateCell(client, "Outside Auto SW",   outsideAutoSwitch.getState(), "ON",   "OFF");

          if (scheduleRunning) {
            client.print("<tr><td>Schedule</td><td style='color:green'>Running (#");
            client.print(currentScheduleIndex);
            client.println(")</td></tr>");
          } else {
            client.println("<tr><td>Schedule</td><td style='color:gray'>Idle</td></tr>");
          }
          client.println("</table>");

          if (pwmSubmitted) client.println("<p style='color:green'>Valve PWM Saved!</p>");

          client.println("<h3>Valve PWM %</h3>");
          client.println("<form action='/savepwm' method='GET'>");
          client.println("<table><tr><th>Valve</th><th>On %</th><th>Hold %</th></tr>");

          client.print("<tr><td>Inside</td>");
          client.print("<td><input type='number' name='vi' value='"); client.print(insideValve.getPwmOn());   client.print("' min='0' max='100'></td>");
          client.print("<td><input type='number' name='hi' value='"); client.print(insideValve.getPwmHold()); client.println("' min='0' max='100'></td></tr>");

          client.print("<tr><td>Outside</td>");
          client.print("<td><input type='number' name='vo' value='"); client.print(outsideValve.getPwmOn());   client.print("' min='0' max='100'></td>");
          client.print("<td><input type='number' name='ho' value='"); client.print(outsideValve.getPwmHold()); client.println("' min='0' max='100'></td></tr>");

          client.print("<tr><td>Tank</td>");
          client.print("<td><input type='number' name='vt' value='"); client.print(tankValve.getPwmOn());   client.print("' min='0' max='100'></td>");
          client.print("<td><input type='number' name='ht' value='"); client.print(tankValve.getPwmHold()); client.println("' min='0' max='100'></td></tr>");

          client.println("</table><br><input type='submit' value='Save'></form>");

          client.println("<hr><p><a href='/update'>Firmware Update</a></p>");
          printHTMLFooter(client);
          break;
        } else {
          if (reqLine.startsWith("GET /save?")) {
            formSubmitted = true;
            for (int i = 0; i < MAX_SCHEDULES; i++) schedules[i].active = false;

            int searchPos = 10;
            int httpPos = reqLine.indexOf(" HTTP/", searchPos);
            int paramsEnd = (httpPos > 0) ? httpPos : reqLine.length();

            while (searchPos < paramsEnd) {
              int nextAmp = reqLine.indexOf('&', searchPos);
              int endStr = (nextAmp > -1 && nextAmp < paramsEnd) ? nextAmp : paramsEnd;
              String pair = reqLine.substring(searchPos, endStr);
              int eq = pair.indexOf('=');
              if (eq > 0) {
                String key = pair.substring(0, eq);
                String val = pair.substring(eq + 1);
                int prefixLen = key.startsWith("time") ? 4 : 3;
                int idx = key.substring(prefixLen).toInt();
                if (idx >= 0 && idx < MAX_SCHEDULES) {
                  if (key.startsWith("act")) {
                    schedules[idx].active = true;
                  } else if (key.startsWith("tim")) {
                    int pC = val.indexOf("%3A");
                    if (pC >= 0) {
                      schedules[idx].startHour   = val.substring(0, pC).toInt();
                      schedules[idx].startMinute = val.substring(pC + 3).toInt();
                    } else {
                      pC = val.indexOf(':');
                      if (pC >= 0) {
                        schedules[idx].startHour   = val.substring(0, pC).toInt();
                        schedules[idx].startMinute = val.substring(pC + 1).toInt();
                      }
                    }
                  } else if (key.startsWith("dur")) {
                    schedules[idx].durationMinutes = val.toInt();
                  }
                }
              }
              searchPos = endStr + 1;
            }
            saveSchedules();
          } else if (reqLine.startsWith("GET /zone?")) {
            int httpPos   = reqLine.indexOf(" HTTP/", 10);
            int paramsEnd = (httpPos > 0) ? httpPos : reqLine.length();
            int searchPos = 10;
            while (searchPos < paramsEnd) {
              int nextAmp = reqLine.indexOf('&', searchPos);
              int endStr  = (nextAmp > -1 && nextAmp < paramsEnd) ? nextAmp : paramsEnd;
              String pair = reqLine.substring(searchPos, endStr);
              int eq = pair.indexOf('=');
              if (eq > 0) {
                String key = pair.substring(0, eq);
                int    val = pair.substring(eq + 1).toInt();
                if      (key == "inside")  webReqInside  = (val != 0);
                else if (key == "outside") webReqOutside = (val != 0);
              }
              searchPos = endStr + 1;
            }
          } else if (reqLine.startsWith("GET /savepwm?")) {
            pwmSubmitted = true;

            int searchPos = 13;
            int httpPos   = reqLine.indexOf(" HTTP/", searchPos);
            int paramsEnd = (httpPos > 0) ? httpPos : reqLine.length();

            while (searchPos < paramsEnd) {
              int nextAmp = reqLine.indexOf('&', searchPos);
              int endStr  = (nextAmp > -1 && nextAmp < paramsEnd) ? nextAmp : paramsEnd;
              String pair = reqLine.substring(searchPos, endStr);
              int eq = pair.indexOf('=');
              if (eq > 0) {
                String key = pair.substring(0, eq);
                int    val = constrain(pair.substring(eq + 1).toInt(), 0, 100);
                if      (key == "vi") { valvePwm[0] = val; insideValve.setPwmOn(val); }
                else if (key == "vo") { valvePwm[1] = val; outsideValve.setPwmOn(val); }
                else if (key == "vt") { valvePwm[2] = val; tankValve.setPwmOn(val); }
                else if (key == "hi") { valvePwm[3] = val; insideValve.setPwmHold(val); }
                else if (key == "ho") { valvePwm[4] = val; outsideValve.setPwmHold(val); }
                else if (key == "ht") { valvePwm[5] = val; tankValve.setPwmHold(val); }
              }
              searchPos = endStr + 1;
            }
            saveValvePwm();
          }
          reqLine = "";
        }
      }
    }
  }

  delay(1);
  client.stop();
}
