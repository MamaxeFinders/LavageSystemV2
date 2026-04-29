# CHANGELOG

## 2026-04-29 (latest)
- Apps Script deployment consolidation:
  - Bound to primary program spreadsheet ID `1bb8spIX9BdwmkA-crni0Jk1OEpsrDHqu1ENX05W30sA`.
  - Added quick-access links and updated setup menu actions.
- Added setup UX improvements:
  - New setup health chips with red/orange/green status states.
  - Added dedicated Google setup validation (`WEB_APP_URL`, `ADMIN_ALLOWLIST`).
- Added Stripe machine mapping module:
  - New sidebar `StripeMappingSidebar.html`.
  - Save/read machine-to-payment-link mapping in `Machines` sheet.
  - Enforced payment-link to machine consistency in webhook resolution.
- Added QR/URL catalog module:
  - New sidebar `PaymentQrSidebar.html`.
  - Generates machine-specific entry URLs and QR image links.
- Time zone standardization:
  - Apps Script project set to `Europe/Paris`.
  - Dashboard timestamp rendering aligned to Paris time.
  - Spreadsheet time zone auto-enforced to Paris from script runtime.
- Stripe amount hardening:
  - Webhook amount uses Stripe `amount_total` as smallest currency unit.
  - EUR path explicitly handled as cents (`1.00 EUR => 100`).
  - Invalid/non-EUR amount events are blocked and flagged for manual review.

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
