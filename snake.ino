/**
 * @file snake.ino 
 * @brief Snake Pro - A WiFi-controlled Snake clone for Arduino Nano RP2040 Connect
 * @brief This is a prototype project that is currently undergoing further development
 * @details Features an embedded web server acting as a smartphone controller, 
 * SD card level loading, captive portal DNS, and an ST7735 TFT display.
 */

#include <SPI.h>
#include <SD.h>
#include <WiFiNINA_Generic.h>   
#include <WiFiWebServer.h>  
#include <WiFiUdp_Generic.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "pitches.h"

// --- HARDWARE & PIN DEFINITIONS ---
#define TFT_CS    10 
#define TFT_RST   9
#define TFT_DC    8
#define SD_CS_PIN 4
#define BUZZER_PIN 3

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// --- NETWORK CONFIGURATION ---
const char ssid[] = "Snake_Pro_Network";        
const char pass[] = "12345678"; 
WiFiWebServer server(80); 
WiFiUDP dnsUDP;
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
IPAddress lastClientIP;

// --- GAME STATE & VARIABLES ---
enum GameState { MENU, PLAYING, GAMEOVER, WAIT_CONNECT, ERROR_STATE, LOADING };
GameState currentState = WAIT_CONNECT;

const int MAP_WIDTH = 20;
const int MAP_HEIGHT = 16;
const int MAX_SNAKE_LEN = MAP_WIDTH * MAP_HEIGHT; 
char gameMap[MAP_HEIGHT][MAP_WIDTH];
int snake_coord[MAX_SNAKE_LEN][2]; 
int snakeLen = 0;
char direction = 'R';

unsigned long lastMoveTime;
int speedInterval = 350; 
unsigned int currentLevel = 0;
const char* fileLvlName = "currLvl.txt";

// --- AUDIO CONFIGURATION ---
const int totalNotes = 16;
int noteDuration = 200; 
unsigned long lastNoteTime = 0;
unsigned long previousMillis = 0;
int currentNote = 0;
bool isPlaying = true;

const int melody[] = { NOTE_D5, NOTE_G4, NOTE_A4, NOTE_B4, NOTE_C5, NOTE_D5, NOTE_G4, NOTE_G4, NOTE_E5, NOTE_C5, NOTE_D5, NOTE_E5, NOTE_FS5, NOTE_G5, NOTE_G4, NOTE_G4 };
const int noteDurations[] = { 4, 8, 8, 8, 8, 4, 4, 4, 4, 8, 8, 8, 8, 4, 4, 4 };

// --- WEB CONTROLLER UI (HTML/JS/CSS) ---
const char controllerHTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Snake Pro Controller</title>
  <style>
    :root { --neon-green: #39ff14; --dark-bg: #121212; --panel-bg: #1e1e1e; }
    body { background-color: var(--dark-bg); color: white; font-family: 'Segoe UI', sans-serif; margin: 0; overflow: hidden; height: 100vh; display: flex; flex-direction: column; align-items: center; justify-content: center; }
    .snake-container { position: relative; padding: 20px; margin-bottom: 20px; }
    .snake-title { font-size: 3rem; font-weight: 900; letter-spacing: 5px; color: var(--neon-green); text-shadow: 0 0 10px var(--neon-green); z-index: 2; position: relative; }
    #menu { text-align: center; background: var(--panel-bg); padding: 40px; border-radius: 20px; border: 2px solid #333; box-shadow: 0 10px 30px rgba(0,0,0,0.5); }
    select { width: 100%; padding: 12px; margin: 20px 0; background: #333; color: white; border: 1px solid var(--neon-green); border-radius: 8px; font-size: 1rem; }
    .start-btn { background: var(--neon-green); color: black; border: none; padding: 15px 40px; font-weight: bold; border-radius: 30px; font-size: 1.2rem; cursor: pointer; transition: 0.3s; }
    .start-btn:active { transform: scale(0.9); opacity: 0.8; }
    #controller { display: none; text-align: center; }
    .d-pad { display: grid; grid-template-columns: repeat(3, 90px); grid-template-rows: repeat(3, 90px); gap: 15px; margin-top: 20px; }
    .btn { background-color: #2a2a2a; border: 2px solid #444; border-radius: 15px; color: white; font-size: 40px; cursor: pointer; display: flex; align-items: center; justify-content: center; box-shadow: 0 5px 0 #000; transition: 0.1s; }
    .btn:active { transform: translateY(4px); box-shadow: 0 1px 0 #000; background: #444; color: var(--neon-green); }
    .up { grid-column: 2; grid-row: 1; } .left { grid-column: 1; grid-row: 2; } .right { grid-column: 3; grid-row: 2; } .down { grid-column: 2; grid-row: 3; }
    .close-btn { margin-top: 50px; background: #ff4444; color: white; border: none; padding: 10px 20px; border-radius: 10px; font-weight: bold; cursor: pointer; }
  </style>
  <script>
    function sendCommand(dir) { fetch('/?dir=' + dir).catch(e => console.log("Retrying...")); }
    function startGame() { let lvl = document.getElementById('levelSelect').value; fetch('/?start=true&lvl=' + lvl).then(() => { document.getElementById('menu').style.display = 'none'; document.getElementById('controller').style.display = 'block'; }); }
    function closeGame() { fetch('/?stop=true').then(() => { document.getElementById('menu').style.display = 'block'; document.getElementById('controller').style.display = 'none'; }); }
    document.addEventListener('touchstart', function(e) { if (e.touches.length > 1) e.preventDefault(); }, { passive: false });
  </script>
</head>
<body>
  <div id="menu">
    <div class="snake-container"><div class="snake-title">SNAKE PRO</div></div>
    <label>SELECT LEVEL</label>
    <select id="levelSelect"><option value="1">GREEN FIELDS (EASY)</option><option value="2">THE LABYRINTH (HARD)</option><option value="3">SNAKE PIT (EXPERT)</option></select><br>
    <button class="start-btn" onclick="startGame()">START MISSION</button>
  </div>
  <div id="controller">
    <h2 style="color:var(--neon-green); margin-bottom:30px;">CONTROLLER</h2>
    <div class="d-pad">
      <div class="btn up" onclick="sendCommand('U')">▲</div><div class="btn left" onclick="sendCommand('L')">◀</div><div class="btn right" onclick="sendCommand('R')">▶</div><div class="btn down" onclick="sendCommand('D')">▼</div>
    </div>
    <button class="close-btn" onclick="closeGame()">CLOSE & EXIT</button>
  </div>
</body>
</html>
)=====";

// --- UTILITY & HARDWARE INITIALIZATION ---
void initDisplay() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(0,0);
  tft.setTextSize(1);
}

void printError(const char *msg) {
  initDisplay();
  tft.setCursor(1, 30);
  tft.setTextColor(ST77XX_RED);
  tft.println(msg);
}

// --- NETWORK FUNCTIONS ---
void respondToDNS() {
  int packetSize = dnsUDP.parsePacket();
  if (packetSize > 0) {
    byte packetBuffer[512];
    dnsUDP.read(packetBuffer, packetSize);
    packetBuffer[2] |= 0x80; 
    packetBuffer[3] |= 0x80; 
    packetBuffer[6] = 0; packetBuffer[7] = 1; 
    dnsUDP.beginPacket(dnsUDP.remoteIP(), dnsUDP.remotePort());
    dnsUDP.write(packetBuffer, packetSize);
    dnsUDP.write((uint8_t)0xc0); dnsUDP.write((uint8_t)0x0c);
    dnsUDP.write((uint8_t)0x00); dnsUDP.write((uint8_t)0x01);
    dnsUDP.write((uint8_t)0x00); dnsUDP.write((uint8_t)0x01);
    dnsUDP.write((uint8_t)0x00); dnsUDP.write((uint8_t)0x00);
    dnsUDP.write((uint8_t)0x00); dnsUDP.write((uint8_t)0x3c);
    dnsUDP.write((uint8_t)0x00); dnsUDP.write((uint8_t)0x04);
    dnsUDP.write(apIP[0]); dnsUDP.write(apIP[1]);
    dnsUDP.write(apIP[2]); dnsUDP.write(apIP[3]);
    dnsUDP.endPacket();
  }
}

void handleRoot() {
  IPAddress currentIP = server.client().remoteIP();
  if (currentIP != lastClientIP) {
    currentState = MENU;
    lastClientIP = currentIP;
  }

  if (server.hasArg("start")) {
    currentState = LOADING;
    int reqLvl = server.arg("lvl").toInt();
    currentLevel = (reqLvl > 0 && reqLvl <= 3) ? reqLvl : 1; 

    String filename = "/maps/level" + String(currentLevel) + ".txt";
    loadMap(filename.c_str()); 
    
    tft.fillScreen(ST77XX_BLACK);
    renderMap();
    server.send(200, "text/plain", "Game Started");
    currentState = PLAYING; 
    return;
  } 
  else if (server.hasArg("dir")) {
    String dirArg = server.arg("dir");
    if (dirArg == "U" && direction != 'D') direction = 'U';
    else if (dirArg == "D" && direction != 'U') direction = 'D';
    else if (dirArg == "L" && direction != 'R') direction = 'L';
    else if (dirArg == "R" && direction != 'L') direction = 'R';
    server.send(200, "text/plain", "OK");
    return;
  } 
  else if (server.hasArg("stop")) {
    memset(gameMap, 0, sizeof(gameMap));
    memset(snake_coord, 0, sizeof(snake_coord));
    direction = 'D';
    currentState = MENU; 
    tft.fillScreen(ST77XX_BLACK);
    server.send(200, "text/plain", "Stopped");
    return;
  }
  server.send_P(200, "text/html", controllerHTML);
}

// --- FILE SYSTEM (SD) FUNCTIONS ---
void loadMap(const char *map) {
  if (!SD.exists(map)) return;
  File myFile = SD.open(map, FILE_READ);
  if (myFile) {
    snakeLen = 0;
    int row = 0;
    while (myFile.available() && row < MAP_HEIGHT) {
      String line = myFile.readStringUntil('\n');
      line.trim(); 
      for (int col = 0; col < MAP_WIDTH && col < line.length(); col++) {
        gameMap[row][col] = line[col];
        if (gameMap[row][col] == 'S' && snakeLen < MAX_SNAKE_LEN) {
          snake_coord[snakeLen][0] = row;
          snake_coord[snakeLen][1] = col;
          snakeLen++;
        }
      }
      row++;
    }
    myFile.close();
  }
}

// --- GAME LOGIC & RENDERING ---
void renderMap() {
  int tileSize = 8;
  for (int y = 0; y < MAP_HEIGHT; y++) {
    for (int x = 0; x < MAP_WIDTH; x++) {
      uint16_t color;
      switch (gameMap[y][x]) {
        case '1': color = ST77XX_WHITE; break; 
        case 'S': color = ST77XX_GREEN; break; 
        case 'F': color = ST77XX_RED;   break; 
        default:  color = ST77XX_BLACK; break; 
      }
      tft.fillRect(x * tileSize, y * tileSize, tileSize, tileSize, color);
    }
  }
}

void moveSnake(int nY, int nX, int pY, int pX){
    if (nY < 0 || nY >= MAP_HEIGHT || nX < 0 || nX >= MAP_WIDTH || gameMap[nY][nX] == '1' || gameMap[nY][nX] == 'S') {
      currentState = GAMEOVER;
      return;
    }

    if (gameMap[nY][nX] == 'F') {
      gameMap[nY][nX] = 'S';
      if (snakeLen < MAX_SNAKE_LEN) {
        snake_coord[snakeLen][0] = nY;
        snake_coord[snakeLen][1] = nX;
        snakeLen++;
      }
      
      int attempts = 0;
      while (attempts < 100) {
        int ry = 1 + (rand() % (MAP_HEIGHT - 2));
        int rx = 1 + (rand() % (MAP_WIDTH - 2));
        if (gameMap[ry][rx] == '0' || gameMap[ry][rx] == 0) {
          gameMap[ry][rx] = 'F';
          break;
        }
        attempts++;
      }
    } else {
      gameMap[nY][nX] = 'S'; 
      gameMap[snake_coord[0][0]][snake_coord[0][1]] = '0';
      for (int i = 1; i < snakeLen; i++) {
        snake_coord[i - 1][0] = snake_coord[i][0];
        snake_coord[i - 1][1] = snake_coord[i][1];
      }
      snake_coord[snakeLen - 1][0] = nY;
      snake_coord[snakeLen - 1][1] = nX;
    }
}

void handleGame() {
    switch (direction) {
      case 'U': moveSnake(snake_coord[snakeLen - 1][0] - 1, snake_coord[snakeLen - 1][1], snake_coord[snakeLen - 1][0], snake_coord[snakeLen - 1][1]); break;
      case 'D': moveSnake(snake_coord[snakeLen - 1][0] + 1, snake_coord[snakeLen - 1][1], snake_coord[snakeLen - 1][0], snake_coord[snakeLen - 1][1]); break;
      case 'R': moveSnake(snake_coord[snakeLen - 1][0], snake_coord[snakeLen - 1][1] + 1, snake_coord[snakeLen - 1][0], snake_coord[snakeLen - 1][1]); break;
      case 'L': moveSnake(snake_coord[snakeLen - 1][0], snake_coord[snakeLen - 1][1] - 1, snake_coord[snakeLen - 1][0], snake_coord[snakeLen - 1][1]); break;
  }
}

// --- MAIN ARDUINO ROUTINES ---
void setup() {
  Serial.begin(115200);

  // Isolate SPI devices
  pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH);
  pinMode(SD_CS_PIN, OUTPUT); digitalWrite(SD_CS_PIN, HIGH);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  initDisplay();
  
  if (!SD.begin(SD_CS_PIN)) {
      printError("SD Init Failed.");
  }

  WiFi.beginAP(ssid, pass);  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500); timeout++;
  }

  dnsUDP.begin(DNS_PORT);
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", ""); 
  });
  server.on("/", handleRoot);
  server.begin();
  
  lastMoveTime = millis();
}

void loop() {
  respondToDNS();
  server.handleClient(); 

  switch (currentState) {
    case MENU:
      initDisplay();
      tft.setCursor(10, 35); 
      tft.setTextSize(2);
      tft.setTextColor(ST77XX_GREEN);
      tft.println("WAITING FOR\nCONTROLLER");
      break;

    case PLAYING:
      if (millis() - lastMoveTime >= speedInterval) {
        handleGame(); 
        renderMap();  
        lastMoveTime = millis();
      }
      break;

    case GAMEOVER:
      initDisplay();
      tft.setCursor(20, 50);
      tft.setTextColor(ST77XX_RED);
      tft.setTextSize(2);
      tft.println("GAME OVER");
      delay(2000); 
      currentState = WAIT_CONNECT; 
      break;

    case WAIT_CONNECT:
      initDisplay();
      tft.setCursor(1, 50);
      tft.setTextColor(ST77XX_GREEN);
      tft.println("Connect to WiFi...");
      delay(250);
      break;
      
    default:
      break;
  }
}