#include "softap/softap.h"

#include "config/prefs.h"
#include "gps/gps.h"
#include "log/wigle_log.h"
#include "softap/portal_css.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <WebServer.h>
#include <stdio.h>
#include <string.h>

#ifndef TAWNI_VERSION
#define TAWNI_VERSION "0.0.0"
#endif

char g_flash_msg[96] = "";
bool g_flash_ok = true;

namespace {

WebServer g_server(80);
bool g_active = false;
bool g_stop_req = false;
bool g_ota_ok = false;
bool g_ota_active = false;
bool g_routes_ok = false;
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
  if (s && s[0]) g_server.chunkWrite(s, strlen(s));
}

void send_progmem_css(void) {
  char buf[256];
  size_t fed = 0;
  for (size_t off = 0; off < kPortalCssBytes;) {
    size_t n = kPortalCssBytes - off;
    if (n > sizeof(buf)) n = sizeof(buf);
    memcpy_P(buf, PORTAL_CSS + off, n);
    g_server.chunkWrite(buf, n);
    off += n;
    fed += n;
    if (fed >= 1024) {
      fed = 0;
      delay(0);
      yield();
    }
  }
}

const char* gps_badge_class(void) {
  GpsFix fix = {};
  gps_get(&fix);
  switch (fix.state) {
    case kGpsFix:
      return "badge-success";
    case kGpsSearching:
      return "badge-warning";
    default:
      return "badge-ghost";
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

void send_html_head(const char* title) {
  send_flash_chunks(PSTR(
      "<!DOCTYPE html><html data-theme=tawni><head><meta charset=utf-8>"
      "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
      "<link rel=stylesheet href=/portal.css?v=3>"));
  {
    char line[96];
    snprintf(line, sizeof(line), "<title>%s</title></head>", title ? title : "Wardriver");
    send_ram_chunk(line);
  }
}

void send_navbar(void) {
  send_flash_chunks(PSTR(
      "<div class=\"navbar bg-base-200 rounded-box px-3 min-h-12\">"
      "<div class=navbar-start><span class=\"font-bold text-lg\">Wardriver</span>"
      "<span class=\"badge badge-ghost badge-sm ml-2\">Rufous</span></div>"
      "<div class=navbar-end><span class=\"text-xs opacity-70\">v" TAWNI_VERSION "</span></div>"
      "</div>"));
}

void send_reboot_notice(const char* msg) {
  g_server.sendHeader(F("Connection"), F("close"));
  g_server.chunkResponseBegin("text/html");
  send_html_head("Wardriver");
  send_flash_chunks(PSTR("<body class=portal-shell><main class=portal-main>"));
  send_navbar();
  {
    char line[200];
    snprintf(line, sizeof(line),
             "<div class=\"card bg-base-200 shadow-sm\"><div class=\"card-body p-4 gap-2\">"
             "<h1 class=\"card-title text-base\">%s</h1>"
             "<p class=\"text-sm opacity-70\">Rebooting. You can leave this Wi-Fi.</p>"
             "</div></div></main></body></html>",
             msg ? msg : "Saved");
    send_ram_chunk(line);
  }
  g_server.chunkResponseEnd();
  delay(400);
  ESP.restart();
}

void send_page(const char* flash_msg, bool flash_ok) {
  Serial.printf("Portal page (heap %u)\n", (unsigned)ESP.getFreeHeap());
  g_server.chunkResponseBegin("text/html");
  delay(0);
  send_html_head("Wardriver");
  send_flash_chunks(PSTR("<body class=portal-shell><main class=portal-main>"));
  send_navbar();

  if (flash_msg && flash_msg[0]) {
    char line[200];
    snprintf(line, sizeof(line),
             "<div class=\"alert %s text-sm\"><span>%s</span></div>",
             flash_ok ? "alert-success" : "alert-error", flash_msg);
    send_ram_chunk(line);
  }

  // 1. Status
  {
    char line[640];
    snprintf(line, sizeof(line),
             "<div class=\"card bg-base-200 shadow-sm\">"
             "<div class=\"card-body portal-card gap-4 p-5\">"
             "<h2 class=\"card-title text-base\">Status</h2>"
             "<p class=\"text-sm opacity-70 leading-relaxed\">Survey is paused while this portal is up. "
             "You are on the device setup hotspot.</p>"
             "<div class=\"flex flex-wrap gap-3\">"
             "<span class=\"badge badge-outline\">Logging %s</span>"
             "<span class=\"badge %s\">%s</span></div>"
             "<p class=\"text-sm opacity-70 mt-1\">CSV %u B | Wi-Fi %u | BLE %u | ~%u KB free</p>"
             "<code class=\"text-xs opacity-80 block mt-1\">http://%s</code></div></div>",
             wigle_logging() ? "ON" : "OFF", gps_badge_class(), gps_status_brief(),
             (unsigned)wigle_file_size(), (unsigned)wigle_wifi_rows(),
             (unsigned)wigle_ble_rows(), (unsigned)(wigle_free_bytes() / 1024),
             g_ip[0] ? g_ip : "-");
    send_ram_chunk(line);
  }

  // 2. Log
  send_flash_chunks(PSTR(
      "<div class=\"card bg-base-200 shadow-sm\">"
      "<div class=\"card-body portal-card gap-4 p-5\">"
      "<h2 class=\"card-title text-base\">Log</h2>"
      "<a class=\"btn btn-primary btn-block\" href=/log.csv>Download Wigle CSV</a>"
      "<form method=POST action=/log/clear id=clearForm "
      "class=\"mt-3 pt-2 border-t border-base-300\">"
      "<button class=\"btn btn-ghost btn-sm btn-error\" type=submit>Clear log</button>"
      "</form>"
      "<p class=\"text-xs opacity-60 mt-2\">Clear erases the on-device CSV. Confirm when asked.</p>"
      "</div></div>"));

  // 3. Screen
  {
    const bool land = prefs_get_orient() == kOrientLandscape;
    char line[520];
    snprintf(line, sizeof(line),
             "<div class=\"card bg-base-200 shadow-sm\">"
             "<div class=\"card-body portal-card gap-4 p-5\">"
             "<h2 class=\"card-title text-base\">Screen</h2>"
             "<p class=\"text-sm opacity-70 leading-relaxed\">Changing orientation reboots. "
             "Button jobs stay the same.</p>"
             "<form method=POST action=/orient class=\"join w-full mt-1\">"
             "<button class=\"btn %s join-item flex-1\" name=v value=0 type=submit>Portrait</button>"
             "<button class=\"btn %s join-item flex-1\" name=v value=1 type=submit>Landscape</button>"
             "</form></div></div>",
             land ? "btn-ghost" : "btn-primary", land ? "btn-primary" : "btn-ghost");
    send_ram_chunk(line);
  }

  // 4. Firmware update
  send_flash_chunks(PSTR(
      "<div class=\"card bg-base-200 shadow-sm\">"
      "<div class=\"card-body portal-card gap-4 p-5\">"
      "<h2 class=\"card-title text-base\">Firmware update</h2>"
      "<p class=\"text-sm opacity-70 leading-relaxed\">App <code>.bin</code> only. Stay powered.</p>"
      "<form id=otaForm class=\"flex flex-col gap-4 w-full mt-1\">"
      "<input class=\"file-input file-input-bordered w-full\" type=file name=firmware "
      "accept=.bin required>"
      "<button class=\"btn btn-primary btn-block\" type=submit>Upload &amp; reboot</button></form>"
      "<progress id=otaBar class=\"progress progress-primary w-full mt-2\" value=0 max=100></progress>"
      "<p id=otaMsg class=\"text-sm opacity-70 mt-1\"></p></div></div>"));

  // Exit (before danger)
  send_flash_chunks(PSTR(
      "<form method=POST action=/stop class=mt-2>"
      "<button class=\"btn btn-outline btn-block\" type=submit>Exit portal</button></form>"
      "<p class=\"text-xs opacity-60 mt-2\">Returns to the cabin. Survey can run again.</p>"));

  // 5. Danger zone
  send_flash_chunks(PSTR(
      "<div class=\"collapse collapse-arrow bg-base-200 mt-2\">"
      "<input type=checkbox>"
      "<div class=\"collapse-title text-sm font-medium opacity-70\">Danger zone</div>"
      "<div class=collapse-content>"
      "<form method=POST action=/factory-reset id=factoryForm>"
      "<button class=\"btn btn-error btn-sm\" type=submit>Erase prefs &amp; log</button></form>"
      "<p class=\"text-xs opacity-60 mt-2\">Clears display prefs. Reboots after wipe.</p>"
      "</div></div>"));

  send_flash_chunks(PSTR(
      "<script>"
      "var otaForm=document.getElementById('otaForm');"
      "var otaBar=document.getElementById('otaBar');"
      "var otaMsg=document.getElementById('otaMsg');"
      "if(otaForm){otaForm.addEventListener('submit',function(ev){ev.preventDefault();"
      "var f=otaForm.querySelector('input[type=file]').files[0];"
      "if(!f){otaMsg.textContent='Pick a .bin first';return;}"
      "var xhr=new XMLHttpRequest();xhr.open('POST','/update');"
      "xhr.upload.onprogress=function(e){if(e.lengthComputable){"
      "otaBar.value=Math.round((e.loaded/e.total)*100);}};"
      "xhr.onload=function(){if(xhr.status===200){otaMsg.textContent=xhr.responseText;"
      "otaBar.value=100;}else{otaMsg.textContent=xhr.responseText||('HTTP '+xhr.status);}};"
      "xhr.onerror=function(){otaMsg.textContent='Upload failed'};"
      "var fd=new FormData();fd.append('firmware',f);xhr.send(fd);"
      "otaMsg.textContent='Uploading...';});}"
      "var factoryForm=document.getElementById('factoryForm');"
      "if(factoryForm){factoryForm.addEventListener('submit',function(ev){"
      "if(!confirm('Factory reset and reboot?'))ev.preventDefault();});}"
      "var clearForm=document.getElementById('clearForm');"
      "if(clearForm){clearForm.addEventListener('submit',function(ev){"
      "if(!confirm('Erase Wigle CSV on device?'))ev.preventDefault();});}"
      "</script></main></body></html>"));
  g_server.chunkResponseEnd();
}

void handle_portal_css() {
  g_server.sendHeader(F("Cache-Control"), F("public, max-age=86400"));
  g_server.chunkResponseBegin("text/css");
  send_progmem_css();
  g_server.chunkResponseEnd();
}

void handle_root() {
  if (g_flash_msg[0]) {
    send_page(g_flash_msg, g_flash_ok);
    g_flash_msg[0] = '\0';
    return;
  }
  send_page(nullptr, true);
}

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
  if (!LittleFS.exists(wigle_path())) wigle_ensure_file();
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
  Serial.println("Portal factory reset");
  send_reboot_notice("Factory reset");
}

void handle_update_upload() {
  HTTPUpload& upload = g_server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    g_ota_ok = false;
    g_ota_active = true;
    Serial.printf("Portal OTA start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
      g_ota_active = false;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning() && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      g_ota_ok = true;
      Serial.printf("Portal OTA success: %u bytes\n", (unsigned)upload.totalSize);
    } else {
      Update.printError(Serial);
    }
    g_ota_active = false;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    g_ota_ok = false;
    g_ota_active = false;
  }
}

void handle_update_done() {
  g_server.sendHeader(F("Connection"), F("close"));
  if (g_ota_ok && !Update.hasError()) {
    g_server.send(200, "text/plain", F("OK - rebooting..."));
    delay(400);
    ESP.restart();
    return;
  }
  g_server.send(500, "text/plain", F("Update failed"));
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
  g_server.chunkResponseBegin("text/html");
  send_html_head("Wardriver");
  send_flash_chunks(PSTR("<body class=portal-shell><main class=portal-main>"));
  send_navbar();
  send_flash_chunks(PSTR(
      "<div class=\"card bg-base-200 shadow-sm\"><div class=\"card-body p-4 gap-2\">"
      "<h1 class=\"card-title text-base\">Portal stopping...</h1>"
      "<p class=\"text-sm opacity-70\">Cabin returns. Survey can run again.</p>"
      "</div></div></main></body></html>"));
  g_server.chunkResponseEnd();
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
void handle_ms_connecttest() { g_server.send(200, "text/plain", F("Microsoft Connect Test")); }
void handle_ms_ncsi() { g_server.send(200, "text/plain", F("Microsoft NCSI")); }

void handle_not_found() {
  char loc[40];
  snprintf(loc, sizeof(loc), "http://%s/", g_ip[0] ? g_ip : "192.168.4.1");
  g_server.sendHeader(F("Location"), loc, true);
  g_server.send(302, "text/plain", "");
}

void register_routes(void) {
  if (g_routes_ok) return;
  g_server.on("/", HTTP_GET, handle_root);
  g_server.on("/portal.css", HTTP_GET, handle_portal_css);
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
  g_routes_ok = true;
}

void radio_off_settle(void) {
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(200);
}

void set_ip_from(const IPAddress& ip) {
  snprintf(g_ip, sizeof(g_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

}  // namespace

bool softap_start(void) {
  if (g_active) return true;
  g_stop_req = false;
  g_ota_ok = false;
  g_ota_active = false;
  delay(100);
  radio_off_settle();
  {
    const uint32_t settle_until = millis() + 600;
    while ((int32_t)(millis() - settle_until) < 0) {
      delay(40);
      if (ESP.getMaxAllocHeap() >= 24000) break;
    }
  }
  WiFi.mode(WIFI_AP);
  delay(80);
  bool ok = WiFi.softAP(kSoftApSsid, nullptr, 1, 0, 1);
  if (!ok) {
    radio_off_settle();
    WiFi.mode(WIFI_AP);
    delay(80);
    ok = WiFi.softAP(kSoftApSsid, nullptr, 1, 0, 1);
  }
  if (!ok) {
    Serial.println("SoftAP start FAILED");
    WiFi.mode(WIFI_OFF);
    g_active = false;
    return false;
  }
  delay(200);
  set_ip_from(WiFi.softAPIP());
  Serial.printf("SoftAP %s http://%s\n", kSoftApSsid, g_ip);
  register_routes();
  g_server.begin();
  g_active = true;
  return true;
}

void softap_stop(void) {
  if (!g_active) return;
  g_server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  g_active = false;
  g_stop_req = false;
  g_ip[0] = '\0';
  Serial.println("Portal stopped");
  delay(250);
}

void softap_loop(void) {
  if (!g_active) return;
  g_server.handleClient();
}

bool softap_active(void) { return g_active; }
const char* softap_ip(void) { return g_ip; }
bool softap_stop_requested(void) { return g_stop_req; }
void softap_clear_stop_request(void) { g_stop_req = false; }
bool softap_ota_active(void) { return g_ota_active; }

void softap_set_flash_message(const char* msg, bool ok) {
  snprintf(g_flash_msg, sizeof(g_flash_msg), "%s", msg ? msg : "");
  g_flash_ok = ok;
}
