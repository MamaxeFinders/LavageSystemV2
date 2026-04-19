# Clean Wash V2 Architecture

## Recommended single stack
Use **Google Apps Script + Google Sheets only** for the cloud/backend/UI layer.

### Why this is the best fit
- **Easiest to deploy and maintain** with your current skills and existing Google setup.
- **Reliable enough** because all critical real-time control stays local on the machines and on the central F4 bridge.
- **No Firebase complexity** for authentication, security rules, and deployment.
- **Google Sheets** remains excellent for accounting exports and historical reporting.
- **Apps Script web app** can provide the admin dashboard and remote control UI.
- **MQTT** is used only between the central bridge and EMQX for command delivery, which removes dependence on router port forwarding.

## Final V2 architecture

### Local / machine layer
- The installed number of `CAISSE`, `ASPI`, and `AIR` units is configurable on the central bridge instead of being fixed forever in firmware.
- Active `CAISSE` units keep local logic for safety, credit countdown, output sequencing, LCD, and local user buttons.
- Active `ASPI` units keep local logic for coin pulse, stop button, vacuum/air outputs.
- Active `AIR` units follow the ASPI model later.
- All field boards continue to operate **autonomously** if the central F4 bridge or internet is down.

### Communication modes
- Each active machine can be put into `NORMAL` mode or `SAFE` mode.
- `NORMAL` mode allows full remote communication through the bridge.
- `SAFE` mode keeps the machine in local standalone operation and blocks remote credit/control flows for that machine, which is useful when a partial function such as RS485 communication is unreliable but the local machine logic still works.

### Internal network
- **RS485 half-duplex**
- **Central F4 = single master**
- `CAISSE`, `ASPI`, `AIR` = slaves
- RS485 is used for:
  - status polling
  - add credit
  - faults and alarms
  - acknowledgements
  - remote maintenance commands

### External / cloud layer
- **Central F4** connects outbound to **EMQX Cloud** by MQTT.
- **Google Apps Script** receives Stripe webhooks and admin commands, then publishes MQTT commands to EMQX using the EMQX HTTPS Publish API.
- **Central F4** also sends machine snapshots and events to Apps Script by HTTPS.
- The F4 OLED shows general bridge health and machine summary.
- The F4 SD card is used as an offline spool so unsent snapshots, events, and acknowledgements can be uploaded later after the connection comes back.
- **Google Sheets** stores:
  - current machine state
  - event log
  - payment log
  - admin command log
  - aggregated reporting views
- **Apps Script Web App** serves the admin dashboard UI.

## Why not direct Stripe QR to a static Stripe product anymore
The V2 requirement says users must not be able to pay if a machine is faulty.

So the new QR/payment flow should be:
1. User scans machine QR.
2. Your Apps Script web app checks the current health of that machine from the status sheet.
3. If machine is healthy, the page redirects to Stripe Checkout / Payment Link.
4. If machine is faulty or disabled, the page blocks payment and shows an out-of-service message.

This is much safer than a static Stripe QR that bypasses machine health.

## RS485 protocol v1
Use simple ASCII frames with CRC16 and sequence numbers.

### Master to slave
- `@<node>,PING,<seq>,<crc>`
- `@<node>,GET_STATUS,<seq>,<crc>`
- `@<node>,ADD_CREDIT,<cents>,<seq>,<crc>`
- `@<node>,ENABLE,<seq>,<crc>`
- `@<node>,DISABLE,<seq>,<crc>`
- `@<node>,RESET_FAULT,<seq>,<crc>`
- `@<node>,TEST_OUTPUT,<output>,<state>,<seq>,<crc>`

### Slave to master
- `@<node>,ACK,<seq>,RECEIVED,<crc>`
- `@<node>,ACK,<seq>,APPLIED,<crc>`
- `@<node>,STATUS,<payload>,<seq>,<crc>`
- `@<node>,FAULT,<faultCode>,<seq>,<crc>`
- `@<node>,EVENT,<eventCode>,<value>,<seq>,<crc>`

### Required acknowledgements
For every remote credit or control action you asked for these stages:
1. command received
2. command applied
3. process started (when relevant)
4. process ended / completed
5. fault event if something failed

## Device IDs
Use one common firmware per device family and store the identity in NVS/Preferences.

- CAISSE firmware: same file for nodes 1–4
- ASPI firmware: same file for nodes 1–2
- AIR firmware: same file for node 1

Stored values:
- `device_type` = `CAISSE` / `ASPI` / `AIR`
- `device_id` = integer

Set these values from a setup mode during installation.

## Reporting model in Google Sheets
Use one spreadsheet with these tabs:

### `Machines`
Current state table (one row per machine):
- machine_id
- type
- online
- healthy
- fault_code
- last_seen
- current_credit_cents
- current_program
- running
- pressure safety status
- gel
- shock
- temperature
- humidity
- last_payment_id
- firmware_version

### `Events`
Append-only log:
- timestamp
- machine_id
- event_type
- value_1
- value_2
- source
- seq
- note

### `Payments`
Append-only log:
- timestamp
- stripe_event_id
- machine_id
- amount_cents
- sessions
- status
- source

### `Commands`
Append-only log:
- timestamp
- command_id
- target_machine
- command_type
- value
- source
- status
- ack_stage

### `DailySummary`
Apps Script can compute:
- revenue by machine per day
- sessions per machine per day
- last 10 days rolling view
- week summary

## Repository structure
- `firmware/Central_F4_MQTT_RS485_Master.ino`
- `firmware/shared/rs485_protocol.h`
- `firmware/shared/rs485_protocol.cpp`
- `firmware/caisse/CAISSE_V2_RS485.ino`
- `firmware/aspi/ASPI_V2_RS485.ino`
- `apps_script/Code.gs`
- `apps_script/Index.html`
- `apps_script/appsscript.json`
- `docs/DEPLOYMENT.md`

## Firmware to load
- Load `firmware/caisse/CAISSE_V2_RS485.ino` on CAISSE ESP32 boards.
- Load `firmware/aspi/ASPI_V2_RS485.ino` on ASPI ESP32 boards.
- Load `firmware/Central_F4_MQTT_RS485_Master.ino` on the KC868-F4 central board.

Each device family now has one deployable firmware file in the repository.

## Recommended implementation phases

### Phase 1
- Central F4 as MQTT + RS485 master
- CAISSE integration first
- Apps Script web app + Sheets
- Stripe pre-check page
- live dashboard and remote credit

### Phase 2
- ASPI integration
- AIR integration
- richer troubleshooting pages
- richer analytics and maintenance tools
