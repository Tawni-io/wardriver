#pragma once

#include <WebServer.h>

/** Fullscreen SoftAP map (Leaflet CDN). Survey stays paused. */
void softap_send_map_page(WebServer& server);

/** JSON: {ok,state,lat,lon,sats,age_ms} — Rufous GPS only. */
void softap_handle_api_gps(WebServer& server);

/**
 * JSON array of last-hit-per-BSSID from saved Wigle log.
 * Query: type=all|wifi|ble (default all). Rows without lat/lon omitted.
 */
void softap_handle_api_points(WebServer& server);
