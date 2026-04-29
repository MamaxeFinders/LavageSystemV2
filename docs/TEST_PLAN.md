# Clean Wash V2 Test Plan

## Firmware and bridge checks
1. F4 boots, OLED updates, no relay pulse at startup.
2. F4 switch modes reflected in snapshot payload.
3. CAISSE/ASPI boot local mode and still operate with central disconnected.
4. RS485 poll returns `STATUS` and `ACK`.
5. `ADD_CREDIT` path returns `RECEIVED` then `APPLIED` then status refresh.
6. Remote `disable` command puts selected CAISSE/ASPI in `HORS SERVICE` state.
7. Remote `enable` command restores selected CAISSE/ASPI to active state.

## Provisioning checks
1. New board with empty preferences reports `UNASSIGNED` and `board_uid`.
2. Assignment command stores machine id and rs485 id in Preferences.
3. Hidden unassign long press resets assignment.

## Apps Script checks
1. `Carwash Setup` menu appears in Google Sheets.
2. Setup sidebar can save properties and status shows configured/missing.
3. EMQX publish test works.
4. Stripe connection test works.
5. Sheet write test logs in `Setup_Log`.

## Payment/ack checks
1. Stripe paid event publishes credit command.
2. Command ack updates payment status.
3. Publish fail sets `manual_review_required = TRUE`.
4. Fault ack sets final status to `PAID_NO_MACHINE_ACK`.

## Reliability checks
1. MQTT down does not stop local CAISSE/ASPI operation.
2. Apps Script write failure does not stop local machine operation.
3. Missing SD card on F4 only creates warning behavior.
