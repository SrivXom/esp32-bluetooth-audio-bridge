#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include "BluetoothA2DPSource.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"

// --- PIN DEFINITIONS ---
#define SD_CS              5
#define BTN_PLAY_PAUSE     25
#define BTN_NEXT           34
#define BTN_PREV           35
#define BTN_VOL_DOWN       32
#define BTN_VOL_UP         33

// --- SETTINGS ---
const char* ap_ssid = "ESP_Audio_Bridge";
const char* ap_pass = "12345678";
const byte DNS_PORT = 53;

BluetoothA2DPSource a2dp_source;
WebServer server(80);
DNSServer dnsServer;
File audioFile;

// --- AUDIO PLAYLIST ---
const char* playlist[] = {"/Uttar Pradesh.wav", "/Rajasthan.wav", "/Bihar.wav", "/Sikkim.wav",
    "/Himachal Pradesh.wav", "/Punjab.wav", "/Uttarakhand.wav", "/Haryana.wav",
    "/Tripura.wav", "/Meghalaya.wav", "/Assam.wav", "/West Bengal.wav",
    "/Arunachal Pradesh.wav", "/Nagaland.wav", "/Manipur.wav", "/Mizoram.wav",
    "/Jharkhand.wav", "/Chattisgarh.wav", "/Madhya Pradesh.wav", "/Gujarat.wav",
    "/Odisha.wav", "/Maharashtra.wav", "/Telangana.wav", "/Andhra Pradesh.wav",
    "/Goa.wav", "/Karnataka.wav", "/Kerala.wav", "/Tamil Nadu.wav",
    "/Ladakh.wav", "/Jammu and Kashmir.wav", "/Chandigarh.wav", "/Delhi.wav",
    "/Lakshwadeep.wav", "/Andaman and Nicobar.wav",
    "/Dadar and Nagar Haveli and Daman and Diu.wav", "/Puduchery.wav"};
const int NUM_TRACKS = sizeof(playlist) / sizeof(playlist[0]);
int currentTrack = 0;
volatile bool isPlaying = false;
int currentVolume = 60;

// --- GLOBALS ---
static const int MAX_DEV = 15;
struct BtDev { esp_bd_addr_t addr; String name; bool valid; };
BtDev devices[MAX_DEV];
volatile int dev_count = 0;
volatile bool scanning = false;
volatile bool scan_done = false;

// Kill Switch Flag
volatile bool speaker_selected = false;

// --- BUTTON DEBOUNCE GLOBALS ---
const unsigned long DEBOUNCE_TIME = 250;
unsigned long lastPlayTime = 0;
unsigned long lastNextTime = 0;
unsigned long lastPrevTime = 0;
unsigned long lastVolDTime = 0;
unsigned long lastVolUTime = 0;

bool lastPlay = HIGH;
bool lastNext = HIGH;
bool lastPrev = HIGH;
bool lastVolD = HIGH;
bool lastVolU = HIGH;

// --- MODERN UI (DASHBOARD) ---
const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><title>Audio Bridge</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  :root { --bg: #0f172a; --card: #1e293b; --accent: #38bdf8; --danger: #ef4444; --text: #f8fafc; }
  body { font-family: system-ui, -apple-system, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; }
  .container { max-width: 500px; margin: auto; }
  .card { background: var(--card); padding: 24px; border-radius: 16px; border: 1px solid #334155; box-shadow: 0 20px 25px -5px rgba(0,0,0,0.5); }
  h2 { margin: 0 0 20px 0; color: var(--accent); font-size: 1.5rem; }
  .status-box { display: flex; align-items: center; justify-content: center; background: #0f172a; padding: 12px; border-radius: 8px; margin-bottom: 20px; font-weight: bold; font-family: monospace; font-size: 0.9rem; text-align: center; }
  .dot { height: 10px; width: 10px; border-radius: 50%; display: inline-block; margin-right: 10px; background: #64748b; }
  .dot-online { background-color: #22c55e; box-shadow: 0 0 8px #22c55e; }
  .dot-pulse { background-color: var(--accent); animation: pulse 1.5s infinite; }
  @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
  .btn { width: 100%; padding: 14px; border: none; border-radius: 10px; font-weight: 600; cursor: pointer; transition: 0.2s; font-size: 1rem; }
  .btn-primary { background: var(--accent); color: #0f172a; margin-bottom: 15px; }
  .btn-primary:active { transform: scale(0.98); }
  .btn-danger { background: transparent; color: var(--danger); border: 1px solid var(--danger); }
  #list { margin-top: 20px; text-align: left; }
  .dev-item { background: #334155; padding: 16px; border-radius: 10px; margin-bottom: 10px; cursor: pointer; border: 1px solid transparent; }
  .dev-item:active { background: #475569; border-color: var(--accent); }
  .dev-name { font-weight: 600; display: block; color: #fff; }
  .dev-mac { font-size: 11px; color: #94a3b8; font-family: monospace; }
</style></head>
<body>
  <div class="container">
    <div class="card">
          <h2>Audio Bridge Console</h2>
          <div class="status-box">
            <span id="dot" class="dot"></span>
            <span id="st">INITIALIZING</span>
          </div>
          <button class="btn btn-primary" onclick="cmd('/scan')">Search for Speakers</button>
          <div id="list"></div>
      </div>
  </div>
<script>
  let pollingInterval;
  function cmd(p){
      fetch(p,{method:'POST'});
      if(p==='/scan'){
          document.getElementById('st').innerText = "SCANNING...";
          document.getElementById('dot').className = "dot dot-pulse";
      document.getElementById('list').innerHTML = "<p style='color:#94a3b8;text-align:center'>Detecting Bluetooth signals...</p>";
      }
  }
  function connect(mac, name){
      document.getElementById('st').innerHTML = "CONNECTING...<br>UI WILL NOW GO OFFLINE";
      document.getElementById('dot').className = "dot dot-online";
      clearInterval(pollingInterval);
      fetch('/connect?mac='+mac,{method:'POST'});
  }

  pollingInterval = setInterval(()=>{
      fetch('/status').then(r=>r.json()).then(d=>{
        const st = document.getElementById('st');
        const dot = document.getElementById('dot');
        if(!d.scanning){
             st.innerText = d.connected ? "STREAMING ACTIVE" : "AP ONLINE";
             dot.className = d.connected ? "dot dot-online" : "dot";
        }
        if(d.scan_done) {
             fetch('/devices').then(r=>r.json()).then(devs=>{
          let h=''; devs.forEach(v=>{ h+=`<div class='dev-item' onclick="connect('${v.a}','${v.n}')"><span class='dev-name'>${v.n}</span><span class='dev-mac'>${v.a}</span></div>`; });
               document.getElementById('list').innerHTML = h;
             });
        }
      }).catch(e => {
            console.log("Server unreachable. Streaming mode active.");
      });
    },3000);
</script></body></html>
)=====";

// --- BT LOGIC HELPERS ---
String bdAddrToString(const esp_bd_addr_t bda) {
    char s[18];
  sprintf(s, "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return String(s);
}
String extractNameFromEir(const uint8_t *eir, uint8_t eir_len) {
    if (!eir || eir_len == 0) return "";
    int i = 0;
    while (i < eir_len) {
        uint8_t field_len = eir[i];
        if (field_len == 0) break;
        uint8_t type = eir[i + 1];
    if (type == ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME || type == ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME) {
            int name_len = field_len - 1;
            if (name_len > 0) {
                String nm = "";
                for (int k = 0; k < name_len; k++) nm += (char)eir[i + 2 + k];
                nm.trim(); return nm;
            }
        }
        i += field_len + 1;
    }
    return "";
}

void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    if (event == ESP_BT_GAP_DISC_RES_EVT) {
        esp_bd_addr_t bda; memcpy(bda, param->disc_res.bda, 6);
        String name = "";
        for (int i = 0; i < param->disc_res.num_prop; i++) {
            if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR)
        name = extractNameFromEir((uint8_t*)param->disc_res.prop[i].val, (uint8_t)param->disc_res.prop[i].len);
        }
        if (name.length() == 0) return;
        for (int k = 0; k < dev_count; k++) {
            if (devices[k].valid && memcmp(devices[k].addr, bda, 6) == 0) return;
        }
        if (dev_count < MAX_DEV) {
            memcpy(devices[dev_count].addr, bda, 6);
            devices[dev_count].name = name;
            devices[dev_count].valid = true;
            Serial.printf("[BT] Found: %s\n", name.c_str());
            dev_count++;
        }
    } else if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            Serial.println("[BT] Scan Finished.");
            scanning = false; scan_done = true;
        }
    }
}

// --- AUDIO FUNCTIONS ---
void playTrack(int index) {
        // 1. SAFETY LOCK: Tell the BT task to stop reading immediately
        isPlaying = false;

        // 2. Wait 30ms to ensure the BT task finishes its current read cycle safely
        delay(30);

        if (audioFile) {
            audioFile.close();
        }

        audioFile = SD.open(playlist[index]);
        if (audioFile) {
            audioFile.seek(44); // Skip header
            isPlaying = true;
            Serial.printf("▶ Playing: %s\n", playlist[index]);
        } else {
            Serial.printf("❌ Failed to open: %s\n", playlist[index]);
            isPlaying = false;
        }
}

// --- STANDARD STEREO AUDIO DATA (Reverted) ---
// --- MONO-TO-STEREO FIX (Memory Safe) ---
int32_t get_sound_data(Frame *data, int32_t frameCount) {
    if (!isPlaying || !audioFile || !audioFile.available() || !a2dp_source.is_connected())
{
        memset(data, 0, frameCount * sizeof(Frame));
        return frameCount;
    }

    // Treat the original stereo buffer as a temporary mono buffer
    int16_t *monoData = (int16_t *)data;

    // Read half the amount of data (because Mono is exactly half the size of Stereo)
    size_t bytesToRead = frameCount * 2;
    size_t bytesRead = audioFile.read((uint8_t*)monoData, bytesToRead);

    if (bytesRead == 0) {
        isPlaying = false;
        Serial.println("⏹ Audio finished.");
        return 0;
    }

    int samplesRead = bytesRead / 2;

    // Expand backwards to safely fill the stereo buffer without overwriting memory
    // This duplicates the Mono audio into both the Left and Right ears perfectly
    for (int i = samplesRead - 1; i >= 0; i--) {
        int16_t sample = monoData[i];
        data[i].channel1 = sample;
        data[i].channel2 = sample;
    }

    return samplesRead;
}

void setup() {
    Serial.begin(115200);
delay(1000);
Serial.println("\n--- STARTING CAPTIVE PORTAL AUDIO BRIDGE ---");

// 1. Initialize Buttons
pinMode(BTN_PLAY_PAUSE, INPUT_PULLUP);
pinMode(BTN_VOL_DOWN, INPUT_PULLUP);
pinMode(BTN_VOL_UP, INPUT_PULLUP);
pinMode(BTN_PREV, INPUT);
pinMode(BTN_NEXT, INPUT);

// Initialize button history with actual physical state
lastPlay = digitalRead(BTN_PLAY_PAUSE);
lastNext = digitalRead(BTN_NEXT);
lastPrev = digitalRead(BTN_PREV);
lastVolD = digitalRead(BTN_VOL_DOWN);
lastVolU = digitalRead(BTN_VOL_UP);

// 2. Initialize SD Card
if (!SD.begin(SD_CS, SPI, 4000000)) {
    Serial.println("❌ [ERROR] SD Mount Failed!");
} else {
    Serial.println("✅ [SYS] SD Card Mounted.");
}

// 3. RAM Optimization & BT Init
esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
a2dp_source.set_auto_reconnect(false);
a2dp_source.start(get_sound_data);
a2dp_source.set_volume(currentVolume);
esp_bt_gap_register_callback(gapCallback);
Serial.println("[SYS] Bluetooth Stack Active.");

// 4. Initialize WiFi Access Point
WiFi.mode(WIFI_AP);
WiFi.softAP(ap_ssid, ap_pass);
Serial.print("[WIFI] AP Ready: "); Serial.println(WiFi.softAPIP());

// 5. Initialize DNS Redirect
dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

server.on("/", [](){ server.send_P(200, "text/html", INDEX_HTML); });
server.onNotFound([](){ server.send_P(200, "text/html", INDEX_HTML); });

server.on("/status", [](){
  StaticJsonDocument<128> doc;
  doc["scanning"] = (bool)scanning;
  doc["scan_done"] = (bool)scan_done;
  doc["connected"] = (bool)a2dp_source.is_connected();
  String o; serializeJson(doc, o); server.send(200, "application/json", o);
});

server.on("/devices", [](){
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();
    for(int i=0; i<dev_count; i++){
        JsonObject obj = arr.add<JsonObject>();
        obj["n"] = devices[i].name;
        obj["a"] = bdAddrToString(devices[i].addr);
    }
    String o; serializeJson(doc, o); server.send(200, "application/json", o);
  });

  server.on("/scan", HTTP_POST, [](){
    if (a2dp_source.is_connected()) a2dp_source.disconnect();
    dev_count = 0; scanning = true; scan_done = false;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/connect", HTTP_POST, [](){
    String m = server.arg("mac");
    esp_bd_addr_t addr; int b[6];
    if (sscanf(m.c_str(), "%X:%X:%X:%X:%X:%X", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5])
== 6) {
        for (int i=0; i<6; i++) addr[i] = (uint8_t)b[i];
        Serial.printf("[WEB] Linking to MAC: %s\n", m.c_str());
        a2dp_source.connect_to(addr);
    }
    server.send(200, "application/json", "{\"ok\":true}");

    speaker_selected = true;
  });
    server.begin();
    Serial.println("[SYS] Web Server Operational. Waiting for user setup...");

    // --- 6. THE CONFIGURATION TRAP ---
    while (!speaker_selected) {
        dnsServer.processNextRequest();
        server.handleClient();
        delay(10);
    }

    // --- 7. TEARDOWN SEQUENCE ---
    Serial.println("[SYS] Speaker selected. Stopping web services...");
    delay(1000);

    server.stop();
    dnsServer.stop();
    WiFi.mode(WIFI_OFF);

  Serial.println("[SYS] Web services disabled. ESP32 is now dedicated to SD Audio Streaming.");
}

// --- MAIN LOOP ---
void loop() {

        // Read Current Button States
        bool currPlay = digitalRead(BTN_PLAY_PAUSE);
   bool currNext = digitalRead(BTN_NEXT);
   bool currPrev = digitalRead(BTN_PREV);
   bool currVolD = digitalRead(BTN_VOL_DOWN);
   bool currVolU = digitalRead(BTN_VOL_UP);

   unsigned long currentMillis = millis();

    // PLAY/PAUSE (D25)
    if (currPlay == LOW && lastPlay == HIGH && (currentMillis - lastPlayTime >
DEBOUNCE_TIME)) {
        if (isPlaying) {
            isPlaying = false;
            Serial.println("⏸ Paused");
        } else {
            if (a2dp_source.is_connected()) {
                   playTrack(currentTrack);
            } else {
                   Serial.println("⏳ Waiting for Bluetooth...");
            }
        }
        lastPlayTime = currentMillis;
    }

    // NEXT TRACK (D34)
    if (currNext == LOW && lastNext == HIGH && (currentMillis - lastNextTime >
DEBOUNCE_TIME)) {
        currentTrack = (currentTrack + 1) % NUM_TRACKS;
        if (a2dp_source.is_connected()) playTrack(currentTrack);
        lastNextTime = currentMillis;
    }

    // PREVIOUS TRACK (D35)
    if (currPrev == LOW && lastPrev == HIGH && (currentMillis - lastPrevTime >
DEBOUNCE_TIME)) {
        currentTrack = (currentTrack - 1 + NUM_TRACKS) % NUM_TRACKS;
        if (a2dp_source.is_connected()) playTrack(currentTrack);
        lastPrevTime = currentMillis;
    }

    // VOLUME DOWN (D32)
    if (currVolD == LOW && lastVolD == HIGH && (currentMillis - lastVolDTime >
DEBOUNCE_TIME)) {
        currentVolume = max(0, currentVolume - 10);
        a2dp_source.set_volume(currentVolume);
        Serial.printf("🔉 Volume: %d\n", currentVolume);
        lastVolDTime = currentMillis;
    }

    // VOLUME UP (D33)
    if (currVolU == LOW && lastVolU == HIGH && (currentMillis - lastVolUTime >
DEBOUNCE_TIME)) {
        currentVolume = min(127, currentVolume + 10);
        a2dp_source.set_volume(currentVolume);
        Serial.printf("🔊 Volume: %d\n", currentVolume);
        lastVolUTime = currentMillis;
    }
    // Update Last States
    lastPlay = currPlay;
    lastNext = currNext;
    lastPrev = currPrev;
    lastVolD = currVolD;
    lastVolU = currVolU;
}