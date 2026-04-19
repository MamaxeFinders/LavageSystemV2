#include <Wire.h>
#include <PCF8574.h>
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "../shared/rs485_protocol.h"

using namespace RS485Proto;

Preferences preferences;
WiFiManager wm;

// Verify these pins on the actual ASPI controller wiring.
#define RS485_TX_PIN 13
#define RS485_RX_PIN 16
HardwareSerial RS485Serial(1);
#define CONFIG_BUTTON_PIN 0

const char* DEVICE_TYPE = "ASPI";
const char* PREF_NAMESPACE = "aspi-v2";
const char* PREF_DEVICE_ID = "device_id";
const char* PREF_DEVICE_TYPE = "device_type";

int deviceId = 1;
String deviceName = "ASPI";
String rxLineBuffer;

long creditDeciCents = 0;
unsigned long previousCreditTickMs = 0;
int selectedProgram = 4;
bool programStartPending = false;
unsigned long programStartAtMs = 0;
const unsigned long programStartDelay = 3000;
bool machineEnabled = true;
bool machineFault = false;
String lastFaultCode = "NONE";
unsigned long lastHeartbeatMs = 0;
unsigned long lastIdleRefreshMs = 0;
uint16_t localEventSeq = 0;

const int SDA_PIN = 4;
const int SCL_PIN = 5;
const uint32_t I2C_CLOCK_HZ = 50000;

PCF8574 pcf8574_in1(0x22, SDA_PIN, SCL_PIN);
PCF8574 pcf8574_out1(0x24, SDA_PIN, SCL_PIN);

const unsigned long CREDIT_DECREMENT_INTERVAL = 2000;
const int CREDIT_DECREMENT_DECI[] = {12, 12, 0, 0, 0, 1000, 0, 0};
const long CREDIT_INPUT_CENTS[] = {50, 100, 200, 400};

int relay_out_sequence[8][8] = {
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {1, 0, 0, 0, 0, 0, 0, 0},
  {0, 1, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0}
};

String InputDef[] = {"COIN", "COIN", "COIN", "BUTTON", "BUTTON", "STOP", "NA", "NA"};
String ProgDisplay[] = {"NA", "NA", "NA", "<<<--- ASPI", "AIR --->>>", " STOP ", "NA", "NA"};
int Standby_Output[] = {0, 0, 0, 0, 0, 0, 0, 1};
int allOFF_Output[] = {0, 0, 0, 0, 0, 0, 0, 0};

LiquidCrystal_I2C lcd(0x27, 16, 2);
String lastLcdLine1 = "";
String lastLcdLine2 = "";

void showDisabledScreen() {
  displayMessage("BONJOUR         ", "HORS SERVICE    ", true);
}

void applyMachineEnabledState(bool enabled) {
  machineEnabled = enabled;
  if (!machineEnabled) {
    creditDeciCents = 0;
    programStartPending = false;
    selectedProgram = 4;
    activateRelays(allOFF_Output, -1);
    showDisabledScreen();
  } else {
    activateRelays(Standby_Output, -1);
    displayMessage("BONJOUR         ", "INSEREZ PIECE   ", true);
  }
}

const char* boolAsString(bool value) {
  return value ? "1" : "0";
}

uint16_t nextLocalSeq() {
  ++localEventSeq;
  if (localEventSeq == 0) {
    ++localEventSeq;
  }
  return localEventSeq;
}

void saveDeviceId(int newDeviceId) {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putInt(PREF_DEVICE_ID, newDeviceId);
  preferences.putString(PREF_DEVICE_TYPE, DEVICE_TYPE);
  preferences.end();
}

void openConfigPortalIfNeeded() {
  if (digitalRead(CONFIG_BUTTON_PIN) != LOW) {
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  char deviceIdBuffer[8];
  snprintf(deviceIdBuffer, sizeof(deviceIdBuffer), "%d", deviceId);
  WiFiManagerParameter paramDeviceId("device_id", "ASPI device ID (1..2)", deviceIdBuffer, sizeof(deviceIdBuffer));

  wm.setConfigPortalTimeout(180);
  std::vector<const char*> menu = {"param", "sep", "restart", "exit"};
  wm.setMenu(menu);
  wm.setClass("invert");
  wm.addParameter(&paramDeviceId);

  char portalName[24];
  snprintf(portalName, sizeof(portalName), "ASPI_%d_CONFIG", deviceId);
  displayMessage("CONFIG PORTAL   ", portalName, true);
  wm.startConfigPortal(portalName);

  int newId = String(paramDeviceId.getValue()).toInt();
  if (newId >= 1 && newId <= 2) {
    deviceId = newId;
    saveDeviceId(deviceId);
  }
  WiFi.softAPdisconnect(true);
}

long roundedCreditCents() {
  if (creditDeciCents <= 0) {
    return 0;
  }
  return (creditDeciCents + 5) / 10;
}

String centsToEuroText(long centsRounded) {
  long euros = centsRounded / 100;
  long cents = labs(centsRounded % 100);
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%ld.%02ld E", euros, cents);
  return String(buffer);
}

void displayMessage(const String& messageL1, const String& messageL2, bool clearLCD) {
  if (clearLCD) {
    lcd.clear();
  }
  lastLcdLine1 = messageL1.substring(0, 16);
  lastLcdLine2 = messageL2.substring(0, 16);
  lcd.setCursor(0, 0);
  lcd.print(lastLcdLine1);
  lcd.setCursor(0, 1);
  lcd.print(lastLcdLine2);
  Serial.println(lastLcdLine1 + " & " + lastLcdLine2);
}

void activateRelays(int* outputStatus, int forceLow) {
  for (int i = 0; i < 8; ++i) {
    const bool relayOn = outputStatus[i] == 1 && (i + 1) != forceLow;
    pcf8574_out1.digitalWrite(i, relayOn ? LOW : HIGH);
  }
}

int getInputIndexINPUTSTATUS() {
  for (int i = 0; i < 8; ++i) {
    if (pcf8574_in1.digitalRead(i) == LOW) {
      return i + 1;
    }
  }
  return -1;
}

String readRs485Line() {
  while (RS485Serial.available()) {
    char ch = static_cast<char>(RS485Serial.read());
    if (ch == '\n' || ch == '\r') {
      if (rxLineBuffer.length() == 0) {
        continue;
      }
      String line = rxLineBuffer;
      rxLineBuffer = "";
      return line;
    }
    rxLineBuffer += ch;
    if (rxLineBuffer.length() > 255) {
      rxLineBuffer = "";
    }
  }
  return "";
}

void sendFrame(const String& command, const String& arg1, const String& arg2, uint16_t seq) {
  const String frame = buildFrame(static_cast<uint8_t>(deviceId), command, arg1, arg2, seq);
  RS485Serial.println(frame);
  Serial.print("RS485 TX ");
  Serial.println(frame);
}

String buildStatusPayload(const char* reason) {
  String payload;
  payload = setPayloadValue(payload, "type", DEVICE_TYPE);
  payload = setPayloadValue(payload, "id", String(deviceId));
  payload = setPayloadValue(payload, "credit", String(roundedCreditCents()));
  payload = setPayloadValue(payload, "prog", String(selectedProgram));
  payload = setPayloadValue(payload, "run", boolAsString(!programStartPending && creditDeciCents > 0 && selectedProgram > 0));
  payload = setPayloadValue(payload, "ok", boolAsString(!machineFault));
  payload = setPayloadValue(payload, "en", boolAsString(machineEnabled));
  payload = setPayloadValue(payload, "fault", lastFaultCode.length() == 0 ? "NONE" : lastFaultCode);
  payload = setPayloadValue(payload, "reason", reason);
  return payload;
}

void sendAck(uint16_t seq, const char* stage, const String& detail) {
  sendFrame("ACK", stage, detail, seq);
}

void sendStatus(uint16_t seq, const char* reason) {
  sendFrame("STATUS", buildStatusPayload(reason), reason, seq);
}

void sendFault(uint16_t seq, const char* code, const char* detail) {
  sendFrame("FAULT", code, detail, seq);
}

void cooperativeDelay(unsigned long delayMs) {
  unsigned long startedAt = millis();
  while (millis() - startedAt < delayMs) {
    String line = readRs485Line();
    if (line.length() > 0) {
      Frame frame = parseFrame(line);
      if (frame.valid && (frame.nodeId == deviceId || frame.nodeId == BROADCAST_NODE_ID)) {
        sendAck(frame.seq, "RECEIVED", frame.command);
      }
    }
    delay(5);
  }
}

void serviceRemote() {
  String line = readRs485Line();
  if (line.length() == 0) {
    return;
  }

  Frame frame = parseFrame(line);
  if (!frame.valid) {
    return;
  }
  if (frame.nodeId != deviceId && frame.nodeId != BROADCAST_NODE_ID) {
    return;
  }

  sendAck(frame.seq, "RECEIVED", frame.command);

  if (frame.command == "PING") {
    sendStatus(frame.seq, "ping");
    return;
  }

  if (frame.command == "GET_STATUS") {
    sendStatus(frame.seq, "polled");
    return;
  }

  if (frame.command == "ADD_CREDIT") {
    if (!machineEnabled) {
      sendFault(frame.seq, "DISABLED", "Machine disabled");
      return;
    }
    creditDeciCents += static_cast<long>(frame.arg1.toInt()) * 10L;
    machineFault = false;
    lastFaultCode = "NONE";
    displayMessage("REMOTE CREDIT   ", centsToEuroText(roundedCreditCents()), true);
    sendAck(frame.seq, "APPLIED", String("credit=") + roundedCreditCents());
    sendStatus(frame.seq, "credit_added");
    return;
  }

  if (frame.command == "ENABLE") {
    applyMachineEnabledState(true);
    sendAck(frame.seq, "APPLIED", "enabled");
    sendStatus(frame.seq, "enabled");
    return;
  }

  if (frame.command == "DISABLE") {
    applyMachineEnabledState(false);
    sendAck(frame.seq, "APPLIED", "disabled");
    sendStatus(frame.seq, "disabled");
    return;
  }

  if (frame.command == "TEST_OUTPUT") {
    const int outputIndex = frame.arg1.toInt();
    const int outputState = frame.arg2.toInt();
    if (outputIndex < 1 || outputIndex > 8) {
      sendFault(frame.seq, "BAD_OUTPUT", "Output index must be 1..8");
      return;
    }
    sendAck(frame.seq, "RUNNING", String("output=") + outputIndex);
    pcf8574_out1.digitalWrite(outputIndex - 1, outputState ? LOW : HIGH);
    sendAck(frame.seq, "COMPLETED", outputState ? "on" : "off");
    sendStatus(frame.seq, "test_output");
    return;
  }

  if (frame.command == "RESET_FAULT") {
    machineFault = false;
    lastFaultCode = "NONE";
    sendAck(frame.seq, "APPLIED", "fault_reset");
    sendStatus(frame.seq, "fault_reset");
    return;
  }

  sendFault(frame.seq, "BAD_CMD", frame.command.c_str());
}

void setup() {
  Serial.begin(115200);
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  preferences.begin(PREF_NAMESPACE, true);
  deviceId = preferences.getInt(PREF_DEVICE_ID, 1);
  preferences.end();
  saveDeviceId(deviceId);
  openConfigPortalIfNeeded();

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  RS485Serial.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

  for (int i = 0; i < 8; ++i) {
    pcf8574_in1.pinMode(i, INPUT);
    pcf8574_out1.pinMode(i, OUTPUT);
  }
  pcf8574_in1.begin();
  pcf8574_out1.begin();

  lcd.init();
  lcd.backlight();
  activateRelays(allOFF_Output, -1);
  cooperativeDelay(100);
  activateRelays(Standby_Output, -1);
  displayMessage("READY           ", deviceName + " " + String(deviceId), true);
  sendStatus(nextLocalSeq(), "boot");
}

void loop() {
  serviceRemote();

  int inputIndex = getInputIndexINPUTSTATUS();
  if (!digitalRead(0)) {
    cooperativeDelay(2000);
    displayMessage("RESET           ", "3 sec           ", true);
    cooperativeDelay(3000);
    if (!digitalRead(0)) {
      ESP.restart();
    }
  } else if (inputIndex > 0 && InputDef[inputIndex - 1] == "COIN" && machineEnabled) {
    creditDeciCents += CREDIT_INPUT_CENTS[inputIndex - 1] * 10L;
    displayMessage("CREDIT          ", centsToEuroText(roundedCreditCents()), true);
    activateRelays(allOFF_Output, -1);
    sendStatus(nextLocalSeq(), "coin");
    cooperativeDelay(300);
  } else if (inputIndex > 0 && InputDef[inputIndex - 1] == "STOP" && creditDeciCents > 0) {
    creditDeciCents = 0;
    programStartPending = false;
    displayMessage("STOP            ", "                ", true);
    activateRelays(allOFF_Output, -1);
    sendAck(nextLocalSeq(), "COMPLETED", "stop_button");
    sendStatus(nextLocalSeq(), "stop");
    cooperativeDelay(1000);
  } else if (inputIndex > 0 && InputDef[inputIndex - 1] == "BUTTON" && creditDeciCents > 0 && inputIndex != selectedProgram) {
    selectedProgram = inputIndex;
    displayMessage(ProgDisplay[selectedProgram - 1], "DEPART PROGRAMME", true);
    activateRelays(allOFF_Output, -1);
    programStartPending = true;
    programStartAtMs = millis();
    sendAck(nextLocalSeq(), "RUNNING", String("program=") + selectedProgram);
    sendStatus(nextLocalSeq(), "program_select");
  } else if (creditDeciCents > 0 && programStartPending) {
    if (millis() - programStartAtMs >= programStartDelay) {
      programStartPending = false;
    }
  } else if (creditDeciCents > 0 && !programStartPending && machineEnabled && !machineFault) {
    activateRelays(relay_out_sequence[selectedProgram - 1], -1);
    unsigned long currentTime = millis();
    if (currentTime - previousCreditTickMs >= CREDIT_DECREMENT_INTERVAL) {
      previousCreditTickMs = currentTime;
      int decrement = CREDIT_DECREMENT_DECI[selectedProgram - 1];
      if (creditDeciCents >= decrement) {
        creditDeciCents -= decrement;
        displayMessage(ProgDisplay[selectedProgram - 1], centsToEuroText(roundedCreditCents()), false);
      } else {
        creditDeciCents = 0;
        programStartPending = false;
        activateRelays(Standby_Output, -1);
        sendAck(nextLocalSeq(), "COMPLETED", "credit_empty");
        sendStatus(nextLocalSeq(), "credit_empty");
      }
    }
  } else {
    creditDeciCents = max(creditDeciCents, 0L);
    if (machineEnabled) {
      activateRelays(Standby_Output, -1);
    } else {
      activateRelays(allOFF_Output, -1);
    }
    if (millis() - lastIdleRefreshMs >= 4000) {
      if (machineEnabled) {
        displayMessage("BONJOUR         ", "INSEREZ PIECE   ", false);
      } else {
        showDisabledScreen();
      }
      lastIdleRefreshMs = millis();
    }
  }

  if (millis() - lastHeartbeatMs >= 5000) {
    sendStatus(0, "heartbeat");
    lastHeartbeatMs = millis();
  }
}