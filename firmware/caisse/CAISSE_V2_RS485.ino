#include <PCF8574.h>
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>
#include <DHT.h>
#include <WiFiManager.h>
#include <Preferences.h>

#include "../shared/rs485_protocol.h"

using namespace RS485Proto;

WiFiManager wm;
WiFiManagerParameter custom_field;
Preferences preferences;

// Verify these pins on the actual CAISSE controller wiring.
#define RS485_TX_PIN 13
#define RS485_RX_PIN 16
HardwareSerial RS485Serial(1);

#define DHTPIN 32
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

const char* DEVICE_TYPE = "CAISSE";
const char* PREF_NAMESPACE = "caisse-v2";
const char* PREF_DEVICE_ID = "device_id";
const char* PREF_DEVICE_TYPE = "device_type";

int deviceId = 1;
String deviceName = "CAISSE";
String rxLineBuffer;

unsigned int ALARMcount = 0;
const unsigned int ALARMcountMAX = 3;
long creditDeciCents = 0;
unsigned long previousCreditTickMs = 0;
bool presostatFault = false;
bool programStarted = false;
int selectedProgram = 0;
bool gelOutputEnabled = false;
bool machineEnabled = true;
bool machineFault = false;
String lastFaultCode = "NONE";
unsigned long lastHeartbeatMs = 0;
unsigned long lastIdleScreenMs = 0;
unsigned long lastStatusPushMs = 0;
uint16_t localEventSeq = 0;

PCF8574 pcf8574_in1(0x22, 4, 5);
PCF8574 pcf8574_in2(0x21, 4, 5);
PCF8574 pcf8574_out1(0x24, 4, 5);
PCF8574 pcf8574_out2(0x25, 4, 5);

const unsigned long CREDIT_DECREMENT_INTERVAL = 2000;
const int CREDIT_DECREMENT_DECI[] = {24, 24, 22, 24, 30, 0, 0, 30};
const long CREDIT_INPUT_CENTS[] = {100, 200, 300, 400};

int relay_out_sequence[8][16] = {
  {0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0},
  {1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
  {0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

String InputDef[] = {"COIN", "COIN", "COIN", "COIN", "NA", "SHOCK", "GEL", "PRESOSTAT"};
String InputButton[] = {"BUTTON", "BUTTON", "BUTTON", "BUTTON", "JANTES", "NA", "NA", "STOP"};
String ProgDisplay[] = {" 1    ", " 2    ", " 3    ", " 4    ", "JANTES", "NA", "NA", " STOP "};
int ALARMoutput = 6;   // Verify Y15 mapping on installed board.
int PUMPoutput = 9;    // Verify Y9 pump inhibit mapping on installed board.
int TimePreStart = 3;

int Standby_Output[] =   {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int GEL_Output[] =       {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int PRESOSTAT_Output[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
int allOFF_Output[] =    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

LiquidCrystal_I2C lcd(0x27, 16, 2);

void showDisabledScreen() {
  displayMessage("BONJOUR         ", "HORS SERVICE    ", true);
}

void applyMachineEnabledState(bool enabled) {
  machineEnabled = enabled;
  if (!machineEnabled) {
    creditDeciCents = 0;
    programStarted = false;
    selectedProgram = 0;
    activateRelays(allOFF_Output, -1);
    showDisabledScreen();
  } else {
    activateRelays(Standby_Output, -1);
    displayMessage("BONJOUR         ", "INSEREZ PIECE   ", true);
  }
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

String centsToEuroText(long centsRounded) {
  long euros = centsRounded / 100;
  long cents = labs(centsRounded % 100);
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%ld.%02ld E", euros, cents);
  return String(buffer);
}

long roundedCreditCents() {
  if (creditDeciCents <= 0) {
    return 0;
  }
  return (creditDeciCents + 5) / 10;
}

void displayMessage(const String& messageL1, const String& messageL2, bool clearLCD) {
  if (clearLCD) {
    lcd.clear();
  }
  lcd.setCursor(0, 0);
  lcd.print(messageL1.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(messageL2.substring(0, 16));
  Serial.println(messageL1 + " & " + messageL2);
}

void activateRelays(int* outputStatus, int forceLow) {
  for (int i = 0; i < 16; ++i) {
    const bool relayOn = outputStatus[i] == 1 && (i + 1) != forceLow;
    if (i < 8) {
      pcf8574_out1.digitalWrite(i, relayOn ? LOW : HIGH);
    } else {
      pcf8574_out2.digitalWrite(i - 8, relayOn ? LOW : HIGH);
    }
  }
}

int getInputIndexINPUTSTATUS(uint8_t inputStatus) {
  for (int i = 0; i < 8; ++i) {
    if ((inputStatus & (1 << i)) == 0) {
      return i + 1;
    }
  }
  return -1;
}

int getInputIndexBUTTON(uint8_t inputStatus) {
  for (int i = 7; i >= 0; --i) {
    if ((inputStatus & (1 << i)) == 0) {
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
  payload = setPayloadValue(payload, "run", boolAsString(programStarted));
  payload = setPayloadValue(payload, "ok", boolAsString(!machineFault));
  payload = setPayloadValue(payload, "en", boolAsString(machineEnabled));
  payload = setPayloadValue(payload, "preso", boolAsString(presostatFault));
  payload = setPayloadValue(payload, "gel", boolAsString(gelOutputEnabled));
  payload = setPayloadValue(payload, "shock", boolAsString(ALARMcount > 0));
  payload = setPayloadValue(payload, "fault", lastFaultCode.length() == 0 ? "NONE" : lastFaultCode);
  payload = setPayloadValue(payload, "reason", reason);

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  if (!isnan(temp)) {
    payload = setPayloadValue(payload, "temp", String(temp, 1));
  }
  if (!isnan(hum)) {
    payload = setPayloadValue(payload, "hum", String(hum, 1));
  }
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

void sendEvent(const char* eventCode, const String& value, uint16_t seq) {
  sendFrame("EVENT", eventCode, value, seq);
}

const char* boolAsString(bool value) {
  return value ? "1" : "0";
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

  if (frame.command == "RESET_FAULT") {
    machineFault = false;
    presostatFault = false;
    ALARMcount = 0;
    lastFaultCode = "NONE";
    sendAck(frame.seq, "APPLIED", "fault_reset");
    sendStatus(frame.seq, "fault_reset");
    return;
  }

  if (frame.command == "TEST_OUTPUT") {
    if (programStarted) {
      sendFault(frame.seq, "BUSY", "Program running");
      return;
    }
    const int outputIndex = frame.arg1.toInt();
    const int outputState = frame.arg2.toInt();
    if (outputIndex < 1 || outputIndex > 16) {
      sendFault(frame.seq, "BAD_OUTPUT", "Output index must be 1..16");
      return;
    }
    sendAck(frame.seq, "RUNNING", String("output=") + outputIndex);
    if (outputIndex <= 8) {
      pcf8574_out1.digitalWrite(outputIndex - 1, outputState ? LOW : HIGH);
    } else {
      pcf8574_out2.digitalWrite(outputIndex - 9, outputState ? LOW : HIGH);
    }
    sendAck(frame.seq, "COMPLETED", outputState ? "on" : "off");
    sendStatus(frame.seq, "test_output");
    return;
  }

  sendFault(frame.seq, "BAD_CMD", frame.command.c_str());
}

void cooperativeDelay(unsigned long delayMs) {
  unsigned long startedAt = millis();
  while (millis() - startedAt < delayMs) {
    serviceRemote();
    delay(5);
  }
}

void saveParamCallback() {
  String customValueStr;
  if (wm.server->hasArg("customfieldid")) {
    customValueStr = wm.server->arg("customfieldid");
  }
  int newId = customValueStr.toInt();
  if (newId >= 1 && newId <= 4) {
    saveDeviceId(newId);
  }
}

void setupWifiManager() {
  pinMode(0, INPUT_PULLUP);
  if (digitalRead(0) != LOW) {
    WiFi.mode(WIFI_OFF);
    return;
  }

  WiFi.mode(WIFI_AP);
  const char* custom_radio_str =
    "<br/><label for='customfieldid'>Choisir CAISSE</label><br><br>"
    "<input type='radio' name='customfieldid' value='1' checked> 1<br>"
    "<input type='radio' name='customfieldid' value='2'> 2<br>"
    "<input type='radio' name='customfieldid' value='3'> 3<br>"
    "<input type='radio' name='customfieldid' value='4'> 4";
  new (&custom_field) WiFiManagerParameter(custom_radio_str);
  wm.addParameter(&custom_field);
  wm.setSaveParamsCallback(saveParamCallback);
  std::vector<const char*> menu = {"wifi", "info", "sep", "param", "sep", "restart", "exit"};
  wm.setMenu(menu);
  wm.setClass("invert");
  wm.setConfigPortalTimeout(20);

  char wifiName[32];
  snprintf(wifiName, sizeof(wifiName), "CAISSE_%d", deviceId);
  displayMessage("CONFIG PORTAL   ", wifiName, true);
  wm.startConfigPortal(wifiName);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  pinMode(0, INPUT_PULLUP);

  preferences.begin(PREF_NAMESPACE, true);
  deviceId = preferences.getInt(PREF_DEVICE_ID, 1);
  preferences.end();
  saveDeviceId(deviceId);

  cooperativeDelay(deviceId * 100);

  setupWifiManager();
  RS485Serial.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  dht.begin();

  for (int i = 0; i < 8; ++i) {
    pcf8574_in1.pinMode(i, INPUT);
    pcf8574_in2.pinMode(i, INPUT);
    pcf8574_out1.pinMode(i, OUTPUT);
    pcf8574_out2.pinMode(i, OUTPUT);
  }

  pcf8574_in1.begin();
  pcf8574_in2.begin();
  pcf8574_out1.begin();
  pcf8574_out2.begin();

  lcd.init();
  lcd.backlight();
  displayMessage("LOADING         ", "                ", true);
  activateRelays(allOFF_Output, -1);
  cooperativeDelay(200);
  activateRelays(Standby_Output, -1);
  displayMessage("READY           ", deviceName + " " + String(deviceId), true);
  sendStatus(nextLocalSeq(), "boot");
}

void loop() {
  serviceRemote();

  uint8_t inputStatus = pcf8574_in2.digitalReadAll();
  int inputIndex = getInputIndexINPUTSTATUS(inputStatus);

  if (!digitalRead(0)) {
    displayMessage("TEMP: " + String(dht.readTemperature(), 1), "HUM: " + String(dht.readHumidity(), 1), true);
    cooperativeDelay(2000);
    displayMessage("RESET           ", "3 sec           ", true);
    cooperativeDelay(3000);
    if (!digitalRead(0)) {
      wm.resetSettings();
      ESP.restart();
    }
  } else if (inputIndex > 0 && InputDef[inputIndex - 1] == "PRESOSTAT") {
    presostatFault = true;
    machineFault = true;
    lastFaultCode = "PRESOSTAT";
    creditDeciCents = 0;
    activateRelays(PRESOSTAT_Output, -1);
    displayMessage("PROBLEM         ", "PRESOSTAT       ", true);
    sendFault(nextLocalSeq(), "PRESOSTAT", "Pressure safety triggered");
    cooperativeDelay(3000);
    return;
  } else if (inputIndex > 0 && InputDef[inputIndex - 1] == "SHOCK") {
    pcf8574_out2.digitalWrite(ALARMoutput, LOW);
    ++ALARMcount;
    displayMessage("ALARM           ", String(ALARMcount) + "/" + String(ALARMcountMAX), true);
    if (ALARMcount >= ALARMcountMAX) {
      machineFault = true;
      lastFaultCode = "SHOCK";
      creditDeciCents = 0;
      sendFault(nextLocalSeq(), "SHOCK", "Shock input threshold reached");
      ALARMcount = 0;
    }
    cooperativeDelay(3000);
    pcf8574_out2.digitalWrite(ALARMoutput, HIGH);
    return;
  } else if (inputIndex > 0 && InputDef[inputIndex - 1] == "COIN") {
    if (machineEnabled && !machineFault) {
      creditDeciCents += CREDIT_INPUT_CENTS[inputIndex - 1] * 10L;
      displayMessage("                ", "CREDIT : " + centsToEuroText(roundedCreditCents()), true);
      ALARMcount = 0;
      sendStatus(nextLocalSeq(), "coin");
      cooperativeDelay(400);
    }
  } else if (inputIndex > 0 && InputDef[inputIndex - 1] == "GEL" && !programStarted && !gelOutputEnabled) {
    gelOutputEnabled = true;
  } else if ((inputIndex <= 0 || InputDef[inputIndex - 1] != "GEL") && !programStarted && gelOutputEnabled) {
    gelOutputEnabled = false;
  }

  if (creditDeciCents > 0 && machineEnabled && !machineFault) {
    uint8_t actionInput = pcf8574_in1.digitalReadAll();
    int buttonIndex = getInputIndexBUTTON(actionInput);
    if (buttonIndex > 0 && InputButton[buttonIndex - 1] == "STOP") {
      selectedProgram = buttonIndex;
      programStarted = false;
      activateRelays(Standby_Output, -1);
      displayMessage("PROG: STOP      ", "CREDIT : " + centsToEuroText(roundedCreditCents()), true);
      cooperativeDelay(400);
    } else if (buttonIndex > 0 && InputButton[buttonIndex - 1] == "BUTTON" && InputButton[selectedProgram > 0 ? selectedProgram - 1 : 0] != "JANTES") {
      selectedProgram = buttonIndex;
    } else if (buttonIndex > 0 && InputButton[buttonIndex - 1] == "JANTES" && (selectedProgram == 0 || InputButton[selectedProgram - 1] != "BUTTON")) {
      selectedProgram = buttonIndex;
    }

    if (selectedProgram > 0 && InputButton[selectedProgram - 1] != "STOP") {
      if (!programStarted) {
        activateRelays(relay_out_sequence[selectedProgram - 1], PUMPoutput);
        programStarted = true;
        displayMessage("! PRET !        ", "                ", true);
        sendAck(nextLocalSeq(), "RUNNING", String("program=") + selectedProgram);
        for (int countdown = TimePreStart; countdown > 0; --countdown) {
          displayMessage("! PRET !        ", "       " + String(countdown) + "        ", false);
          cooperativeDelay(1000);
        }
      } else {
        activateRelays(relay_out_sequence[selectedProgram - 1], -1);
      }

      unsigned long currentTime = millis();
      if (currentTime - previousCreditTickMs >= CREDIT_DECREMENT_INTERVAL) {
        previousCreditTickMs = currentTime;
        int decrement = CREDIT_DECREMENT_DECI[selectedProgram - 1];
        if (creditDeciCents >= decrement) {
          creditDeciCents -= decrement;
          displayMessage("PROG: " + String(ProgDisplay[selectedProgram - 1]), "CREDIT : " + centsToEuroText(roundedCreditCents()), false);
        } else {
          creditDeciCents = 0;
          programStarted = false;
          activateRelays(Standby_Output, -1);
          sendAck(nextLocalSeq(), "COMPLETED", "credit_empty");
          sendStatus(nextLocalSeq(), "credit_empty");
        }
      }
    }
  } else {
    selectedProgram = 0;
    programStarted = false;
    if (!machineEnabled) {
      activateRelays(allOFF_Output, -1);
    } else if (gelOutputEnabled) {
      activateRelays(GEL_Output, -1);
    } else {
      activateRelays(Standby_Output, -1);
    }
    if (millis() - lastIdleScreenMs >= 4000) {
      if (!machineEnabled) {
        showDisabledScreen();
      } else {
        displayMessage("BONJOUR         ", machineFault ? "HORS SERVICE    " : "INSEREZ PIECE   ", false);
      }
      lastIdleScreenMs = millis();
    }
  }

  if (millis() - lastHeartbeatMs >= 5000) {
    sendStatus(0, "heartbeat");
    lastHeartbeatMs = millis();
  }
  if (millis() - lastStatusPushMs >= 15000) {
    sendStatus(0, "idle_status");
    lastStatusPushMs = millis();
  }

  delay(10);
}