#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
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

static AsyncWebServer server(80);
static AsyncEventSource events("/events");

bool otaInProgress = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void printWiFiStatus() {
  Serial.print("SSID: "); Serial.println(WiFi.SSID());
  Serial.print("IP Address: "); Serial.println(WiFi.localIP());
}

static bool checkOTAAuth(AsyncWebServerRequest* request) {
  if (!request->hasHeader("Authorization")) return false;
  String expected = "Basic " + base64::encode(String(OTA_USER) + ":" + OTA_PASS);
  return request->header("Authorization") == expected;
}

// ---------------------------------------------------------------------------
// HTML page
// ---------------------------------------------------------------------------

static String buildPage(bool formSaved, bool phaseSaved, bool pwmSaved) {
  unsigned long e = currentEpoch + ((millis() - lastEpochUpdateMillis) / 1000);
  int eh = (e % 86400L) / 3600;
  int em = (e % 3600) / 60;
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", eh, em);

  char phaseDesc[64];
  sprintf(phaseDesc, "Alternates Inside (%dmin) then Outside (%dmin) per cycle.", insidePhaseMins, outsidePhaseMins);

  String html = F("<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:Arial;padding:20px;}"
    "table,th,td{border:1px solid black;border-collapse:collapse;padding:5px;}"
    ".on{color:green} .off{color:gray}"
    ".btn{display:inline-block;padding:12px 24px;margin-right:12px;color:white;"
         "text-decoration:none;border-radius:6px;font-size:16px}"
    ".btn-on{background:#4CAF50} .btn-off{background:#aaa}"
    "</style>"
    "<script>"
    "var es=new EventSource('/events');"
    "function upd(id,val,onText,offText,onCls,offCls){"
    "  var el=document.getElementById(id); if(!el)return;"
    "  var on=(val==='1'||val==='ON'||val==='OPEN'||val==='running');"
    "  el.textContent=on?onText:offText;"
    "  el.className=on?onCls:offCls;"
    "}"
    "es.addEventListener('pump',     function(e){upd('s-pump',    e.data,'ON','OFF','on','off')});"
    "es.addEventListener('tank',     function(e){upd('s-tank',    e.data,'OPEN','CLOSED','on','off')});"
    "es.addEventListener('inside',   function(e){upd('s-inside',  e.data,'OPEN','CLOSED','on','off')});"
    "es.addEventListener('outside',  function(e){upd('s-outside', e.data,'OPEN','CLOSED','on','off')});"
    "es.addEventListener('swi',      function(e){upd('s-swi',     e.data,'ON','OFF','on','off')});"
    "es.addEventListener('swo',      function(e){upd('s-swo',     e.data,'ON','OFF','on','off')});"
    "es.addEventListener('swia',     function(e){upd('s-swia',    e.data,'ON','OFF','on','off')});"
    "es.addEventListener('swoa',     function(e){upd('s-swoa',    e.data,'ON','OFF','on','off')});"
    "es.addEventListener('sched',    function(e){"
    "  var el=document.getElementById('s-sched'); if(!el)return;"
    "  el.textContent=e.data; el.className=(e.data==='Idle')?'off':'on';"
    "  var btn=document.getElementById('stop-sched');"
    "  if(btn) btn.style.display=(e.data==='Idle')?'none':'inline-block';"
    "});"
    "es.addEventListener('time',     function(e){"
    "  var el=document.getElementById('s-time'); if(el) el.textContent=e.data+' (UTC-6)';"
    "});"
    "</script>"
    "</head><body>");

  html += "<h2>Sprinky Controller - Merida</h2>";
  html += "<p>Time: <span id='s-time'>" + String(timeStr) + " (UTC-6)</span></p>";
  html += "<p>Firmware: v" FIRMWARE_VERSION "</p>";

  // --- Schedules ---
  if (formSaved) html += "<p style='color:green'>Schedules saved!</p>";
  html += "<h3>Schedules</h3><p>" + String(phaseDesc) + "</p>";
  html += "<form action='/save' method='GET'>";
  html += "<table><tr><th>ID</th><th>Active</th><th>Start (HH:MM)</th><th>Duration (min)</th></tr>";
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    char ts[6];
    sprintf(ts, "%02d:%02d", schedules[i].startHour, schedules[i].startMinute);
    html += "<tr><td>" + String(i) + "</td>";
    html += "<td><input type='checkbox' name='act" + String(i) + "' value='1'" +
            (schedules[i].active ? " checked" : "") + "></td>";
    html += "<td><input type='time' name='time" + String(i) + "' value='" + ts + "'></td>";
    html += "<td><input type='number' name='dur" + String(i) + "' value='" +
            schedules[i].durationMinutes + "' min='0' max='300'></td></tr>";
  }
  html += "</table><br><input type='submit' value='Save'></form>";

  // --- Auto Phase Durations ---
  if (phaseSaved) html += "<p style='color:green'>Phase durations saved!</p>";
  html += "<h3>Auto Phase Durations</h3>";
  html += "<form action='/savephase' method='GET'>";
  html += "<table><tr><th>Zone</th><th>Duration (min)</th></tr>";
  html += "<tr><td>Inside</td><td><input type='number' name='ip' value='" +
          String(insidePhaseMins) + "' min='1' max='60'></td></tr>";
  html += "<tr><td>Outside</td><td><input type='number' name='op' value='" +
          String(outsidePhaseMins) + "' min='1' max='60'></td></tr>";
  html += "</table><br><input type='submit' value='Save'></form>";

  // --- Manual Zone Control ---
  html += "<h3>Manual Zone Control</h3><p style='margin-bottom:8px'>";
  html += webReqInside
    ? "<a href='/zone?inside=0' class='btn btn-on'>Inside: ON</a>"
    : "<a href='/zone?inside=1' class='btn btn-off'>Inside: OFF</a>";
  html += webReqOutside
    ? "<a href='/zone?outside=0' class='btn btn-on'>Outside: ON</a>"
    : "<a href='/zone?outside=1' class='btn btn-off'>Outside: OFF</a>";
  html += "</p>";

  // --- Status (live via SSE) ---
  html += "<h3>Status</h3>";
  html += "<table><tr><th>Component</th><th>State</th></tr>";

  auto row = [&](const char* label, const char* id, bool on, const char* onT, const char* offT) {
    html += "<tr><td>"; html += label;
    html += "</td><td class='"; html += (on ? "on" : "off");
    html += "' id='"; html += id; html += "'>";
    html += (on ? onT : offT);
    html += "</td></tr>";
  };

  row("Pump",           "s-pump",   pumpActive,                   "ON",   "OFF");
  row("Tank Valve",     "s-tank",   tankValve.isActive(),         "OPEN", "CLOSED");
  row("Inside Valve",   "s-inside", insideValve.isActive(),       "OPEN", "CLOSED");
  row("Outside Valve",  "s-outside",outsideValve.isActive(),      "OPEN", "CLOSED");
  row("Inside Switch",  "s-swi",    insideSwitch.getState(),      "ON",   "OFF");
  row("Outside Switch", "s-swo",    outsideSwitch.getState(),     "ON",   "OFF");
  row("Inside Auto SW", "s-swia",   insideAutoSwitch.getState(),  "ON",   "OFF");
  row("Outside Auto SW","s-swoa",   outsideAutoSwitch.getState(), "ON",   "OFF");

  String schedText = scheduleRunning
    ? ("Running (#" + String(currentScheduleIndex) + ")")
    : "Idle";
  html += "<tr><td>Schedule</td><td><span class='";
  html += scheduleRunning ? "on" : "off";
  html += "' id='s-sched'>" + schedText + "</span>";
  html += " <a id='stop-sched' href='/stopschedule' class='btn btn-on' "
          "style='padding:4px 10px;font-size:13px;margin-left:10px;background:#d9534f;display:";
  html += scheduleRunning ? "inline-block" : "none";
  html += "'>Stop</a></td></tr>";
  html += "</table>";

  // --- Valve PWM ---
  if (pwmSaved) html += "<p style='color:green'>Valve PWM saved!</p>";
  html += "<h3>Valve PWM %</h3>";
  html += "<form action='/savepwm' method='GET'>";
  html += "<table><tr><th>Valve</th><th>On %</th><th>Hold %</th></tr>";
  html += "<tr><td>Inside</td>"
          "<td><input type='number' name='vi' value='" + String(insideValve.getPwmOn())   + "' min='0' max='100'></td>"
          "<td><input type='number' name='hi' value='" + String(insideValve.getPwmHold()) + "' min='0' max='100'></td></tr>";
  html += "<tr><td>Outside</td>"
          "<td><input type='number' name='vo' value='" + String(outsideValve.getPwmOn())   + "' min='0' max='100'></td>"
          "<td><input type='number' name='ho' value='" + String(outsideValve.getPwmHold()) + "' min='0' max='100'></td></tr>";
  html += "<tr><td>Tank</td>"
          "<td><input type='number' name='vt' value='" + String(tankValve.getPwmOn())   + "' min='0' max='100'></td>"
          "<td><input type='number' name='ht' value='" + String(tankValve.getPwmHold()) + "' min='0' max='100'></td></tr>";
  html += "</table><br><input type='submit' value='Save'></form>";

  html += "<hr><p><a href='/update'>Firmware Update</a></p>";
  html += "</body></html>";
  return html;
}

// ---------------------------------------------------------------------------
// SSE push  (called from main loop)
// ---------------------------------------------------------------------------

void webserverPushStatus() {
  if (events.count() == 0) return;

  unsigned long e = currentEpoch + ((millis() - lastEpochUpdateMillis) / 1000);
  int eh = (e % 86400L) / 3600;
  int em = (e % 3600) / 60;
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", eh, em);

  events.send(pumpActive              ? "1" : "0", "pump");
  events.send(tankValve.isActive()    ? "1" : "0", "tank");
  events.send(insideValve.isActive()  ? "1" : "0", "inside");
  events.send(outsideValve.isActive() ? "1" : "0", "outside");
  events.send(insideSwitch.getState()      ? "1" : "0", "swi");
  events.send(outsideSwitch.getState()     ? "1" : "0", "swo");
  events.send(insideAutoSwitch.getState()  ? "1" : "0", "swia");
  events.send(outsideAutoSwitch.getState() ? "1" : "0", "swoa");
  events.send(timeStr, "time");

  if (scheduleRunning) {
    String s = "Running (#" + String(currentScheduleIndex) + ")";
    events.send(s.c_str(), "sched");
  } else {
    events.send("Idle", "sched");
  }
}

// ---------------------------------------------------------------------------
// OTA upload handler
// ---------------------------------------------------------------------------

static void shutdownIrrigation() {
  extern bool pumpActive;
  pumpActive = false;
  digitalWrite(PIN_PUMP, LOW);
  insideValve.turnOff();
  outsideValve.turnOff();
  tankValve.turnOff();
  scheduleRunning = false;
  Serial.println("OTA: irrigation shut down");
}

// ---------------------------------------------------------------------------
// Server setup
// ---------------------------------------------------------------------------

void webserverBegin() {
  // --- Main page ---
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    bool fs = request->hasParam("_fs");
    bool ps = request->hasParam("_ps");
    bool ws = request->hasParam("_ws");
    request->send(200, "text/html", buildPage(fs, ps, ws));
  });

  // --- Save schedules ---
  server.on("/save", HTTP_GET, [](AsyncWebServerRequest* request) {
    for (int i = 0; i < MAX_SCHEDULES; i++) schedules[i].active = false;
    for (int i = 0; i < MAX_SCHEDULES; i++) {
      String keyAct  = "act"  + String(i);
      String keyTime = "time" + String(i);
      String keyDur  = "dur"  + String(i);
      if (request->hasParam(keyAct))  schedules[i].active = true;
      if (request->hasParam(keyTime)) {
        String t = request->getParam(keyTime)->value();
        int c = t.indexOf(':');
        if (c >= 0) {
          schedules[i].startHour   = constrain(t.substring(0, c).toInt(),   0,  23);
          schedules[i].startMinute = constrain(t.substring(c + 1).toInt(),  0,  59);
        }
      }
      if (request->hasParam(keyDur)) {
        schedules[i].durationMinutes = constrain(request->getParam(keyDur)->value().toInt(), 0, 300);
      }
    }
    saveSchedules();
    request->redirect("/?_fs=1");
  });

  // --- Save phase durations ---
  server.on("/savephase", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("ip")) insidePhaseMins  = constrain(request->getParam("ip")->value().toInt(), 1, 60);
    if (request->hasParam("op")) outsidePhaseMins = constrain(request->getParam("op")->value().toInt(), 1, 60);
    savePhaseMins();
    request->redirect("/?_ps=1");
  });

  // --- Stop running schedule ---
  server.on("/stopschedule", HTTP_GET, [](AsyncWebServerRequest* request) {
    stopSchedule();
    request->redirect("/");
  });

  // --- Zone toggle ---
  server.on("/zone", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("inside"))  webReqInside  = request->getParam("inside")->value().toInt() != 0;
    if (request->hasParam("outside")) webReqOutside = request->getParam("outside")->value().toInt() != 0;
    request->redirect("/");
  });

  // --- Save valve PWM ---
  server.on("/savepwm", HTTP_GET, [](AsyncWebServerRequest* request) {
    auto applyPwm = [&](const char* key, int idx, void(*setFn)(int)) {
      if (request->hasParam(key)) {
        int v = constrain(request->getParam(key)->value().toInt(), 0, 100);
        valvePwm[idx] = v;
        setFn(v);
      }
    };
    applyPwm("vi", 0, [](int v){ insideValve.setPwmOn(v); });
    applyPwm("vo", 1, [](int v){ outsideValve.setPwmOn(v); });
    applyPwm("vt", 2, [](int v){ tankValve.setPwmOn(v); });
    applyPwm("hi", 3, [](int v){ insideValve.setPwmHold(v); });
    applyPwm("ho", 4, [](int v){ outsideValve.setPwmHold(v); });
    applyPwm("ht", 5, [](int v){ tankValve.setPwmHold(v); });
    saveValvePwm();
    request->redirect("/?_ws=1");
  });

  // --- OTA GET (form) ---
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!checkOTAAuth(request)) {
      request->requestAuthentication("Sprinky OTA");
      return;
    }
    String html = "<!DOCTYPE html><html><body><h3>Firmware Update</h3>"
                  "<form method='POST' action='/update' enctype='multipart/form-data'>"
                  "<input type='file' name='firmware' accept='.bin'><br><br>"
                  "<input type='submit' value='Upload &amp; Flash'>"
                  "</form><p><a href='/'>Back</a></p></body></html>";
    request->send(200, "text/html", html);
  });

  // --- OTA POST (upload) ---
  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest* request) {
      if (!checkOTAAuth(request)) { otaInProgress = false; request->send(401); return; }
      bool ok = !Update.hasError();
      if (!ok) otaInProgress = false;
      String html = ok
        ? "<!DOCTYPE html><html><body><h3>Update Successful!</h3>"
          "<p>Rebooting...</p>"
          "<script>setTimeout(function(){window.location='/';},10000);</script>"
          "</body></html>"
        : "<!DOCTYPE html><html><body><h3>Update Failed</h3>"
          "<p><a href='/update'>Try again</a></p></body></html>";
      AsyncWebServerResponse* resp = request->beginResponse(200, "text/html", html);
      resp->addHeader("Connection", "close");
      request->send(resp);
      if (ok) {
        delay(500);
        ESP.restart();
      }
    },
    [](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
      // Auth is enforced by the completion handler; silently drop chunks from
      // unauthenticated requests without attempting to send a response here.
      if (!checkOTAAuth(request)) return;
      if (index == 0) {
        otaInProgress = true;
        shutdownIrrigation();
        Serial.printf("OTA: start %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial); otaInProgress = false; return; }
      }
      if (!Update.isRunning()) return;
      if (Update.write(data, len) != len) {
        Update.printError(Serial);
        Update.abort();
        return;
      }
      if (final) {
        if (Update.end(true)) Serial.printf("OTA: done (%u bytes)\n", index + len);
        else Update.printError(Serial);
      }
    }
  );

  // --- SSE ---
  events.onConnect([](AsyncEventSourceClient* client) {
    Serial.println("SSE client connected");
  });
  server.addHandler(&events);

  server.begin();
}
