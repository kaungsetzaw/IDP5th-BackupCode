#include <WiFi.h>
#include <esp_now.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>     
#include <sys/time.h>

WebServer server(80);
Preferences preferences;

const int configButtonPin = 0; 
const int buzzerPin = 18;      

String wifi_ssid;
String wifi_password;

const char* www_username = "admin";
const char* www_password = "admin123";

uint8_t cowMac[] = {0x1C, 0xDB, 0xD4, 0x76, 0x67, 0xD4}; 
uint8_t chickenMac[] = {0xA4, 0xCB, 0x8F, 0xDA, 0x37, 0xE4}; 
uint8_t fishMac[] = {0xA4, 0xCB, 0x8F, 0xD9, 0x7D, 0x98}; 

typedef struct cow_message {
  int unit_id;
  float temperature;
  int activity_state;
  int rssi_value;
  bool geofence_alert;
  bool is_sick;       
  bool is_critical;   
} cow_message;

typedef struct chicken_message {
  int unit_id;
  float temperature;
  int air_quality;
  bool fan_status;
  bool is_hot;        
  bool is_poor_air;   
  bool is_critical;   
} chicken_message;

typedef struct fish_message {
  int unit_id;
  float temperature;
  float ph_level;
  bool is_hot;        
  bool is_bad_ph;     
  bool is_critical;   
} fish_message;

cow_message cowData;
chicken_message chickenData;
fish_message fishData;

unsigned long lastCowUpdate = 0; 
unsigned long lastChickenUpdate = 0; 
unsigned long lastFishUpdate = 0; 

String rssi_ranges;

// 🆕 Hidden pH Manual Override Variables
bool manual_ph_active = false;
float manual_ph_value = 7.0;
float manual_ph_offset = 0.0; // 🆕 Tracks the continuous random walk

// 📊 Continuous History Array
struct HistoryPoint {
  uint32_t timestamp;
  float temp;
  int8_t rssi;
  uint8_t activity;
};

const int MAX_HISTORY = 4000; 
HistoryPoint history_log[MAX_HISTORY];
int history_index = 0;
int history_count = 0;

String parseLocation(int rssi, String configStr) {
    int startIndex = 0;
    while (startIndex < configStr.length()) {
        int semiIndex = configStr.indexOf(';', startIndex);
        if (semiIndex == -1) semiIndex = configStr.length();
        String segment = configStr.substring(startIndex, semiIndex);

        int c1 = segment.indexOf(',');
        int c2 = segment.indexOf(',', c1 + 1);
        int c3 = segment.indexOf(',', c2 + 1);
        int c4 = segment.indexOf(',', c3 + 1); 

        if (c1 != -1 && c2 != -1 && c3 != -1 && c4 != -1) {
            String name = segment.substring(0, c1);
            int maxVal = segment.substring(c1 + 1, c2).toInt();
            int minVal = segment.substring(c2 + 1, c3).toInt();
            if (rssi <= maxVal && rssi >= minVal) return name;
        }
        startIndex = semiIndex + 1;
    }
    return "Unknown Zone";
}

bool isZoneCritical(int rssi, String configStr) {
    int startIndex = 0;
    while (startIndex < configStr.length()) {
        int semiIndex = configStr.indexOf(';', startIndex);
        if (semiIndex == -1) semiIndex = configStr.length();
        String segment = configStr.substring(startIndex, semiIndex);

        int c1 = segment.indexOf(',');
        int c2 = segment.indexOf(',', c1 + 1);
        int c3 = segment.indexOf(',', c2 + 1);
        int c4 = segment.indexOf(',', c3 + 1);

        if (c1 != -1 && c2 != -1 && c3 != -1 && c4 != -1) {
            int maxVal = segment.substring(c1 + 1, c2).toInt();
            int minVal = segment.substring(c2 + 1, c3).toInt();
            bool isCrit = segment.substring(c4 + 1).toInt() == 1; 
            if (rssi <= maxVal && rssi >= minVal) return isCrit;
        }
        startIndex = semiIndex + 1;
    }
    return true; 
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  if (len == sizeof(cow_message) && memcmp(mac_addr, cowMac, 6) == 0) {
    memcpy(&cowData, incomingData, sizeof(cowData));
    lastCowUpdate = millis(); 
    
    uint32_t current_time = time(NULL);
    if (current_time > 1000000000) { 
      int last_idx = (history_index - 1 + MAX_HISTORY) % MAX_HISTORY;
      
      if (history_count == 0 || current_time > history_log[last_idx].timestamp) {
          history_log[history_index].timestamp = current_time;
          history_log[history_index].rssi = cowData.rssi_value;
          history_log[history_index].temp = cowData.temperature;
          history_log[history_index].activity = cowData.activity_state;
          
          history_index = (history_index + 1) % MAX_HISTORY;
          if (history_count < MAX_HISTORY) history_count++;
      }
    }
  } 
  else if (len == sizeof(chicken_message) && memcmp(mac_addr, chickenMac, 6) == 0) {
    memcpy(&chickenData, incomingData, sizeof(chickenData));
    lastChickenUpdate = millis(); 
  } 
  else if (len == sizeof(fish_message) && memcmp(mac_addr, fishMac, 6) == 0) {
    memcpy(&fishData, incomingData, sizeof(fishData));
    lastFishUpdate = millis(); 
  }
}

String getHistoryJSON() {
  String json = "[";
  for(int i=0; i<history_count; i++) {
    int idx = (history_index - history_count + i + MAX_HISTORY) % MAX_HISTORY;
    json += "{\"t\":" + String(history_log[idx].timestamp) + ",\"r\":" + String(history_log[idx].rssi) + "}";
    if(i < history_count - 1) json += ",";
  }
  json += "]";
  return json;
}

String getZonesJSON() {
  String json = "[";
  int startIndex = 0;
  bool first = true;
  while (startIndex < rssi_ranges.length()) {
      int semiIndex = rssi_ranges.indexOf(';', startIndex);
      if (semiIndex == -1) semiIndex = rssi_ranges.length();
      String segment = rssi_ranges.substring(startIndex, semiIndex);

      int c1 = segment.indexOf(',');
      int c2 = segment.indexOf(',', c1 + 1);
      int c3 = segment.indexOf(',', c2 + 1);
      int c4 = segment.indexOf(',', c3 + 1);

      if (c1 != -1 && c2 != -1 && c3 != -1 && c4 != -1) {
          if(!first) json += ",";
          String name = segment.substring(0, c1);
          String maxVal = segment.substring(c1 + 1, c2);
          String minVal = segment.substring(c2 + 1, c3);
          String color = segment.substring(c3 + 1, c4);
          json += "{\"name\":\"" + name + "\",\"max\":" + maxVal + ",\"min\":" + minVal + ",\"color\":\"" + color + "\"}";
          first = false;
      }
      startIndex = semiIndex + 1;
  }
  json += "]";
  return json;
}

void handleSetTime() {
  if (server.hasArg("t")) {
    time_t t = server.arg("t").toInt();
    if (t > 1000000000) {
      struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
      settimeofday(&tv, NULL);
      Serial.printf("\n✅ Time Synced Automatically from Web Browser: %ld\n", t);
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleRoot() {
  time_t now;
  time(&now);
  struct tm timeinfo;
  bool timeValid = (now > 1000000000);
  if (timeValid) {
    localtime_r(&now, &timeinfo);
  }
  
  bool isCowOffline = (millis() - lastCowUpdate > 15000);

  String html;
  html.reserve(15000); 
  
  html += R"=====(
  <!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>Smart Farm Dashboard</title>
  <style>
    :root { --bg: #f8fafc; --card: #ffffff; --text: #0f172a; --muted: #64748b; 
    --green: #10b981; --green-bg: #d1fae5; --red: #ef4444; --red-bg: #fee2e2; 
    --gray: #94a3b8; --gray-bg: #f1f5f9; --border: #e2e8f0; --blue: #3b82f6;}
    body { font-family: system-ui, -apple-system, sans-serif; background-color: var(--bg); color: var(--text); margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
    .header { text-align: center; margin-bottom: 20px; width: 100%; max-width: 1000px; position: relative;}
    .header h2 { margin: 0 0 10px 0; font-size: 28px; font-weight: 800; color: #1e293b; }
    .time-badge { background: var(--card); padding: 8px 20px; border-radius: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.05); font-size: 14px; font-weight: 500; color: var(--muted); border: 1px solid var(--border); display: inline-block; }
    .nav-btn { position: absolute; right: 0; top: 0; padding: 8px 16px; background: var(--blue); color: white; text-decoration: none; border-radius: 8px; font-weight: 600; font-size: 14px; box-shadow: 0 2px 4px rgba(59,130,246,0.3); transition: 0.2s;}
    .nav-btn:hover { background: #2563eb; }
    .grid { display: grid; gap: 24px; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); width: 100%; max-width: 1000px; }
    .card { background: var(--card); border-radius: 16px; padding: 24px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.05); border: 1px solid var(--border); transition: transform 0.2s ease; }
    .card.alert-card { border: 2px solid var(--red); box-shadow: 0 4px 15px rgba(239, 68, 68, 0.15); }
    .card.graph-card { grid-column: 1 / -1; padding: 15px 24px; }
    .card-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); padding-bottom: 15px; margin-bottom: 18px; }
    .card-header h3 { margin: 0; font-size: 18px; font-weight: 700;}
    .status { padding: 6px 12px; border-radius: 20px; font-size: 11px; font-weight: 800; text-transform: uppercase; }
    .status.online { background: var(--green-bg); color: #065f46; }
    .status.critical { background: var(--red-bg); color: #991b1b; }
    .status.offline { background: var(--gray-bg); color: #475569; }
    .data-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 14px; font-size: 15px; }
    .data-label { color: var(--muted); display: flex; align-items: center; font-weight: 500;}
    .data-value { font-weight: 700; color: var(--text); }
    .text-red { color: var(--red); }
    .text-green { color: var(--green); }
    .text-blue { color: var(--blue); }
    .signal { font-size: 12px; color: #cbd5e1; margin-left: 5px; }
    .offline-msg { text-align: center; color: var(--muted); font-style: italic; padding: 20px 0; }
    canvas { width: 100%; height: 280px; background: #ffffff; border-radius: 8px; border: 1px solid var(--border); margin-top: 15px;}
    select { padding: 6px 12px; border-radius: 6px; border: 1px solid var(--border); font-weight: 600; color: var(--text); cursor: pointer; outline: none; background: #f8fafc;}
    @keyframes fadeIn { from { opacity: 0.7; } to { opacity: 1; } } body { animation: fadeIn 0.4s ease-in-out; }
    @media (max-width: 600px) { .nav-btn { position: relative; display: block; margin: 15px auto 0 auto; width: fit-content;} }
  </style>
  <script>
    function saveSelection() {
        let sel = document.getElementById('timeRange');
        localStorage.setItem('savedTimeRange', sel.value);
        drawChart();
    }
    setInterval(function(){
      fetch('/').then(r => r.text()).then(html => {
        let parser = new DOMParser();
        let doc = parser.parseFromString(html, 'text/html');
        document.getElementById('cards-container').innerHTML = doc.getElementById('cards-container').innerHTML;
        updateGraphData(doc.getElementById('rawHistory').innerText, doc.getElementById('graphState').innerText);
      });
    }, 5000);
  </script>
  </head><body>
  <div class='header'>
    <h2>🌾 Smart Animal Farm</h2>
    <a href="/control" class="nav-btn">⚙️ Control Panel</a>
    %TIME_BADGE%
  </div>
  
  <div class='grid' id='data-grid'>
    <div id="cards-container" style="display: contents;">
        %COW_CARD%
        %CHICKEN_CARD%
        %FISH_CARD%
    </div>
    
    <div class="card graph-card">
        <div class="card-header">
            <h3>📊 Cow Location History</h3>
            <select id="timeRange" onchange="saveSelection()">
                <option value="60">Last 1 Min</option>
                <option value="180">Last 3 Mins</option>
                <option value="300">Last 5 Mins</option>
                <option value="900">Last 15 Mins</option>
                <option value="3600">Last 1 Hour</option>
                <option value="18000">Last 5 Hours</option>
            </select>
        </div>
        <canvas id="rssiChart"></canvas>
    </div>
  </div>

  <div id="rawHistory" style="display:none;">%HISTORY_DATA%</div>
  <div id="graphState" style="display:none;">%GRAPH_STATE%</div>

  <script>
    let storedRange = localStorage.getItem('savedTimeRange');
    if(storedRange) { document.getElementById('timeRange').value = storedRange; } 
    else { document.getElementById('timeRange').value = "18000"; }

    let historyData = %HISTORY_DATA%;
    let graphState = %GRAPH_STATE%;
    const zones = %ZONE_DATA%;
    const canvas = document.getElementById('rssiChart');
    const ctx = canvas.getContext('2d');
    
    function resizeCanvas() {
        canvas.width = canvas.parentElement.clientWidth - 48; 
        canvas.height = 280;
        drawChart();
    }
    window.addEventListener('resize', resizeCanvas);

    function updateGraphData(newHistoryStr, newStateStr) {
        historyData = JSON.parse(newHistoryStr);
        graphState = JSON.parse(newStateStr);
        drawChart();
    }

    function drawChart() {
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        let now = graphState.now;
        let windowSeconds = parseInt(document.getElementById('timeRange').value);
        let minTime = now - windowSeconds;
        let maxTime = now;
        
        const padL = 45; const padR = 15; const padT = 15; const padB = 25; 
        const w = canvas.width - padL - padR; const h = canvas.height - padT - padB;
        
        const mapY = (val) => padT + h - ((val + 100) / 100 * h);
        const mapX = (t) => padL + ((t - minTime) / (maxTime - minTime)) * w;

        zones.forEach(zone => {
            let yMax = mapY(zone.min); let yMin = mapY(zone.max); 
            if (yMin < padT) yMin = padT; if (yMax > padT + h) yMax = padT + h;
            ctx.fillStyle = zone.color;
            ctx.fillRect(padL, yMin, w, yMax - yMin);
            ctx.fillStyle = 'rgba(0,0,0,0.4)'; ctx.font = 'bold 11px sans-serif';
            ctx.textAlign = 'left'; ctx.fillText(zone.name, padL + 8, yMin + 16);
        });

        ctx.strokeStyle = '#cbd5e1'; ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(padL, padT); ctx.lineTo(padL, padT + h); ctx.lineTo(padL + w, padT + h); ctx.stroke();
        ctx.fillStyle = '#64748b'; ctx.font = '11px sans-serif'; ctx.textAlign = 'right';
        for(let i=0; i>=-100; i-=25) {
            let y = mapY(i); ctx.fillText(i, padL - 8, y + 4);
            ctx.beginPath(); ctx.strokeStyle = 'rgba(0,0,0,0.05)'; ctx.moveTo(padL, y); ctx.lineTo(padL + w, y); ctx.stroke();
        }

        ctx.textAlign = 'center';
        for(let i=0; i<=4; i++) {
            let t = minTime + (maxTime - minTime) * (i/4); let x = mapX(t); let date = new Date(t * 1000);
            let timeStr = date.getHours().toString().padStart(2, '0') + ':' + date.getMinutes().toString().padStart(2, '0');
            if (windowSeconds <= 60) timeStr += ':' + date.getSeconds().toString().padStart(2, '0');
            ctx.fillText(timeStr, x, padT + h + 16);
        }

        let validPoints = []; let prePoint = null;
        for(let i=0; i<historyData.length; i++) {
            if(historyData[i].t < minTime) prePoint = historyData[i]; 
            else validPoints.push(historyData[i]);
        }
        
        let drawPoints = [];
        if(prePoint) drawPoints.push({t: minTime, r: prePoint.r}); 
        else if (validPoints.length > 0) drawPoints.push({t: minTime, r: validPoints[0].r}); 
        
        drawPoints.push(...validPoints);
        if(!graphState.offline) { drawPoints.push({t: maxTime, r: graphState.rssi}); }

        if(drawPoints.length > 1) {
            ctx.beginPath(); ctx.strokeStyle = '#1e293b'; ctx.lineWidth = 2.5; ctx.lineJoin = 'round';
            drawPoints.forEach((point, index) => {
                let x = mapX(point.t); let y = mapY(point.r);
                if(index === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y); 
            });
            ctx.stroke();
            ctx.fillStyle = '#3b82f6';
            validPoints.forEach((point) => {
                let x = mapX(point.t); let y = mapY(point.r);
                ctx.beginPath(); ctx.arc(x, y, 3, 0, Math.PI * 2); ctx.fill();
            });
            if(!graphState.offline) {
                let curX = mapX(maxTime); let curY = mapY(graphState.rssi);
                ctx.fillStyle = '#ef4444'; ctx.beginPath(); ctx.arc(curX, curY, 5, 0, Math.PI * 2); ctx.fill();
            }
        }
    }
    setTimeout(resizeCanvas, 100); 
  </script>
  </body></html>
  )=====";

  String timeBadgeHTML = "";
  if (timeValid) {
    timeBadgeHTML = "<div class='time-badge'>🕒 <span id='liveClock'>Loading Time...</span></div>";
    timeBadgeHTML += "<script>var rtcTime=new Date(" + String(timeinfo.tm_year + 1900) + "," + String(timeinfo.tm_mon) + "," + String(timeinfo.tm_mday) + "," + String(timeinfo.tm_hour) + "," + String(timeinfo.tm_min) + "," + String(timeinfo.tm_sec) + ");const months=['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];function updateClock(){var y=rtcTime.getFullYear();var mo=months[rtcTime.getMonth()];var d=rtcTime.getDate().toString().padStart(2,'0');var h=rtcTime.getHours().toString().padStart(2,'0');var m=rtcTime.getMinutes().toString().padStart(2,'0');var s=rtcTime.getSeconds().toString().padStart(2,'0');document.getElementById('liveClock').innerHTML=mo+' '+d+', '+y+' &nbsp;|&nbsp; '+h+':'+m+':'+s;rtcTime.setSeconds(rtcTime.getSeconds()+1);}updateClock();setInterval(updateClock,1000);</script>";
    timeBadgeHTML += "<script>setTimeout(() => fetch('/set_time?t=' + Math.floor(Date.now() / 1000)), 2000);</script>";
  } else {
    timeBadgeHTML = "<div class='time-badge' style='color: var(--red);'>🕒 Syncing Time from Browser...</div>";
    timeBadgeHTML += "<script>fetch('/set_time?t=' + Math.floor(Date.now() / 1000)).then(() => setTimeout(() => location.reload(), 1000));</script>";
  }
  html.replace("%TIME_BADGE%", timeBadgeHTML);

  bool mainBuzzerTrigger = false;

  String cowCard = "";
  if (isCowOffline) {
     cowCard = "<div class='card'><div class='card-header'><h3>🐄 Cow Unit</h3><span class='status offline'>Offline</span></div><div class='offline-msg'>No connection established.</div></div>";
  } else {
     bool zone_critical = isZoneCritical(cowData.rssi_value, rssi_ranges);
     bool cow_overall_critical = cowData.is_critical || zone_critical; 
     if (cow_overall_critical) mainBuzzerTrigger = true;
     
     String locationText = parseLocation(cowData.rssi_value, rssi_ranges);
     String locStyle = zone_critical ? "text-red" : "text-blue";

     cowCard = "<div class='" + String(cow_overall_critical ? "card alert-card" : "card") + "'><div class='card-header'><h3>🐄 Cow Unit</h3><span class='" + String(cow_overall_critical ? "status critical" : "status online") + "'>" + String(cow_overall_critical ? "Critical" : "Online") + "</span></div>";
     cowCard += "<div class='data-row'><span class='data-label'>🌡️ Temperature</span><span class='" + String(cowData.is_sick ? "data-value text-red" : "data-value") + "'>" + String(cowData.temperature, 1) + " &deg;C" + String(cowData.is_sick ? " (Fever)" : "") + "</span></div>";
     String actText = cowData.activity_state == 2 ? "Fighting!" : (cowData.activity_state == 1 ? "Active" : "Idle");
     cowCard += "<div class='data-row'><span class='data-label'>🏃 Activity</span><span class='" + String(cowData.activity_state == 2 ? "data-value text-red" : "data-value") + "'>" + actText + "</span></div>";
     cowCard += "<div class='data-row'><span class='data-label'>📍 Location <span class='signal'>(" + String(cowData.rssi_value) + " dBm)</span></span><span class='data-value " + locStyle + "'>" + locationText + "</span></div></div>";
  }
  html.replace("%COW_CARD%", cowCard);

  String chickenCard = "";
  bool isChickenOffline = (millis() - lastChickenUpdate > 15000);
  if (isChickenOffline) {
     chickenCard = "<div class='card'><div class='card-header'><h3>🐔 Chicken Unit</h3><span class='status offline'>Offline</span></div><div class='offline-msg'>No connection established.</div></div>";
  } else {
     if (chickenData.is_critical) mainBuzzerTrigger = true;
     chickenCard = "<div class='" + String(chickenData.is_critical ? "card alert-card" : "card") + "'><div class='card-header'><h3>🐔 Chicken Unit</h3><span class='" + String(chickenData.is_critical ? "status critical" : "status online") + "'>" + String(chickenData.is_critical ? "Critical" : "Online") + "</span></div>";
     chickenCard += "<div class='data-row'><span class='data-label'>🌡️ Temperature</span><span class='" + String(chickenData.is_hot ? "data-value text-red" : "data-value") + "'>" + String(chickenData.temperature, 1) + " &deg;C" + String(chickenData.is_hot ? " (Hot)" : "") + "</span></div>";
     chickenCard += "<div class='data-row'><span class='data-label'>💨 Air Quality</span><span class='" + String(chickenData.is_poor_air ? "data-value text-red" : "data-value") + "'>" + String(chickenData.air_quality) + " AQI" + String(chickenData.is_poor_air ? " (Poor)" : " (Good)") + "</span></div>";
     chickenCard += "<div class='data-row'><span class='data-label'>🌀 Exhaust Fan</span><span class='" + String(chickenData.fan_status ? "data-value text-green" : "data-value") + "'>" + String(chickenData.fan_status ? "Running" : "Standby") + "</span></div></div>";
  }
  html.replace("%CHICKEN_CARD%", chickenCard);

  String fishCard = "";
  bool isFishOffline = (millis() - lastFishUpdate > 15000);
  
  // If Prototype Mode is ON, we force the Fish card to show online for testing
  if (manual_ph_active) isFishOffline = false; 

  if (isFishOffline) {
     fishCard = "<div class='card'><div class='card-header'><h3>🐟 Fish Pond</h3><span class='status offline'>Offline</span></div><div class='offline-msg'>No connection established.</div></div>";
  } else {
     // 🆕 Realistic Fluctuation Logic (Continuous Random Walk)
     float display_ph = fishData.ph_level;
     
     if (manual_ph_active) {
         // Generate random step between -0.10 and +0.10
         float step = random(-10, 11) / 100.0;
         manual_ph_offset += step;
         
         // Clamp the total offset between -0.3 and +0.3
         if (manual_ph_offset > 0.3) manual_ph_offset = 0.3;
         if (manual_ph_offset < -0.3) manual_ph_offset = -0.3;
         
         display_ph = manual_ph_value + manual_ph_offset;
     }
     
     bool display_bad_ph = manual_ph_active ? (display_ph < 6.5 || display_ph > 8.5) : fishData.is_bad_ph;
     bool display_fish_critical = fishData.is_hot || display_bad_ph;

     if (display_fish_critical) mainBuzzerTrigger = true;
     
     fishCard = "<div class='" + String(display_fish_critical ? "card alert-card" : "card") + "'><div class='card-header'><h3>🐟 Fish Pond</h3><span class='" + String(display_fish_critical ? "status critical" : "status online") + "'>" + String(display_fish_critical ? "Critical" : "Online") + "</span></div>";
     fishCard += "<div class='data-row'><span class='data-label'>🌡️ Water Temp</span><span class='" + String(fishData.is_hot ? "data-value text-red" : "data-value") + "'>" + String(fishData.temperature, 1) + " &deg;C" + String(fishData.is_hot ? " (Danger)" : "") + "</span></div>";
     
     String phStyle = display_bad_ph ? "data-value text-red" : "data-value";
     
     // 🆕 No override text here, looks completely natural
     fishCard += "<div class='data-row'><span class='data-label'>💧 pH Level</span><span class='" + phStyle + "'>" + String(display_ph, 2) + String(display_bad_ph ? " (Imbalanced)" : " (Optimal)") + "</span></div></div>";
  }
  html.replace("%FISH_CARD%", fishCard);

  String graphStateStr = "{\"now\":" + String(time(NULL)) + ",\"rssi\":" + String(cowData.rssi_value) + ",\"offline\":" + String(isCowOffline ? "true" : "false") + "}";
  html.replace("%GRAPH_STATE%", graphStateStr);
  html.replace("%HISTORY_DATA%", getHistoryJSON());
  html.replace("%ZONE_DATA%", getZonesJSON());

  if (mainBuzzerTrigger) digitalWrite(buzzerPin, HIGH);
  else digitalWrite(buzzerPin, LOW);

  server.send(200, "text/html", html);
}

void handleControl() {
  if (!server.authenticate(www_username, www_password)) return server.requestAuthentication();

  String html = R"=====(
  <!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>Admin Control Panel</title>
  <style>
    :root { --bg: #f8fafc; --card: #ffffff; --text: #0f172a; --border: #e2e8f0; --blue: #3b82f6; --red: #ef4444; --green: #10b981;}
    body { font-family: system-ui, -apple-system, sans-serif; background-color: var(--bg); color: var(--text); margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
    .container { background: var(--card); border-radius: 16px; padding: 30px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.05); width: 100%; max-width: 800px; border: 1px solid var(--border); }
    h2 { margin-top: 0; color: #1e293b; border-bottom: 2px solid var(--border); padding-bottom: 10px; display: flex; justify-content: space-between; align-items: center;}
    .nav-btn { display: inline-block; margin-bottom: 20px; padding: 8px 16px; background: #e2e8f0; color: #334155; text-decoration: none; border-radius: 8px; font-weight: 600; font-size: 14px; transition: 0.2s;}
    .nav-btn:hover { background: #cbd5e1; }
    .dl-btn { background: var(--green); color: white; padding: 6px 12px; border-radius: 6px; text-decoration: none; font-size: 14px; font-weight: 600;}
    .dl-btn:hover { background: #059669; }
    
    .range-row { display: grid; grid-template-columns: 2fr 1fr 1fr 1fr auto auto; gap: 10px; margin-bottom: 15px; align-items: center; background: #f1f5f9; padding: 15px; border-radius: 8px;}
    .input-group { display: flex; flex-direction: column;}
    label { font-size: 11px; font-weight: 700; color: #64748b; margin-bottom: 5px; text-transform: uppercase;}
    input { padding: 8px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 14px;}
    input[type="color"] { padding: 0; height: 36px; width: 100%; cursor: pointer;}
    .btn { padding: 10px 15px; border: none; border-radius: 8px; font-weight: 600; cursor: pointer; transition: 0.2s; }
    .btn-red { background: var(--red); color: white; margin-top: 18px; padding: 8px 12px; }
    .btn-red:hover { background: #dc2626; }
    .btn-blue { background: var(--blue); color: white; width: 100%; margin-top: 20px; font-size: 16px; padding: 12px;}
    .btn-blue:hover { background: #2563eb; }
    .btn-outline { background: transparent; border: 2px dashed #94a3b8; color: #64748b; width: 100%; margin-top: 10px; }
    .btn-outline:hover { border-color: var(--blue); color: var(--blue); }
    
    .success-msg { background: #d1fae5; color: #065f46; padding: 10px; border-radius: 8px; text-align: center; margin-bottom: 20px; font-weight: 600; display: %SUCCESS_DISPLAY%;}
    @media (max-width: 700px) { .range-row { grid-template-columns: 1fr 1fr; } .zone-name-group { grid-column: 1 / -1; } .btn-red { grid-column: 1 / -1; margin-top: 5px; } h2 { flex-direction: column; align-items: flex-start; gap: 10px;} }
  </style>
  </head><body>
  <div class="container">
    <a href="/" class="nav-btn">⬅ Back to Dashboard</a>
    <h2>
      <span>⚙️ Geofence Zones</span>
      <a href="/download_csv" class="dl-btn">📥 Download Event CSV</a>
    </h2>
    <p style="color:#64748b; font-size: 14px;">Set the signal ranges and check 'Alarm 🔔' for critical zones.</p>
    
    <div class="success-msg">✅ Configuration Saved Successfully!</div>

    <form action="/save_ranges" method="POST" id="configForm">
      <input type="hidden" name="ranges" id="rangesData">
      <div id="rangesContainer"></div>
      
      <button type="button" class="btn btn-outline" onclick="addRangeRow('', 0, -100, '#ffffff', 0)">➕ Add New Zone</button>
      <button type="submit" class="btn btn-blue" onclick="prepareSubmit()">💾 Save Settings</button>
    </form>
  </div>

  <script>
    const savedData = "%SAVED_RANGES%";
    const container = document.getElementById("rangesContainer");
    
    const pastels = ['#ffb3ba', '#baffc9', '#bae1ff', '#ffffba', '#f3e8ff'];
    let colorIndex = 0;

    function addRangeRow(name, max, min, color, isCrit) {
      let pickColor = color;
      if(!color) { pickColor = pastels[colorIndex % pastels.length]; colorIndex++; }
      let checked = (isCrit == 1) ? "checked" : "";
      
      const row = document.createElement("div");
      row.className = "range-row";
      row.innerHTML = `
        <div class="input-group zone-name-group">
          <label>Zone Name</label>
          <input type="text" class="zone-name" value="${name}" placeholder="e.g. Milling" required>
        </div>
        <div class="input-group">
          <label>Max (-)</label>
          <input type="number" class="zone-max" value="${max}" max="0" min="-100" required>
        </div>
        <div class="input-group">
          <label>Min (-)</label>
          <input type="number" class="zone-min" value="${min}" max="0" min="-100" required>
        </div>
        <div class="input-group">
          <label>Graph Color</label>
          <input type="color" class="zone-color" value="${pickColor}" required>
        </div>
        <div class="input-group" style="align-items: center;">
          <label>Alarm 🔔</label>
          <input type="checkbox" class="zone-crit" ${checked} style="width: 22px; height: 22px; margin-top: 5px; cursor: pointer;">
        </div>
        <button type="button" class="btn btn-red" onclick="this.parentElement.remove()">X</button>
      `;
      container.appendChild(row);
    }

    if(savedData.length > 0) {
      const zones = savedData.split(";");
      zones.forEach(zone => {
        const parts = zone.split(",");
        if(parts.length === 5) addRangeRow(parts[0], parts[1], parts[2], parts[3], parts[4]);
      });
    } else {
      addRangeRow("Milling", 0, -25, "#ffb3ba", 0); 
      addRangeRow("Grazing", -26, -84, "#baffc9", 0);
      addRangeRow("Out of Bounds", -85, -100, "#bae1ff", 1);
    }

    function prepareSubmit() {
      const rows = document.querySelectorAll('.range-row');
      let finalString = [];
      rows.forEach(row => {
        const name = row.querySelector('.zone-name').value;
        const max = row.querySelector('.zone-max').value;
        const min = row.querySelector('.zone-min').value;
        const color = row.querySelector('.zone-color').value;
        const isCrit = row.querySelector('.zone-crit').checked ? 1 : 0;
        if(name) finalString.push(`${name},${max},${min},${color},${isCrit}`);
      });
      document.getElementById('rangesData').value = finalString.join(';');
    }
  </script>
  </body></html>
  )=====";

  if (server.hasArg("success")) html.replace("%SUCCESS_DISPLAY%", "block");
  else html.replace("%SUCCESS_DISPLAY%", "none");
  html.replace("%SAVED_RANGES%", rssi_ranges);
  server.send(200, "text/html", html);
}

void handleSave() {
  if (!server.authenticate(www_username, www_password)) return server.requestAuthentication();
  if (server.hasArg("ranges")) {
    rssi_ranges = server.arg("ranges");
    preferences.putString("rssi_ranges", rssi_ranges); 
  }
  server.sendHeader("Location", "/control?success=1", true);
  server.send(302, "text/plain", "");
}

void handlePhControl() {
  if (!server.authenticate(www_username, www_password)) return server.requestAuthentication();

  String html = R"=====(
  <!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>pH Prototype Control</title>
  <style>
    body { font-family: system-ui, -apple-system, sans-serif; background-color: #f8fafc; color: #0f172a; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
    .container { background: #ffffff; border-radius: 16px; padding: 30px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.05); width: 100%; max-width: 600px; border: 1px solid #e2e8f0; }
    h2 { margin-top: 0; color: #1e293b; border-bottom: 2px solid #e2e8f0; padding-bottom: 10px; }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 20px; }
    .btn { padding: 12px; border: none; border-radius: 8px; font-weight: 600; font-size: 15px; cursor: pointer; transition: 0.2s; color: white; text-align: left; }
    .btn:hover { opacity: 0.9; transform: translateY(-1px); }
    .btn-acid { background: #ef4444; } 
    .btn-neutral { background: #10b981; } 
    .btn-base { background: #3b82f6; } 
    .btn-disable { background: #64748b; width: 100%; margin-top: 20px; text-align: center; font-size: 16px; padding: 15px;}
    .status-badge { display: inline-block; padding: 6px 12px; border-radius: 20px; font-size: 13px; font-weight: 800; background: %STATUS_BG%; color: %STATUS_COLOR%; margin-bottom: 20px; }
    .nav-btn { display: inline-block; margin-bottom: 20px; padding: 8px 16px; background: #e2e8f0; color: #334155; text-decoration: none; border-radius: 8px; font-weight: 600; font-size: 14px; }
    @media (max-width: 500px) { .grid { grid-template-columns: 1fr; } }
  </style>
  <script>
    function setPh(val) {
      fetch('/set_ph?active=1&val=' + val).then(() => location.reload());
    }
    function disableOverride() {
      fetch('/set_ph?active=0').then(() => location.reload());
    }
  </script>
  </head><body>
  <div class="container">
    <a href="/" class="nav-btn">⬅ Back to Dashboard</a>
    <h2>💧 pH Sensor Prototype Lab</h2>
    <div class="status-badge">%STATUS_TEXT%</div>
    
    <div style="color:#64748b; font-size: 14px; margin-bottom: 15px;">Click a button below to instantly override the Fish Pond pH value on the dashboard for testing.</div>
    
    <div class="grid">
      <!-- Acidic -->
      <button class="btn btn-acid" onclick="setPh(2.4)">🍋 Lime Juice (pH 2.4)</button>
      <button class="btn btn-acid" onclick="setPh(3.0)">🏺 Vinegar (pH 3.0)</button>
      <button class="btn btn-acid" onclick="setPh(5.0)">☕ Black Coffee (pH 5.0)</button>
      <button class="btn btn-acid" onclick="setPh(6.0)">🥛 Normal Milk (pH 6.0)</button>
      
      <!-- Neutral (Optimal) -->
      <button class="btn btn-neutral" onclick="setPh(7.0)">💧 Purified Water (pH 7.0)</button>
      <button class="btn btn-neutral" onclick="setPh(7.5)">🚰 Clean Tap Water (pH 7.5)</button>
      
      <!-- Alkaline (Base) -->
      <button class="btn btn-base" onclick="setPh(9.0)">🧂 Baking Soda (pH 9.0)</button>
      <button class="btn btn-base" onclick="setPh(10.0)">🧼 Soap Water (pH 10.0)</button>
      <button class="btn btn-base" onclick="setPh(11.5)">🧴 Ammonia (pH 11.5)</button>
      <button class="btn btn-base" onclick="setPh(12.5)">🧪 Bleach (pH 12.5)</button>
    </div>

    <button class="btn btn-disable" onclick="disableOverride()">🛑 Disable Override (Resume Actual Sensor)</button>
  </div>
  </body></html>
  )=====";

  if (manual_ph_active) {
      html.replace("%STATUS_BG%", "#fef3c7");
      html.replace("%STATUS_COLOR%", "#b45309");
      html.replace("%STATUS_TEXT%", "⚠️ OVERRIDE ACTIVE : " + String(manual_ph_value, 1) + " pH");
  } else {
      html.replace("%STATUS_BG%", "#d1fae5");
      html.replace("%STATUS_COLOR%", "#065f46");
      html.replace("%STATUS_TEXT%", "✅ USING ACTUAL SENSOR DATA");
  }
  
  server.send(200, "text/html", html);
}

void handleSetPh() {
  if (!server.authenticate(www_username, www_password)) return server.requestAuthentication();
  
  if (server.hasArg("active")) {
    manual_ph_active = (server.arg("active") == "1");
  }
  if (server.hasArg("val")) {
    manual_ph_value = server.arg("val").toFloat();
    // 🆕 Reset the offset when a new value is clicked
    manual_ph_offset = 0.0;
  }
  
  server.send(200, "text/plain", "OK");
}

void handleDownloadCSV() {
  if (!server.authenticate(www_username, www_password)) return server.requestAuthentication();
  
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=\"cow_event_history.csv\"");
  server.send(200, "text/csv", "");
  
  server.sendContent("Timestamp_Epoch,Date_Time,RSSI,Zone,Temperature_C,Activity,Event_Trigger\n");
  
  String last_zone = "";
  int last_activity = -1;
  bool last_temp_crit = false;
  bool is_first = true;
  
  for(int i = 0; i < history_count; i++) {
    int idx = (history_index - history_count + i + MAX_HISTORY) % MAX_HISTORY;
    HistoryPoint pt = history_log[idx];
    
    String zone = parseLocation(pt.rssi, rssi_ranges);
    bool temp_crit = (pt.temp > 38.5); 
    
    String trigger_reason = "";
    bool should_write = false;

    if (is_first) {
        should_write = true;
        trigger_reason = "Initial State";
        is_first = false;
    } else {
        if (zone != last_zone) {
            should_write = true;
            trigger_reason += "Zone Changed ";
        }
        if (pt.activity != last_activity) {
            should_write = true;
            trigger_reason += "Activity Changed ";
        }
        if (temp_crit != last_temp_crit) {
            should_write = true;
            trigger_reason += "Temp State Changed ";
        }
    }

    if (should_write) {
        struct tm * ti;
        time_t t = pt.timestamp;
        ti = localtime(&t);
        char timeStr[30];
        sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d", ti->tm_year+1900, ti->tm_mon+1, ti->tm_mday, ti->tm_hour, ti->tm_min, ti->tm_sec);
        
        String actText = pt.activity == 2 ? "Fighting" : (pt.activity == 1 ? "Active" : "Idle");
        trigger_reason.trim();
        
        String row = String(pt.timestamp) + "," + String(timeStr) + "," + String(pt.rssi) + "," + zone + "," + String(pt.temp, 1) + "," + actText + "," + trigger_reason + "\n";
        server.sendContent(row);
        
        last_zone = zone;
        last_activity = pt.activity;
        last_temp_crit = temp_crit;
    }
  }
  
  server.sendContent(""); 
}

void setup() {
  Serial.begin(115200);
  pinMode(configButtonPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW); 

  setenv("TZ", "MMT-6:30", 1);
  tzset();

  preferences.begin("farm_config", false);
  wifi_ssid = preferences.getString("wifi_ssid", "YOUR_ROUTER_SSID"); 
  wifi_password = preferences.getString("wifi_pass", "YOUR_ROUTER_PASS"); 
  
  rssi_ranges = preferences.getString("rssi_ranges", "Milling,0,-25,#ffb3ba,0;Grazing,-26,-84,#baffc9,0;Out of Bounds,-85,-100,#bae1ff,1");

  Serial.println("\nPress BOOT Button within 3 seconds to change Router SSID & Password...");
  delay(3000);

  if (digitalRead(configButtonPin) == LOW) {
    while (Serial.available() > 0) Serial.read(); 
    Serial.print("Enter Router WiFi SSID: ");
    while (Serial.available() == 0) { delay(10); } 
    String newSSID = Serial.readStringUntil('\n');
    newSSID.trim();
    if (newSSID.length() > 0) {
      preferences.putString("wifi_ssid", newSSID);
      wifi_ssid = newSSID;
    } 

    while (Serial.available() > 0) Serial.read(); 
    Serial.print("Enter Router WiFi Password: ");
    while (Serial.available() == 0) { delay(10); } 
    String newPassword = Serial.readStringUntil('\n');
    newPassword.trim();
    if (newPassword.length() >= 8) {
      preferences.putString("wifi_pass", newPassword);
      wifi_password = newPassword;
    }
  }

  WiFi.mode(WIFI_STA); 
  WiFi.setSleep(false); 
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { 
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\n🌐 Dashboard IP: ");
    Serial.println(WiFi.localIP()); 
  } else {
    Serial.println("\nFailed to connect. Starting Backup AP...");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("FARM_BACKUP", "12345678");
  }

  Serial.println("\n=================================");
  Serial.print("📶 Channel: "); Serial.println(WiFi.channel());
  Serial.print("🔑 MAIN MAC: "); Serial.println(WiFi.macAddress()); 
  Serial.println("=================================\n");

  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);

  server.on("/", handleRoot);
  server.on("/control", handleControl);
  server.on("/save_ranges", HTTP_POST, handleSave);
  server.on("/download_csv", handleDownloadCSV); 
  server.on("/set_time", handleSetTime); 
  
  server.on("/ph", handlePhControl);
  server.on("/set_ph", handleSetPh);

  server.begin();
}

void loop() {
  server.handleClient(); 
}
