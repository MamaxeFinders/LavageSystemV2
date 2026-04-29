# Clean Wash V2 Setup Guide

## 1. Firmware files to use
- `firmware/central/F4_Central_MQTT_RS485_Master.ino`
- `firmware/caisse/CAISSE_V2.ino`
- `firmware/aspi/ASPI_V2.ino`
- `firmware/shared/rs485_protocol.h`
- `firmware/shared/rs485_protocol.cpp`

## 2. Upload order
1. Upload `CAISSE_V2.ino` to each CAISSE board.
2. Upload `ASPI_V2.ino` to each ASPI board.
3. Upload `F4_Central_MQTT_RS485_Master.ino` to KinCony F4.

## 3. Field provisioning flow
1. Fresh CAISSE/ASPI boots as `UNASSIGNED` and reports `board_uid` in status payload.
2. Admin opens provisioning view and maps `board_uid` to machine id and RS485 id.
3. Apps Script publishes `assign_device` command through EMQX.
4. Field board stores assignment to Preferences and starts responding on assigned RS485 id.

## 4. F4 lever switch meaning
- SW1: maintenance mode, blocks remote operation commands.
- SW2: local-only mode, monitoring only.
- SW3: settings unlock, required for assignment/config mode changes.
- SW4: service test mode, required for test/service commands.

## 5. Notes
- Local machine logic always remains primary.
- Cloud failures should not stop CAISSE/ASPI local usage.
- SD card support is optional on central; missing SD is warning only.

## 6. Google account bootstrap (info@efinders.fr)
1. Open the Apps Script project while logged in as `info@efinders.fr`.
2. Use Drive folder `Lavage` with ID `1nRXyDgha3opObmQV1VdYSwIO06ucAbL8`.
3. In Apps Script, run function `bootstrapLavageSheetForEfinders` once.
4. This will:
	- Create or link spreadsheet `Clean Wash V2 - Operations`.
	- Move it into the Drive folder above.
	- Save `SPREADSHEET_ID` in Script Properties.
	- Create all required tabs/headers automatically.

## 7. Setup sidebars and tests
1. In the linked sheet menu, open `Carwash Setup > Open Setup Panel`.
2. Save these required properties:
	- `EMQX_API_BASE`, `EMQX_APP_ID`, `EMQX_APP_SECRET`
	- `MQTT_TOPIC_CMD`, `MQTT_TOPIC_STATUS`, `MQTT_TOPIC_ACK`
	- `STRIPE_WEBHOOK_SECRET`, `STRIPE_SECRET_KEY`
	- `WEB_APP_URL`, `ADMIN_ALLOWLIST`
3. Use `Refresh health` and verify chips become `FUNCTIONAL`.
4. Google checks validate web app URL format and admin allow list emails.

## 8. Stripe mapping and QR catalog
1. Open `Carwash Setup > Open Stripe Mapping Panel`.
2. Map each machine to one or two Stripe Payment Link IDs (`plink_...`).
3. Open `Carwash Setup > Open QR & URL Generator`.
4. Generate and copy one entry URL per machine for sticker QR codes.

## 9. Currency and timestamp rules
1. Stripe credit amount uses `checkout.session.amount_total` (smallest currency unit).
2. For EUR: `1.00 EUR = 100` machine credit units.
3. Non-EUR or invalid amount webhook events are blocked for manual review.
4. Google Apps Script and dashboard timestamps are standardized to Europe/Paris.
