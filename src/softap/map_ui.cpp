#include "softap/map_ui.h"

#include "diag.h"
#include "gps/gps.h"
#include "log/wigle_log.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

enum MapFilter : uint8_t { kFilterAll = 0, kFilterWifi, kFilterBle };

/** Last N geolocated hits from the saved log (ring). ~48 B Ã— 500 â‰ˆ 24 KB. */
constexpr uint16_t kMaxPoints = 500;
constexpr size_t kLineMax = 384;

// Off-stack: /api/points runs inside handleClient (deep call stack + WDT risk).
char g_map_line[kLineMax];
char g_map_fields[16][96];

struct MapPoint {
  char mac[18];
  char ssid[28];
  float lat;
  float lon;
  int16_t channel;
  int8_t rssi;
  uint8_t flags;  // bit0 used, bit1 ble
};

constexpr uint8_t kUsed = 1u;
constexpr uint8_t kBle = 2u;

void json_escape(const char* in, char* out, size_t out_len) {
  if (!out || out_len == 0) return;
  size_t o = 0;
  if (!in) {
    out[0] = '\0';
    return;
  }
  for (const char* p = in; *p && o + 2 < out_len; p++) {
    const char c = *p;
    if (c == '"' || c == '\\') {
      if (o + 3 >= out_len) break;
      out[o++] = '\\';
      out[o++] = c;
    } else if ((uint8_t)c < 0x20) {
      continue;
    } else {
      out[o++] = c;
    }
  }
  out[o] = '\0';
}

MapFilter parse_filter(WebServer& server) {
  if (!server.hasArg("type")) return kFilterAll;
  const String t = server.arg("type");
  if (t.equalsIgnoreCase("wifi")) return kFilterWifi;
  if (t.equalsIgnoreCase("ble")) return kFilterBle;
  return kFilterAll;
}

/** Split CSV line into up to max_fields; returns field count. */
int parse_csv_fields(const char* line, char fields[][96], int max_fields) {
  if (!line || max_fields <= 0) return 0;
  int fi = 0;
  size_t fo = 0;
  bool in_q = false;
  fields[0][0] = '\0';
  for (const char* p = line; *p; p++) {
    const char c = *p;
    if (c == '\r' || c == '\n') break;
    if (in_q) {
      if (c == '"') {
        if (p[1] == '"') {
          if (fo + 1 < 96) fields[fi][fo++] = '"';
          p++;
        } else {
          in_q = false;
        }
      } else if (fo + 1 < 96) {
        fields[fi][fo++] = c;
      }
    } else if (c == '"') {
      in_q = true;
    } else if (c == ',') {
      fields[fi][fo] = '\0';
      fi++;
      if (fi >= max_fields) return max_fields;
      fo = 0;
      fields[fi][0] = '\0';
    } else if (fo + 1 < 96) {
      fields[fi][fo++] = c;
    }
  }
  fields[fi][fo] = '\0';
  return fi + 1;
}

MapPoint* alloc_points(uint16_t n) {
  MapPoint* p =
      (MapPoint*)heap_caps_calloc(n, sizeof(MapPoint), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) {
    p = (MapPoint*)heap_caps_calloc(n, sizeof(MapPoint), MALLOC_CAP_SPIRAM);
  }
  if (!p) {
    p = (MapPoint*)calloc(n, sizeof(MapPoint));
  }
  return p;
}

void free_points(MapPoint* p) {
  if (!p) return;
  // calloc / heap_caps_calloc — free() is safe for both on this platform.
  free(p);
}

/** Push into a ring; keeps the chronologically last `cap` hits. */
void ring_push(MapPoint* ring, uint16_t cap, uint16_t& head, uint16_t& count,
               const MapPoint& in) {
  if (!ring || cap == 0 || !in.mac[0]) return;
  ring[head] = in;
  ring[head].flags |= kUsed;
  head = (uint16_t)((head + 1u) % cap);
  if (count < cap) count++;
}

void send_chunk(WebServer& server, const char* s) {
  if (s && s[0]) server.chunkWrite(s, strlen(s));
}

void send_flash(WebServer& server, PGM_P s) {
  if (!s) return;
  char buf[192];
  const size_t len = strlen_P(s);
  size_t fed = 0;
  for (size_t off = 0; off < len;) {
    size_t n = len - off;
    if (n > sizeof(buf)) n = sizeof(buf);
    memcpy_P(buf, s + off, n);
    server.chunkWrite(buf, n);
    off += n;
    fed += n;
    if (fed >= 512) {
      fed = 0;
      delay(0);
      yield();
    }
  }
}

bool read_line_file(File& f, char* buf, size_t buf_len) {
  if (!buf || buf_len < 2 || !f.available()) return false;
  size_t n = f.readBytesUntil('\n', buf, buf_len - 1);
  buf[n] = '\0';
  while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n')) {
    buf[--n] = '\0';
  }
  return true;
}

}  // namespace

void softap_handle_api_gps(WebServer& server) {
  GpsFix fix = {};
  gps_get(&fix);
  const char* state = "quiet";
  if (fix.state == kGpsFix) state = "fix";
  else if (fix.state == kGpsSearching) state = "searching";

  char body[192];
  if (fix.state == kGpsFix && fix.have_latlon) {
    const uint32_t age =
        fix.last_fix_ms ? (uint32_t)(millis() - fix.last_fix_ms) : 0u;
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"state\":\"%s\",\"lat\":%.7f,\"lon\":%.7f,\"sats\":%u,\"age_ms\":%u}",
             state, fix.lat_deg, fix.lon_deg, (unsigned)fix.sats, (unsigned)age);
  } else {
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"state\":\"%s\",\"lat\":null,\"lon\":null,\"sats\":%u,\"age_ms\":0}",
             state, (unsigned)fix.sats);
  }
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.send(200, "application/json", body);
}

void softap_handle_api_points(WebServer& server) {
  const MapFilter filter = parse_filter(server);
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.chunkResponseBegin("application/json");
  send_chunk(server, "[");

  Serial.printf("Map points: start heap=%u psram=%u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());
  diag_wdt_feed();

  MapPoint* ring = alloc_points(kMaxPoints);
  uint16_t ring_head = 0;
  uint16_t ring_count = 0;
  uint32_t parsed = 0;
  uint32_t kept = 0;
  uint32_t skipped = 0;

  File f = LittleFS.open(wigle_path(), "r");
  if (!ring) {
    Serial.println("Map points: ring alloc FAILED");
  }
  if (f && ring) {
    // Skip Wigle meta + header
    (void)read_line_file(f, g_map_line, sizeof(g_map_line));
    (void)read_line_file(f, g_map_line, sizeof(g_map_line));

    while (read_line_file(f, g_map_line, sizeof(g_map_line))) {
      if (g_map_line[0] == '\0') continue;
      const int n = parse_csv_fields(g_map_line, g_map_fields, 16);
      parsed++;

      // Feed WDT: this handler blocks loop() — 12s panic WDT will reboot otherwise.
      if ((parsed & 15u) == 0u) {
        diag_wdt_feed();
        yield();
      }

      if (n < 9) {
        skipped++;
        continue;
      }
      const char* type = g_map_fields[n - 1];
      const bool is_ble = (strcasecmp(type, "BLE") == 0);
      const bool is_wifi = (strcasecmp(type, "WIFI") == 0);
      if (!is_ble && !is_wifi) {
        skipped++;
        continue;
      }
      if (filter == kFilterWifi && !is_wifi) continue;
      if (filter == kFilterBle && !is_ble) continue;

      const char* lat_s = (n > 8) ? g_map_fields[7] : "";
      const char* lon_s = (n > 8) ? g_map_fields[8] : "";
      if (!lat_s[0] || !lon_s[0]) {
        skipped++;
        continue;
      }
      char* end_lat = nullptr;
      char* end_lon = nullptr;
      const float lat = strtof(lat_s, &end_lat);
      const float lon = strtof(lon_s, &end_lon);
      if (end_lat == lat_s || end_lon == lon_s) {
        skipped++;
        continue;
      }
      if (!isfinite(lat) || !isfinite(lon)) {
        skipped++;
        continue;
      }
      if (lat == 0.0f && lon == 0.0f) {
        skipped++;
        continue;
      }

      MapPoint pt = {};
      snprintf(pt.mac, sizeof(pt.mac), "%s", g_map_fields[0]);
      snprintf(pt.ssid, sizeof(pt.ssid), "%s", g_map_fields[1]);
      pt.channel = (n > 4) ? (int16_t)atoi(g_map_fields[4]) : 0;
      pt.rssi = (n > 6) ? (int8_t)atoi(g_map_fields[6]) : 0;
      pt.lat = lat;
      pt.lon = lon;
      pt.flags = (uint8_t)(kUsed | (is_ble ? kBle : 0));
      ring_push(ring, kMaxPoints, ring_head, ring_count, pt);
      kept++;
    }
    f.close();
  } else if (f) {
    f.close();
  }

  Serial.printf("Map points: parsed=%u kept=%u ring=%u skipped=%u heap=%u table=%s\n",
                (unsigned)parsed, (unsigned)kept, (unsigned)ring_count, (unsigned)skipped,
                (unsigned)ESP.getFreeHeap(), ring ? "ok" : "null");
  diag_wdt_feed();

  bool first = true;
  char esc_ssid[64];
  char esc_mac[40];
  char out[220];
  uint32_t emitted = 0;
  if (ring && ring_count > 0) {
    // Oldest → newest so the browser gets chronological last-N.
    const uint16_t start =
        (uint16_t)((ring_head + kMaxPoints - ring_count) % kMaxPoints);
    for (uint16_t i = 0; i < ring_count; i++) {
      const MapPoint& p = ring[(uint16_t)((start + i) % kMaxPoints)];
      const bool ble = (p.flags & kBle) != 0;
      json_escape(p.ssid, esc_ssid, sizeof(esc_ssid));
      json_escape(p.mac, esc_mac, sizeof(esc_mac));
      snprintf(out, sizeof(out),
               "%s{\"mac\":\"%s\",\"ssid\":\"%s\",\"ch\":%d,\"rssi\":%d,"
               "\"lat\":%.7f,\"lon\":%.7f,\"ble\":%s}",
               first ? "" : ",", esc_mac, esc_ssid, (int)p.channel, (int)p.rssi, p.lat,
               p.lon, ble ? "true" : "false");
      send_chunk(server, out);
      first = false;
      emitted++;
      if ((emitted & 7u) == 0u) {
        diag_wdt_feed();
        yield();
      }
    }
  }
  if (ring) free_points(ring);

  send_chunk(server, "]");
  server.chunkResponseEnd();
  Serial.printf("Map points emitted=%u\n", (unsigned)emitted);
  diag_wdt_feed();
}

void softap_send_map_page(WebServer& server) {
  server.chunkResponseBegin("text/html");
  send_flash(server, PSTR(
      "<!DOCTYPE html><html data-theme=tawni class=map-page><head><meta charset=utf-8>"
      "<meta name=viewport content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
      "<meta name=apple-mobile-web-app-capable content=yes>"
      "<title>Wardriver Map</title>"
      "<link rel=stylesheet href=/portal.css?v=2>"
      "<link rel=stylesheet href=\"https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/leaflet.min.css\">"
      "<link rel=stylesheet href=\"https://cdnjs.cloudflare.com/ajax/libs/leaflet.markercluster/1.5.3/MarkerCluster.css\">"
      "<link rel=stylesheet href=\"https://cdnjs.cloudflare.com/ajax/libs/leaflet.markercluster/1.5.3/MarkerCluster.Default.css\">"
      "</head><body class=map-page>"
      "<div class=map-chrome id=bar>"
      "<a class=\"btn btn-primary btn-sm\" href=/>Setup</a>"
      "<button type=button class=\"btn btn-ghost btn-sm\" id=fAll>All</button>"
      "<button type=button class=\"btn btn-ghost btn-sm\" id=fWifi>Wi-Fi</button>"
      "<button type=button class=\"btn btn-ghost btn-sm\" id=fBle>BLE</button>"
      "<button type=button class=\"btn btn-ghost btn-sm\" id=recenter>My GPS</button>"
      "<span class=\"text-xs opacity-70\" id=status>Loading…</span>"
      "</div>"
      "<div id=map></div>"
      "<div class=map-load id=load>"
      "<span class=\"loading loading-spinner loading-lg text-primary\"></span>"
      "<div class=font-semibold id=loadMsg>Loading map…</div></div>"
      "<div class=map-banner id=banner style=display:none>"
      "Map tiles/scripts need phone mobile data while on the setup hotspot. "
      "Saved log still loads from the device.</div>"
      "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/leaflet.min.js\"></script>"
      "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/leaflet.markercluster/1.5.3/leaflet.markercluster.js\"></script>"
      "<script>"
      "(function(){"
      "var banner=document.getElementById('banner');"
      "var statusEl=document.getElementById('status');"
      "var loadEl=document.getElementById('load');"
      "var loadMsg=document.getElementById('loadMsg');"
      "function setLoading(on,msg){"
      "if(on){loadEl.classList.remove('hide');if(msg)loadMsg.textContent=msg;"
      "statusEl.className='text-xs text-primary';statusEl.textContent=msg||'Loading…';}"
      "else{loadEl.classList.add('hide');statusEl.className='text-xs opacity-70';}"
      "}"
      "if(!window.L||!L.map){banner.style.display='grid';setLoading(false);"
      "statusEl.textContent='CDN blocked';return;}"
      "var filter='all';"
      "var map=L.map('map',{zoomControl:false,attributionControl:true}).setView([0,0],2);"
      "L.control.zoom({position:'bottomleft'}).addTo(map);"
      "var tiles=L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{"
      "maxZoom:19,attribution:'&copy; <a href=https://www.openstreetmap.org/copyright>OpenStreetMap</a>'"
      "});"
      "tiles.on('tileerror',function(){banner.style.display='grid';});"
      "tiles.addTo(map);"
      "var cluster=L.markerClusterGroup({showCoverageOnHover:false,maxClusterRadius:48});"
      "map.addLayer(cluster);"
      "var me=null;"
      "function setFilterBtns(){"
      "[['fAll','all'],['fWifi','wifi'],['fBle','ble']].forEach(function(p){"
      "var b=document.getElementById(p[0]);"
      "if(!b)return;"
      "b.className='btn btn-sm '+(filter===p[1]?'btn-primary':'btn-ghost');"
      "});}"
      "function rssiOpacity(r){"
      "var x=Math.max(-90,Math.min(-30,r||-80));"
      "return 0.35+((-30-x)/60)*0.55;"
      "}"
      "function loadPoints(){"
      "setLoading(true,'Loading last 500 points…');"
      "fetch('/api/points?type='+filter).then(function(r){"
      "if(!r.ok)throw new Error('HTTP '+r.status);"
      "return r.json();"
      "}).then(function(pts){"
      "if(!Array.isArray(pts))throw new Error('bad json');"
      "loadMsg.textContent='Drawing markers…';"
      "cluster.clearLayers();"
      "var bounds=[];"
      "pts.forEach(function(p){"
      "if(typeof p.lat!=='number'||typeof p.lon!=='number')return;"
      "var color=p.ble?'#7C9CFF':'#3DDC97';"
      "var m=L.circleMarker([p.lat,p.lon],{"
      "radius:7,color:'#0B0F14',weight:1,fillColor:color,fillOpacity:rssiOpacity(p.rssi)"
      "});"
      "var title=p.ssid||'(hidden)';"
      "var html='<b>'+title+'</b><br>'+(p.ble?'BLE':'Wi-Fi')+' · '+p.mac+"
      "'<br>RSSI '+p.rssi+(p.ble?'':' · ch '+p.ch);"
      "m.bindPopup(html);cluster.addLayer(m);bounds.push([p.lat,p.lon]);"
      "});"
      "setLoading(false);"
      "statusEl.textContent=pts.length+' recent (saved log)';"
      "if(bounds.length){map.fitBounds(bounds,{padding:[48,48],maxZoom:17});}"
      "else if(me){map.setView(me.getLatLng(),15);}"
      "}).catch(function(e){setLoading(false);statusEl.textContent='Points failed';"
      "console&&console.log(e);});}"
      "function pollGps(){"
      "fetch('/api/gps').then(function(r){return r.json();}).then(function(g){"
      "if(g.state!=='fix'||g.lat==null){"
      "if(me){map.removeLayer(me);me=null;}"
      "return;"
      "}"
      "var ll=[g.lat,g.lon];"
      "if(!me){"
      "me=L.circleMarker(ll,{radius:9,color:'#fff',weight:2,fillColor:'#F05152',fillOpacity:0.95});"
      "me.bindPopup('You (Rufous GPS)');me.addTo(map);"
      "}else{me.setLatLng(ll);}"
      "}).catch(function(){});"
      "}"
      "document.getElementById('fAll').onclick=function(){filter='all';setFilterBtns();loadPoints();};"
      "document.getElementById('fWifi').onclick=function(){filter='wifi';setFilterBtns();loadPoints();};"
      "document.getElementById('fBle').onclick=function(){filter='ble';setFilterBtns();loadPoints();};"
      "document.getElementById('recenter').onclick=function(){"
      "if(me){map.setView(me.getLatLng(),Math.max(map.getZoom(),16));}"
      "else{statusEl.textContent='No GPS fix yet';}"
      "};"
      "setFilterBtns();loadPoints();pollGps();setInterval(pollGps,2000);"
      "})();"
      "</script></body></html>"));
  server.chunkResponseEnd();
}
