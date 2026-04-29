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
