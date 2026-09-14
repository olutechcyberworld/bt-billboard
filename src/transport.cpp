#include "transport.h"
#include "config.h"
#include "display.h"
#include "protocol.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

static WebSocketsServer gWs(WS_PORT);

static const char* CUSTOM_PORTAL_HEAD =
  "<style>"
  "body{background:#0b0b0f;color:#eaeaea;font-family:sans-serif;}"
  "h1,h2{color:#4fd1c5;} button,input{border-radius:6px;}"
  "</style>";

// ─────────────────────────────────────────────────────────────────────────────
static void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      IPAddress ip = gWs.remoteIP(num);
      Serial.printf("[WS]   Client %u connected from %s\n",
                    num, ip.toString().c_str());
      // Always announce readiness — see the original rationale: the boot
      // marquee can easily outlast the app's connect timeout, and the socket
      // is fully live from webSocket.begin() onward.
      transport_reply(num, "R:\n");
      break;
    }
    case WStype_DISCONNECTED:
      Serial.printf("[WS]   Client %u disconnected\n", num);
      break;
    case WStype_TEXT: {
      // Fix C4: length-bounded String constructor so embedded NULs and any
      // trailing whitespace in T: payloads survive the read. No trim().
      String f((const char*)payload, length);
      if (f.length() > 0) protocol_dispatch(num, f);
      break;
    }
    default: break;
  }
}

void transport_reply(uint8_t clientNum, const char* frame) {
  gWs.sendTXT(clientNum, frame);
  Serial.printf("[TX→%u] %s", clientNum, frame);
}

void transport_broadcast(const char* frame) {
  gWs.broadcastTXT(frame);
  Serial.printf("[TX*] %s", frame);
}

// ─────────────────────────────────────────────────────────────────────────────
void transport_begin() {
  // Placeholder before the (briefly blocking) STA connect attempt.
  display_show_provisioning("Connecting to Wi-Fi...");

  WiFiManager wm;
  wm.setCustomHeadElement(CUSTOM_PORTAL_HEAD);
  wm.setHostname(DEVICE_HOSTNAME);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(10);

  bool wifiOK = wm.autoConnect(PROVISIONING_AP_NAME);

  if (!wifiOK) {
    Serial.println(F("[WiFi] Saved credentials unavailable — captive portal active"));
    char apMsg[96];
    snprintf(apMsg, sizeof(apMsg),
             "Connect to Wi-Fi \"%s\" then visit %s to set up",
             PROVISIONING_AP_NAME, WiFi.softAPIP().toString().c_str());
    display_show_provisioning(apMsg);

    unsigned long start = millis();
    const unsigned long TIMEOUT_MS = 180000UL;    // setConfigPortalTimeout is ignored
                                                  // in non-blocking mode
    while (!wifiOK) {
      wm.process();
      display_tick();                             // keep the marquee alive
      if (WiFi.status() == WL_CONNECTED) wifiOK = true;
      else if (millis() - start > TIMEOUT_MS) break;
    }
  }

  if (!wifiOK) {
    Serial.println(F("[WiFi] Provisioning failed — restarting"));
    delay(2000);
    ESP.restart();
  }

  Serial.printf("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // Disabling modem sleep removes the ~100-200 ms wake penalty that would
  // otherwise dominate the round-trip latency of every protocol frame.
  WiFi.setSleep(false);

  display_end_provisioning();

  if (MDNS.begin(DEVICE_HOSTNAME)) {
    Serial.printf("[mDNS] http://%s.local/\n", DEVICE_HOSTNAME);
  }

  gWs.begin();
  gWs.onEvent(onWsEvent);
  Serial.printf("[WS]   Listening on port %u\n", (unsigned)WS_PORT);

  ArduinoOTA.setHostname(DEVICE_HOSTNAME);
  ArduinoOTA.begin();
  Serial.println(F("[OTA]  Ready"));
}

void transport_loop() {
  gWs.loop();
  ArduinoOTA.handle();
}