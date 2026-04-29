# CHANGELOG

## 2026-04-29
- Added requested deliverable firmware paths:
  - `firmware/central/F4_Central_MQTT_RS485_Master.ino`
  - `firmware/caisse/CAISSE_V2.ino`
  - `firmware/aspi/ASPI_V2.ino`
- Central F4 updates:
  - Added lever switch mode handling (maintenance, local-only, settings unlock, service test).
  - Added remote-command gating based on lever switch mode.
  - Added mode flags to MQTT heartbeat and Apps Script snapshot payloads.
  - Changed prolonged cloud/MQTT outages from restart behavior to warning-only behavior.
- CAISSE/ASPI updates:
  - Added board UID and assignment persistence fields.
  - Added `ASSIGN_DEVICE` support.
  - Added unassigned status payload fields.
  - Added hidden long-press unassign reset backup.
- Apps Script updates:
  - Added `Carwash Setup` custom menu and setup sidebar backend.
  - Added secure Script Properties save/status flow.
  - Added setup test functions (EMQX, Stripe, sheet write, config).
  - Expanded sheet schema and added provisioning actions.
  - Improved payment logs with manual-review and final-status fields.
- Dashboard update:
  - Added provisioning queue table in web UI.
