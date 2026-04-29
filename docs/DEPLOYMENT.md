# Clean Wash V2 Deployment

## Overview

This deployment keeps payment, remote control, and reporting in Google Apps Script plus Google Sheets, while every machine stays locally autonomous. The central KC868-F4 bridges cloud commands to RS485, shows bridge health on its OLED, and buffers unsent cloud data to SD storage when the uplink is down.

## Deliverables

- `firmware/central/F4_Central_MQTT_RS485_Master.ino`
- `firmware/shared/rs485_protocol.h`
- `firmware/shared/rs485_protocol.cpp`
- `firmware/caisse/CAISSE_V2.ino`
- `firmware/aspi/ASPI_V2.ino`
- `apps_script/Code.gs`
- `apps_script/Index.html`
- `apps_script/appsscript.json`

## 1. Verify hardware wiring

- Confirm the KC868-F4 board wiring matches the firmware assumptions:
  - I2C on GPIO8/GPIO18 for the OLED and PCF8574
  - RS485 UART on GPIO16/GPIO17
  - PCF8574 address `0x24`
  - SD card CS pin constant in firmware is verified on the installed board before deployment
- Confirm the CAISSE and ASPI RS485 TX/RX pins against the installed ESP32 controller wiring.
- Confirm relay output indexes used for `ALARMoutput`, `PUMPoutput`, and any maintenance `TEST_OUTPUT` action.
- Confirm the OLED I2C address `0x3C` and any site-specific board revisions before upload.

## 2. EMQX Cloud setup

- Create one MQTT client for the central bridge.
- Set the bridge topics exactly as follows:
  - `carwash/site1/bridge/cmd`
  - `carwash/site1/bridge/ack`
  - `carwash/site1/bridge/status`
- Create or enable the EMQX HTTPS Publish API credentials used by Apps Script.

## 3. Google Apps Script setup

- Create a standalone Apps Script project.
- Copy the three files from `apps_script/` into that project.
- Set Script Properties:
  - `SPREADSHEET_ID`
  - `EMQX_API_BASE`
  - `EMQX_APP_ID`
  - `EMQX_APP_SECRET`
  - `MQTT_TOPIC_CMD` with value `carwash/site1/bridge/cmd`
  - `STRIPE_MACHINE_LINK_BASE` with the machine-specific Stripe redirect base
- Deploy as Web App.
- Open the deployed URL once so the sheets schema is created.

## 4. Google Sheets schema

The Apps Script project creates these tabs automatically:

- `Machines`
- `Events`
- `Payments`
- `Commands`
- `DailySummary`

## 5. Central KC868-F4 firmware

- Open `firmware/central/F4_Central_MQTT_RS485_Master.ino` in Arduino IDE or PlatformIO.
- Install required libraries:
  - `PubSubClient`
  - `ArduinoJson`
  - `PCF8574`
  - `Adafruit GFX`
  - `Adafruit SSD1306`
- `WiFiManager`
- `SD`
- MQTT and Apps Script credentials no longer need to be hardcoded and uploaded again in firmware.
- Hold `BOOT` during power-up to open the bridge setup portal.
- Connect to the bridge AP from a phone or laptop and enter:
  - bridge device ID
  - site ID
  - MQTT host
  - MQTT port
  - MQTT username
  - MQTT password
  - command topic
  - ack topic
  - status topic
  - Apps Script web app URL
  - number of CAISSE units to track on this site
  - number of ASPI units to track on this site
  - number of AIR units to track on this site
- These values are stored in Preferences/NVS on the ESP32.
- On normal reboot, the bridge reuses the last valid saved configuration automatically and does not need the admin to enter settings again.
- The setup portal only needs to be opened on first install, if configuration is missing, or when the admin explicitly holds `BOOT` to change settings.
- The bridge only tracks and polls the configured number of CAISSE, ASPI, and AIR units. Unused slots stay out of the active machine list.
- Each active machine can also be switched by the admin between `NORMAL` mode and `SAFE` mode from the web dashboard.
- `NORMAL` mode means full bridge behavior: polling, remote credit, remote enable/disable, and payment eligibility checks.
- `SAFE` mode means the unit stays local-only: the bridge stops relying on RS485 for that machine and remote credit/payment commands are rejected, while the machine can still operate on its own local logic.
- The OLED shows bridge identity, uplink state, MQTT state, offline queue depth, and machine availability summary.
- If the uplink is unavailable, Apps Script uploads are stored on the SD card and retried automatically when connectivity comes back.
- Keep the F4 relay outputs unused for normal credit flow. They remain OFF at startup by design.
- Upload and verify in the serial monitor:
  - WiFi uplink gets an IP address
  - MQTT subscribes successfully
  - periodic RS485 polling starts
  - snapshots reach the Apps Script endpoint every 10 seconds
  - OLED status updates continuously
  - SD queue depth increases during uplink loss and drains after reconnection

## 6. CAISSE deployment

- Start from `firmware/caisse/CAISSE_V2.ino`.
- Keep the mature local runtime behavior intact:
  - pressure safety input
  - gel
  - shock
  - countdown
  - user buttons
  - relay sequencing
  - LCD
  - WiFiManager setup mode
  - DHT
- Hold `BOOT` at startup to reopen the CAISSE setup portal later without uploading firmware again.
- Set the device ID during installation via WiFiManager.
- On normal reboot, CAISSE starts directly with the last saved device ID and does not wait for WiFiManager.
- Verify that startup leaves every relay OFF or in the explicit standby pattern only. No pulse test is allowed.

## 7. ASPI deployment

- Start from `firmware/aspi/ASPI_V2.ino`.
- Keep the mature local runtime behavior intact:
  - local coin pulse inputs
  - stop button
  - vacuum and air output logic
  - LCD
- Hold `BOOT` at startup to open the ASPI setup portal and set the device ID without uploading firmware again.
- The ASPI device ID is stored in Preferences/NVS.
- On normal reboot, ASPI starts directly with the last saved device ID.
- Verify that startup does not pulse any relay.

## 8. QR and Stripe flow

- Point each machine QR code to the Apps Script web app using `action=stripe_entry`.
- Example:
  - `https://script.google.com/macros/s/.../exec?action=stripe_entry&machine_id=1`
- The page checks the latest machine snapshot before allowing the Stripe redirect.
- If the machine is offline, disabled, or faulty, payment is blocked.

## 9. Validation checklist

- Trigger an admin add-credit command from the web app.
- Confirm Apps Script publishes to EMQX.
- Confirm the KC868-F4 receives the MQTT command and sends RS485 `ADD_CREDIT`.
- Confirm the slave returns staged acknowledgements.
- Confirm the central bridge forwards staged acknowledgements to:
  - MQTT `carwash/site1/bridge/ack`
  - Apps Script `command_ack`
- Confirm the `Machines` sheet reflects current health and credit.
- Confirm the payment entry page blocks access when a machine is disabled or faulty.
- Disconnect the uplink temporarily and confirm pending status/event uploads are written to SD and replayed when the uplink returns.

## 10. Operating notes

- Local autonomy is the primary rule. If WiFi, MQTT, the SD queue, or the central bridge fails, the CAISSE and ASPI boards continue working locally.
- `SAFE` mode is the operator-controlled version of that same principle for one machine: keep the unit running locally even when a partial communication path is unreliable.
- Remote credit always becomes an RS485 `ADD_CREDIT` request. It is not converted into a direct relay pulse in normal mode.
- `legacy_pulse` exists only as explicit maintenance mode and requires `maintenance_mode=true` in the command payload.
- Apps Script and Stripe secrets stay in Script Properties on Google side. ESP32-side bridge settings now live in Preferences/NVS and can be edited from the device setup portal instead of by uploading firmware again.
- The intended operating mode is: configure once, reboot fast, and only reopen configuration when an admin explicitly requests a change.