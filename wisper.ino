#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include <SH1106Wire.h>
#include <ArduinoJson.h>
#include <FS.h>

// ===== Global Variables for Display & Inactivity =====
bool screenOn = true;              // true if display is active
unsigned long lastActivityTime = 0;  // time of last button press (activity)

// ===== Normal UI Setup (Task Manager & Pomodoro) =====

// ----- WiFi Credentials -----
const char* wifiSSID = "Jadaun -4G";
const char* wifiPassword = "Jadaun1974";

// ----- OLED Setup for Normal UI -----
// SH1106 OLED via I2C (address 0x3C, SDA = GPIO4, SCL = GPIO5)
SH1106Wire display(0x3C, 4, 5);

// ----- NTP Client Setup -----
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000); // IST offset

// ----- Web Server Setup -----
ESP8266WebServer server(80);

// ----- Task Structure -----
struct Task {
  String title;
  String description;
  int startSeconds;  // seconds from midnight
  int duration;      // in seconds (end - start)
  String priority;
};
#define MAX_TASKS 20
Task tasks[MAX_TASKS];
int taskCount = 0;
int prevActiveTaskIndex = -1;  // for LED feedback on task change

// ----- Pomodoro Variables -----
bool pomodoroMode = false;     // toggled by pushbutton on GPIO12 (D6)
bool pomodoroActive = false;   // true when a Pomodoro session is running
bool pomodoroPaused = false;   // for pause/resume
int pomodoroWorkMinutes = 25;  // default work period (minutes)
int pomodoroBreakMinutes = 5;  // default break period (minutes)
int pomodoroSets = 4;          // default number of sets
int pomodoroSetIndex = 0;
bool isWorkPeriod = true;      // true: work; false: break
unsigned long pomodoroPeriodStart = 0;
unsigned long pomodoroPeriodDuration = 0; // in ms

// ----- Hardware Button Setup for Normal UI -----
const int modeSwitchPin = 14;    // Large time mode switch (D5)
bool modeLarge = false;
bool lastSwitchState = HIGH;
unsigned long lastToggleTime = 0;
const unsigned long buttonDebounceDelay = 500;

const int pomoSwitchPin = 12;    // Pomodoro mode switch (D6)
bool lastPomoSwitchState = HIGH;
unsigned long lastPomoToggleTime = 0;
const unsigned long pomoDebounceDelay = 500;

// ----- LED Setup -----
#define LED_PIN 15

// ----- Other Timing Variables -----
unsigned long startupTime = 0;
bool showWelcome = true;
unsigned long lastScrollUpdate = 0;
const int scrollDelay = 300; // in ms
int scrollOffsetTitle = 0;
int scrollOffsetDesc = 0;
const int displayWidth = 128;
int lastHour = -1;  // for hourly LED blink

// ===== Alternate UI (Animation Mode) Setup =====

// Global state for the animated eyes demo:
static const int SCREEN_WIDTH_ALT = 128;
static const int SCREEN_HEIGHT_ALT = 64;
static const int ref_eye_height = 40;
static const int ref_eye_width = 40;
static const int ref_space_between_eye = 10;
static const int ref_corner_radius = 10;
int left_eye_height = ref_eye_height;
int left_eye_width = ref_eye_width;
int left_eye_x = 32;
int left_eye_y = 32;
int right_eye_x = 32 + ref_eye_width + ref_space_between_eye;
int right_eye_y = 32;
int right_eye_height = ref_eye_height;
int right_eye_width = ref_eye_width;

int demo_mode = 1; // flag for demo cycling
static const int max_animation_index = 8;
int current_animation_index = 0;

// ----- Alternate UI Mode Toggle -----
// Use D7 (GPIO13) to toggle alternate UI mode.
const int altUIModeButtonPin = 13;
bool altUIMode = false;
bool lastAltButtonState = HIGH;
unsigned long lastAltToggleTime = 0;
const unsigned long altDebounceDelay = 300;

// For non-blocking animation timing in alternate mode:
unsigned long lastAnimationTime = 0;
const unsigned long animationInterval = 500; // 500ms between animation frames

// ===== Normal UI Helper Functions =====

void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
  }
}

int parseTime(String timeStr) {
  int colonIndex = timeStr.indexOf(':');
  if (colonIndex == -1) return 0;
  int hours = timeStr.substring(0, colonIndex).toInt();
  int minutes = timeStr.substring(colonIndex + 1).toInt();
  return hours * 3600 + minutes * 60;
}

int getSecondsFromTime(String timeStr) {
  int firstColon = timeStr.indexOf(':');
  int lastColon = timeStr.lastIndexOf(':');
  int hours = timeStr.substring(0, firstColon).toInt();
  int minutes = timeStr.substring(firstColon+1, lastColon).toInt();
  int seconds = timeStr.substring(lastColon+1).toInt();
  return hours * 3600 + minutes * 60 + seconds;
}

String formatSecondsToHMS(int totalSeconds) {
  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;
  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", hours, minutes, seconds);
  return String(buf);
}

void turnScreenOff() {
  display.setContrast(0);
  screenOn = false;
}

void turnScreenOn() {
  display.setContrast(255);
  screenOn = true;
  lastActivityTime = millis();
}

// ===== Persistence Functions =====

void saveState() {
  StaticJsonDocument<4096> doc;
  JsonArray tasksArray = doc.createNestedArray("tasks");
  for (int i = 0; i < taskCount; i++) {
    JsonObject t = tasksArray.createNestedObject();
    t["title"] = tasks[i].title;
    t["description"] = tasks[i].description;
    int hours = tasks[i].startSeconds / 3600;
    int minutes = (tasks[i].startSeconds % 3600) / 60;
    char buf[6];
    sprintf(buf, "%02d:%02d", hours, minutes);
    t["start"] = buf;
    int endSec = tasks[i].startSeconds + tasks[i].duration;
    int endH = endSec / 3600;
    int endM = (endSec % 3600) / 60;
    sprintf(buf, "%02d:%02d", endH, endM);
    t["end"] = buf;
    t["priority"] = tasks[i].priority;
  }
  JsonObject pomo = doc.createNestedObject("pomodoro");
  pomo["active"] = pomodoroActive;
  pomo["paused"] = pomodoroPaused;
  pomo["work"] = pomodoroWorkMinutes;
  pomo["break"] = pomodoroBreakMinutes;
  pomo["sets"] = pomodoroSets;
  pomo["setIndex"] = pomodoroSetIndex;
  
  File file = SPIFFS.open("/data.json", "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  serializeJson(doc, file);
  file.close();
}

void loadState() {
  if (!SPIFFS.exists("/data.json")) {
    Serial.println("No saved state found.");
    return;
  }
  File file = SPIFFS.open("/data.json", "r");
  if (!file) {
    Serial.println("Failed to open saved state file");
    return;
  }
  StaticJsonDocument<4096> doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println("Failed to parse saved state");
    file.close();
    return;
  }
  JsonArray tasksArray = doc["tasks"].as<JsonArray>();
  taskCount = 0;
  for (JsonObject t : tasksArray) {
    if (taskCount >= MAX_TASKS) break;
    tasks[taskCount].title = t["title"].as<String>();
    tasks[taskCount].description = t["description"].as<String>();
    String startStr = t["start"].as<String>();
    String endStr = t["end"].as<String>();
    tasks[taskCount].startSeconds = parseTime(startStr);
    int endSec = parseTime(endStr);
    tasks[taskCount].duration = endSec - tasks[taskCount].startSeconds;
    tasks[taskCount].priority = t["priority"].as<String>();
    taskCount++;
  }
  JsonObject pomo = doc["pomodoro"].as<JsonObject>();
  pomodoroActive = pomo["active"].as<bool>();
  pomodoroPaused = pomo["paused"].as<bool>();
  pomodoroWorkMinutes = pomo["work"].as<int>();
  pomodoroBreakMinutes = pomo["break"].as<int>();
  pomodoroSets = pomo["sets"].as<int>();
  pomodoroSetIndex = pomo["setIndex"].as<int>();
  file.close();
}

// ===== Web Server Handlers =====

String getFullUI() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Advanced Task Manager - Interactive UI</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Quicksand:wght@400;500;700&display=swap">
  <style>
    :root { --primary-color: #3498db; --secondary-color: #5dade2; --background-gradient: linear-gradient(135deg, #f9f9ff, #e0eafc); }
    body { font-family: 'Quicksand', sans-serif; background: var(--background-gradient); margin: 20px; color: #333; }
    #app { max-width: 600px; margin: 0 auto; background: #fff; padding: 20px; border-radius: 15px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
    h1, h2 { text-align: center; color: var(--primary-color); }
    form { display: flex; flex-direction: column; gap: 10px; margin-bottom: 20px; }
    form input, form textarea, form select, form button { padding: 10px; font-size: 1em; border: 1px solid #ddd; border-radius: 8px; }
    form button { background: var(--primary-color); color: #fff; border: none; cursor: pointer; }
    table { width: 100%; border-collapse: collapse; }
    th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
    th { background: var(--primary-color); color: #fff; }
    .edit-btn { margin-left: 5px; padding: 5px; background: var(--secondary-color); color: #fff; border: none; cursor: pointer; font-size: 0.8em; }
    #clear-btn, #json-btn, #startpomo-btn, #stoppomo-btn, #pausepomo-btn { margin-top: 10px; padding: 10px; background: var(--secondary-color); color: #fff; border: none; cursor: pointer; }
    #json-container { display: none; margin-top: 10px; }
    #json-container textarea { width: 100%; height: 100px; }
    /* Pomodoro Section */
    #pomo-section { margin-top: 20px; padding: 10px; border: 1px solid #ddd; border-radius: 8px; }
    #pomo-section label { display: block; margin-top: 10px; }
  </style>
</head>
<body>
  <div id="app">
    <h1>Advanced Task Manager</h1>
    <form id="task-form">
      <input type="text" id="task-title" placeholder="Task Title" required>
      <textarea id="task-desc" placeholder="Task Description"></textarea>
      <input type="time" id="task-start" required>
      <input type="time" id="task-end" required>
      <select id="task-priority" required>
        <option value="">Select Priority</option>
        <option value="High">High</option>
        <option value="Medium">Medium</option>
        <option value="Low">Low</option>
      </select>
      <button type="submit">Add Task</button>
    </form>
    <button id="clear-btn">Clear Timetable</button>
    <button id="json-btn">Add JSON Code</button>
    <div id="json-container">
      <textarea id="json-code" placeholder='{"tasks":[{"title":"Math","description":"Homework","start":"15:00","end":"16:00","priority":"High"}]}'></textarea>
      <button id="uploadjson-btn">Upload JSON Code</button>
    </div>
    <div id="pomo-section">
      <h2>Pomodoro Settings</h2>
      <label>Work Minutes (5-180):
        <input type="range" id="pomo-work" min="5" max="180" value="25">
        <span id="pomo-work-val">25</span>
      </label>
      <label>Break Minutes (2-30):
        <input type="range" id="pomo-break" min="2" max="30" value="5">
        <span id="pomo-break-val">5</span>
      </label>
      <label>Number of Sets:
        <input type="number" id="pomo-sets" min="1" max="10" value="4">
      </label>
      <button id="startpomo-btn">Start Pomodoro</button>
      <button id="pausepomo-btn">Pause/Resume Pomodoro</button>
      <button id="stoppomo-btn">Stop Pomodoro</button>
    </div>
    <h2>Your Tasks</h2>
    <table>
      <tr>
        <th>Title</th><th>Description</th><th>Start</th><th>End</th><th>Priority</th><th>Edit</th>
      </tr>
  )rawliteral";
  for (int i = 0; i < taskCount; i++) {
    int startH = tasks[i].startSeconds / 3600;
    int startM = (tasks[i].startSeconds % 3600) / 60;
    int endSec = tasks[i].startSeconds + tasks[i].duration;
    int endH = endSec / 3600;
    int endM = (endSec % 3600) / 60;
    char startBuf[6], endBuf[6];
    sprintf(startBuf, "%02d:%02d", startH, startM);
    sprintf(endBuf, "%02d:%02d", endH, endM);
    html += "<tr><td>" + tasks[i].title + "</td><td>" + tasks[i].description +
            "</td><td>" + String(startBuf) + "</td><td>" + String(endBuf) +
            "</td><td>" + tasks[i].priority + "</td><td><button class='edit-btn' onclick='editTask(" + String(i) + ")'>Edit</button></td></tr>";
  }
  html += R"rawliteral(
    </table>
  </div>
  <script>
    function editTask(index) {
      let newStart = prompt("Enter new start time (HH:MM) for task " + index + ":");
      let newEnd = prompt("Enter new end time (HH:MM) for task " + index + ":");
      if(newStart && newEnd) {
        let updateObj = { index: index, start: newStart, end: newEnd };
        let xhr = new XMLHttpRequest();
        xhr.open('POST', '/updatetask', true);
        xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
        xhr.onreadystatechange = function(){
          if(xhr.readyState === 4 && xhr.status === 200){
            location.reload();
          }
        };
        xhr.send('json=' + encodeURIComponent(JSON.stringify(updateObj)));
      }
    }
    
    document.getElementById('pomo-work').addEventListener('input', function(){
      document.getElementById('pomo-work-val').textContent = this.value;
    });
    document.getElementById('pomo-break').addEventListener('input', function(){
      document.getElementById('pomo-break-val').textContent = this.value;
    });
    
    document.getElementById('task-form').addEventListener('submit', function(e) {
      e.preventDefault();
      let title = document.getElementById('task-title').value;
      let desc = document.getElementById('task-desc').value;
      let start = document.getElementById('task-start').value;
      let end = document.getElementById('task-end').value;
      let priority = document.getElementById('task-priority').value;
      let taskObj = { tasks: [ { title: title, description: desc, start: start, end: end, priority: priority } ] };
      let xhr = new XMLHttpRequest();
      xhr.open('POST', '/upload', true);
      xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
      xhr.onreadystatechange = function(){
        if(xhr.readyState === 4 && xhr.status === 200){
          location.reload();
        }
      };
      xhr.send('json=' + encodeURIComponent(JSON.stringify(taskObj)));
    });
    
    document.getElementById('clear-btn').addEventListener('click', function() {
      let xhr = new XMLHttpRequest();
      xhr.open('GET', '/clear', true);
      xhr.onreadystatechange = function() {
        if(xhr.readyState === 4 && xhr.status === 200){
          location.reload();
        }
      };
      xhr.send();
    });
    
    document.getElementById('json-btn').addEventListener('click', function() {
      let container = document.getElementById('json-container');
      container.style.display = (container.style.display === "none") ? "block" : "none";
    });
    
    document.getElementById('uploadjson-btn').addEventListener('click', function() {
      let jsonCode = document.getElementById('json-code').value;
      let xhr = new XMLHttpRequest();
      xhr.open('POST', '/upload', true);
      xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
      xhr.onreadystatechange = function(){
        if(xhr.readyState === 4 && xhr.status === 200){
          location.reload();
        }
      };
      xhr.send('json=' + encodeURIComponent(JSON.stringify(jsonCode)));
    });
    
    document.getElementById('startpomo-btn').addEventListener('click', function() {
      let work = document.getElementById('pomo-work').value;
      let brk = document.getElementById('pomo-break').value;
      let sets = document.getElementById('pomo-sets').value;
      let pomoObj = { work: work, break: brk, sets: sets };
      let xhr = new XMLHttpRequest();
      xhr.open('POST', '/startpomodoro', true);
      xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
      xhr.onreadystatechange = function(){
        if(xhr.readyState === 4 && xhr.status === 200){
          alert("Pomodoro session started.");
        }
      };
      xhr.send('json=' + encodeURIComponent(JSON.stringify(pomoObj)));
    });
    
    document.getElementById('stoppomo-btn').addEventListener('click', function() {
      let xhr = new XMLHttpRequest();
      xhr.open('GET', '/stoppomodoro', true);
      xhr.onreadystatechange = function() {
        if(xhr.readyState === 4 && xhr.status === 200){
          alert("Pomodoro session stopped.");
          location.reload();
        }
      };
      xhr.send();
    });
    
    document.getElementById('pausepomo-btn').addEventListener('click', function() {
      let xhr = new XMLHttpRequest();
      xhr.open('GET', '/pausepomodoro', true);
      xhr.onreadystatechange = function() {
        if(xhr.readyState === 4 && xhr.status === 200){
          alert("Pomodoro pause toggled.");
          location.reload();
        }
      };
      xhr.send();
    });
  </script>
</body>
</html>
)rawliteral";
  return html;
}

void handleRoot() {
  Serial.println("Serving interactive UI...");
  String ui = getFullUI();
  server.send(200, "text/html", ui);
}

void handleUpload() {
  if (server.hasArg("json")) {
    String jsonData = server.arg("json");
    Serial.println("Received task JSON:");
    Serial.println(jsonData);
    
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, jsonData);
    if (error) {
      Serial.print("JSON Parse Error: ");
      Serial.println(error.c_str());
      server.send(400, "text/plain", "Invalid JSON data");
      return;
    }
    
    JsonArray taskArray = doc["tasks"].as<JsonArray>();
    for (JsonObject obj : taskArray) {
      if (taskCount >= MAX_TASKS) break;
      tasks[taskCount].title = obj["title"].as<String>();
      tasks[taskCount].description = obj["description"].as<String>();
      String startStr = obj["start"].as<String>();
      String endStr = obj["end"].as<String>();
      tasks[taskCount].startSeconds = parseTime(startStr);
      int endSec = parseTime(endStr);
      tasks[taskCount].duration = endSec - tasks[taskCount].startSeconds;
      tasks[taskCount].priority = obj["priority"].as<String>();
      
      Serial.print("Task ");
      Serial.print(taskCount);
      Serial.print(": ");
      Serial.println(tasks[taskCount].title);
      taskCount++;
    }
    saveState();
    server.send(200, "text/plain", "Tasks uploaded successfully!");
  } else {
    server.send(400, "text/plain", "No JSON data received");
  }
}

void handleClear() {
  taskCount = 0;
  pomodoroActive = false;
  pomodoroMode = false;
  pomodoroPaused = false;
  Serial.println("Timetable cleared.");
  saveState();
  server.send(200, "text/plain", "Timetable cleared!");
}

void handleStartPomodoro() {
  if (server.hasArg("json")) {
    String jsonData = server.arg("json");
    Serial.println("Received Pomodoro JSON:");
    Serial.println(jsonData);
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, jsonData);
    if (error) {
      Serial.print("Pomodoro JSON Parse Error: ");
      Serial.println(error.c_str());
      server.send(400, "text/plain", "Invalid Pomodoro JSON data");
      return;
    }
    pomodoroWorkMinutes = doc["work"].as<int>();
    pomodoroBreakMinutes = doc["break"].as<int>();
    pomodoroSets = doc["sets"].as<int>();
    
    pomodoroSetIndex = 0;
    isWorkPeriod = true;
    pomodoroPeriodDuration = pomodoroWorkMinutes * 60000UL;
    pomodoroPeriodStart = millis();
    pomodoroActive = true;
    pomodoroMode = true;
    pomodoroPaused = false;
    Serial.print("Pomodoro started: Work ");
    Serial.print(pomodoroWorkMinutes);
    Serial.print(" min, Break ");
    Serial.print(pomodoroBreakMinutes);
    Serial.print(" min, Sets ");
    Serial.println(pomodoroSets);
    blinkLED(1, 200);
    saveState();
    server.send(200, "text/plain", "Pomodoro session started!");
  } else {
    server.send(400, "text/plain", "No Pomodoro JSON data received");
  }
}

void handleStopPomodoro() {
  pomodoroActive = false;
  pomodoroMode = false;
  pomodoroPaused = false;
  Serial.println("Pomodoro session stopped.");
  blinkLED(2, 200);
  saveState();
  server.send(200, "text/plain", "Pomodoro session stopped!");
}

void handlePausePomodoro() {
  pomodoroPaused = !pomodoroPaused;
  Serial.println("Pomodoro pause toggled.");
  saveState();
  server.send(200, "text/plain", "Pomodoro pause toggled!");
}

void handleUpdateTask() {
  if (server.hasArg("json")) {
    String jsonData = server.arg("json");
    Serial.println("Received update task JSON:");
    Serial.println(jsonData);
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, jsonData);
    if (error) {
      Serial.print("Update Task JSON Parse Error: ");
      Serial.println(error.c_str());
      server.send(400, "text/plain", "Invalid update task JSON data");
      return;
    }
    int index = doc["index"].as<int>();
    if (index < 0 || index >= taskCount) {
      server.send(400, "text/plain", "Invalid task index");
      return;
    }
    String newStart = doc["start"].as<String>();
    String newEnd = doc["end"].as<String>();
    tasks[index].startSeconds = parseTime(newStart);
    int endSec = parseTime(newEnd);
    tasks[index].duration = endSec - tasks[index].startSeconds;
    
    Serial.print("Task ");
    Serial.print(index);
    Serial.println(" updated.");
    saveState();
    server.send(200, "text/plain", "Task updated successfully!");
  } else {
    server.send(400, "text/plain", "No JSON data received");
  }
}

// ===== Normal UI Display Functions =====

void drawWelcomeScreen() {
  display.clear();
  display.drawString(0, 0, "IP: " + WiFi.localIP().toString());
  display.drawString(0, 8, "PROJECT WISPER");
  display.drawString(0, 16, "By @Rudrajadaun1");
  display.drawString(0, 24, "Imperial time manager");
  display.display();
  delay(1000);
}

void drawNormalScreen() {
  String currentTimeStr = timeClient.getFormattedTime();
  int currentSeconds = getSecondsFromTime(currentTimeStr);
  
  int currentHour = currentTimeStr.substring(0,2).toInt();
  if (lastHour == -1) lastHour = currentHour;
  if (currentHour != lastHour) {
    blinkLED(1, 200);
    lastHour = currentHour;
  }
  
  int activeTaskIndex = -1;
  int remainingSec = 0;
  for (int i = 0; i < taskCount; i++) {
    int taskStart = tasks[i].startSeconds;
    int taskEnd = taskStart + tasks[i].duration;
    if (currentSeconds >= taskStart && currentSeconds < taskEnd) {
      activeTaskIndex = i;
      remainingSec = taskEnd - currentSeconds;
      break;
    }
  }
  
  if (prevActiveTaskIndex == -1 && activeTaskIndex != -1) {
    blinkLED(2, 200);
  }
  if (prevActiveTaskIndex != -1 && activeTaskIndex == -1) {
    blinkLED(3, 200);
  }
  prevActiveTaskIndex = activeTaskIndex;
  
  if (millis() - lastScrollUpdate > scrollDelay) {
    lastScrollUpdate = millis();
    scrollOffsetTitle++;
    scrollOffsetDesc++;
  }
  
  display.clear();
  display.drawString(2, 0, currentTimeStr);
  
  if (activeTaskIndex != -1) {
    String title = tasks[activeTaskIndex].title + " (" + tasks[activeTaskIndex].priority + ")";
    String desc = tasks[activeTaskIndex].description;
    String remainStr = formatSecondsToHMS(remainingSec);
    
    int titleWidth = display.getStringWidth(title);
    int titleX = 2;
    if (titleWidth > (displayWidth - 4)) {
      titleX = 2 - (scrollOffsetTitle % (titleWidth - (displayWidth - 4) + 10));
    }
    
    int descWidth = display.getStringWidth(desc);
    int descX = 2;
    if (descWidth > (displayWidth - 4)) {
      descX = 2 - (scrollOffsetDesc % (descWidth - (displayWidth - 4) + 10));
    }
    
    display.drawString(titleX, 10, "Task: " + title);
    display.drawString(descX, 20, "Desc: " + desc);
    display.drawString(2, 30, "Remain: " + remainStr);
  } else {
    display.drawString(2, 10, "No Task Active");
  }
  
  display.display();
}

void drawLargeTimeScreen() {
  String currentTimeStr = timeClient.getFormattedTime();
  display.clear();
  display.setFont(ArialMT_Plain_24);
  int w = display.getStringWidth(currentTimeStr);
  int x = (displayWidth - w) / 2;
  int y = (64 - 24) / 2;
  display.drawString(x, y, currentTimeStr);
  display.display();
  display.setFont(ArialMT_Plain_10);
}

void drawPomodoroScreen() {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  
  if (pomodoroPaused) {
    display.drawString(0, 0, "Pomodoro Paused");
    display.display();
    return;
  }
  
  unsigned long now = millis();
  unsigned long elapsed = now - pomodoroPeriodStart;
  if (elapsed >= pomodoroPeriodDuration) {
    blinkLED(5, 200);
    if (isWorkPeriod) {
      isWorkPeriod = false;
      pomodoroPeriodDuration = pomodoroBreakMinutes * 60000UL;
    } else {
      pomodoroSetIndex++;
      if (pomodoroSetIndex >= pomodoroSets) {
        pomodoroActive = false;
        pomodoroMode = false;
        blinkLED(2, 200);
        display.setFont(ArialMT_Plain_10);
        saveState();
        return;
      }
      isWorkPeriod = true;
      pomodoroPeriodDuration = pomodoroWorkMinutes * 60000UL;
    }
    pomodoroPeriodStart = now;
    elapsed = 0;
  }
  unsigned long remaining = pomodoroPeriodDuration - elapsed;
  String timeStr = formatSecondsToHMS(remaining / 1000);
  String periodStr = isWorkPeriod ? "WORK" : "BREAK";
  String setStr = "Set " + String(pomodoroSetIndex + 1) + "/" + String(pomodoroSets);
  
  display.drawString(0, 0, "Pomodoro Mode");
  display.drawString(0, 12, periodStr);
  display.drawString(0, 24, timeStr);
  display.drawString(0, 36, setStr);
  display.drawString(0, 48, "UI to cancel");
  display.display();
  display.setFont(ArialMT_Plain_10);
}

// ===== Alternate UI (Animation Mode) Functions =====

void draw_eyes(bool update = true) {
  display.clear();
  int x = left_eye_x - left_eye_width / 2;
  int y = left_eye_y - left_eye_height / 2;
  display.fillRect(x, y, left_eye_width, left_eye_height);
  x = right_eye_x - right_eye_width / 2;
  y = right_eye_y - right_eye_height / 2;
  display.fillRect(x, y, right_eye_width, right_eye_height);
  if(update) {
    display.display();
  }
}

void center_eyes(bool update = true) {
  left_eye_height = ref_eye_height;
  left_eye_width = ref_eye_width;
  right_eye_height = ref_eye_height;
  right_eye_width = ref_eye_width;
  
  left_eye_x = SCREEN_WIDTH_ALT/2 - ref_eye_width/2 - ref_space_between_eye/2;
  left_eye_y = SCREEN_HEIGHT_ALT/2;
  right_eye_x = SCREEN_WIDTH_ALT/2 + ref_eye_width/2 + ref_space_between_eye/2;
  right_eye_y = SCREEN_HEIGHT_ALT/2;
  
  draw_eyes(update);
}

void blink_alt(int speed = 12) {
  draw_eyes();
  for (int i = 0; i < 3; i++) {
    left_eye_height = left_eye_height - speed;
    right_eye_height = right_eye_height - speed;
    draw_eyes();
    delay(20);
  }
  for (int i = 0; i < 3; i++) {
    left_eye_height = left_eye_height + speed;
    right_eye_height = right_eye_height + speed;
    draw_eyes();
    delay(20);
  }
}

void sleep_alt() {
  left_eye_height = 2;
  right_eye_height = 2;
  draw_eyes();
}

void wakeup_alt() {
  sleep_alt();
  for (int h = 0; h <= ref_eye_height; h += 2) {
    left_eye_height = h;
    right_eye_height = h;
    draw_eyes();
    delay(20);
  }
}

void happy_eye() {
  center_eyes(false);
  int offset = ref_eye_height / 2;
  for (int i = 0; i < 10; i++) {
    display.fillRect(left_eye_x - left_eye_width/2 - 1, left_eye_y + offset, left_eye_width, 5);
    display.fillRect(right_eye_x - right_eye_width/2 - 1, right_eye_y + offset, right_eye_width, 5);
    offset -= 2;
    display.display();
    delay(20);
  }
  display.display();
  delay(1000);
}

void saccade(int direction_x, int direction_y) {
  int amp_x = 8;
  int amp_y = 6;
  int blink_amp = 8;
  left_eye_x += amp_x * direction_x;
  right_eye_x += amp_x * direction_x;
  left_eye_y += amp_y * direction_y;
  right_eye_y += amp_y * direction_y;
  left_eye_height -= blink_amp;
  right_eye_height -= blink_amp;
  draw_eyes();
  delay(20);
  left_eye_x -= amp_x * direction_x;
  right_eye_x -= amp_x * direction_x;
  left_eye_y -= amp_y * direction_y;
  right_eye_y -= amp_y * direction_y;
  left_eye_height += blink_amp;
  right_eye_height += blink_amp;
  draw_eyes();
  delay(20);
}

void move_big_eye(int direction) {
  int move_amp = 2;
  int blink_amp = 5;
  for (int i = 0; i < 3; i++) {
    left_eye_x += move_amp * direction;
    right_eye_x += move_amp * direction;
    left_eye_height -= blink_amp;
    right_eye_height -= blink_amp;
    if (direction > 0) {
      right_eye_height += 1;
      right_eye_width += 1;
    } else {
      left_eye_height += 1;
      left_eye_width += 1;
    }
    draw_eyes();
    delay(20);
  }
  for (int i = 0; i < 3; i++) {
    left_eye_x += move_amp * direction;
    right_eye_x += move_amp * direction;
    left_eye_height += blink_amp;
    right_eye_height += blink_amp;
    if (direction > 0) {
      right_eye_height += 1;
      right_eye_width += 1;
    } else {
      left_eye_height += 1;
      left_eye_width += 1;
    }
    draw_eyes();
    delay(20);
  }
  delay(1000);
  for (int i = 0; i < 3; i++) {
    left_eye_x -= move_amp * direction;
    right_eye_x -= move_amp * direction;
    left_eye_height -= blink_amp;
    right_eye_height -= blink_amp;
    if (direction > 0) {
      right_eye_height -= 1;
      right_eye_width -= 1;
    } else {
      left_eye_height -= 1;
      left_eye_width -= 1;
    }
    draw_eyes();
    delay(20);
  }
  for (int i = 0; i < 3; i++) {
    left_eye_x -= move_amp * direction;
    right_eye_x -= move_amp * direction;
    left_eye_height += blink_amp;
    right_eye_height += blink_amp;
    if (direction > 0) {
      right_eye_height -= 1;
      right_eye_width -= 1;
    } else {
      left_eye_height -= 1;
      left_eye_width -= 1;
    }
    draw_eyes();
    delay(20);
  }
  center_eyes();
}

void launch_animation_with_index(int anim_index) {
  if (anim_index > max_animation_index) {
    anim_index = 8;
  }
  switch (anim_index) {
    case 0: wakeup_alt(); break;
    case 1: center_eyes(true); break;
    case 2: move_big_eye(1); break;
    case 3: move_big_eye(-1); break;
    case 4: blink_alt(10); break;
    case 5: blink_alt(20); break;
    case 6: happy_eye(); break;
    case 7: sleep_alt(); break;
    case 8:
      center_eyes(true);
      for (int i = 0; i < 20; i++) {
        int dir_x = random(-1, 2);
        int dir_y = random(-1, 2);
        saccade(dir_x, dir_y);
      }
      break;
  }
}

// ===== Main Setup & Loop =====

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP8266 Advanced Task Manager ===");
  
  pinMode(LED_PIN, OUTPUT);
  blinkLED(3, 200);
  
  SPIFFS.begin();
  
  pinMode(modeSwitchPin, INPUT_PULLUP);
  lastSwitchState = digitalRead(modeSwitchPin);
  
  pinMode(pomoSwitchPin, INPUT_PULLUP);
  lastPomoSwitchState = digitalRead(pomoSwitchPin);
  
  pinMode(altUIModeButtonPin, INPUT_PULLUP);
  lastAltButtonState = digitalRead(altUIModeButtonPin);
  
  Serial.print("Connecting to WiFi: ");
  Serial.println(wifiSSID);
  WiFi.begin(wifiSSID, wifiPassword);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
  
  startupTime = millis();
  showWelcome = true;
  lastHour = -1;
  prevActiveTaskIndex = -1;
  lastActivityTime = millis();
  
  loadState();
  // Reset Pomodoro state on boot:
  pomodoroActive = false;
  pomodoroMode = false;
  pomodoroPaused = false;
  pomodoroSetIndex = 0;
  isWorkPeriod = true;
  
  server.on("/", HTTP_GET, handleRoot);
  server.on("/upload", HTTP_POST, handleUpload);
  server.on("/clear", HTTP_GET, handleClear);
  server.on("/startpomodoro", HTTP_POST, handleStartPomodoro);
  server.on("/stoppomodoro", HTTP_GET, handleStopPomodoro);
  server.on("/pausepomodoro", HTTP_GET, handlePausePomodoro);
  server.on("/updatetask", HTTP_POST, handleUpdateTask);
  server.begin();
  Serial.println("Web server started.");
  
  Serial.println("Initializing OLED...");
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.setFont(ArialMT_Plain_10);
  turnScreenOn();
  
  Serial.println("Starting NTP client...");
  timeClient.begin();
  timeClient.update();
  
  // Initialize lastAnimationTime for alternate mode
  lastAnimationTime = millis();
}

void loop() {
  server.handleClient();
  timeClient.update();
  
  // Check alternate UI mode button (D7)
  bool currentAltButtonState = digitalRead(altUIModeButtonPin);
  // Use falling edge detection for toggle:
  if (lastAltButtonState == HIGH && currentAltButtonState == LOW && (millis() - lastAltToggleTime > altDebounceDelay)) {
    altUIMode = !altUIMode;  // Toggle alternate mode
    lastAltToggleTime = millis();
    display.clear();
    turnScreenOn();
  }
  lastAltButtonState = currentAltButtonState;
  
  // Alternate UI Mode loop (non-blocking):
  if (altUIMode) {
    // Every animationInterval ms, launch next animation frame.
    if (millis() - lastAnimationTime >= animationInterval) {
      launch_animation_with_index(current_animation_index++);
      lastAnimationTime = millis();
      if (current_animation_index > max_animation_index)
        current_animation_index = 0;
    }
    lastActivityTime = millis();
    return; // Skip normal UI code.
  }
  
  // Normal UI code:
  if ((millis() - lastActivityTime > 180000) && screenOn) {
    turnScreenOff();
  }
  
  bool currentSwitchState = digitalRead(modeSwitchPin);
  if (lastSwitchState == HIGH && currentSwitchState == LOW && (millis() - lastToggleTime > buttonDebounceDelay)) {
    if (!screenOn) turnScreenOn();
    lastActivityTime = millis();
    modeLarge = !modeLarge;
    lastToggleTime = millis();
    delay(50);
  }
  lastSwitchState = currentSwitchState;
  
  bool currentPomoState = digitalRead(pomoSwitchPin);
  if (lastPomoSwitchState == HIGH && currentPomoState == LOW && (millis() - lastPomoToggleTime > pomoDebounceDelay)) {
    if (!screenOn) turnScreenOn();
    lastActivityTime = millis();
    if (pomodoroActive) {
      pomodoroActive = false;
      pomodoroMode = false;
    } else {
      pomodoroMode = !pomodoroMode;
      if (pomodoroMode) {
        display.clear();
        display.setFont(ArialMT_Plain_10);
        display.drawString(0, 0, "Pomodoro Mode");
        display.drawString(0, 12, "Visit UI to start");
        display.display();
      }
    }
    lastPomoToggleTime = millis();
    delay(50);
  }
  lastPomoSwitchState = currentPomoState;
  
  if (pomodoroActive) {
    drawPomodoroScreen();
    delay(100);
    return;
  }
  
  if (pomodoroMode && !pomodoroActive) {
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 0, "Pomodoro Mode");
    display.drawString(0, 12, "Visit UI to start");
    display.display();
    delay(100);
    return;
  }
  
  unsigned long now = millis();
  if (showWelcome && (now - startupTime < 5000)) {
    drawWelcomeScreen();
    return;
  } else {
    showWelcome = false;
  }
  
  String currentTimeStr = timeClient.getFormattedTime();
  int currentHour = currentTimeStr.substring(0,2).toInt();
  if (lastHour == -1) lastHour = currentHour;
  if (currentHour != lastHour) {
    blinkLED(1, 200);
    lastHour = currentHour;
  }
  
  if (modeLarge) {
    drawLargeTimeScreen();
  } else {
    drawNormalScreen();
  }
  
  delay(100);
}
