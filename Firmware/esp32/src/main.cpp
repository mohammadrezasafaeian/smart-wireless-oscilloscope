#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "config.h"
#include "structures.h"
#include "state.h"

const char* SSID = "SmartScope-Pro";
const char* PASSWORD = "12345678";

// External declarations
extern void setup_spi_slave();
extern bool handle_spi_transaction_nonblocking();
extern uint8_t* get_rx_buffer();
extern bool is_spi_data_ready();
extern void parse_measurements(String line);
extern String build_measurement_json(MeasData& d);

// ==================== GLOBAL INSTANCES ====================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
HardwareSerial SerialSTM(2);

// Global state
MeasData meas = {0};
SignalStats sigStats = {0};

// ==================== TIMING CONFIGURATION ====================
// Adjust these for speed vs stability tradeoff
static const uint32_t BINARY_INTERVAL_NORMAL = 50;    // 20 FPS when healthy
static const uint32_t BINARY_INTERVAL_SLOW = 150;     // ~7 FPS when stressed
static const uint32_t MEAS_INTERVAL = 500;            // 2 Hz for measurements
static const uint32_t BROADCAST_INTERVAL = 200;       // State sync throttle
static const uint32_t ERROR_RECOVERY_TIME = 2000;     // 2 sec to recover
static const uint32_t MAX_ERRORS_PER_CLIENT = 3;      // Before marking slow
static const uint32_t MAX_CONSECUTIVE_ERRORS = 10;    // Before system pause

// ==================== CLIENT TRACKING ====================
struct ClientInfo {
    uint32_t id;
    uint32_t lastPing;
    uint32_t lastBinary;
    uint32_t lastText;
    uint8_t errorCount;
    uint8_t speed;  // 0=fast, 1=slow, 2=paused
    bool active;
};
ClientInfo clients[MAX_WS_CLIENTS] = {0};

// ==================== SYSTEM STATE ====================
static uint32_t lastBroadcastTime = 0;
static bool pendingBroadcast = false;
static uint32_t consecutiveErrors = 0;
static uint32_t lastErrorTime = 0;
static uint8_t systemSpeed = 0;  // 0=fast, 1=slow, 2=paused

// ==================== HELPER FUNCTIONS ====================
int findClientSlot(uint32_t id) {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (clients[i].active && clients[i].id == id) {
            return i;
        }
    }
    return -1;
}

int getFreeClientSlot() {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!clients[i].active) {
            return i;
        }
    }
    return -1;
}

void removeClient(uint32_t id) {
    int slot = findClientSlot(id);
    if (slot >= 0) {
        clients[slot].active = false;
        Serial.printf("   Removed client #%u from slot %d\n", id, slot);
    }
}

int countActiveClients() {
    int count = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (clients[i].active) count++;
    }
    return count;
}

// Get appropriate interval based on client and system speed
uint32_t getClientInterval(int slot) {
    uint8_t speed = max(clients[slot].speed, systemSpeed);
    switch (speed) {
        case 0: return BINARY_INTERVAL_NORMAL;
        case 1: return BINARY_INTERVAL_SLOW;
        default: return 1000;  // Paused - 1 FPS
    }
}

// ==================== BUILD STATE JSON ====================
String buildStateJson() {
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        "{\"type\":\"state\",\"displayMode\":%d,\"frequency\":%lu,\"timebase\":%lu,\"duty\":%u,\"running\":%s}",
        sharedState.displayMode,
        sharedState.frequency,
        sharedState.timebase,
        sharedState.dutyCycle,
        sharedState.running ? "true" : "false"
    );
    return String(buffer);
}

// ==================== BROADCAST STATE TO ALL CLIENTS ====================
void broadcastState() {
    if (ws.count() == 0) return;
    if (systemSpeed >= 2) {
        pendingBroadcast = true;
        return;
    }
    
    String stateJson = buildStateJson();
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!clients[i].active) continue;
        if (clients[i].speed >= 2) continue;
        
        AsyncWebSocketClient* client = ws.client(clients[i].id);
        if (client == nullptr || client->status() != WS_CONNECTED) {
            clients[i].active = false;
            continue;
        }
        
        client->text(stateJson);
    }
    
    Serial.printf("📢 Broadcast: %s\n", stateJson.c_str());
}

// ==================== SEND BINARY TO CLIENTS ====================
void sendBinaryToClients(uint8_t* buffer, size_t len) {
    if (ws.count() == 0) return;
    if (systemSpeed >= 2) return;
    
    uint32_t now = millis();
    
    // Add 4-byte header
    static uint8_t sendBuffer[BUFFER_SIZE + 4];
    sendBuffer[0] = (uint8_t)sharedState.displayMode;
    sendBuffer[1] = sharedState.running ? 1 : 0;
    sendBuffer[2] = systemSpeed;
    sendBuffer[3] = 0;
    memcpy(sendBuffer + 4, buffer, len);
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!clients[i].active) continue;
        if (clients[i].speed >= 2) continue;
        
        uint32_t interval = getClientInterval(i);
        if ((now - clients[i].lastBinary) < interval) {
            continue;
        }
        
        AsyncWebSocketClient* client = ws.client(clients[i].id);
        if (client == nullptr) {
            clients[i].active = false;
            continue;
        }
        
        if (client->status() != WS_CONNECTED) {
            clients[i].active = false;
            continue;
        }
        
        client->binary(sendBuffer, len + 4);
        clients[i].lastBinary = now;
    }
}

// ==================== SEND MEASUREMENTS TO ALL CLIENTS ====================
void sendMeasurementsToClients(const String& json) {
    if (systemSpeed >= 2) return;
    
    uint32_t now = millis();
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!clients[i].active) continue;
        if (clients[i].speed >= 2) continue;
        
        if ((now - clients[i].lastText) < MEAS_INTERVAL) {
            continue;
        }
        
        AsyncWebSocketClient* client = ws.client(clients[i].id);
        if (client == nullptr || client->status() != WS_CONNECTED) {
            clients[i].active = false;
            continue;
        }
        
        client->text(json);
        clients[i].lastText = now;
    }
}

// ==================== PING CLIENTS FOR KEEP-ALIVE ====================
void pingClients() {
    static uint32_t lastPingTime = 0;
    uint32_t now = millis();
    
    if ((now - lastPingTime) < WS_PING_INTERVAL) return;
    lastPingTime = now;
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!clients[i].active) continue;
        
        if ((now - clients[i].lastPing) > WS_TIMEOUT_MS) {
            Serial.printf("⚠ Client #%u timeout\n", clients[i].id);
            AsyncWebSocketClient* client = ws.client(clients[i].id);
            if (client != nullptr) client->close();
            clients[i].active = false;
            continue;
        }
        
        AsyncWebSocketClient* client = ws.client(clients[i].id);
        if (client != nullptr && client->status() == WS_CONNECTED) {
            client->ping();
        }
    }
}

// ==================== PROCESS COMMAND & SYNC STATE ====================
void processCommand(const char* cmd, uint32_t senderId) {
    bool stateChanged = false;
    
    if (cmd[0] == 'X') {
        int mode = (cmd[1] == ':') ? atoi(cmd + 2) : atoi(cmd + 1);
        if (mode == 0 && sharedState.displayMode != MODE_TIME_DOMAIN) {
            sharedState.displayMode = MODE_TIME_DOMAIN;
            stateChanged = true;
            Serial.println("→ Mode: TIME");
        } else if (mode == 1 && sharedState.displayMode != MODE_FREQ_DOMAIN) {
            sharedState.displayMode = MODE_FREQ_DOMAIN;
            stateChanged = true;
            Serial.println("→ Mode: FFT");
        }
    }
    else if (cmd[0] == 'F') {
        uint32_t val = atoi((cmd[1] == ':') ? cmd + 2 : cmd + 1);
        if (val > 0 && val != sharedState.frequency) {
            sharedState.frequency = val;
            stateChanged = true;
        }
    }
    else if (cmd[0] == 'T') {
        uint32_t val = atoi((cmd[1] == ':') ? cmd + 2 : cmd + 1);
        if (val > 0 && val != sharedState.timebase) {
            sharedState.timebase = val;
            stateChanged = true;
        }
    }
    else if (cmd[0] == 'D') {
        uint8_t val = atoi((cmd[1] == ':') ? cmd + 2 : cmd + 1);
        if (val > 0 && val <= 100 && val != sharedState.dutyCycle) {
            sharedState.dutyCycle = val;
            stateChanged = true;
        }
    }
    else if (cmd[0] == 'M' || cmd[0] == 'E') {
        stateChanged = true;
    }
    else if (strncmp(cmd, "RUN", 3) == 0 && !sharedState.running) {
        sharedState.running = true;
        stateChanged = true;
    }
    else if (strncmp(cmd, "STOP", 4) == 0 && sharedState.running) {
        sharedState.running = false;
        stateChanged = true;
    }
    else if (strncmp(cmd, "RESET", 5) == 0) {
        meas.reset();
        sigStats.reset();
        stateChanged = true;
    }
    
    // Forward to STM32
    char stmCmd[32];
    if ((cmd[0] == 'X' || cmd[0] == 'F' || cmd[0] == 'T' || 
         cmd[0] == 'D' || cmd[0] == 'M' || cmd[0] == 'E') && cmd[1] != ':') {
        snprintf(stmCmd, sizeof(stmCmd), "%c:%s", cmd[0], cmd + 1);
    } else {
        strncpy(stmCmd, cmd, sizeof(stmCmd) - 1);
        stmCmd[sizeof(stmCmd) - 1] = '\0';
    }
    SerialSTM.printf("%s\n", stmCmd);
    
    if (stateChanged) {
        meas.reset();
        sigStats.reset();
        sharedState.lastChangeTime = millis();
        
        uint32_t now = millis();
        if (systemSpeed < 2 && (now - lastBroadcastTime) >= BROADCAST_INTERVAL) {
            broadcastState();
            lastBroadcastTime = now;
            pendingBroadcast = false;
        } else {
            pendingBroadcast = true;
        }
    }
}

// ==================== WEBSOCKET EVENT HANDLER ====================
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
    
    switch (type) {
        case WS_EVT_CONNECT: {
            uint32_t clientId = client->id();
            Serial.printf("✓ Client #%u connected from %s\n", 
                          clientId, client->remoteIP().toString().c_str());
            
            int slot = getFreeClientSlot();
            if (slot >= 0) {
                clients[slot].id = clientId;
                clients[slot].lastPing = millis();
                clients[slot].lastBinary = 0;
                clients[slot].lastText = 0;
                clients[slot].errorCount = 0;
                clients[slot].speed = 0;
                clients[slot].active = true;
            } else {
                Serial.println("⚠ No free slots!");
                client->close();
                return;
            }
            
            // Reset system speed for new connection
            systemSpeed = 0;
            consecutiveErrors = 0;
            
            char initJson[384];
            snprintf(initJson, sizeof(initJson),
                "{\"type\":\"init\",\"version\":\"2.6\",\"clientId\":%u,"
                "\"fftParams\":{\"sampleRate\":%u,\"fftSize\":%u,\"displayBins\":%u,\"maxFreq\":%u,\"hzPerBin\":%.2f}}",
                clientId, FFT_SAMPLE_RATE, FFT_SIZE, DISPLAY_BINS, 
                (uint32_t)MAX_DISPLAY_FREQ, HZ_PER_BIN
            );
            client->text(initJson);
            client->text(buildStateJson());
            
            Serial.printf("   Slot %d | Total: %d\n", slot, ws.count());
            break;
        }
        
        case WS_EVT_DISCONNECT: {
            Serial.printf("✗ Client #%u disconnected\n", client->id());
            removeClient(client->id());
            if (countActiveClients() == 0) {
                systemSpeed = 0;
                consecutiveErrors = 0;
            }
            break;
        }
        
        case WS_EVT_ERROR: {
            int slot = findClientSlot(client->id());
            if (slot >= 0) {
                clients[slot].errorCount++;
                consecutiveErrors++;
                lastErrorTime = millis();
                
                // Gradual slowdown per client
                if (clients[slot].errorCount >= MAX_ERRORS_PER_CLIENT) {
                    if (clients[slot].speed < 2) {
                        clients[slot].speed++;
                        Serial.printf("⚠ Client #%u slowed to level %d\n", 
                            client->id(), clients[slot].speed);
                    }
                }
                
                // System slowdown
                if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                    if (systemSpeed < 2) {
                        systemSpeed++;
                        Serial.printf("🔻 System speed reduced to level %d\n", systemSpeed);
                    }
                }
            }
            break;
        }
        
        case WS_EVT_PONG: {
            int slot = findClientSlot(client->id());
            if (slot >= 0) {
                clients[slot].lastPing = millis();
                // Successful pong = client is healthy, speed up
                if (clients[slot].errorCount > 0) clients[slot].errorCount--;
                if (clients[slot].speed > 0 && clients[slot].errorCount == 0) {
                    clients[slot].speed--;
                }
                consecutiveErrors = 0;
            }
            break;
        }
        
        case WS_EVT_DATA: {
            AwsFrameInfo *info = (AwsFrameInfo*)arg;
            
            if (info->final && info->index == 0 && info->len == len &&
                info->opcode == WS_TEXT && len > 0 && len < 255) {
                
                char cmd[256];
                memcpy(cmd, data, len);
                cmd[len] = '\0';
                
                uint32_t clientId = client->id();
                int slot = findClientSlot(clientId);
                
                // Successful receive = healthy
                if (slot >= 0) {
                    if (clients[slot].errorCount > 0) clients[slot].errorCount--;
                    if (clients[slot].speed > 0 && clients[slot].errorCount == 0) {
                        clients[slot].speed--;
                    }
                }
                consecutiveErrors = 0;
                
                Serial.printf("← #%u: %s\n", clientId, cmd);
                
                if (strcmp(cmd, "PING") == 0) {
                    client->text("{\"type\":\"pong\"}");
                    if (slot >= 0) clients[slot].lastPing = millis();
                    return;
                }
                
                if (strcmp(cmd, "GETSTATE") == 0) {
                    client->text(buildStateJson());
                    return;
                }
                
                processCommand(cmd, clientId);
            }
            break;
        }
    }
}

// ==================== UART DATA HANDLER ====================
void handle_uart() {
    static String uartBuffer = "";
    static uint32_t lastMeasSend = 0;
    
    while (SerialSTM.available()) {
        char c = SerialSTM.read();
        
        if (c == '\n') {
            uartBuffer.trim();
            
            if (uartBuffer.length() > 0) {
                parse_measurements(uartBuffer);
                
                if (meas.valid && meas.frequency_hz > 0) {
                    sigStats.updateStability(meas.frequency_hz);
                }
                
                uint32_t now = millis();
                if (systemSpeed < 2 && ws.count() > 0 && meas.valid && 
                    (now - lastMeasSend) >= MEAS_INTERVAL) {
                    lastMeasSend = now;
                    sendMeasurementsToClients(build_measurement_json(meas));
                }
            }
            uartBuffer = "";
        } 
        else if (c != '\r') {
            uartBuffer += c;
            if (uartBuffer.length() > 200) uartBuffer = "";
        }
    }
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n╔═══════════════════════════════════════╗");
    Serial.println("║  Smart Oscilloscope Pro v2.6          ║");
    Serial.println("║  Adaptive Speed Control               ║");
    Serial.println("╚═══════════════════════════════════════╝\n");
    
    sharedState.reset();
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        clients[i] = {0, 0, 0, 0, 0, 0, false};
    }
    
    Serial.print("SPIFFS... ");
    if (!SPIFFS.begin(true)) {
        Serial.println("FAILED!");
        while(1) delay(1000);
    }
    Serial.println("OK");
    
    Serial.print("UART... ");
    SerialSTM.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    Serial.println("OK");
    
    Serial.print("SPI... ");
    setup_spi_slave();
    
    Serial.println("WiFi AP...");
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    
    IPAddress ip(192, 168, 4, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    
    if (!WiFi.softAP(SSID, PASSWORD, 1, 0, MAX_WS_CLIENTS + 2)) {
        Serial.println("❌ AP Failed!");
        while(1) delay(1000);
    }
    
    Serial.printf("✓ AP: %s @ %s\n", SSID, WiFi.softAPIP().toString().c_str());
    
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
        r->send(SPIFFS, "/index.html", "text/html");
    });
    
    server.on("/state", HTTP_GET, [](AsyncWebServerRequest *r) {
        r->send(200, "application/json", buildStateJson());
    });
    
    server.on("/health", HTTP_GET, [](AsyncWebServerRequest *r) {
        char buf[256];
        snprintf(buf, sizeof(buf), 
            "{\"ok\":true,\"up\":%lu,\"clients\":%d,\"heap\":%u,\"speed\":%d,\"errors\":%lu}",
            millis()/1000, countActiveClients(), ESP.getFreeHeap(), 
            systemSpeed, consecutiveErrors);
        r->send(200, "application/json", buf);
    });
    
    server.serveStatic("/", SPIFFS, "/");
    server.onNotFound([](AsyncWebServerRequest *r) {
        r->send(404, "text/plain", "Not Found");
    });
    
    server.begin();
    Serial.println("✓ Ready!\n");
}

// ==================== MAIN LOOP ====================
void loop() {
    static uint32_t lastHeartbeat = 0;
    static uint32_t lastCleanup = 0;
    static uint32_t lastBinaryCheck = 0;
    
    uint32_t now = millis();
    
    // Auto-recovery: speed up if no errors for a while
    if (systemSpeed > 0 && (now - lastErrorTime) > ERROR_RECOVERY_TIME) {
        systemSpeed--;
        Serial.printf("✓ System speed restored to level %d\n", systemSpeed);
        lastErrorTime = now;  // Prevent immediate re-trigger
    }
    
    // Pending broadcast
    if (pendingBroadcast && systemSpeed < 2 && 
        (now - lastBroadcastTime) >= BROADCAST_INTERVAL) {
        broadcastState();
        lastBroadcastTime = now;
        pendingBroadcast = false;
    }
    
    // UART
    handle_uart();
    
    // Binary data - adaptive interval
    uint32_t binaryInterval = (systemSpeed == 0) ? BINARY_INTERVAL_NORMAL : 
                              (systemSpeed == 1) ? BINARY_INTERVAL_SLOW : 500;
    
    if ((now - lastBinaryCheck) >= binaryInterval) {
        lastBinaryCheck = now;
        
        if (is_spi_data_ready() && handle_spi_transaction_nonblocking()) {
            if (ws.count() > 0) {
                sendBinaryToClients(get_rx_buffer(), BUFFER_SIZE);
            }
        }
    }
    
    // Keep-alive
    pingClients();
    
    // Heartbeat
    if ((now - lastHeartbeat) > 10000) {
        lastHeartbeat = now;
        
        Serial.println("─────────────────────────────────────");
        Serial.printf("♥ %lus | Heap:%u | Clients:%d | Speed:%d\n", 
            now/1000, ESP.getFreeHeap(), countActiveClients(), systemSpeed);
        
        if (meas.valid) {
            Serial.printf("  %luHz %umVpp | %s\n", 
                (unsigned long)meas.frequency_hz, (unsigned)meas.amplitude_mv,
                sharedState.displayMode == MODE_FREQ_DOMAIN ? "FFT" : "TIME");
        }
        Serial.println("─────────────────────────────────────\n");
    }
    
    // Cleanup
    if ((now - lastCleanup) > 2000) {
        lastCleanup = now;
        ws.cleanupClients();
    }
    
    delay(1);
}