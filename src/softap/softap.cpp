#include "softap/softap.h"

#include "config/prefs.h"
#include "gps/gps.h"
#include "log/wigle_log.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <stdio.h>
#include <string.h>

#ifndef TAWNI_VERSION
#define TAWNI_VERSION "0.0.0"
#endif

namespace {

WebServer g_server(80);
DNSServer g_dns;
bool g_active = false;
bool g_stop_req = false;
bool g_ota_ok = false;
bool g_ota_active = false;
char g_ip[16] = "";

void send_flash_chunks(PGM_P s) {
  if (!s) return;
  char buf[192];
  const size_t len = strlen_P(s);
  size_t fed = 0;
  for (size_t off = 0; off < len;) {
    size_t n = len - off;
    if (n > sizeof(buf)) n = sizeof(buf);
    memcpy_P(buf, s + off, n);
    g_server.chunkWrite(buf, n);
    off += n;
    fed += n;
    if (fed >= 512) {
      fed = 0;
      delay(0);
      yield();
    }
  }
}

void send_ram_chunk(const char* s) {
  if (s && s[0]) {
    g_server.chunkWrite(s, strlen(s));
  }
}

const char* gps_status_brief(void) {
  GpsFix fix = {};
  gps_get(&fix);
  switch (fix.state) {
    case kGpsFix:
      return "GPS fix";
    case kGpsSearching:
      return "GPS searching";
    default:
      return "GPS quiet";
  }
}

void send_reboot_notice(const char* msg) {
  g_server.sendHeader(F("Connection"), F("close"));
  char html[420];
  snprintf(html, sizeof(html),
           "<!DOCTYPE html><html><head><meta charset=utf-8>"
           "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
           "<title>TawniWardriver</title>"
           "<style>body{font-family:system-ui,sans-serif;background:#0B0F14;"
           "color:#E8EEF4;margin:0;padding:24px;max-width:480px}"
           "p{color:#8B98A8}</style></head><body>"
           "<h1>%s</h1><p>Rebooting… You can leave this Wi‑Fi.</p>"
           "</body></html>",
           msg ? msg : "Saved");
  g_server.send(200, "text/html", html);
  delay(400);
  ESP.restart();
}

void send_page(const char* flash_msg, bool flash_ok) {
  Serial.printf("SoftAP page stream (heap %u maxblk %u)\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  g_server.chunkResponseBegin("text/html");
  delay(0);

  send_flash_chunks(PSTR(
      "<!DOCTYPE html><html><head><meta charset=utf-8>"
      "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
      "<title>TawniWardriver</title><style>"
      "body{font-family:system-ui,sans-serif;background:#0B0F14;color:#E8EEF4;"
      "margin:0;padding:16px;max-width:480px}"
      "h1{font-size:1.25rem;margin:0 0 4px}"
      "h2{font-size:1rem;margin:22px 0 8px}"
      ".muted{color:#8B98A8;font-size:.9rem;margin:0 0 12px;line-height:1.4}"
      "input[type=file]{width:100%;box-sizing:border-box;margin:8px 0;padding:10px;"
      "border-radius:6px;border:1px solid #2A3544;background:#151C26;color:#E8EEF4}"
      "button,.btn{display:inline-block;margin:8px 8px 0 0;padding:12px 16px;"
      "border:0;border-radius:6px;font-size:1rem;cursor:pointer;text-decoration:none}"
      ".primary{background:#3DDC97;color:#0B0F14;font-weight:600}"
      ".secondary{background:#2A3544;color:#E8EEF4}"
      ".danger{background:#F05152;color:#fff}"
      ".ok{color:#3DDC97}.err{color:#F05152}"
      "hr{border:0;border-top:1px solid #2A3544;margin:22px 0}"
      ".bar{height:8px;background:#2A3544;border-radius:4px;overflow:hidden;margin:8px 0}"
      ".bar>i{display:block;height:100%;width:0;background:#3DDC97}"
      "details{margin-top:16px}.details-body{margin-top:8px}"
      "</style></head><body>"
      "<h1>TawniWardriver</h1>"));

  {
    char line[96];
    snprintf(line, sizeof(line), "<p class=muted>v" TAWNI_VERSION " · Rufous</p>");
    send_ram_chunk(line);
  }

  if (flash_msg && flash_msg[0]) {
    char line[160];
    snprintf(line, sizeof(line), "<p class=\"%s\">%s</p>", flash_ok ? "ok" : "err", flash_msg);
    send_ram_chunk(line);
  }

  {
    char line[320];
    snprintf(line, sizeof(line),
             "<h2>Status</h2>"
             "<p class=muted>Survey paused. Start/stop logging on the device.</p>"
             "<p>Logging <b>%s</b> · %s<br>"
             "CSV %u B · Wi‑Fi %u · BLE %u · ~%u KB free</p>",
             wigle_logging() ? "ON" : "OFF", gps_status_brief(),
             (unsigned)wigle_file_size(), (unsigned)wigle_wifi_rows(),
             (unsigned)wigle_ble_rows(), (unsigned)(wigle_free_bytes() / 1024));
    send_ram_chunk(line);
  }

  {
    const bool land = prefs_get_orient() == kOrientLandscape;
    char line[420];
    snprintf(line, sizeof(line),
             "<h2>Screen</h2>"
             "<p class=muted>Portrait is default. Landscape uses the wide layout. "
             "Changing orientation reboots the device. Buttons keep the same jobs.</p>"
             "<form method=POST action=/orient>"
             "<button class=\"%s\" name=v value=0 type=submit>Portrait</button>"
             "<button class=\"%s\" name=v value=1 type=submit>Landscape</button>"
             "</form>",
             land ? "secondary" : "primary", land ? "primary" : "secondary");
    send_ram_chunk(line);
  }

  send_flash_chunks(PSTR(
      "<h2>Log</h2>"
      "<p><a class=\"btn primary\" href=/log.csv>Download Wigle CSV</a></p>"
      "<form method=POST action=/log/clear id=clearForm>"
      "<button class=danger type=submit>Clear log</button></form>"
      "<h2>Firmware</h2>"
      "<p class=muted>App <code>.bin</code> only — stay powered.</p>"
      "<form id=otaForm>"
      "<input type=file name=firmware accept=.bin required>"
      "<button class=primary type=submit>Upload &amp; reboot</button>"
      "</form>"
      "<div class=bar><i id=otaBar></i></div>"
      "<p id=otaMsg class=muted></p>"
      "<hr>"
      "<form method=POST action=/stop>"
      "<button class=primary type=submit>Exit hotspot</button>"
      "</form>"
      "<p class=muted>Returns to the cabin.</p>"
      "<details><summary class=muted>Factory reset</summary>"
      "<div class=details-body>"
      "<form method=POST action=/factory-reset id=factoryForm>"
      "<button class=danger type=submit>Erase prefs &amp; log</button>"
      "</form>"
      "<p class=muted>Reboots after wipe.</p>"
      "</div></details>"
      "<script>"
      "var otaForm=document.getElementById('otaForm');"
      "var otaBar=document.getElementById('otaBar');"
      "var otaMsg=document.getElementById('otaMsg');"
      "if(otaForm){otaForm.addEventListener('submit',function(ev){"
      "ev.preventDefault();"
      "var f=otaForm.querySelector('input[type=file]').files[0];"
      "if(!f){otaMsg.textContent='Pick a .bin first';return;}"
      "var xhr=new XMLHttpRequest();"
      "xhr.open('POST','/update');"
      "xhr.upload.onprogress=function(e){"
      "if(e.lengthComputable){otaBar.style.width=((e.loaded/e.total)*100)+'%';}"
      "};"
      "xhr.onload=function(){"
      "if(xhr.status===200){otaMsg.textContent=xhr.responseText;otaBar.style.width='100%';}"
      "else{otaMsg.textContent=xhr.responseText||('HTTP '+xhr.status);}"
      "};"
      "xhr.onerror=function(){otaMsg.textContent='Upload failed';};"
      "var fd=new FormData();fd.append('firmware',f);xhr.send(fd);"
      "otaMsg.textContent='Uploading…';});}"
      "var factoryForm=document.getElementById('factoryForm');"
      "if(factoryForm){factoryForm.addEventListener('submit',function(ev){"
      "if(!confirm('Factory reset and reboot?'))ev.preventDefault();"
      "});}"
      "var clearForm=document.getElementById('clearForm');"
      "if(clearForm){clearForm.addEventListener('submit',function(ev){"
      "if(!confirm('Erase Wigle CSV on device?'))ev.preventDefault();"
      "});}"
      "</script>"
      "</body></html>"));

  g_server.chunkResponseEnd();
}

void handle_root() { send_page(nullptr, true); }

void handle_orient() {
  const String v = g_server.arg("v");
  const uint8_t want = (v == "1") ? kOrientLandscape : kOrientPortrait;
  if (want == prefs_get_orient()) {
    send_page(want == kOrientLandscape ? "Already landscape." : "Already portrait.", true);
    return;
  }
  if (!prefs_set_orient(want)) {
    send_page("Could not save orientation.", false);
    return;
  }
  send_reboot_notice(want == kOrientLandscape ? "Landscape" : "Portrait");
}

void handle_log_clear() {
  if (!wigle_clear()) {
    send_page("Could not clear log.", false);
    return;
  }
  send_page("Log cleared.", true);
}

void handle_log_csv() {
  if (!LittleFS.exists(wigle_path())) {
    wigle_ensure_file();
  }
  File f = LittleFS.open(wigle_path(), "r");
  if (!f) {
    g_server.send(404, "text/plain", "No log file");
    return;
  }
  g_server.sendHeader(F("Content-Disposition"), F("attachment; filename=\"tawni-wigle.csv\""));
  g_server.streamFile(f, "text/csv");
  f.close();
}

void handle_factory_reset() {
  wigle_set_logging(false);
  wigle_clear();
  prefs_factory_reset();
  Serial.println("SoftAP factory reset — clearing prefs/log and rebooting");
  send_reboot_notice("Factory reset");
}

void handle_update_upload() {
  HTTPUpload& upload = g_server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    g_ota_ok = false;
    g_ota_active = true;
    Serial.printf("SoftAP OTA start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
      g_ota_active = false;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning()) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      g_ota_ok = true;
      Serial.printf("SoftAP OTA success: %u bytes\n", (unsigned)upload.totalSize);
    } else {
      Update.printError(Serial);
    }
    g_ota_active = false;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    g_ota_ok = false;
    g_ota_active = false;
    Serial.println("SoftAP OTA aborted");
  }
}

void handle_update_done() {
  g_server.sendHeader(F("Connection"), F("close"));
  if (g_ota_ok && !Update.hasError()) {
    g_server.send(200, "text/plain", F("OK — rebooting…"));
    delay(400);
    ESP.restart();
    return;
  }
  String err = F("Update failed");
  if (Update.hasError()) {
    err += F(" (check Serial / keep power on / use app .bin not full flash image)");
  }
  g_server.send(500, "text/plain", err);
  g_ota_ok = false;
  g_ota_active = false;
}

void handle_goodbye() {
  g_server.sendHeader(F("Cache-Control"), F("no-store"), true);
  if (!g_stop_req) {
    g_server.sendHeader(F("Location"), F("/"), true);
    g_server.send(302, "text/plain", "");
    return;
  }
  g_server.send(200, "text/html",
                F("<!DOCTYPE html><html><head><meta charset=utf-8>"
                  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
                  "<title>TawniWardriver</title>"
                  "<style>"
                  "body{font-family:system-ui,sans-serif;background:#0B0F14;color:#E8EEF4;"
                  "margin:0;padding:24px;max-width:480px}"
                  "h1{font-size:1.25rem;margin:0 0 8px}"
                  "p{color:#8B98A8;line-height:1.4}"
                  "</style></head><body>"
                  "<h1>Hotspot stopping…</h1>"
                  "<p>You can leave this Wi‑Fi network. Cabin returns.</p>"
                  "</body></html>"));
}

void handle_stop() {
  g_stop_req = true;
  g_server.sendHeader(F("Location"), F("/goodbye"), true);
  g_server.send(303, "text/plain", "");
}

void handle_stop_get() {
  g_server.sendHeader(F("Location"), F("/goodbye"), true);
  g_server.send(302, "text/plain", "");
}

void handle_generate_204() { g_server.send(204); }

void handle_apple_captive() {
  g_server.send(200, "text/html",
                F("<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"));
}

void handle_ms_connecttest() {
  g_server.send(200, "text/plain", F("Microsoft Connect Test"));
}

void handle_ms_ncsi() { g_server.send(200, "text/plain", F("Microsoft NCSI")); }

void handle_not_found() {
  char loc[40];
  snprintf(loc, sizeof(loc), "http://%s/", g_ip[0] ? g_ip : "192.168.4.1");
  g_server.sendHeader(F("Location"), loc, true);
  g_server.send(302, "text/plain", "");
}

}  // namespace

bool softap_start(void) {
  if (g_active) return true;

  g_stop_req = false;
  g_ota_ok = false;
  g_ota_active = false;

  delay(150);

  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  {
    const uint32_t settle_until = millis() + 600;
    while ((int32_t)(millis() - settle_until) < 0) {
      delay(40);
      if (ESP.getMaxAllocHeap() >= 24000) {
        break;
      }
    }
    delay(150);
  }
  Serial.printf("SoftAP pre-AP (heap %u maxblk %u)\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  WiFi.mode(WIFI_AP);
  delay(80);

  bool ok = WiFi.softAP(kSoftApSsid, nullptr, 1, 0, 1);
  if (!ok) {
    Serial.println("SoftAP start FAILED — retry after WIFI_OFF");
    WiFi.mode(WIFI_OFF);
    delay(500);
    WiFi.mode(WIFI_AP);
    delay(80);
    ok = WiFi.softAP(kSoftApSsid, nullptr, 1, 0, 1);
  }
  if (!ok) {
    Serial.println("SoftAP start FAILED");
    WiFi.mode(WIFI_OFF);
    return false;
  }

  delay(200);
  IPAddress ip = WiFi.softAPIP();
  snprintf(g_ip, sizeof(g_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  Serial.printf("SoftAP %s  http://%s  ch=%d  heap=%u maxblk=%u\n", kSoftApSsid, g_ip,
                WiFi.channel(), (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());

  g_dns.start(53, "*", ip);

  g_server.on("/", HTTP_GET, handle_root);
  g_server.on("/orient", HTTP_POST, handle_orient);
  g_server.on("/log/clear", HTTP_POST, handle_log_clear);
  g_server.on("/log.csv", HTTP_GET, handle_log_csv);
  g_server.on("/factory-reset", HTTP_POST, handle_factory_reset);
  g_server.on("/update", HTTP_POST, handle_update_done, handle_update_upload);
  g_server.on("/stop", HTTP_POST, handle_stop);
  g_server.on("/stop", HTTP_GET, handle_stop_get);
  g_server.on("/goodbye", HTTP_GET, handle_goodbye);
  g_server.on("/generate_204", HTTP_GET, handle_generate_204);
  g_server.on("/hotspot-detect.html", HTTP_GET, handle_apple_captive);
  g_server.on("/library/test/success.html", HTTP_GET, handle_apple_captive);
  g_server.on("/connecttest.txt", HTTP_GET, handle_ms_connecttest);
  g_server.on("/ncsi.txt", HTTP_GET, handle_ms_ncsi);
  g_server.on("/fwlink", HTTP_GET, handle_not_found);
  g_server.onNotFound(handle_not_found);
  g_server.begin();

  g_active = true;
  return true;
}

void softap_stop(void) {
  if (!g_active) return;
  g_server.stop();
  g_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  g_active = false;
  g_stop_req = false;
  g_ip[0] = '\0';
  Serial.println("SoftAP stopped");
  delay(350);
}

void softap_loop(void) {
  if (!g_active) return;
  g_dns.processNextRequest();
  g_server.handleClient();
}

bool softap_active(void) { return g_active; }

const char* softap_ip(void) { return g_ip; }

bool softap_stop_requested(void) { return g_stop_req; }

void softap_clear_stop_request(void) { g_stop_req = false; }

bool softap_ota_active(void) { return g_ota_active; }
