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

static char gDeviceId[7]  = { '\0' };   // 6 hex chars, last 3 MAC octets
static char gHostname[32] = { '\0' };   // DEVICE_HOSTNAME_PREFIX + "-" + gDeviceId
static char gApName[48]   = { '\0' };   // PROVISIONING_AP_PREFIX + "-" + gDeviceId
static char gMacStr[18]   = { '\0' };   // "AA:BB:CC:DD:EE:FF"

const char* transport_device_id() { return gDeviceId; }
const char* transport_hostname()  { return gHostname; }
const char* transport_mac()       { return gMacStr;   }

// Every unit ships with the same compiled-in DEVICE_HOSTNAME_PREFIX /
// PROVISIONING_AP_PREFIX; without a per-device suffix, two billboards on the
// same network would fight over one mDNS hostname and present an identical
// captive-portal AP name during provisioning. The factory STA MAC is unique
// per chip and available before any Wi-Fi connection is established, so it
// is a reliable, zero-configuration source for that suffix.
static void derive_device_identity() {
  WiFi.mode(WIFI_STA);   // ensures the radio is initialised so the MAC reads correctly
  // Explicit max TX power rather than trusting the SDK/region default, which
  // on some builds is set conservatively low. This is a small, legitimate
  // mitigation for a marginal RF link -- it does not fix an antenna-matching
  // problem, but it's free and strictly helps rather than hurts.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(gDeviceId, sizeof(gDeviceId), "%02x%02x%02x", mac[3], mac[4], mac[5]);
  snprintf(gHostname, sizeof(gHostname), "%s-%s", DEVICE_HOSTNAME_PREFIX, gDeviceId);
  snprintf(gApName,   sizeof(gApName),   "%s-%s", PROVISIONING_AP_PREFIX, gDeviceId);
  snprintf(gMacStr,   sizeof(gMacStr),   "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

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
  derive_device_identity();
  Serial.printf("[ID]   Device ID %s (MAC %s)\n", gDeviceId, gMacStr);

  // Placeholder before the (briefly blocking) STA connect attempt.
  display_show_provisioning("Connecting to Wi-Fi...");

  WiFiManager wm;
  wm.setCustomHeadElement(CUSTOM_PORTAL_HEAD);
  wm.setHostname(gHostname);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(10);

  bool wifiOK = wm.autoConnect(gApName);

  if (!wifiOK) {
    Serial.println(F("[WiFi] Saved credentials unavailable — captive portal active"));
    char apMsg[112];
    snprintf(apMsg, sizeof(apMsg),
             "Connect to Wi-Fi \"%s\" then visit %s to set up",
             gApName, WiFi.softAPIP().toString().c_str());
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

  // Announce the address on the physical panel itself: mDNS/NSD resolution
  // is not reliable on every phone/OS combination, and the serial monitor
  // is usually closed by the time the device is mounted in its final
  // location. This is the one address-discovery path with no dependency on
  // network discovery working at all. Shown on every boot, not just first
  // provisioning, since DHCP can hand out a different address next time.
  char ipMsg[80];
  snprintf(ipMsg, sizeof(ipMsg), "IP %s   ID %s",
           WiFi.localIP().toString().c_str(), gDeviceId);
  display_show_provisioning(ipMsg);
  unsigned long ipShownAt = millis();
  const unsigned long IP_DISPLAY_MS = 6000UL;
  while (millis() - ipShownAt < IP_DISPLAY_MS) {
    display_tick();
  }

  display_end_provisioning();

  if (MDNS.begin(gHostname)) {
    // Makes the device visible to NsdManager-based scans (see the app spec's
    // Section 5.1 dependency note: hostname resolution alone is not enough,
    // Android's discovery API needs an actual advertised service record).
    //
    // MDNSResponder overloads addServiceTxt() for (char*), (const char*),
    // and (String) alike. A bare string literal argument matches all three
    // equally well (the literal->char* conversion is a legacy allowance, not
    // a worse match than literal->const char*), so the call is ambiguous
    // unless every argument is explicitly typed as const char* to rule the
    // other two overloads out.
    MDNS.addService((const char*)"ws", (const char*)"tcp", WS_PORT);
    MDNS.addServiceTxt((const char*)"ws", (const char*)"tcp", (const char*)"mac", (const char*)gMacStr);
    MDNS.addServiceTxt((const char*)"ws", (const char*)"tcp", (const char*)"id",  (const char*)gDeviceId);
    MDNS.addServiceTxt((const char*)"ws", (const char*)"tcp", (const char*)"fw",  (const char*)FW_VERSION);
    Serial.printf("[mDNS] http://%s.local/  (service ws._tcp advertised)\n", gHostname);
  }

  gWs.begin();
  gWs.onEvent(onWsEvent);
  Serial.printf("[WS]   Listening on port %u\n", (unsigned)WS_PORT);

  ArduinoOTA.setHostname(gHostname);
  ArduinoOTA.begin();
  Serial.println(F("[OTA]  Ready"));
}

void transport_loop() {
  gWs.loop();
  ArduinoOTA.handle();
}