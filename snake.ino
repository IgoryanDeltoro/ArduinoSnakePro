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
#define TFT_CS     10
#define TFT_RST    9
#define TFT_DC     8
#define SD_CS_PIN  4
#define BUZZER_PIN 3

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// --- NETWORK CONFIGURATION ---
const char ssid[] = "Snake_Pro_Network";
const char pass[] = "12345678";
WiFiWebServer server(80);
WiFiUDP dnsUDP;
const uint16_t DNS_PORT       = 53;
const int      DNS_HEADER_LEN = 12;   
const int      DNS_MAX_PACKET = 512;  
IPAddress apIP(192, 168, 4, 1);
IPAddress lastClientIP;

// --- GAME STATE & VARIABLES ---
enum GameState { MENU, PLAYING, GAMEOVER, WAIT_CONNECT, ERROR_STATE, LOADING };
GameState     currentState = WAIT_CONNECT;
bool          needsRedraw  = true;     
unsigned long stateSince   = 0;        
const char   *errorMsg     = "";       

const int MAP_WIDTH     = 20;
const int MAP_HEIGHT    = 16;
const int TILE_SIZE     = 8;           
const int MAX_SNAKE_LEN = MAP_WIDTH * MAP_HEIGHT;
char gameMap[MAP_HEIGHT][MAP_WIDTH];
int  snake_coord[MAX_SNAKE_LEN][2];    
int  snakeLen  = 0;
char direction = 'R';                  
char movedDir  = 'R';                  

unsigned long       lastMoveTime  = 0;
const unsigned long speedInterval = 350;   
const unsigned long GAMEOVER_MS   = 2000;
unsigned int currentLevel = 0;

// --- AUDIO CONFIGURATION (not used yet) ---
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

// --- STATE HELPERS ---
void setState(GameState s) {
  currentState = s;
  stateSince   = millis();
  needsRedraw  = true;
}

// --- DISPLAY HELPERS ---
void initDisplay() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(0, 0);
  tft.setTextSize(1);
}

void showMessage(const char *msg, uint8_t size, int16_t x, int16_t y, uint16_t color) {
  initDisplay();
  tft.setCursor(x, y);
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.println(msg);
  needsRedraw = false;
}

static inline bool isEmptyCell(char c) {
  return c == '0' || c == 0 || c == ' ';
}

uint16_t tileColor(char c) {
  switch (c) {
    case '1': return ST77XX_WHITE;
    case 'S': return ST77XX_GREEN;
    case 'F': return ST77XX_RED;
    default:  return ST77XX_BLACK;
  }
}

void drawTile(int y, int x) {
  tft.fillRect(x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE, TILE_SIZE, tileColor(gameMap[y][x]));
}

void renderMap() { 
  for (int y = 0; y < MAP_HEIGHT; y++)
    for (int x = 0; x < MAP_WIDTH; x++)
      drawTile(y, x);
}

// --- NETWORK FUNCTIONS ---
void respondToDNS() {
  int packetSize = dnsUDP.parsePacket();
  if (packetSize <= 0) return;

  if (packetSize < DNS_HEADER_LEN || packetSize > DNS_MAX_PACKET) return; 

  byte buf[DNS_MAX_PACKET];
  int len = dnsUDP.read(buf, packetSize);
  if (len < DNS_HEADER_LEN) return;
  if (buf[2] & 0x80) return;                

  int pos = DNS_HEADER_LEN;
  while (pos < len && buf[pos] != 0) {
    if (buf[pos] & 0xC0) return;             
    pos += buf[pos] + 1;                     
  }
  pos += 1 + 4;                             
  if (pos > len) return;                     

  buf[2] |= 0x80;                           
  buf[3] |= 0x80;                           
  buf[4] = 0;  buf[5] = 1;                  
  buf[6] = 0;  buf[7] = 1;                   
  buf[8] = buf[9] = buf[10] = buf[11] = 0;  

  const uint8_t answer[] = {
    0xC0, 0x0C,             
    0x00, 0x01,              
    0x00, 0x01,             
    0x00, 0x00, 0x00, 0x3C,  
    0x00, 0x04              
  };
  const uint8_t ip[4] = { apIP[0], apIP[1], apIP[2], apIP[3] };

  dnsUDP.beginPacket(dnsUDP.remoteIP(), dnsUDP.remotePort());
  dnsUDP.write(buf, pos);                  
  dnsUDP.write(answer, sizeof(answer));
  dnsUDP.write(ip, sizeof(ip));
  dnsUDP.endPacket();
}

// --- FILE SYSTEM (SD) FUNCTIONS ---
bool loadMap(const char *path) {
  memset(gameMap, '0', sizeof(gameMap));
  snakeLen = 0;

  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  int row = 0, col = 0;
  while (f.available() && row < MAP_HEIGHT) {
    char c = f.read();
    if (c == '\r') continue;                
    if (c == '\n') { row++; col = 0; continue; }
    if (col >= MAP_WIDTH) continue;          
    gameMap[row][col] = c;
    if (c == 'S' && snakeLen < MAX_SNAKE_LEN) {
      snake_coord[snakeLen][0] = row;
      snake_coord[snakeLen][1] = col;
      snakeLen++;
    }
    col++;
  }
  f.close();
  return snakeLen > 0;                       
}

// --- GAME LOGIC ---
bool hasFood() {
  for (int y = 0; y < MAP_HEIGHT; y++)
    for (int x = 0; x < MAP_WIDTH; x++)
      if (gameMap[y][x] == 'F') return true;
  return false;
}

void spawnFood() {
  for (int attempts = 0; attempts < 100; attempts++) {
    int ry = 1 + random(MAP_HEIGHT - 2);   
    int rx = 1 + random(MAP_WIDTH - 2);
    if (isEmptyCell(gameMap[ry][rx])) {
      gameMap[ry][rx] = 'F';
      drawTile(ry, rx);
      return;
    }
  }
}

void gameOver() {
  setState(GAMEOVER);
}

void moveSnake(int nY, int nX) {
  if (nY < 0 || nY >= MAP_HEIGHT || nX < 0 || nX >= MAP_WIDTH || gameMap[nY][nX] == '1') {
    gameOver();
    return;
  }

  int tailY = snake_coord[0][0];
  int tailX = snake_coord[0][1];

  if (gameMap[nY][nX] == 'S' && !(nY == tailY && nX == tailX)) {
    gameOver();
    return;
  }

  if (gameMap[nY][nX] == 'F') {
    if (snakeLen < MAX_SNAKE_LEN) {
      snake_coord[snakeLen][0] = nY;
      snake_coord[snakeLen][1] = nX;
      snakeLen++;
    }
    gameMap[nY][nX] = 'S';
    drawTile(nY, nX);
    spawnFood();
  } else {
    gameMap[tailY][tailX] = '0';
    drawTile(tailY, tailX);
    for (int i = 1; i < snakeLen; i++) {
      snake_coord[i - 1][0] = snake_coord[i][0];
      snake_coord[i - 1][1] = snake_coord[i][1];
    }
    snake_coord[snakeLen - 1][0] = nY;
    snake_coord[snakeLen - 1][1] = nX;
    gameMap[nY][nX] = 'S';
    drawTile(nY, nX);
  }
}

void handleGame() {
  if (snakeLen <= 0) { gameOver(); return; } 
  int hY = snake_coord[snakeLen - 1][0];
  int hX = snake_coord[snakeLen - 1][1];
  switch (direction) {
    case 'U': hY--; break;
    case 'D': hY++; break;
    case 'L': hX--; break;
    case 'R': hX++; break;
  }
  movedDir = direction;                     
  moveSnake(hY, hX);
}

// --- HTTP HANDLER ---
void handleRoot() {
  IPAddress currentIP = server.client().remoteIP();
  if (currentIP != lastClientIP) {
    lastClientIP = currentIP;
    if (currentState == WAIT_CONNECT) setState(MENU);
  }

  if (server.hasArg("start")) {
    int reqLvl = server.arg("lvl").toInt();
    currentLevel = (reqLvl >= 1 && reqLvl <= 3) ? reqLvl : 1;

    char filename[24];                                           
    snprintf(filename, sizeof(filename), "/maps/level%u.txt", currentLevel);

    if (!loadMap(filename)) {                                   
      errorMsg = "Map load failed";
      setState(ERROR_STATE);
      server.send(500, "text/plain", "Map load failed");
      return;
    }

    randomSeed(micros());                    
    direction = movedDir = 'R';          
    if (!hasFood()) spawnFood();          

    tft.fillScreen(ST77XX_BLACK);
    renderMap();
    lastMoveTime = millis();
    setState(PLAYING);
    needsRedraw = false;                 
    server.send(200, "text/plain", "Game Started");
    return;
  }

  if (server.hasArg("dir")) {
    char d = server.arg("dir").charAt(0);   
    if      (d == 'U' && movedDir != 'D') direction = 'U';
    else if (d == 'D' && movedDir != 'U') direction = 'D';
    else if (d == 'L' && movedDir != 'R') direction = 'L';
    else if (d == 'R' && movedDir != 'L') direction = 'R';
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("stop")) {
    memset(gameMap, 0, sizeof(gameMap));
    memset(snake_coord, 0, sizeof(snake_coord));
    snakeLen  = 0;                       
    direction = movedDir = 'R';             
    setState(MENU);
    server.send(200, "text/plain", "Stopped");
    return;
  }

  server.send_P(200, "text/html", controllerHTML);
}

// --- MAIN ARDUINO ROUTINES ---
void setup() {
  Serial.begin(115200);

  pinMode(TFT_CS, OUTPUT);    digitalWrite(TFT_CS, HIGH);
  pinMode(SD_CS_PIN, OUTPUT); digitalWrite(SD_CS_PIN, HIGH);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  initDisplay();

  setState(WAIT_CONNECT);

  if (!SD.begin(SD_CS_PIN)) {
    errorMsg = "SD Init Failed.";
    setState(ERROR_STATE);
  }

  if (WiFi.beginAP(ssid, pass) != WL_AP_LISTENING) {
    errorMsg = "WiFi AP failed.";
    setState(ERROR_STATE);
  }

  dnsUDP.begin(DNS_PORT);
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  respondToDNS();
  server.handleClient();

  switch (currentState) {
    case WAIT_CONNECT:
      if (needsRedraw) showMessage("Connect to WiFi...", 1, 1, 50, ST77XX_GREEN); 
      break;

    case MENU:
      if (needsRedraw) showMessage("WAITING FOR\nCONTROLLER", 2, 10, 35, ST77XX_GREEN);  
      break;

    case PLAYING:
      if (millis() - lastMoveTime >= speedInterval) {
        lastMoveTime = millis();
        handleGame();                       
      }
      break;

    case GAMEOVER:
      if (needsRedraw) showMessage("GAME OVER", 2, 20, 50, ST77XX_RED);
      if (millis() - stateSince >= GAMEOVER_MS) setState(MENU); 
      break;

    case ERROR_STATE:
      if (needsRedraw) showMessage(errorMsg, 1, 1, 30, ST77XX_RED); 
      break;

    default:
      break;
  }
}
