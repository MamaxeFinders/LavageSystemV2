#include <Arduino.h>
#include <WiFiManager.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <PCF8574.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "shared/rs485_protocol.h"

using namespace RS485Proto;

// KC868-F4 board mapping from KinCony F4 ESP32-S3 docs.
#define F4_I2C_SDA_PIN 8
#define F4_I2C_SCL_PIN 18
#define F4_RS485_TX_PIN 16
#define F4_RS485_RX_PIN 17
#define F4_CONFIG_BUTTON_PIN 0
#define F4_PCf8574_ADDRESS 0x24
#define F4_RELAY_FIRST_PCF_PIN 4
#define F4_RELAY_COUNT 4
#define F4_OLED_WIDTH 128
#define F4_OLED_HEIGHT 64
#define F4_OLED_ADDRESS 0x3C
#define F4_SD_SPI_SCK_PIN 42
#define F4_SD_SPI_MISO_PIN 44
#define F4_SD_SPI_MOSI_PIN 43
#define F4_SD_CS_PIN 39

PCF8574 relayBoard(F4_PCf8574_ADDRESS, F4_I2C_SDA_PIN, F4_I2C_SCL_PIN);
Adafruit_SSD1306 oled(F4_OLED_WIDTH, F4_OLED_HEIGHT, &Wire, -1);
SPIClass sdSpi(FSPI);

#define RS485_TX_PIN F4_RS485_TX_PIN
#define RS485_RX_PIN F4_RS485_RX_PIN
#define CONFIG_BUTTON_PIN F4_CONFIG_BUTTON_PIN
HardwareSerial RS485Serial(2);

const char* PREF_NAMESPACE = "f4-v2";
const char* PREF_DEVICE_ID = "device_id";
const char* PREF_SITE_ID = "site_id";
const char* PREF_MQTT_HOST = "mqtt_host";
const char* PREF_MQTT_PORT = "mqtt_port";
const char* PREF_MQTT_USER = "mqtt_user";
const char* PREF_MQTT_PASS = "mqtt_pass";
const char* PREF_TOPIC_CMD = "topic_cmd";
const char* PREF_TOPIC_ACK = "topic_ack";
const char* PREF_TOPIC_STATUS = "topic_status";
const char* PREF_SCRIPT_URL = "script_url";
const char* PREF_CAISSE_COUNT = "caisse_cnt";
const char* PREF_ASPI_COUNT = "aspi_cnt";
const char* PREF_AIR_COUNT = "air_cnt";

Preferences preferences;
WiFiManager wm;

String configDeviceId = "kc868-f4-site1";
String configSiteId = "site1";
String configMqttHost = "";
uint16_t configMqttPort = 8883;
String configMqttUsername = "";
String configMqttPassword = "";
String configTopicCmd = "carwash/site1/bridge/cmd";
String configTopicAck = "carwash/site1/bridge/ack";
String configTopicStatus = "carwash/site1/bridge/status";
String configScriptBaseUrl = "";
uint8_t configCaisseCount = 4;
uint8_t configAspiCount = 2;
uint8_t configAirCount = 1;

const unsigned long POLL_INTERVAL_MS = 450;
const unsigned long STALE_MACHINE_MS = 15000;
const unsigned long STATUS_PUSH_MS = 10000;
const unsigned long HEARTBEAT_MS = 60000;
const unsigned long MQTT_RETRY_MS = 5000;
const unsigned long ETH_DOWN_RESTART_MS = 180000;
const unsigned long MQTT_DOWN_RESTART_MS = 300000;
const unsigned long PENDING_TIMEOUT_MS = 12000;
const unsigned long LEGACY_PULSE_DEFAULT_MS = 350;
const unsigned long OLED_REFRESH_MS = 1000;
const unsigned long OFFLINE_FLUSH_MS = 7000;
const size_t OLED_VISIBLE_LINES = 6;
const char* OFFLINE_QUEUE_FILE = "/offline_queue.ndjson";

enum MachineType {
  MACHINE_CAISSE,
  MACHINE_ASPI,
  MACHINE_AIR
};

enum MachineCommMode {
  COMM_MODE_NORMAL,
  COMM_MODE_SAFE
};

struct MachineState {
  uint8_t nodeId;
  MachineType type;
  uint8_t familyIndex;
  const char* name;
  bool active;
  MachineCommMode commMode;
  bool online;
  bool healthy;
  bool enabled;
  bool running;
  bool presostat;
  bool gel;
  bool shock;
  int creditCents;
  int program;
  float temperature;
  float humidity;
  String faultCode;
  String lastReason;
  unsigned long lastSeenMs;
  unsigned long lastStatusAtMs;
};

MachineState machines[] = {
  {1, MACHINE_CAISSE, 1, "CAISSE_1", true, COMM_MODE_NORMAL, false, false, true, false, false, false, false, 0, 0, NAN, NAN, "OFFLINE", "boot", 0, 0},
  {2, MACHINE_CAISSE, 2, "CAISSE_2", true, COMM_MODE_NORMAL, false, false, true, false, false, false, false, 0, 0, NAN, NAN, "OFFLINE", "boot", 0, 0},
  {3, MACHINE_CAISSE, 3, "CAISSE_3", true, COMM_MODE_NORMAL, false, false, true, false, false, false, false, 0, 0, NAN, NAN, "OFFLINE", "boot", 0, 0},
  {4, MACHINE_CAISSE, 4, "CAISSE_4", true, COMM_MODE_NORMAL, false, false, true, false, false, false, false, 0, 0, NAN, NAN, "OFFLINE", "boot", 0, 0},
  {5, MACHINE_ASPI, 1, "ASPI_1", true, COMM_MODE_NORMAL, false, false, true, false, false, false, false, 0, 0, NAN, NAN, "OFFLINE", "boot", 0, 0},
  {6, MACHINE_ASPI, 2, "ASPI_2", true, COMM_MODE_NORMAL, false, false, true, false, false, false, false, 0, 0, NAN, NAN, "OFFLINE", "boot", 0, 0},
  {7, MACHINE_AIR, 1, "AIR_1", true, COMM_MODE_NORMAL, false, false, true, false, false, false, false, 0, 0, NAN, NAN, "OFFLINE", "boot", 0, 0}
};

const size_t MACHINE_COUNT = sizeof(machines) / sizeof(machines[0]);
const size_t MAX_PENDING = 16;

struct PendingRs485Command {
  bool active;
  uint16_t seq;
  uint8_t nodeId;
  String commandType;
  String commandId;
  String source;
  String value;
  unsigned long sentAtMs;
};

PendingRs485Command pendingCommands[MAX_PENDING];

void clearPending(PendingRs485Command* pending);

volatile bool ethConnected = false;
unsigned long bootMs = 0;
unsigned long ethDownSinceMs = 0;
unsigned long mqttDownSinceMs = 0;
unsigned long lastPollMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastStatusPushMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastHealthLogMs = 0;
unsigned long lastDisplayRefreshMs = 0;
unsigned long lastOfflineFlushMs = 0;
uint16_t seqCounter = 0;
size_t pollIndex = 0;
String rs485LineBuffer;
bool sdAvailable = false;
size_t lastOfflineQueueDepth = 0;

WiFiClientSecure mqttTlsClient;
PubSubClient mqttClient(mqttTlsClient);

void copyStringToBuffer(const String& value, char* buffer, size_t bufferLen) {
  if (bufferLen == 0) {
    return;
  }
  value.substring(0, bufferLen - 1).toCharArray(buffer, bufferLen);
  buffer[bufferLen - 1] = '\0';
}

uint8_t clampCount(uint8_t value, uint8_t maxValue) {
  return value > maxValue ? maxValue : value;
}

String modePrefKey(uint8_t nodeId) {
  return String("mode_") + nodeId;
}

MachineCommMode parseCommMode(const String& value, MachineCommMode fallback) {
  String normalized = value;
  normalized.trim();
  normalized.toUpperCase();
  if (normalized == "SAFE") {
    return COMM_MODE_SAFE;
  }
  if (normalized == "NORMAL") {
    return COMM_MODE_NORMAL;
  }
  return fallback;
}

const char* machineCommModeName(MachineCommMode mode) {
  return mode == COMM_MODE_SAFE ? "SAFE" : "NORMAL";
}

uint8_t configuredCountForType(MachineType type) {
  switch (type) {
    case MACHINE_CAISSE:
      return configCaisseCount;
    case MACHINE_ASPI:
      return configAspiCount;
    case MACHINE_AIR:
      return configAirCount;
  }
  return 0;
}

bool machineShouldBeActive(const MachineState& machine) {
  return machine.familyIndex <= configuredCountForType(machine.type);
}

void applyMachineLayoutConfig() {
  configCaisseCount = clampCount(configCaisseCount, 4);
  configAspiCount = clampCount(configAspiCount, 2);
  configAirCount = clampCount(configAirCount, 1);

  for (size_t index = 0; index < MACHINE_COUNT; ++index) {
    MachineState& machine = machines[index];
    machine.active = machineShouldBeActive(machine);
    if (!machine.active) {
      machine.online = false;
      machine.healthy = false;
      machine.enabled = true;
      machine.running = false;
      machine.creditCents = 0;
      machine.program = 0;
      machine.faultCode = "NOT_CONFIGURED";
      machine.lastReason = "not_configured";
      machine.lastSeenMs = 0;
      machine.lastStatusAtMs = 0;
    } else if (machine.faultCode == "NOT_CONFIGURED") {
      machine.faultCode = "OFFLINE";
      machine.lastReason = "boot";
    }
  }
}

void saveMachineCommModes() {
  for (size_t index = 0; index < MACHINE_COUNT; ++index) {
    preferences.putString(modePrefKey(machines[index].nodeId).c_str(), machineCommModeName(machines[index].commMode));
  }
}

void clearPendingForNode(uint8_t nodeId) {
  for (size_t index = 0; index < MAX_PENDING; ++index) {
    if (pendingCommands[index].active && pendingCommands[index].nodeId == nodeId) {
      clearPending(&pendingCommands[index]);
    }
  }
}

bool machineAllowsRemoteRs485(const MachineState& machine) {
  return machine.active && machine.commMode == COMM_MODE_NORMAL;
}

void loadRuntimeConfig() {
  preferences.begin(PREF_NAMESPACE, true);
  configDeviceId = preferences.getString(PREF_DEVICE_ID, configDeviceId);
  configSiteId = preferences.getString(PREF_SITE_ID, configSiteId);
  configMqttHost = preferences.getString(PREF_MQTT_HOST, configMqttHost);
  configMqttPort = preferences.getUShort(PREF_MQTT_PORT, configMqttPort);
  configMqttUsername = preferences.getString(PREF_MQTT_USER, configMqttUsername);
  configMqttPassword = preferences.getString(PREF_MQTT_PASS, configMqttPassword);
  configTopicCmd = preferences.getString(PREF_TOPIC_CMD, configTopicCmd);
  configTopicAck = preferences.getString(PREF_TOPIC_ACK, configTopicAck);
  configTopicStatus = preferences.getString(PREF_TOPIC_STATUS, configTopicStatus);
  configScriptBaseUrl = preferences.getString(PREF_SCRIPT_URL, configScriptBaseUrl);
  configCaisseCount = clampCount(static_cast<uint8_t>(preferences.getUChar(PREF_CAISSE_COUNT, configCaisseCount)), 4);
  configAspiCount = clampCount(static_cast<uint8_t>(preferences.getUChar(PREF_ASPI_COUNT, configAspiCount)), 2);
  configAirCount = clampCount(static_cast<uint8_t>(preferences.getUChar(PREF_AIR_COUNT, configAirCount)), 1);
  for (size_t index = 0; index < MACHINE_COUNT; ++index) {
    machines[index].commMode = parseCommMode(preferences.getString(modePrefKey(machines[index].nodeId).c_str(), machineCommModeName(machines[index].commMode)), machines[index].commMode);
  }
  preferences.end();
  applyMachineLayoutConfig();
}

void saveRuntimeConfig() {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putString(PREF_DEVICE_ID, configDeviceId);
  preferences.putString(PREF_SITE_ID, configSiteId);
  preferences.putString(PREF_MQTT_HOST, configMqttHost);
  preferences.putUShort(PREF_MQTT_PORT, configMqttPort);
  preferences.putString(PREF_MQTT_USER, configMqttUsername);
  preferences.putString(PREF_MQTT_PASS, configMqttPassword);
  preferences.putString(PREF_TOPIC_CMD, configTopicCmd);
  preferences.putString(PREF_TOPIC_ACK, configTopicAck);
  preferences.putString(PREF_TOPIC_STATUS, configTopicStatus);
  preferences.putString(PREF_SCRIPT_URL, configScriptBaseUrl);
  preferences.putUChar(PREF_CAISSE_COUNT, configCaisseCount);
  preferences.putUChar(PREF_ASPI_COUNT, configAspiCount);
  preferences.putUChar(PREF_AIR_COUNT, configAirCount);
  saveMachineCommModes();
  preferences.end();
}

bool configIsComplete() {
  return configMqttHost.length() > 0 &&
    configMqttUsername.length() > 0 &&
    configMqttPassword.length() > 0 &&
    configScriptBaseUrl.startsWith("http") &&
    configTopicCmd.length() > 0 &&
    configTopicAck.length() > 0 &&
    configTopicStatus.length() > 0;
}

void openConfigPortal(bool forced) {
  WiFi.mode(WIFI_AP_STA);

  char deviceIdBuffer[40];
  char siteIdBuffer[24];
  char mqttHostBuffer[96];
  char mqttPortBuffer[8];
  char mqttUserBuffer[64];
  char mqttPassBuffer[64];
  char topicCmdBuffer[96];
  char topicAckBuffer[96];
  char topicStatusBuffer[96];
  char scriptUrlBuffer[192];
  char caisseCountBuffer[4];
  char aspiCountBuffer[4];
  char airCountBuffer[4];

  copyStringToBuffer(configDeviceId, deviceIdBuffer, sizeof(deviceIdBuffer));
  copyStringToBuffer(configSiteId, siteIdBuffer, sizeof(siteIdBuffer));
  copyStringToBuffer(configMqttHost, mqttHostBuffer, sizeof(mqttHostBuffer));
  copyStringToBuffer(String(configMqttPort), mqttPortBuffer, sizeof(mqttPortBuffer));
  copyStringToBuffer(configMqttUsername, mqttUserBuffer, sizeof(mqttUserBuffer));
  copyStringToBuffer(configMqttPassword, mqttPassBuffer, sizeof(mqttPassBuffer));
  copyStringToBuffer(configTopicCmd, topicCmdBuffer, sizeof(topicCmdBuffer));
  copyStringToBuffer(configTopicAck, topicAckBuffer, sizeof(topicAckBuffer));
  copyStringToBuffer(configTopicStatus, topicStatusBuffer, sizeof(topicStatusBuffer));
  copyStringToBuffer(configScriptBaseUrl, scriptUrlBuffer, sizeof(scriptUrlBuffer));
  copyStringToBuffer(String(configCaisseCount), caisseCountBuffer, sizeof(caisseCountBuffer));
  copyStringToBuffer(String(configAspiCount), aspiCountBuffer, sizeof(aspiCountBuffer));
  copyStringToBuffer(String(configAirCount), airCountBuffer, sizeof(airCountBuffer));

  WiFiManagerParameter paramDeviceId("device_id", "Bridge device ID", deviceIdBuffer, sizeof(deviceIdBuffer));
  WiFiManagerParameter paramSiteId("site_id", "Site ID", siteIdBuffer, sizeof(siteIdBuffer));
  WiFiManagerParameter paramMqttHost("mqtt_host", "MQTT host", mqttHostBuffer, sizeof(mqttHostBuffer));
  WiFiManagerParameter paramMqttPort("mqtt_port", "MQTT port", mqttPortBuffer, sizeof(mqttPortBuffer));
  WiFiManagerParameter paramMqttUser("mqtt_user", "MQTT username", mqttUserBuffer, sizeof(mqttUserBuffer));
  WiFiManagerParameter paramMqttPass("mqtt_pass", "MQTT password", mqttPassBuffer, sizeof(mqttPassBuffer));
  WiFiManagerParameter paramTopicCmd("topic_cmd", "Command topic", topicCmdBuffer, sizeof(topicCmdBuffer));
  WiFiManagerParameter paramTopicAck("topic_ack", "Ack topic", topicAckBuffer, sizeof(topicAckBuffer));
  WiFiManagerParameter paramTopicStatus("topic_status", "Status topic", topicStatusBuffer, sizeof(topicStatusBuffer));
  WiFiManagerParameter paramScriptUrl("script_url", "Apps Script URL", scriptUrlBuffer, sizeof(scriptUrlBuffer));
  WiFiManagerParameter paramCaisseCount("caisse_count", "CAISSE units (0..4)", caisseCountBuffer, sizeof(caisseCountBuffer));
  WiFiManagerParameter paramAspiCount("aspi_count", "ASPI units (0..2)", aspiCountBuffer, sizeof(aspiCountBuffer));
  WiFiManagerParameter paramAirCount("air_count", "AIR units (0..1)", airCountBuffer, sizeof(airCountBuffer));

  wm.setConfigPortalTimeout(300);
  std::vector<const char*> menu = {"wifi", "sep", "param", "sep", "restart", "exit"};
  wm.setMenu(menu);
  wm.setClass("invert");
  wm.addParameter(&paramDeviceId);
  wm.addParameter(&paramSiteId);
  wm.addParameter(&paramMqttHost);
  wm.addParameter(&paramMqttPort);
  wm.addParameter(&paramMqttUser);
  wm.addParameter(&paramMqttPass);
  wm.addParameter(&paramTopicCmd);
  wm.addParameter(&paramTopicAck);
  wm.addParameter(&paramTopicStatus);
  wm.addParameter(&paramScriptUrl);
  wm.addParameter(&paramCaisseCount);
  wm.addParameter(&paramAspiCount);
  wm.addParameter(&paramAirCount);

  const char* portalName = forced ? "F4_BRIDGE_CONFIG" : "F4_BRIDGE_SETUP";
  lcdShow("CONFIG PORTAL", portalName);
  wm.startConfigPortal(portalName);

  String nextDeviceId = String(paramDeviceId.getValue());
  String nextSiteId = String(paramSiteId.getValue());
  String nextMqttHost = String(paramMqttHost.getValue());
  String nextMqttUsername = String(paramMqttUser.getValue());
  String nextMqttPassword = String(paramMqttPass.getValue());
  String nextTopicCmd = String(paramTopicCmd.getValue());
  String nextTopicAck = String(paramTopicAck.getValue());
  String nextTopicStatus = String(paramTopicStatus.getValue());
  String nextScriptUrl = String(paramScriptUrl.getValue());
  uint16_t nextMqttPort = static_cast<uint16_t>(String(paramMqttPort.getValue()).toInt());
  uint8_t nextCaisseCount = clampCount(static_cast<uint8_t>(String(paramCaisseCount.getValue()).toInt()), 4);
  uint8_t nextAspiCount = clampCount(static_cast<uint8_t>(String(paramAspiCount.getValue()).toInt()), 2);
  uint8_t nextAirCount = clampCount(static_cast<uint8_t>(String(paramAirCount.getValue()).toInt()), 1);

  nextDeviceId.trim();
  nextSiteId.trim();
  nextMqttHost.trim();
  nextMqttUsername.trim();
  nextMqttPassword.trim();
  nextTopicCmd.trim();
  nextTopicAck.trim();
  nextTopicStatus.trim();
  nextScriptUrl.trim();
  if (nextMqttPort == 0) {
    nextMqttPort = 8883;
  }

  if (nextMqttHost.length() > 0 &&
      nextMqttUsername.length() > 0 &&
      nextMqttPassword.length() > 0 &&
      nextTopicCmd.length() > 0 &&
      nextTopicAck.length() > 0 &&
      nextTopicStatus.length() > 0 &&
      nextScriptUrl.startsWith("http")) {
    configDeviceId = nextDeviceId.length() > 0 ? nextDeviceId : configDeviceId;
    configSiteId = nextSiteId.length() > 0 ? nextSiteId : configSiteId;
    configMqttHost = nextMqttHost;
    configMqttPort = nextMqttPort;
    configMqttUsername = nextMqttUsername;
    configMqttPassword = nextMqttPassword;
    configTopicCmd = nextTopicCmd;
    configTopicAck = nextTopicAck;
    configTopicStatus = nextTopicStatus;
    configScriptBaseUrl = nextScriptUrl;
    configCaisseCount = nextCaisseCount;
    configAspiCount = nextAspiCount;
    configAirCount = nextAirCount;
    applyMachineLayoutConfig();
    saveRuntimeConfig();
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

const char* machineTypeName(MachineType type) {
  switch (type) {
    case MACHINE_CAISSE:
      return "CAISSE";
    case MACHINE_ASPI:
      return "ASPI";
    case MACHINE_AIR:
      return "AIR";
  }
  return "UNKNOWN";
}

MachineState* findMachineByNode(uint8_t nodeId) {
  for (size_t index = 0; index < MACHINE_COUNT; ++index) {
    if (machines[index].active && machines[index].nodeId == nodeId) {
      return &machines[index];
    }
  }
  return nullptr;
}

MachineState* findMachineByName(const String& machineName) {
  for (size_t index = 0; index < MACHINE_COUNT; ++index) {
    if (machines[index].active && String(machines[index].name) == machineName) {
      return &machines[index];
    }
  }
  return nullptr;
}

MachineState* findMachineFromJson(JsonVariantConst machineField, JsonVariantConst machineIdField, JsonVariantConst pisteField) {
  if (!machineField.isNull()) {
    MachineState* byName = findMachineByName(String(machineField.as<const char*>()));
    if (byName != nullptr) {
      return byName;
    }
  }
  if (!machineIdField.isNull()) {
    int nodeId = machineIdField.as<int>();
    if (nodeId > 0) {
      MachineState* byNode = findMachineByNode(static_cast<uint8_t>(nodeId));
      if (byNode != nullptr) {
        return byNode;
      }
    }
  }
  if (!pisteField.isNull()) {
    int nodeId = pisteField.as<int>();
    if (nodeId > 0) {
      return findMachineByNode(static_cast<uint8_t>(nodeId));
    }
  }
  return nullptr;
}

uint16_t nextSeq() {
  ++seqCounter;
  if (seqCounter == 0) {
    ++seqCounter;
  }
  return seqCounter;
}

void lcdShow(const String& line1, const String& line2) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println(line1.substring(0, 21));
  oled.println(line2.substring(0, 21));
  oled.display();
}

void allRelaysOff() {
  for (uint8_t relayIndex = 0; relayIndex < F4_RELAY_COUNT; ++relayIndex) {
    relayBoard.digitalWrite(F4_RELAY_FIRST_PCF_PIN + relayIndex, HIGH);
  }
}

void setRelayState(uint8_t relayIndex, bool enabled) {
  if (relayIndex < 1 || relayIndex > F4_RELAY_COUNT) {
    return;
  }
  relayBoard.digitalWrite(F4_RELAY_FIRST_PCF_PIN + relayIndex - 1, enabled ? LOW : HIGH);
}

String localIpString() {
  return WiFi.isConnected() ? WiFi.localIP().toString() : String("0.0.0.0");
}

bool sdFileExists(const char* path) {
  return sdAvailable && SD.exists(path);
}

size_t countOfflineQueueRecords() {
  if (!sdFileExists(OFFLINE_QUEUE_FILE)) {
    return 0;
  }

  File file = SD.open(OFFLINE_QUEUE_FILE, FILE_READ);
  if (!file) {
    return 0;
  }

  size_t count = 0;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      ++count;
    }
  }
  file.close();
  return count;
}

bool initSdCard() {
  sdSpi.begin(F4_SD_SPI_SCK_PIN, F4_SD_SPI_MISO_PIN, F4_SD_SPI_MOSI_PIN, F4_SD_CS_PIN);
  sdAvailable = SD.begin(F4_SD_CS_PIN, sdSpi);
  lastOfflineQueueDepth = countOfflineQueueRecords();
  if (!sdAvailable) {
    Serial.println("SD init failed, offline queue disabled until card is available");
  }
  return sdAvailable;
}

bool appendOfflineRecord(const String& action, const String& payload) {
  if (!sdAvailable && !initSdCard()) {
    return false;
  }

  File file = SD.open(OFFLINE_QUEUE_FILE, FILE_APPEND);
  if (!file) {
    return false;
  }

  StaticJsonDocument<256> envelope;
  envelope["action"] = action;
  envelope["payload"] = payload;
  envelope["timestamp_ms"] = millis();

  String line;
  serializeJson(envelope, line);
  file.println(line);
  file.close();
  ++lastOfflineQueueDepth;
  return true;
}

bool sendRawJsonToAppsScript(const String& action, const String& payload, bool queueOnFailure) {
  if (!ethConnected) {
    return queueOnFailure ? appendOfflineRecord(action, payload) : false;
  }
  if (!configScriptBaseUrl.startsWith("http")) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  const String url = configScriptBaseUrl + "?action=" + action;
  if (!http.begin(client, url)) {
    return queueOnFailure ? appendOfflineRecord(action, payload) : false;
  }

  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(payload);
  const bool ok = code >= 200 && code < 300;
  if (!ok) {
    Serial.printf("HTTP %s failed: %d\n", action.c_str(), code);
  }
  http.end();

  if (!ok && queueOnFailure) {
    return appendOfflineRecord(action, payload);
  }
  return ok;
}

void flushOfflineQueue() {
  if (!ethConnected || !sdFileExists(OFFLINE_QUEUE_FILE)) {
    return;
  }

  File source = SD.open(OFFLINE_QUEUE_FILE, FILE_READ);
  if (!source) {
    return;
  }

  File retry = SD.open("/offline_retry.ndjson", FILE_WRITE);
  if (!retry) {
    source.close();
    return;
  }

  while (source.available()) {
    String line = source.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
      continue;
    }

    StaticJsonDocument<320> envelope;
    if (deserializeJson(envelope, line) != DeserializationError::Ok) {
      retry.println(line);
      continue;
    }

    const String action = envelope["action"] | "";
    const String payload = envelope["payload"] | "";
    if (!sendRawJsonToAppsScript(action, payload, false)) {
      retry.println(line);
    }
  }

  source.close();
  retry.close();
  SD.remove(OFFLINE_QUEUE_FILE);
  if (SD.exists("/offline_retry.ndjson")) {
    SD.rename("/offline_retry.ndjson", OFFLINE_QUEUE_FILE);
  }
  lastOfflineQueueDepth = countOfflineQueueRecords();
}

void refreshOledStatus() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println(configDeviceId.substring(0, 21));
  oled.println(String("NET:") + (ethConnected ? "UP " : "DOWN ") + localIpString());
  oled.println(String("MQTT:") + (mqttClient.connected() ? "UP" : "DOWN") + " Q:" + lastOfflineQueueDepth);

  size_t onlineCount = 0;
  size_t safeCount = 0;
  for (size_t index = 0; index < MACHINE_COUNT; ++index) {
    if (!machines[index].active) {
      continue;
    }
    if (machines[index].online) {
      ++onlineCount;
    }
    if (machines[index].commMode == COMM_MODE_SAFE) {
      ++safeCount;
    }
  }

  oled.println(String("ONLINE ") + onlineCount + "/" + (configCaisseCount + configAspiCount + configAirCount));
  oled.println(String("SAFE:") + safeCount + " SD:" + (sdAvailable ? "OK" : "OFF"));

  for (size_t index = 0; index < MACHINE_COUNT; ++index) {
    if (!machines[index].active) {
      continue;
    }
    oled.print(machines[index].name);
    oled.print(':');
    oled.print(machines[index].commMode == COMM_MODE_SAFE ? 'S' : (machines[index].online ? 'O' : 'X'));
    oled.print(' ');
  }
  oled.display();
}

String nowIsoString() {
  return String(static_cast<unsigned long>(millis()));
}

void restartBoard(const char* reason) {
  Serial.print("Restart: ");
  Serial.println(reason);
  delay(150);
  ESP.restart();
}

void publishJson(const char* topic, const JsonDocument& document, bool retained) {
  if (!mqttClient.connected()) {
    return;
  }
  char buffer[1536];
  const size_t length = serializeJson(document, buffer, sizeof(buffer));
  mqttClient.publish(topic, buffer, length, retained);
}

void postJsonToAppsScript(const String& action, const JsonDocument& document) {
  String payload;
  serializeJson(document, payload);
  sendRawJsonToAppsScript(action, payload, true);
}

void publishBridgeAck(const String& commandId, const String& targetMachine, const String& commandType, const String& ackStage, const String& status, const String& note) {
  StaticJsonDocument<384> document;
  document["device"] = configDeviceId;
  document["site_id"] = configSiteId;
  document["command_id"] = commandId;
  document["target_machine"] = targetMachine;
  document["command_type"] = commandType;
  document["ack_stage"] = ackStage;
  document["status"] = status;
  document["note"] = note;
  document["timestamp_ms"] = millis();
  publishJson(configTopicAck.c_str(), document, false);
}

void postCommandAckToAppsScript(const String& commandId, const String& targetMachine, const String& commandType, const String& source, const String& status, const String& ackStage, const String& note) {
  StaticJsonDocument<384> document;
  document["command_id"] = commandId;
  document["target_machine"] = targetMachine;
  document["command_type"] = commandType;
  document["source"] = source;
  document["status"] = status;
  document["ack_stage"] = ackStage;
  document["note"] = note;
  document["bridge"] = configDeviceId;
  postJsonToAppsScript("command_ack", document);
}

void logCommandProgress(const PendingRs485Command& pending, const String& ackStage, const String& status, const String& note) {
  MachineState* machine = findMachineByNode(pending.nodeId);
  const String machineName = machine != nullptr ? String(machine->name) : String(pending.nodeId);
  publishBridgeAck(pending.commandId, machineName, pending.commandType, ackStage, status, note);
  postCommandAckToAppsScript(pending.commandId, machineName, pending.commandType, pending.source, status, ackStage, note);
}

PendingRs485Command* findPendingBySeq(uint16_t seq, uint8_t nodeId) {
  for (size_t index = 0; index < MAX_PENDING; ++index) {
    if (pendingCommands[index].active && pendingCommands[index].seq == seq && pendingCommands[index].nodeId == nodeId) {
      return &pendingCommands[index];
    }
  }
  return nullptr;
}

void clearPending(PendingRs485Command* pending) {
  if (pending == nullptr) {
    return;
  }
  pending->active = false;
  pending->seq = 0;
  pending->nodeId = 0;
  pending->commandType = "";
  pending->commandId = "";
  pending->source = "";
  pending->value = "";
  pending->sentAtMs = 0;
}

PendingRs485Command* rememberPending(uint16_t seq, uint8_t nodeId, const String& commandType, const String& commandId, const String& source, const String& value) {
  PendingRs485Command* slot = nullptr;
  for (size_t index = 0; index < MAX_PENDING; ++index) {
    if (!pendingCommands[index].active) {
      slot = &pendingCommands[index];
      break;
    }
  }
  if (slot == nullptr) {
    slot = &pendingCommands[0];
  }
  slot->active = true;
  slot->seq = seq;
  slot->nodeId = nodeId;
  slot->commandType = commandType;
  slot->commandId = commandId;
  slot->source = source;
  slot->value = value;
  slot->sentAtMs = millis();
  return slot;
}

void expirePendingCommands() {
  const unsigned long now = millis();
  for (size_t index = 0; index < MAX_PENDING; ++index) {
    PendingRs485Command& pending = pendingCommands[index];
    if (!pending.active) {
      continue;
    }
    if (now - pending.sentAtMs > PENDING_TIMEOUT_MS) {
      logCommandProgress(pending, "FAULT", "timeout", "No slave reply before timeout");
      clearPending(&pending);
    }
  }
}

String boolAsPayload(bool value) {
  return value ? "1" : "0";
}

void updateMachineStatusFromPayload(MachineState& machine, const String& payload) {
  machine.creditCents = payloadValueInt(payload, "credit", machine.creditCents);
  machine.program = payloadValueInt(payload, "prog", machine.program);
  machine.running = payloadValueBool(payload, "run", machine.running);
  machine.enabled = payloadValueBool(payload, "en", machine.enabled);
  machine.presostat = payloadValueBool(payload, "preso", machine.presostat);
  machine.gel = payloadValueBool(payload, "gel", machine.gel);
  machine.shock = payloadValueBool(payload, "shock", machine.shock);
  machine.healthy = payloadValueBool(payload, "ok", machine.healthy);

  String textValue;
  if (payloadValue(payload, "fault", textValue)) {
    machine.faultCode = textValue;
  }
  if (payloadValue(payload, "reason", textValue)) {
    machine.lastReason = textValue;
  }
  if (payloadValue(payload, "temp", textValue)) {
    machine.temperature = textValue.toFloat();
  }
  if (payloadValue(payload, "hum", textValue)) {
    machine.humidity = textValue.toFloat();
  }
  machine.online = true;
  machine.lastSeenMs = millis();
  machine.lastStatusAtMs = millis();
}

void sendRs485(uint8_t nodeId, const String& command, const String& arg1, const String& arg2, const String& trackedCommandId, const String& trackedSource, const String& trackedType, const String& trackedValue) {
  const uint16_t seq = nextSeq();
  const String frame = buildFrame(nodeId, command, arg1, arg2, seq);
  if (trackedCommandId.length() > 0) {
    rememberPending(seq, nodeId, trackedType, trackedCommandId, trackedSource, trackedValue);
  }
  RS485Serial.println(frame);
  Serial.print("RS485 TX ");
  Serial.println(frame);
}

void sendPoll(MachineState& machine) {
  if (!machineAllowsRemoteRs485(machine)) {
    return;
  }
  sendRs485(machine.nodeId, "GET_STATUS", "", "", "", "poll", "poll", "");
}

void postMachineEvent(const MachineState& machine, const String& eventType, const String& value1, const String& value2, uint16_t seq) {
  StaticJsonDocument<384> document;
  document["machine"] = machine.name;
  document["machine_id"] = machine.nodeId;
  document["event_type"] = eventType;
  document["value_1"] = value1;
  document["value_2"] = value2;
  document["source"] = configDeviceId;
  document["seq"] = seq;
  postJsonToAppsScript("event", document);
}

void publishStatusHeartbeat() {
  StaticJsonDocument<1536> document;
  document["device"] = configDeviceId;
  document["site_id"] = configSiteId;
  document["eth_connected"] = ethConnected;
  document["mqtt_connected"] = mqttClient.connected();
  document["uptime_ms"] = millis() - bootMs;
  JsonArray machinesArray = document.createNestedArray("machines");
  for (size_t index = 0; index < MACHINE_COUNT; ++index) {
    if (!machines[index].active) {
      continue;
    }
    JsonObject item = machinesArray.createNestedObject();
    item["id"] = machines[index].nodeId;
    item["name"] = machines[index].name;
    item["type"] = machineTypeName(machines[index].type);
    item["comm_mode"] = machineCommModeName(machines[index].commMode);
    item["configured_active"] = machines[index].active;
    item["online"] = machines[index].online;
    item["healthy"] = machines[index].healthy;
    item["enabled"] = machines[index].enabled;
    item["running"] = machines[index].running;
    item["fault_code"] = machines[index].faultCode;
    item["credit_cents"] = machines[index].creditCents;
    item["program"] = machines[index].program;
    item["last_seen_ms_ago"] = machines[index].lastSeenMs == 0 ? -1 : static_cast<long>(millis() - machines[index].lastSeenMs);
  }
  publishJson(configTopicStatus.c_str(), document, true);
}

void pushSnapshotToAppsScript() {
  StaticJsonDocument<2048> document;
  document["bridge"] = configDeviceId;
  document["site_id"] = configSiteId;
  document["eth_connected"] = ethConnected;
  document["mqtt_connected"] = mqttClient.connected();
  document["timestamp_ms"] = millis();

  JsonArray machinesArray = document.createNestedArray("machines");
  for (size_t index = 0; index < MACHINE_COUNT; ++index) {
    if (!machines[index].active) {
      continue;
    }
    JsonObject item = machinesArray.createNestedObject();
    item["id"] = machines[index].nodeId;
    item["name"] = machines[index].name;
    item["type"] = machineTypeName(machines[index].type);
    item["bridge_id"] = configDeviceId;
    item["site_id"] = configSiteId;
    item["comm_mode"] = machineCommModeName(machines[index].commMode);
    item["configured_active"] = machines[index].active;
    item["online"] = machines[index].online;
    item["healthy"] = machines[index].healthy;
    item["enabled"] = machines[index].enabled;
    item["running"] = machines[index].running;
    item["presostat"] = machines[index].presostat;
    item["gel"] = machines[index].gel;
    item["shock"] = machines[index].shock;
    item["credit_cents"] = machines[index].creditCents;
    item["current_program"] = machines[index].program;
    item["fault_code"] = machines[index].faultCode;
    item["reason"] = machines[index].lastReason;
    if (!isnan(machines[index].temperature)) {
      item["temperature"] = machines[index].temperature;
    }
    if (!isnan(machines[index].humidity)) {
      item["humidity"] = machines[index].humidity;
    }
    item["last_seen_ms_ago"] = machines[index].lastSeenMs == 0 ? -1 : static_cast<long>(millis() - machines[index].lastSeenMs);
  }

  postJsonToAppsScript("status_snapshot", document);
}

void handleSlaveStage(PendingRs485Command* pending, const String& stage, const String& detail) {
  if (pending == nullptr) {
    return;
  }
  String status = "in_progress";
  if (stage == "APPLIED") {
    status = "applied";
  } else if (stage == "RUNNING") {
    status = "running";
  } else if (stage == "COMPLETED" || stage == "END_EVENT") {
    status = "completed";
  } else if (stage == "FAULT") {
    status = "fault";
  } else if (stage == "RECEIVED") {
    status = "accepted";
  }
  logCommandProgress(*pending, stage, status, detail);
  if (stage == "COMPLETED" || stage == "END_EVENT" || stage == "FAULT") {
    clearPending(pending);
  }
}

void handleRs485Frame(const String& line) {
  const Frame frame = parseFrame(line);
  if (!frame.valid) {
    Serial.print("RS485 bad frame: ");
    Serial.println(line);
    return;
  }

  MachineState* machine = findMachineByNode(frame.nodeId);
  if (machine == nullptr) {
    return;
  }
  machine->online = true;
  machine->lastSeenMs = millis();

  if (frame.command == "ACK") {
    PendingRs485Command* pending = findPendingBySeq(frame.seq, frame.nodeId);
    handleSlaveStage(pending, frame.arg1, frame.arg2);
    postMachineEvent(*machine, "ACK", frame.arg1, frame.arg2, frame.seq);
  } else if (frame.command == "STATUS") {
    updateMachineStatusFromPayload(*machine, frame.arg1);
    PendingRs485Command* pending = findPendingBySeq(frame.seq, frame.nodeId);
    if (pending != nullptr && pending->commandType == "refresh_status") {
      logCommandProgress(*pending, "COMPLETED", "completed", "Fresh status received");
      clearPending(pending);
    }
  } else if (frame.command == "FAULT") {
    machine->healthy = false;
    machine->faultCode = frame.arg1;
    machine->lastReason = frame.arg2;
    PendingRs485Command* pending = findPendingBySeq(frame.seq, frame.nodeId);
    if (pending != nullptr) {
      logCommandProgress(*pending, "FAULT", "fault", frame.arg1 + ":" + frame.arg2);
      clearPending(pending);
    }
    postMachineEvent(*machine, "FAULT", frame.arg1, frame.arg2, frame.seq);
  } else if (frame.command == "EVENT") {
    postMachineEvent(*machine, frame.arg1, frame.arg2, machine->faultCode, frame.seq);
  }
}

void readRs485() {
  while (RS485Serial.available()) {
    const char ch = static_cast<char>(RS485Serial.read());
    if (ch == '\n' || ch == '\r') {
      if (rs485LineBuffer.length() > 0) {
        handleRs485Frame(rs485LineBuffer);
        rs485LineBuffer = "";
      }
    } else {
      rs485LineBuffer += ch;
      if (rs485LineBuffer.length() > 255) {
        rs485LineBuffer = "";
      }
    }
  }
}

void pollNextMachine() {
  for (size_t attempts = 0; attempts < MACHINE_COUNT; ++attempts) {
    MachineState& machine = machines[pollIndex];
    pollIndex = (pollIndex + 1) % MACHINE_COUNT;
    if (!machineAllowsRemoteRs485(machine)) {
      continue;
    }
    sendPoll(machine);
    return;
  }
}

void markStaleMachines() {
  const unsigned long now = millis();
  for (size_t index = 0; index < MACHINE_COUNT; ++index) {
    MachineState& machine = machines[index];
    if (!machine.active) {
      continue;
    }
    if (machine.lastSeenMs == 0 || now - machine.lastSeenMs > STALE_MACHINE_MS) {
      machine.online = false;
      if (machine.commMode == COMM_MODE_NORMAL) {
        machine.healthy = false;
      }
      if (machine.commMode == COMM_MODE_SAFE) {
        machine.lastReason = "safe_mode_local_only";
        if (machine.faultCode.length() == 0 || machine.faultCode == "NONE" || machine.faultCode == "OFFLINE") {
          machine.faultCode = "LOCAL_ONLY";
        }
      } else if (machine.faultCode.length() == 0 || machine.faultCode == "NONE") {
        machine.faultCode = "OFFLINE";
      }
    }
  }
}

void pulseLegacyRelay(uint8_t relayIndex, unsigned long pulseMs) {
  if (relayIndex < 1 || relayIndex > F4_RELAY_COUNT) {
    return;
  }
  setRelayState(relayIndex, true);
  unsigned long startedAt = millis();
  while (millis() - startedAt < pulseMs) {
    readRs485();
    mqttClient.loop();
    delay(5);
  }
  setRelayState(relayIndex, false);
}

void setMachineCommMode(MachineState& machine, MachineCommMode mode) {
  machine.commMode = mode;
  if (mode == COMM_MODE_SAFE) {
    clearPendingForNode(machine.nodeId);
    machine.online = false;
    machine.running = false;
    machine.lastReason = "safe_mode_local_only";
    if (machine.faultCode.length() == 0 || machine.faultCode == "NONE" || machine.faultCode == "OFFLINE") {
      machine.faultCode = "LOCAL_ONLY";
    }
  } else {
    machine.lastReason = "normal_mode";
    if (machine.faultCode == "LOCAL_ONLY") {
      machine.faultCode = machine.lastSeenMs > 0 ? "NONE" : "OFFLINE";
    }
  }
  saveRuntimeConfig();
}

void translateBridgeCommand(const JsonDocument& document) {
  const String commandId = document["id"] | String("cmd_") + millis();
  const String type = document["type"] | "credit";
  const String source = document["source"] | "cloud";
  MachineState* machine = findMachineFromJson(document["machine"], document["machine_id"], document["piste"]);
  if (machine == nullptr) {
    publishBridgeAck(commandId, "UNKNOWN", type, "FAULT", "error", "Unknown machine target");
    postCommandAckToAppsScript(commandId, "UNKNOWN", type, source, "failed", "FAULT", "Unknown machine target");
    return;
  }

  if (type == "set_comm_mode") {
    String requestedMode = "NORMAL";
    if (!document["comm_mode"].isNull()) {
      requestedMode = document["comm_mode"].as<const char*>();
    } else if (!document["value"].isNull()) {
      requestedMode = document["value"].as<const char*>();
    }
    const MachineCommMode nextMode = parseCommMode(requestedMode, machine->commMode);
    setMachineCommMode(*machine, nextMode);
    publishBridgeAck(commandId, machine->name, type, "COMPLETED", "completed", String("Communication mode set to ") + machineCommModeName(nextMode));
    postCommandAckToAppsScript(commandId, machine->name, type, source, "completed", "COMPLETED", String("Communication mode set to ") + machineCommModeName(nextMode));
    return;
  }

  if (!machineAllowsRemoteRs485(*machine)) {
    const String note = machine->commMode == COMM_MODE_SAFE
      ? "Machine is in SAFE mode and stays local-only until the admin switches it back to NORMAL"
      : "Machine is not configured as active on this bridge";
    publishBridgeAck(commandId, machine->name, type, "FAULT", "rejected", note);
    postCommandAckToAppsScript(commandId, machine->name, type, source, "failed", "FAULT", note);
    return;
  }

  if (type == "credit") {
    const int amountCents = document["amount_cents"].isNull() ? (document["amount"] | 0) : (document["amount_cents"] | 0);
    const int sessions = document["sessions"] | 1;
    sendRs485(machine->nodeId, "ADD_CREDIT", String(amountCents), String(sessions), commandId, source, type, String(amountCents));
    publishBridgeAck(commandId, machine->name, type, "RECEIVED", "accepted", "ADD_CREDIT queued to RS485");
    postCommandAckToAppsScript(commandId, machine->name, type, source, "queued", "RECEIVED", "ADD_CREDIT queued to RS485");
  } else if (type == "enable") {
    sendRs485(machine->nodeId, "ENABLE", "", "", commandId, source, type, "");
    publishBridgeAck(commandId, machine->name, type, "RECEIVED", "accepted", "ENABLE queued to RS485");
    postCommandAckToAppsScript(commandId, machine->name, type, source, "queued", "RECEIVED", "ENABLE queued to RS485");
  } else if (type == "disable") {
    sendRs485(machine->nodeId, "DISABLE", "", "", commandId, source, type, "");
    publishBridgeAck(commandId, machine->name, type, "RECEIVED", "accepted", "DISABLE queued to RS485");
    postCommandAckToAppsScript(commandId, machine->name, type, source, "queued", "RECEIVED", "DISABLE queued to RS485");
  } else if (type == "reset_fault") {
    sendRs485(machine->nodeId, "RESET_FAULT", "", "", commandId, source, type, "");
    publishBridgeAck(commandId, machine->name, type, "RECEIVED", "accepted", "RESET_FAULT queued to RS485");
    postCommandAckToAppsScript(commandId, machine->name, type, source, "queued", "RECEIVED", "RESET_FAULT queued to RS485");
  } else if (type == "refresh_status") {
    sendRs485(machine->nodeId, "GET_STATUS", "", "", commandId, source, type, "");
    publishBridgeAck(commandId, machine->name, type, "RECEIVED", "accepted", "GET_STATUS queued to RS485");
    postCommandAckToAppsScript(commandId, machine->name, type, source, "queued", "RECEIVED", "GET_STATUS queued to RS485");
  } else if (type == "test_output") {
    const int outputIndex = document["output"] | 0;
    const int outputState = document["state"] | 0;
    sendRs485(machine->nodeId, "TEST_OUTPUT", String(outputIndex), String(outputState), commandId, source, type, String(outputIndex));
    publishBridgeAck(commandId, machine->name, type, "RECEIVED", "accepted", "TEST_OUTPUT queued to RS485");
    postCommandAckToAppsScript(commandId, machine->name, type, source, "queued", "RECEIVED", "TEST_OUTPUT queued to RS485");
  } else if (type == "legacy_pulse") {
    const bool maintenanceMode = document["maintenance_mode"] | false;
    const uint8_t relayIndex = static_cast<uint8_t>(document["relay"] | 0);
    const unsigned long pulseMs = document["duration_ms"].isNull() ? LEGACY_PULSE_DEFAULT_MS : static_cast<unsigned long>(document["duration_ms"].as<unsigned long>());
    if (!maintenanceMode) {
      publishBridgeAck(commandId, machine->name, type, "FAULT", "rejected", "legacy_pulse requires maintenance_mode=true");
      postCommandAckToAppsScript(commandId, machine->name, type, source, "failed", "FAULT", "legacy_pulse requires maintenance_mode=true");
      return;
    }
    pulseLegacyRelay(relayIndex, pulseMs);
    publishBridgeAck(commandId, machine->name, type, "COMPLETED", "completed", "Legacy relay pulse executed");
    postCommandAckToAppsScript(commandId, machine->name, type, source, "completed", "COMPLETED", "Legacy relay pulse executed");
  } else {
    publishBridgeAck(commandId, machine->name, type, "FAULT", "error", "Unsupported command type");
    postCommandAckToAppsScript(commandId, machine->name, type, source, "failed", "FAULT", "Unsupported command type");
  }
}

void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
  String body;
  body.reserve(length);
  for (unsigned int index = 0; index < length; ++index) {
    body += static_cast<char>(payload[index]);
  }
  Serial.printf("MQTT RX [%s] %s\n", topic, body.c_str());

  StaticJsonDocument<768> document;
  DeserializationError error = deserializeJson(document, body);
  if (error) {
    publishBridgeAck(String("invalid_") + millis(), "UNKNOWN", "unknown", "FAULT", "error", "Invalid JSON payload on command topic");
    return;
  }
  translateBridgeCommand(document);
}

bool connectMqtt() {
  if (!ethConnected) {
    return false;
  }
  mqttTlsClient.setInsecure();
  mqttClient.setServer(configMqttHost.c_str(), configMqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(45);
  mqttClient.setSocketTimeout(10);
  mqttClient.setBufferSize(2048);

  String clientId = configDeviceId + "-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
  const bool ok = mqttClient.connect(clientId.c_str(), configMqttUsername.c_str(), configMqttPassword.c_str());
  if (ok) {
    mqttClient.subscribe(configTopicCmd.c_str(), 1);
    mqttDownSinceMs = 0;
    publishStatusHeartbeat();
    lcdShow("MQTT OK", localIpString());
    return true;
  }
  if (mqttDownSinceMs == 0) {
    mqttDownSinceMs = millis();
  }
  return false;
}

void startCloudUplink() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.setHostname(configDeviceId.c_str());
  WiFi.begin();
}

void setup() {
  Serial.begin(115200);
  bootMs = millis();
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  loadRuntimeConfig();

  Wire.begin(F4_I2C_SDA_PIN, F4_I2C_SCL_PIN, 400000U);

  for (uint8_t pin = 0; pin < 4; ++pin) {
    relayBoard.pinMode(pin, INPUT);
  }
  for (uint8_t relayPin = F4_RELAY_FIRST_PCF_PIN; relayPin < F4_RELAY_FIRST_PCF_PIN + F4_RELAY_COUNT; ++relayPin) {
    relayBoard.pinMode(relayPin, OUTPUT);
  }
  relayBoard.begin();
  allRelaysOff();

  oled.begin(SSD1306_SWITCHCAPVCC, F4_OLED_ADDRESS);
  lcdShow("BOOTING", configDeviceId);
  initSdCard();

  const bool forceConfigPortal = digitalRead(CONFIG_BUTTON_PIN) == LOW;
  const bool wifiCredentialsMissing = WiFi.SSID().length() == 0;
  if (forceConfigPortal || !configIsComplete() || wifiCredentialsMissing) {
    openConfigPortal(forceConfigPortal);
    lcdShow("CONFIG SAVED", configDeviceId);
  }

  RS485Serial.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  startCloudUplink();
}

void loop() {
  const unsigned long now = millis();

  const wl_status_t wifiStatus = WiFi.status();
  const IPAddress localIp = WiFi.localIP();
  const bool linkUp = wifiStatus == WL_CONNECTED;
  const bool hasIp = localIp[0] || localIp[1] || localIp[2] || localIp[3];
  if (linkUp && hasIp) {
    ethConnected = true;
    ethDownSinceMs = 0;
  } else {
    ethConnected = false;
    if (ethDownSinceMs == 0) {
      ethDownSinceMs = now;
    }
  }
  if (!ethConnected && ethDownSinceMs != 0 && now - ethDownSinceMs > ETH_DOWN_RESTART_MS) {
    restartBoard("Cloud uplink down too long");
  }

  if (ethConnected) {
    if (!mqttClient.connected()) {
      if (now - lastMqttAttemptMs >= MQTT_RETRY_MS) {
        lastMqttAttemptMs = now;
        connectMqtt();
      }
      if (mqttDownSinceMs != 0 && now - mqttDownSinceMs > MQTT_DOWN_RESTART_MS) {
        restartBoard("MQTT down too long");
      }
    } else {
      mqttDownSinceMs = 0;
      mqttClient.loop();
    }
    if (now - lastOfflineFlushMs >= OFFLINE_FLUSH_MS) {
      lastOfflineFlushMs = now;
      flushOfflineQueue();
    }
  }

  readRs485();
  expirePendingCommands();

  if (now - lastPollMs >= POLL_INTERVAL_MS) {
    lastPollMs = now;
    pollNextMachine();
  }

  markStaleMachines();

  if (now - lastHeartbeatMs >= HEARTBEAT_MS) {
    lastHeartbeatMs = now;
    publishStatusHeartbeat();
  }
  if (now - lastStatusPushMs >= STATUS_PUSH_MS) {
    lastStatusPushMs = now;
    pushSnapshotToAppsScript();
  }

  if (now - lastDisplayRefreshMs >= OLED_REFRESH_MS) {
    lastDisplayRefreshMs = now;
    lastOfflineQueueDepth = countOfflineQueueRecords();
    refreshOledStatus();
  }

  if (now - lastHealthLogMs >= 300000UL) {
    lastHealthLogMs = now;
    Serial.printf("ETH=%d MQTT=%d heap=%u\n", ethConnected, mqttClient.connected(), ESP.getFreeHeap());
  }
}