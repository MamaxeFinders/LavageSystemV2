# Google Sheet Structure (V2)

## Required tabs
- `System_Config`
- `Machines`
- `Assignments`
- `Payments`
- `Commands`
- `Events`
- `Faults`
- `Daily_Summary`
- `Setup_Log`

## Machines minimum fields
- `machine_id`
- `device_type`
- `rs485_id`
- `board_uid`
- `enabled`
- `maintenance_mode`
- `healthy`
- `last_seen`
- `status_age_seconds`
- `status`
- `last_fault`
- `current_credit`
- `active_program`
- `running_state`
- `presostat`
- `gel`
- `shock`
- `temperature`
- `humidity`
- `stripe_payment_link_1`
- `stripe_payment_link_2`
- `stripe_link_active`
- `notes`

## Assignments fields
- `board_uid`
- `detected_device_type`
- `assigned_machine_id`
- `assigned_rs485_id`
- `assignment_status`
- `assigned_at`
- `last_seen`

## Payments fields
- `timestamp`
- `stripe_event_id`
- `payment_link_id`
- `machine_id`
- `amount`
- `currency`
- `stripe_status`
- `mqtt_publish_status`
- `f4_ack_status`
- `rs485_ack_status`
- `final_status`
- `manual_review_required`
- `notes`

## Commands fields
- `timestamp`
- `command_id`
- `source`
- `target_machine_id`
- `command_type`
- `amount_or_credit`
- `status`
- `ack_stage`
- `retries`
- `error_message`
