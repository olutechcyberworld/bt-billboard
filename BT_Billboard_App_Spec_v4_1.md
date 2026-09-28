# APP IMPLEMENTATION SPECIFICATION
## Wi-Fi-Controlled Scrolling Text Billboard — Android Application

**Document Version:** 4.3 (Updated — a third font, Bold 5x7, added to the device's font registry)
**Document Type:** Agent Implementation Brief
**Prepared By:** Olutech Cyberworld Engineering Team
**Target Platform:** Android (API 21+)
**Language:** Kotlin | **IDE:** Android Studio

This document is self-contained. The implementing agent must not request additional information.

---

## 0. Migration Notes

### v4.2 → v4.3

`Font.entries` gains a third entry, `BOLD_5X7(2, "Bold 5x7")`, and `Constants.FONT_COUNT` moves from 2 to 3. No wire-format change — `K:` already carried an arbitrary index — this is exactly the kind of update the note under 4.2.1 said to expect over time, not a one-off exception to it.

### v4.1 → v4.2

One change, found while bringing up real hardware rather than during a planned feature addition: the device previously replied `T:OK\n` to every well-formed `S:` time-sync frame unconditionally, even when it had no RTC available to actually store the time against (so the sync silently did nothing, with no way for the app to tell). The firmware now checks whether the write actually succeeded and replies `T:ERR\n` when it didn't. This is a breaking change to `UpstreamFrame`'s shape for time sync specifically — `TimeSyncOk` (a bare object) is replaced by `TimeSyncAck(success: Boolean)` — since v4.0/4.1 gave the app no way to represent a failed sync at all. See 5.3 and 6.4.1.

### v4.0 → v4.1

This revision is additive only; nothing described in v4.0 changes format or meaning. Four things are new:

1. **Font selection** (`K:` frame, Section 5.2/5.3, UI in 6.4.5). The device now supports more than one bitmap font for all displayed content (messages, clock, and date alike — the firmware applies one font selection across all three, there is no separate per-content-type font).
2. **Device Info** (`I:` frame). Returns the unit's MAC address, mDNS hostname, and firmware version over the already-open WebSocket connection. Primarily intended for a read-only info row in Settings and for disambiguating between multiple discovered devices.
3. **Scheduler sub-protocol** (`W:` / `L:` frames, Section 5.5). This has existed in the firmware since prior to v4.0 but was never documented in this specification; it is fully specified now. It covers recurring, day-of-week-scoped alerts that can push a message, sound the buzzer, or both, independent of anything the app sends in the moment.
4. **mDNS discovery gap closed.** v4.0 Section 5.1 carried a dependency note stating that `NsdManager`-based scanning would find nothing because the firmware only resolved its own hostname (`MDNS.begin()`) without advertising a discoverable service (`MDNS.addService()`). The firmware now advertises a `_ws._tcp` service record with MAC/ID/firmware-version TXT fields, so the "Scan for device" flow in Section 6.2 should now reliably find units on the network. See Section 5.1 for the details that changed as a result (hostname is no longer a single fixed value).

### v3.0 → v4.0

This revision superseded v3.0 in its entirety for anything transport-related. The hardware controller changed from an ESP32 DevKit to an ESP32-C3 Super Mini, whose radio does not implement Bluetooth Classic (BR/EDR); consequently the app no longer uses Bluetooth at all. All communication now occurs over a WebSocket connection carried on the same Wi-Fi network as the device, which the app joins as an ordinary client rather than pairing with a Bluetooth peripheral.

Initial Wi-Fi credential provisioning for the device itself (connecting the ESP32-C3 to the home or venue network for the first time) is handled entirely by the device's own captive portal at first boot and is outside the scope of this app. The app's only responsibility is to reach the device once it already has an IP address on the network, either by resolving its advertised hostname or by the user entering an address manually.

The frame protocol was otherwise additive, not replaced: every existing `T:`, `C:`, `S:`, `P:`, `B:`, `N:` frame and their upstream acknowledgements were unchanged in format and meaning. Two new frame types, `M:` (display mode) and `F:` (date format), were added, along with a Dashboard-level control surface for them, since the firmware no longer cycles between showing a message and showing the time automatically.

NOTE: All design decisions herein are final and locked. The implementing agent must not deviate from, reinterpret, or seek clarification on any specification in this document.

---

## 1. Project Overview

This document specifies the Android companion application for the Wi-Fi-Controlled Scrolling Text Billboard. The hardware is an ESP32-C3 Super Mini driving a single FC-16 4-unit MAX7219 LED dot matrix display (32x8 pixels) and a DS3231 RTC module. Communication occurs exclusively over a WebSocket connection on the local Wi-Fi network, replacing the Bluetooth Classic SPP transport used in prior versions.

Version 4.0 extended the app with two capabilities that did not exist under the old automatic message/clock cycling: an explicit Display Mode selector (Message, Clock, or Clock & Date) and, when Clock & Date mode is active, a Date Format selector (a scrolling named-weekday style, or one of three static numeric day/month orderings). Both are transmitted to the device as new frame types and both are persisted and retransmitted on every reconnect, following the same pattern already established for scroll speed, brightness, and animation preset.

Version 4.1 adds three more: a Font selector (Section 6.4.5) covering every displayed content type at once; a read-only Device Info panel (Section 6.4.6) surfacing MAC address, hostname, and firmware version; and a Schedules screen (Section 6.5) exposing the device's recurring-alert subsystem, which existed in firmware before this revision but had no app-side specification until now.

The animation system (29 presets, indices 0–28, including the two composite effects) is unchanged since v3.0 and is not affected by any of this.

---

## 2. Technical Stack and Dependencies

### 2.1 Language and IDE
Kotlin targeting Android Studio. Minimum SDK: API 21. Target SDK: latest stable at build time.

### 2.2 Required Dependencies

| Dependency | Purpose | Note |
|---|---|---|
| `com.squareup.okhttp3:okhttp` | WebSocket client | Replaces the system Bluetooth API; OkHttp's native `WebSocket`/`WebSocketListener` is used directly, no wrapper library needed |
| `android.net.nsd` (system API) | Optional service discovery on the local network | No third-party library required; used only for the convenience "scan for device" flow, not the primary connection path |
| `androidx.lifecycle:lifecycle-viewmodel-ktx` | ViewModel coroutine scope | Jetpack standard |
| `androidx.lifecycle:lifecycle-livedata-ktx` | Reactive UI state | Jetpack standard |
| `androidx.navigation:navigation-fragment-ktx` | Screen navigation | Jetpack standard |
| `androidx.datastore:datastore-preferences` | Settings persistence | Replaces SharedPreferences |
| `kotlinx-coroutines-android` | Async socket I/O | Required for background thread work |

### 2.3 Architecture Pattern
MVVM. Zero business logic in Fragments. No Context or View references in ViewModels. WebSocket I/O runs exclusively on `Dispatchers.IO`.

---

## 3. Android Permissions

The transport change removes every Bluetooth-related permission and, with it, the entire runtime permission-request flow that previously gated the Splash screen. A WebSocket connection to a known host on the local network requires only normal (install-time) permissions; none of the permissions below prompt the user at runtime.

| Permission | Runtime? | Purpose |
|---|---|---|
| `INTERNET` | No | Required for any socket I/O, including local-network WebSocket connections |
| `ACCESS_NETWORK_STATE` | No | Detect Wi-Fi connectivity state before attempting to connect |
| `ACCESS_WIFI_STATE` | No | Read current Wi-Fi connection info, used to warn the user if the phone is not on a Wi-Fi network at all |
| `CHANGE_WIFI_MULTICAST_STATE` | No | Required for reliable mDNS/NSD service resolution on some devices; only exercised by the optional discovery flow |

RULE: The Splash screen's blocking-condition check is now Wi-Fi state, not Bluetooth state: verify the device has an active Wi-Fi connection before proceeding, since a WebSocket cannot reach the billboard over mobile data alone unless the device is separately port-forwarded, which is out of scope. No runtime permission dialog is required anywhere in this app.

---

## 4. Application Architecture

### 4.1 Package Structure

```
com.olutech.btbillboard
 ├── ui/ (splash, connection, dashboard, settings)
 ├── network/ (WebSocketManager, WebSocketRepository, FrameParser, DeviceDiscovery)
 ├── model/ (ConnectionState, QueueState, DisplayMode, DateFormat, Frame, UpstreamFrame)
 ├── persistence/ (SettingsDataStore)
 └── MainActivity.kt
```

`DeviceDiscovery` wraps `NsdManager` for the optional "scan for device" convenience flow described in Section 6.2. It is not on the critical path for connecting to a device whose address the user already knows or has used before.

### 4.2 model/DisplayMode.kt and model/DateFormat.kt

```kotlin
enum class DisplayMode(val value: Int) {
    MESSAGE(0),
    CLOCK(1),
    CLOCK_AND_DATE(2)
}

enum class DateFormat(val value: Int) {
    NAMED(0),        // "Mon, Sep 10  14:32" — scrolls
    DD_MM(1),        // "10-09" — static
    DD_SLASH_MM(2),  // "10/09" — static
    MM_DD(3)         // "09-10" — static
}
```

### 4.2.1 model/Font.kt (new in v4.1)

```kotlin
enum class Font(val value: Int, val label: String) {
    DEFAULT(0, "Default"),
    CLASSIC_5X7(1, "Classic 5x7"),
    BOLD_5X7(2, "Bold 5x7")
}
```

RULE: `Font.entries` must be declared in exactly this order and must be kept in sync with the firmware's font registry. Unlike the Animation Spinner (Section 6.4.4), which is a closed, versioned set unlikely to grow, this list is expected to grow as more fonts are added to the device firmware over time — treat `Constants.FONT_COUNT` (Section 9) as something that changes across firmware revisions and confirm it against the currently-deployed firmware's changelog before a release, not something to assume is permanently 2.

### 4.2.2 model/Schedule.kt and model/BuzzPattern.kt and model/AlarmAction.kt (new in v4.1)

```kotlin
enum class AlarmAction(val value: Int) {
    DISPLAY_ONLY(0),
    BUZZ_ONLY(1),
    DISPLAY_AND_BUZZ(2)
}

enum class BuzzPattern(val value: Int, val label: String) {
    SHORT(0, "Short beep"),
    DOUBLE(1, "Double beep"),
    TRIPLE(2, "Triple beep"),
    LONG(3, "Long tone"),
    CONTINUOUS(4, "Continuous")
}

data class Schedule(
    val index: Int,               // 0-15, slot identity — not a sort key
    val enabled: Boolean,
    val hour: Int,                 // 0-23
    val minute: Int,               // 0-59
    val days: Set<DayOfWeekBit>,   // see below — bit0=Sunday .. bit6=Saturday
    val action: AlarmAction,
    val buzzPattern: BuzzPattern,  // meaningful only when action includes buzz
    val durationSec: Int,          // 0-255; 0 means the device's own default (30s)
    val message: String            // max 47 characters — see MAX_SCHED_MSG_LENGTH
)

enum class DayOfWeekBit(val bitIndex: Int, val label: String) {
    SUNDAY(0, "Sun"), MONDAY(1, "Mon"), TUESDAY(2, "Tue"), WEDNESDAY(3, "Wed"),
    THURSDAY(4, "Thu"), FRIDAY(5, "Fri"), SATURDAY(6, "Sat")
}
```

NOTE: `Schedule` is not persisted in app-local DataStore. The device's NVS storage is the sole source of truth for schedules (they survive app reinstall and reconnecting from a different phone); the app fetches the current list via `L:` on demand (see 5.5) rather than caching it as a setting.

### 4.3 FrameParser — All Downstream Encoders

```kotlin
object FrameParser {
    fun encodeText(msg: String): String = "T:$msg\n"
    fun encodeClear(): String = "C:\n"
    fun encodeTimeSync(dt: String): String = "S:$dt\n"
    fun encodeSpeed(pct: Int): String = "P:$pct\n"
    fun encodeBrightness(pct: Int): String = "B:$pct\n"
    fun encodeAnimation(idx: Int): String = "N:$idx\n"          // 0-28
    fun encodeMode(mode: DisplayMode): String = "M:${mode.value}\n"       // 0-2
    fun encodeDateFormat(fmt: DateFormat): String = "F:${fmt.value}\n"    // 0-3
    fun encodeFont(font: Font): String = "K:${font.value}\n"                       // new v4.1
    fun encodeDeviceInfoQuery(): String = "I:\n"                                   // new v4.1
    fun encodeScheduleListQuery(): String = "L:\n"                                 // new v4.1
    fun encodeScheduleDelete(index: Int): String = "W:$index,-\n"                  // new v4.1
    fun encodeScheduleClearAll(): String = "W:!\n"                                 // new v4.1
    fun encodeScheduleWrite(s: Schedule): String {                                 // new v4.1
        val days = (0..6).joinToString("") { bit ->
            if (s.days.any { it.bitIndex == bit }) "1" else "0"
        }
        val hhmm = String.format("%02d%02d", s.hour, s.minute)
        return "W:${s.index},${if (s.enabled) 1 else 0},$hhmm,$days," +
               "${s.action.value},${s.buzzPattern.value},${s.durationSec},${s.message}\n"
    }

    fun decode(raw: String): UpstreamFrame? = when {
        raw.startsWith("R:") -> UpstreamFrame.Ready
        raw.startsWith("A:0") -> UpstreamFrame.Ack(queueOccupied = false)
        raw.startsWith("A:1") -> UpstreamFrame.Ack(queueOccupied = true)
        raw.startsWith("D:") -> UpstreamFrame.ScrollComplete
        raw.startsWith("X:") -> UpstreamFrame.ClearConfirmed
        raw.startsWith("T:OK") -> UpstreamFrame.TimeSyncAck(success = true)         // updated v4.2
        raw.startsWith("T:ERR") -> UpstreamFrame.TimeSyncAck(success = false)       // new v4.2
        raw.startsWith("M:OK") -> UpstreamFrame.ModeAck
        raw.startsWith("F:OK") -> UpstreamFrame.FormatAck
        raw.startsWith("K:OK") -> UpstreamFrame.FontAck(success = true)            // new v4.1
        raw.startsWith("K:ERR") -> UpstreamFrame.FontAck(success = false)          // new v4.1
        raw.startsWith("W:OK") -> UpstreamFrame.ScheduleWriteAck(success = true)   // new v4.1
        raw.startsWith("W:ERR") -> UpstreamFrame.ScheduleWriteAck(success = false) // new v4.1
        raw.startsWith("L:END") -> UpstreamFrame.ScheduleListEnd                   // new v4.1 — check before "L:" row parsing
        raw.startsWith("L:") -> parseScheduleRow(raw)                              // new v4.1 — see 5.5
        raw.startsWith("I:") -> parseDeviceInfo(raw)                               // new v4.1 — see 5.5
        else -> null
    }
}
```

RULE: `L:END` must be matched before the general `L:` row case, since `"L:END".startsWith("L:")` is also true. `parseScheduleRow` and `parseDeviceInfo` are straightforward comma-split parsers over the formats defined in Section 5.5; treat any row that fails to split into the expected field count as `decode()` returning null for that line (ignored silently), consistent with the Section 7 "Unknown upstream frame" rule — a single malformed schedule row must not crash the list-fetch.

### 4.4 SettingsDataStore Keys

| Key | Type | Default | Description |
|---|---|---|---|
| `scroll_speed_pct` | Int | 50 | Speed slider position (0-100) |
| `brightness_pct` | Int | 25 | Brightness slider position (0-100) |
| `scroll_anim_idx` | Int | 0 | Animation preset index (0-28) |
| `display_mode_idx` | Int | 0 | Display mode (0=Message, 1=Clock, 2=Clock & Date) |
| `date_format_idx` | Int | 0 | Date format for Clock & Date mode (0-3) |
| `last_sync_timestamp` | String | "" | ISO datetime of last successful sync |
| `last_host` | String | "" | Last successfully connected hostname or IP address, offered as a default on the Connection Screen |
| `font_idx` | Int | 0 | Font selection (0-1 as of this firmware revision; see Section 4.2.1) — **new in v4.1** |

---

## 5. Communication Protocol

### 5.1 Transport

The app opens a WebSocket connection to `ws://<host>:81`, where `<host>` is either the device's mDNS hostname or an IP address entered or previously saved by the user, and `81` is the fixed port the firmware listens on. There is no discovery-and-pair step analogous to Bluetooth's paired-device list; see Section 6.2 for the supported ways of obtaining a host.

IMPORTANT CHANGE FROM v4.0 — hostname is no longer a single fixed value. Each unit now derives its own mDNS hostname and captive-portal AP name by appending a 6-hex-character suffix taken from its factory Wi-Fi MAC address, so that two billboards on the same network never collide on either name. `Constants.DEFAULT_HOSTNAME = "bt-billboard.local"` from the v4.0 Constants Reference (Section 9) is therefore **removed** — it will not resolve any actual device once more than the very first, single-suffix-less unit existed, and it never resolved a real device under this firmware revision at all. There is no fixed hostname to hardcode or prefill; see 6.2 for how the app is expected to obtain the real one.

The device's short ID (the same 6-hex-character suffix used in its hostname, e.g. `a1b2c3` for hostname `bt-billboard-a1b2c3.local`) is scrolled on the physical LED matrix for a few seconds every time the unit connects to Wi-Fi — on every boot, not only first-time provisioning, since the IP address itself can change between boots if the venue's router reassigns it via DHCP. This is the most reliable way for a user to identify and enter the correct address by hand, independent of whether mDNS/NSD resolves correctly on their particular phone, and is why manual host entry remains the primary, always-reliable connection path (unchanged from v4.0's guidance) even though the discovery issue below is now resolved.

RESOLVED FROM v4.0: `NsdManager`-based auto-discovery requires the firmware to advertise a discoverable service record (an `NsdManager`-visible `_ws._tcp` service), not merely resolve its own hostname. As of this firmware revision the device calls `MDNS.addService("ws", "tcp", 81)` and attaches `mac`, `id`, and `fw` TXT records to that service record. The "Scan for device" flow in Section 6.2 should now reliably surface units on the network; when `NsdServiceInfo.attributes` exposes TXT records on the API level in use, surface the `id` value next to each discovered result so the user can match it against the ID shown on the physical unit, particularly when more than one billboard is deployed on the same network. Absence of results is still not an error state (network conditions, AP client isolation, and OS-level mDNS quirks are all outside this app's control) and manual entry must remain available regardless.

### 5.2 Downstream Frames — App to Device

| Frame | Format | Trigger | Example |
|---|---|---|---|
| Text | `T:<message>\n` | User taps SEND | `T:Hello World\n` |
| Clear | `C:\n` | User taps CLEAR | `C:\n` |
| Time Sync | `S:YYYY-MM-DD HH:MM:SS\n` | Auto on connect or tap | `S:2024-11-15 14:32:00\n` |
| Speed | `P:<0-100>\n` | Slider change or on reconnect | `P:75\n` |
| Brightness | `B:<0-100>\n` | Slider change or on reconnect | `B:40\n` |
| Animation | `N:<0-28>\n` | Selector change or on reconnect | `N:27\n` |
| Display Mode | `M:<0-2>\n` | Dashboard mode selector change or on reconnect | `M:2\n` |
| Date Format | `F:<0-3>\n` | Date format selector change or on reconnect (only meaningful in Clock & Date mode, but always sent) | `F:0\n` |
| Font *(new v4.1)* | `K:<0-N-1>\n` | Font selector change or on reconnect | `K:1\n` |
| Device Info Query *(new v4.1)* | `I:\n` | On demand — e.g. opening a "Device Info" panel | `I:\n` |
| Schedule Write *(new v4.1, see 5.5)* | `W:<idx>,<enabled>,<hhmm>,<days>,<action>,<buzz>,<durationSec>,<message>\n` | Saving a schedule in the Schedules screen | `W:0,1,0730,1111100,2,0,30,Good morning\n` |
| Schedule Delete *(new v4.1, see 5.5)* | `W:<idx>,-\n` | Deleting one schedule slot | `W:0,-\n` |
| Schedule Clear All *(new v4.1, see 5.5)* | `W:!\n` | "Clear all schedules" action | `W:!\n` |
| Schedule List Query *(new v4.1, see 5.5)* | `L:\n` | Opening the Schedules screen, or pull-to-refresh on it | `L:\n` |

NOTE: Sending `T:` while the device is in Clock or Clock & Date mode causes the firmware to switch it back to Message mode unconditionally. The app must reflect this locally: on transmitting a `T:` frame, immediately set the local Display Mode state to `MESSAGE` regardless of what was previously selected, so the Dashboard's mode indicator does not drift out of sync with the device.

### 5.3 Upstream Frames — Device to App

| Frame | Meaning | App Action |
|---|---|---|
| `R:\n` | Device ready | Execute on-connect sequence then navigate to Dashboard |
| `A:0\n` | Accepted; queue empty | `QueueState = Empty`; clear EditText |
| `A:1\n` | Accepted/rejected; queue occupied | `QueueState = Occupied` |
| `D:\n` | Final pass complete (Message mode only; not emitted in Clock or Clock & Date mode) | If was Occupied: `QueueState = Empty` |
| `X:\n` | Clear confirmed | `QueueState = Empty`; show confirmation; set local Display Mode to `MESSAGE` |
| `T:OK\n` | Time sync applied | Update `last_sync_timestamp` in DataStore |
| `T:ERR\n` *(new v4.2)* | Time sync frame was well-formed but the device could not actually store it (no RTC detected/functioning) | Toast shown (see 7); `last_sync_timestamp` is **not** updated — it must keep showing the last time a sync genuinely succeeded, not this attempt |
| `M:OK\n` | Display mode change applied | No DataStore action beyond what was already persisted on selection (see 5.4) |
| `F:OK\n` | Date format change applied | No DataStore action beyond what was already persisted on selection |
| `K:OK\n` *(new v4.1)* | Font change applied | No DataStore action beyond what was already persisted on selection |
| `K:ERR\n` *(new v4.1)* | Font index out of range for this firmware's registry | Toast shown, matching the `M:OK`/`F:OK` pattern in Section 7; local selector value not reverted |
| `I:<mac>,<hostname>,<fw>\n` *(new v4.1)* | Device info response | Populate the Device Info panel (6.4.6). Not part of the on-connect sequence; only sent in response to an `I:` query |
| `W:OK\n` *(new v4.1)* | Schedule write/delete/clear-all applied | Refresh the Schedules screen's list (re-issue `L:` or update local state optimistically) |
| `W:ERR\n` *(new v4.1)* | Schedule write rejected — bad index, bad time, bad day mask, or an out-of-range action/buzz-pattern value (see 5.5 for exact bounds) | Toast shown; the schedule being edited is NOT saved, the editor stays open with the user's input intact |
| `L:<idx>,<enabled>,<hhmm>,<days>,<action>,<buzz>,<durationSec>,<message>\n` *(new v4.1)* | One schedule row; repeated once per configured slot | Append to the in-memory list being built for the Schedules screen |
| `L:END\n` *(new v4.1)* | End of schedule list | Render the accumulated list; if zero `L:` rows preceded this, render the screen's empty state |

### 5.4 On-Connect Sequence (triggered by `R:\n`)

The device does not persist Display Mode or Date Format across power loss or reboot; the app is the source of truth for both and must resend them on every connection, exactly as it already does for speed, brightness, and animation.

- Step 1: Read `scroll_speed_pct` → transmit `P:<value>\n`
- Step 2: Read `brightness_pct` → transmit `B:<value>\n`
- Step 3: Read `scroll_anim_idx` → transmit `N:<value>\n` (0-28)
- Step 4: Read `display_mode_idx` → transmit `M:<value>\n` (0-2)
- Step 5: Read `date_format_idx` → transmit `F:<value>\n` (0-3)
- Step 6: Read `font_idx` → transmit `K:<value>\n` (0 to `Constants.FONT_COUNT - 1`) — **new v4.1**
- Step 7: Phone datetime (`YYYY-MM-DD HH:MM:SS`) → transmit `S:<datetime>\n`
- Step 8: Navigate to Main Dashboard

NOTE: All seven frames must transmit before navigation, in the order listed. This is a stricter requirement than v3.0 only in that additional frames were inserted into the sequence over successive revisions; the ordering rationale (settings before time, time last) is unchanged. This was six frames as of v4.0; Step 6 (Font) is new in v4.1.

NOTE *(new v4.2)*: "Transmit" in Step 7 means send the frame and move on — navigation to the Dashboard must not wait for or depend on a `T:OK\n` actually coming back. A device with no RTC will reply `T:ERR\n` to this on-connect sync exactly as it would to a manual one (7), and that is not a reason to block the user from reaching the Dashboard; treat it the same as the manual Settings-screen case (Toast, DataStore untouched, otherwise continue normally).

NOTE: The schedule list (`L:`) and device info (`I:`) queries are deliberately NOT part of this sequence. Both are on-demand: `L:` is sent when the Schedules screen (6.5) is opened, and `I:` when a Device Info panel (6.4.6) is opened. Neither gates navigation to the Dashboard, since neither has any bearing on what the Dashboard itself displays.

---

### 5.5 Scheduler Sub-Protocol (new documentation, v4.1 — firmware behaviour predates this revision)

The device holds up to `Constants.MAX_SCHEDULES` (16) independent recurring-alert slots in its own non-volatile storage. Each slot fires at most once per matching minute, on whichever days of the week are selected, and can push a message to the display, sound the buzzer, or both — entirely independent of anything the app is doing at that moment.

### 5.5.1 Schedule Write — `W:<idx>,<enabled>,<hhmm>,<days>,<action>,<buzz>,<durationSec>,<message>\n`

| Field | Range | Notes |
|---|---|---|
| `idx` | 0-15 | Slot identity, not a priority or sort order |
| `enabled` | 0 or 1 | Disabled slots are stored but never fire |
| `hhmm` | `0000`-`2359` | Four digits, zero-padded, 24-hour, no separator (e.g. `0730` for 7:30 AM) |
| `days` | Exactly 7 characters of `0`/`1` | Position 0 = Sunday .. position 6 = Saturday, matching `DayOfWeekBit.bitIndex` in 4.2.2 |
| `action` | 0, 1, or 2 | `AlarmAction`: 0 = display only, 1 = buzz only, 2 = both |
| `buzz` | 0-4 | `BuzzPattern`; ignored device-side when `action == 0`, but still send a valid value (0) rather than omitting the field |
| `durationSec` | 0-255 | How long a display alarm stays up; `0` means the device's own default (30 s). Not meaningful when `action == 1` (buzz only), but still send a value |
| `message` | Up to 47 characters | Longer input must be truncated client-side before sending — the device buffer is `SCHED_MSG_LEN - 1` (47) usable characters, distinct from and much shorter than `Constants.MAX_MESSAGE_LENGTH` (256) used for ad hoc `T:` messages. Enforce a separate, shorter `InputFilter` in the Schedule editor |

All eight fields are required on every write; the device rejects (`W:ERR`) a write with fewer than 8 comma-separated fields. There is no partial-update form — to change one field of an existing schedule, resend the entire row with that field changed.

### 5.5.2 Schedule Delete — `W:<idx>,-\n`

Deletes a single slot. This is the one case where a short (2-field) `W:` payload is valid; the literal second field must be the character `-`.

### 5.5.3 Schedule Clear All — `W:!\n`

Deletes every configured schedule in one call. RULE: the Schedules screen must ask for confirmation before sending this — there is no undo, and it is not scoped to a single slot the way Delete is.

### 5.5.4 Schedule List Query — `L:\n` → zero or more `L:` rows, then `L:END\n`

The device replies with one `L:` row per slot that has ever been written (including disabled ones — the app must show disabled schedules in the list, not filter them out, so the user can re-enable them), in slot-index order, followed by a single `L:END\n` with no `L:` rows preceding it if no schedules are configured. There is no pagination; all 16 possible rows can arrive in a single burst and the client should buffer them until `L:END` before updating the visible list, rather than rendering incrementally per row.

### 5.5.5 Known limitation — no real-time "schedule fired" notification

There is currently no upstream frame emitted at the moment a schedule actually fires. If a schedule's action includes the buzzer (`action` 1 or 2), the app has no way to know it happened at all unless the user is physically near the device. If the action is display-only or display-and-buzz, the app likewise gets no signal when the resulting alarm screen appears or clears — `display_dismiss_alarm()` on the device can be triggered by the schedule's own duration timer expiring, with nothing broadcast to connected clients either way.

This is a gap in the current firmware, not an app-side omission — flagging it here rather than routing around it with speculative client-side polling. If real-time awareness of firing alerts becomes a requirement, it needs a new upstream broadcast frame added to the firmware (the display and scheduler modules already have a broadcast callback mechanism used for the `D:` frame, so the pattern to extend is established); treat this section as the specification to update at that point, not as something the app can work around on its own.

---

## 6. Screen Specifications

### 6.1 Splash Screen
Entry point. Checks Wi-Fi hardware availability and current Wi-Fi connection state in sequence, per Section 3. Navigates to Connection Screen only when the phone has an active Wi-Fi connection. No permission-request dialogs occur on this screen; there are no runtime permissions to request.

### 6.2 Connection Screen

Replaces the paired-Bluetooth-device list entirely, since there is no pairing step in this transport. The screen presents:

- A text field for host entry (hostname or IP address), pre-filled with `last_host` from DataStore if present, else empty.
- A CONNECT button, enabled whenever the field is non-empty.
- A "Scan for device" affordance that runs `NsdManager` discovery in the background and, if it finds a matching service, offers to fill the text field with the discovered address. Per the dependency note in Section 5.1, this may find nothing depending on the firmware's current mDNS configuration; its absence of results is not an error state and must not block manual entry.

On CONNECT, the app opens the WebSocket to `ws://<host>:81` and navigates to Dashboard only on `R:\n` receipt within `DEVICE_READY_TIMEOUT_MS` of socket open; otherwise it shows an error and returns to this screen. On successful connection, the entered host is written to `last_host`.

RULE: No pairing, Bluetooth adapter interaction, or device list of any kind appears anywhere in this app.

### 6.3 Main Dashboard Screen

#### 6.3.1 Status Bar
Connection state indicator and queue state badge, both updating in real time from ViewModel LiveData. Unchanged from v3.0 apart from the underlying connection type they reflect.

#### 6.3.2 Display Mode Selector
A three-way segmented control (Message / Clock / Clock & Date), placed above the message composition area, initialized from `display_mode_idx`. On change: persist to DataStore and transmit `M:` if connected. Selecting Clock & Date reveals the Date Format sub-selector described in 6.3.3; selecting either other mode hides it. This selector is the app's primary replacement for the firmware's old automatic message/clock cycling and must remain visible and reachable without navigating into Settings, since it is now a routine, frequent interaction rather than a one-time configuration choice.

#### 6.3.3 Date Format Selector (visible only when Display Mode = Clock & Date)
A four-item Spinner or radio group: Named (e.g. "Mon, Sep 10"), DD-MM, DD/MM, MM-DD, initialized from `date_format_idx`. On change: persist to DataStore and transmit `F:` if connected.

#### 6.3.4 Message Composition
Multiline EditText, character counter "X / 256 characters", 256-character InputFilter, SEND button below. Unchanged from v3.0.

#### 6.3.5 SEND Button Rules
- Disabled when disconnected, queue occupied, or EditText empty. Not otherwise gated by the current Display Mode selector value; sending is always allowed and, per Section 5.2's NOTE, always forces the mode indicator to Message.
- On tap: encode, transmit, set local Display Mode state to `MESSAGE` (updating the segmented control in 6.3.2 to reflect it), EditText stays populated until `A:0` or `A:1` arrives.
- On `A:0`: clear EditText, `QueueState = Empty`. On `A:1`: clear EditText, `QueueState = Occupied`.

#### 6.3.6 CLEAR Button
- Always enabled when connected. Local state resets, including the Display Mode selector reverting to Message, only on `X:\n` receipt.

#### 6.3.7 Disconnection
On WebSocket failure (OkHttp's `onFailure`/`onClosed` callbacks): `ConnectionState = Disconnected`, all dependent elements disable, RECONNECT button appears. App does not auto-navigate away.

### 6.4 Settings Screen
Four sections: Time Synchronisation, Scroll Speed, Brightness, Animation. All disabled when disconnected. Unchanged from v3.0; Display Mode and Date Format live on the Dashboard (6.3.2–6.3.3), not here, since they are treated as a frequent operational control rather than a configuration setting.

#### 6.4.1 Time Synchronisation
Live phone datetime display. "Last Synced:" label. SYNC NOW button transmits current datetime. On `T:OK\n`: update DataStore and label. On `T:ERR\n` *(new v4.2)*: Toast shown, label and DataStore left untouched — see Section 7.

#### 6.4.2 Scroll Speed
SeekBar 0-100. Label "Speed: X%". Init from `scroll_speed_pct`. On change: persist + transmit `P:` if connected.

#### 6.4.3 Brightness
SeekBar 0-100. Label "Brightness: X%". Init from `brightness_pct`. On change: persist + transmit `B:` if connected.

NOTE: MD_Parola intensity 0 does not turn the display off. The display remains visible at minimum brightness. This is expected behaviour, not an error.

#### 6.4.4 Animation Preset
A Spinner populated with 29 preset names below in exactly this order. The Spinner index equals the `N:` frame payload value. Init from `scroll_anim_idx`. On change: persist + transmit `N:` if connected. Unchanged from v3.0.

| Idx | Label | Idx | Label | Idx | Label |
|---|---|---|---|---|---|
| 0 | Scroll Left | 10 | Dissolve | 20 | Opening Cursor |
| 1 | Scroll Right | 11 | Mesh | 21 | Closing |
| 2 | Scroll Up | 12 | Slice | 22 | Closing Cursor |
| 3 | Scroll Down | 13 | Wipe | 23 | Grow Up |
| 4 | Scroll Up-Left | 14 | Wipe Cursor | 24 | Grow Down |
| 5 | Scroll Up-Right | 15 | Scan H | 25 | Instant |
| 6 | Scroll Dn-Left | 16 | Scan H Alt | 26 | Random |
| 7 | Scroll Dn-Right | 17 | Scan V | 27 | Scroll + Cursor |
| 8 | Fade | 18 | Scan V Alt | 28 | Scroll + Scan H |
| 9 | Blinds | 19 | Opening | | |

RULE: Declare all 29 names in a string-array resource in `strings.xml` in exactly the order shown. The array index is the `N:` frame value. Indices 27 and 28 are composite effects — label them identically to the other 27 with no visual distinction in the Spinner.

#### 6.4.5 Font Selector (new in v4.1)

A Spinner populated from `Font.entries` (4.2.1), in enum-declaration order — `Default`, `Classic 5x7` as of this firmware revision. Init from `font_idx`. On change: persist + transmit `K:` if connected. Applies to every displayed content type at once (messages, clock, and clock & date) — there is no independent per-content-type font selection, so this control belongs once in Settings, not duplicated per Display Mode.

On `K:ERR` (Section 5.3): Toast shown, matching the `M:OK`/`F:OK` pattern in Section 7; the local Spinner selection is not reverted, consistent with how mode/format ack failures are already handled.

#### 6.4.6 Device Info (new in v4.1)

A read-only panel showing the values from the most recent `I:` response: MAC address, hostname, firmware version. Populate by sending `I:\n` when this section of Settings becomes visible (not on every connect — see the on-connect NOTE in 5.4). Show a loading state until the response arrives; there is no timeout specified for this query beyond the general disconnection handling in 6.3.7, since a missing response is not disruptive to any other function.

### 6.5 Schedules Screen (new in v4.1)

A dedicated screen, reachable from the Dashboard or a navigation drawer, for managing the device's recurring alerts (Section 5.5). Not part of the Dashboard itself, unlike Display Mode — schedule editing is an infrequent configuration task, the opposite of the "routine, frequent interaction" framing given to the Display Mode selector in 6.3.2.

#### 6.5.1 List View

On screen open: transmit `L:\n`, show a loading state, and render the accumulated list once `L:END\n` arrives (5.5.4). Each row shows: enabled/disabled state, time (`HH:MM`, formatted per system locale is acceptable since this is a pure display, not a transmitted value), the day-of-week pattern as abbreviated labels (e.g. "Mon Wed Fri"), and a truncated preview of the message. Disabled schedules are shown, visually de-emphasized (e.g. reduced opacity), not hidden. Tapping a row opens the editor (6.5.2) pre-filled with that slot's values. A pull-to-refresh gesture re-issues `L:\n`.

An "Add" affordance is enabled only when at least one slot index (0-15) is unused by the currently-loaded list; if all 16 are in use, disable Add and show a message indicating the limit is reached — there is no way to have more than 16 concurrent schedules.

A "Clear All" affordance transmits `W:!\n` (5.5.3) after an explicit confirmation dialog.

#### 6.5.2 Schedule Editor

Presented as a full screen or bottom sheet, consistent with the app's existing navigation pattern. Fields, matching 5.5.1 exactly:

| Field | Control | Notes |
|---|---|---|
| Enabled | Switch | Defaults on for a new schedule |
| Time | Time picker | 24-hour internal representation regardless of the picker's display format |
| Days | 7 toggle chips (Sun-Sat) | At least one must be selected before Save is enabled — a schedule with an all-zero day mask can never fire and is very likely a user error, not a valid "do nothing" configuration |
| Action | 3-way segmented control | Display only / Buzz only / Both, matching `AlarmAction` |
| Buzz Pattern | Spinner | Populated from `BuzzPattern.entries` (4.2.2); disabled (not hidden) when Action = Display only, since the device still expects a valid value in the frame |
| Duration | SeekBar or numeric stepper, 0-255 seconds | Label "Default (30s)" at value 0, per the device's own fallback behaviour; disabled (not hidden) when Action = Buzz only |
| Message | Multiline EditText, 47-character InputFilter | Separate counter from the Dashboard's 256-character message composer (6.3.4) — do not reuse that InputFilter here |

On Save: transmit the `W:` write frame (5.5.1). On `W:OK`: close the editor and refresh the list (either re-issue `L:` or optimistically update the local list with the just-saved values). On `W:ERR`: keep the editor open with the user's input intact and show an error — per 5.5.1, this most often means a field is genuinely out of range, which client-side validation (the table above) should already have prevented, so treat a `W:ERR` reaching the app as worth logging as unexpected rather than a routine, expected outcome.

On Delete (from within the editor, or a swipe action on the list row): transmit `W:<idx>,-\n` (5.5.2). On `W:OK`: remove the row from the list.

---

## 7. Edge Cases

| Scenario | Required Behaviour |
|---|---|
| Wi-Fi disabled mid-session | WebSocket failure → Disconnected state → RECONNECT button. No crash. |
| Device unreachable (wrong network, powered off, out of range of the AP) | Same as above; connection attempt times out per `DEVICE_READY_TIMEOUT_MS` if no `R:` is received, or fails immediately if the socket cannot open. |
| SEND with empty EditText | Button disabled. Cannot occur if UI rules are implemented correctly. |
| SEND with queue occupied | Button disabled. Cannot occur if UI rules are implemented correctly. |
| User types command-like text | Wrapped as `T:` unconditionally. Device displays it as text. |
| No `R:\n` in 10 seconds | Drop socket, show error, return to Connection Screen. |
| `T:OK\n` not received (timeout) | Toast shown. DataStore not updated. |
| `T:ERR\n` received *(new v4.2)* | Distinct from the timeout case above: the device is reachable and responded, it just couldn't apply the sync (typically no RTC present/functioning). Toast shown with wording that reflects this — not generic connectivity language — since retrying immediately won't help if the device genuinely has no RTC. DataStore not updated. |
| `M:OK\n` / `F:OK\n` not received | Toast shown, matching the `T:OK` pattern; the local selector value is not reverted, since the frame was already persisted optimistically on selection. |
| App killed mid-scroll or mid-clock-display | Device runs autonomously in whatever mode was last set. App starts fresh next launch and resends all seven on-connect frames (5.4) once reconnected. |
| Unknown upstream frame | `decode()` returns null. Ignored silently. No crash. |
| DataStore read failure | Use defaults. Log error. No crash. |
| `B:` slider at 0% | Display stays on at min brightness. Not a bug. |
| `N:` index out of valid range (0-28) | **Correction from v4.0:** the firmware silently rejects the frame (logs it and applies nothing); it does not clamp, and sends no ack either way for `N:` regardless of validity. The app has no way to detect this happening, which is exactly why the RULE below still holds: App must not transmit out-of-range values via the Spinner. |
| `M:` index out of valid range (0-2) | **Correction from v4.0:** silently rejected, not clamped — no `M:OK` is sent for an invalid value, so a bug that let one through would be silently ignored device-side with no error surfaced. App must not transmit out-of-range values via the segmented control. |
| `F:` index out of valid range (0-3) | **Correction from v4.0:** silently rejected, not clamped, same as `M:` above. App must not transmit out-of-range values via the selector. |
| `K:` index out of valid range | Rejected with `K:ERR\n` (unlike `N:`/`M:`/`F:` above, the font handler does ack failures). See Edge Case row above for the resulting Toast behaviour. App must still not transmit out-of-range values via the Spinner regardless. |
| `W:` write rejected | `W:ERR\n` received — see 5.5.1 for the specific bounds that trigger this. Editor stays open, user input preserved, error Toast shown. |
| `I:\n` sent but no response | No timeout is specified for this query. If the Device Info panel is left in a loading state indefinitely, that is a symptom of disconnection and should already be caught by the general `ConnectionState` handling in 6.3.7, not a separate timeout. |
| Schedule list has zero entries | `L:END\n` arrives immediately with no `L:` rows preceding it. Not an error — render the Schedules screen's empty state. |
| NSD scan finds no service | Not an error. Manual host entry (using the ID shown on the device's own display — see 5.1) remains available and is the primary path. |

---

## 8. UI Design Constraints

- Portrait and landscape supported. No data loss on rotation.
- Status bar and queue badge update within one UI frame of ViewModel emission.
- All connection-dependent buttons evaluate enabled state reactively from `ConnectionState` LiveData.
- Character counter updates on every keystroke via TextWatcher. Never negative.
- Material Design 3 throughout.
- Brand colours: `#1A1A2E` background, `#00B4D8` accent, white text on dark surfaces.
- No technical terms (`IOException`, `WebSocket`, `TCP/IP`, `NsdManager`) in user-facing strings.
- All strings in `res/values/strings.xml`. Animation names in string-array in correct index order.
- Animation Spinner collapsed label always shows the currently selected preset name, composite or native, with no visual distinction between the two categories.
- The Display Mode segmented control (6.3.2) must be immediately visible on the Dashboard without scrolling, given how frequently it is expected to be used.

---

## 9. Constants Reference

```kotlin
object Constants {
    const val DEFAULT_WS_PORT = 81
    // DEFAULT_HOSTNAME removed in v4.1 — there is no longer a single fixed
    // hostname every device resolves to (see Section 5.1). Do not reintroduce
    // a hardcoded "bt-billboard.local"-style constant; it will not resolve
    // any real device.
    const val DEVICE_READY_TIMEOUT_MS = 10_000L
    const val MAX_MESSAGE_LENGTH = 256
    const val MAX_SCHEDULE_MESSAGE_LENGTH = 47          // new v4.1 — see 5.5.1, distinct from MAX_MESSAGE_LENGTH
    const val DEFAULT_SPEED_PCT = 50
    const val DEFAULT_BRIGHTNESS_PCT = 25
    const val DEFAULT_ANIM_IDX = 0
    const val ANIMATION_COUNT = 29
    const val DISPLAY_MODE_COUNT = 3
    const val DATE_FORMAT_COUNT = 4
    const val FONT_COUNT = 3                            // updated v4.3 — confirm against deployed firmware; see 4.2.1
    const val MAX_SCHEDULES = 16                        // new v4.1
    const val DATETIME_FORMAT = "yyyy-MM-dd HH:mm:ss"

    const val DS_KEY_SPEED = "scroll_speed_pct"
    const val DS_KEY_BRIGHTNESS = "brightness_pct"
    const val DS_KEY_ANIM_IDX = "scroll_anim_idx"
    const val DS_KEY_DISPLAY_MODE = "display_mode_idx"
    const val DS_KEY_DATE_FORMAT = "date_format_idx"
    const val DS_KEY_FONT_IDX = "font_idx"              // new v4.1
    const val DS_KEY_LAST_SYNC = "last_sync_timestamp"
    const val DS_KEY_LAST_HOST = "last_host"
}
```

---

## 10. Agent Deliverables Checklist

| # | Deliverable | Acceptance Criteria |
|---|---|---|
| 1 | Working APK on Android 5.0+ | Installs and launches without crash |
| 2 | Splash: Wi-Fi state check | Blocking condition (active Wi-Fi connection) enforced. No permission dialogs shown. |
| 3 | Connection Screen | Accepts manual host entry, offers optional NSD scan, connects by WebSocket, waits for `R:` frame. |
| 4 | Dashboard: mode + date format selectors | Segmented control and conditional Spinner match Section 6.3.2–6.3.3 exactly. |
| 5 | Dashboard: send + clear + status | Button states and queue badge match spec exactly, including mode-indicator sync on SEND/CLEAR. |
| 6 | Settings: time sync | `S:` frame sent on tap. `T:OK` updates DataStore and label. |
| 7 | Settings: scroll speed | `P:` frame sent on change. Persisted and retransmitted on reconnect. |
| 8 | Settings: brightness | `B:` frame sent on change. Persisted and retransmitted on reconnect. |
| 9 | Settings: animation (29 items) | `N:` frame 0-28 sent on change. Items in correct index order incl. 2 composite entries. |
| 10 | MVVM architecture | No logic in Fragments. No Context in ViewModels. |
| 11 | Background WebSocket listener | `Dispatchers.IO`. Main thread UI updates. No ANR. |
| 12 | FrameParser: 14 encoders | All fourteen `encodeX()` functions implemented correctly (8 from v4.0 plus `encodeFont`, `encodeDeviceInfoQuery`, `encodeScheduleListQuery`, `encodeScheduleDelete`, `encodeScheduleClearAll`, `encodeScheduleWrite`). |
| 13 | FrameParser: 16 decoders | All sixteen upstream frame cases decoded correctly (8 from v4.0 plus `K:OK`, `K:ERR`, `W:OK`, `W:ERR`, `L:END`, `L:` rows, `I:` from v4.1, plus `T:ERR` — new v4.2, replacing the old bare `TimeSyncOk` with `TimeSyncAck(success: Boolean)` — note `L:END` must be checked before the general `L:` case). |
| 14 | DataStore: 8 keys | All eight persisted across app kill and restored correctly, including `font_idx`. |
| 15 | On-connect: 7 frames in order | `P: B: N: M: F: K: S:` transmitted in order before dashboard navigation (5.4). |
| 16 | All edge cases handled | No crash on any scenario in Section 7. |
| 17 | Constants.kt complete | `ANIMATION_COUNT = 29`, `DISPLAY_MODE_COUNT = 3`, `DATE_FORMAT_COUNT = 4`, `FONT_COUNT = 3`, `MAX_SCHEDULES = 16`. No `DEFAULT_HOSTNAME` constant (removed, 5.1/9). No magic strings or numbers anywhere. |
| 18 | strings.xml complete | 29-item animation string-array in correct order. |
| 19 | Settings: Font selector | Spinner matches Section 6.4.5. `K:` sent on change, persisted, retransmitted on reconnect. `K:ERR` handled per Section 7. |
| 20 | Settings: Device Info panel | Matches Section 6.4.6. `I:` sent on panel open, response fields displayed correctly. |
| 21 | Schedules screen: list + editor | Matches Section 6.5 exactly, including the 47-character message limit being distinct from the Dashboard's 256-character one, the all-zero-day-mask guard, and the 16-schedule cap disabling Add. |
| 22 | Schedules screen: write/delete/clear-all | `W:` frames match 5.5.1-5.5.3 exactly, including the 8-field requirement for a full write and the 2-field exception for delete. |

NOTE: All 22 deliverables must be present and functional. Partial submissions are not accepted.
