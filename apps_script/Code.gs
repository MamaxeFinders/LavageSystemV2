const SHEET_MACHINES = 'Machines';
const SHEET_EVENTS = 'Events';
const SHEET_PAYMENTS = 'Payments';
const SHEET_COMMANDS = 'Commands';
const SHEET_DAILY = 'DailySummary';

const MACHINE_HEADERS = [
  'machine_id',
  'machine_name',
  'machine_type',
  'bridge_id',
  'site_id',
  'configured_active',
  'comm_mode',
  'online',
  'healthy',
  'enabled',
  'fault_code',
  'last_reason',
  'credit_cents',
  'current_program',
  'running',
  'presostat',
  'gel',
  'shock',
  'temperature',
  'humidity',
  'last_seen',
  'last_snapshot_at',
  'last_payment_id',
  'firmware_version'
];

const EVENT_HEADERS = [
  'timestamp',
  'machine_id',
  'machine_name',
  'event_type',
  'value_1',
  'value_2',
  'source',
  'seq',
  'note'
];

const PAYMENT_HEADERS = [
  'timestamp',
  'stripe_event_id',
  'machine_id',
  'machine_name',
  'amount_cents',
  'sessions',
  'status',
  'source',
  'command_id'
];

const COMMAND_HEADERS = [
  'timestamp',
  'command_id',
  'target_machine',
  'command_type',
  'value',
  'source',
  'status',
  'ack_stage',
  'note',
  'updated_at'
];

const DAILY_HEADERS = ['date', 'machine_id', 'machine_name', 'revenue_cents', 'session_count'];

function normalizeMachinePropertyKey_(value) {
  return String(value || '')
    .trim()
    .toUpperCase()
    .replace(/[^A-Z0-9]+/g, '_')
    .replace(/^_+|_+$/g, '');
}

function truthy_(value) {
  return value === true || value === 'TRUE' || value === 'true' || value === 1 || value === '1';
}

function normalizeCommMode_(value) {
  const mode = String(value || 'NORMAL').trim().toUpperCase();
  return mode === 'SAFE' ? 'SAFE' : 'NORMAL';
}

function normalizeMachineRecord_(machine) {
  if (!machine) {
    return machine;
  }
  machine.online = truthy_(machine.online);
  machine.healthy = truthy_(machine.healthy);
  machine.enabled = truthy_(machine.enabled);
  machine.configured_active = machine.configured_active === '' || machine.configured_active == null ? true : truthy_(machine.configured_active);
  machine.comm_mode = normalizeCommMode_(machine.comm_mode);
  return machine;
}

function machineAvailable_(machine) {
  const normalized = normalizeMachineRecord_(machine);
  return normalized.configured_active !== false &&
    normalized.comm_mode !== 'SAFE' &&
    normalized.online === true &&
    normalized.healthy === true &&
    normalized.enabled !== false;
}

function doGet(e) {
  ensureSheets_();
  const action = (e && e.parameter && e.parameter.action) || 'app';

  if (action === 'machine_status') {
    return jsonOut_(getMachineStatusByAny_(e.parameter.machine_id || e.parameter.machine || ''));
  }
  if (action === 'admin_data') {
    return jsonOut_(getDashboardData_());
  }
  if (action === 'stripe_entry') {
    return renderIndex_({
      machineEntryMode: true,
      entryMachineKey: e.parameter.machine_id || e.parameter.machine || '',
      entryMachineStatus: getMachineStatusByAny_(e.parameter.machine_id || e.parameter.machine || ''),
      stripeBaseUrl: getRequiredProperty_('STRIPE_MACHINE_LINK_BASE', '')
    });
  }

  return renderIndex_({
    machineEntryMode: false,
    entryMachineKey: '',
    entryMachineStatus: null,
    stripeBaseUrl: getRequiredProperty_('STRIPE_MACHINE_LINK_BASE', '')
  });
}

function doPost(e) {
  ensureSheets_();
  const action = (e && e.parameter && e.parameter.action) || '';
  let body = {};
  try {
    body = e && e.postData && e.postData.contents ? JSON.parse(e.postData.contents) : {};
  } catch (error) {
    return jsonOut_({ ok: false, error: 'invalid_json', detail: String(error) });
  }

  if (action === 'status_snapshot') {
    return jsonOut_(ingestStatusSnapshot_(body));
  }
  if (action === 'event') {
    return jsonOut_(ingestEvent_(body));
  }
  if (action === 'command_ack') {
    return jsonOut_(ingestCommandAck_(body));
  }
  if (action === 'stripe') {
    return jsonOut_(handleStripeWebhook_(body));
  }
  return jsonOut_({ ok: false, error: 'unknown_action', action: action });
}

function renderIndex_(templateData) {
  const tpl = HtmlService.createTemplateFromFile('Index');
  tpl.bootstrap = templateData;
  return tpl.evaluate()
    .setTitle('Clean Wash V2')
    .setXFrameOptionsMode(HtmlService.XFrameOptionsMode.ALLOWALL);
}

function include(filename) {
  return HtmlService.createHtmlOutputFromFile(filename).getContent();
}

function jsonOut_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}

function getRequiredProperty_(name, fallback) {
  const value = PropertiesService.getScriptProperties().getProperty(name);
  return value || fallback || '';
}

function getSpreadsheet_() {
  const spreadsheetId = getRequiredProperty_('SPREADSHEET_ID');
  return SpreadsheetApp.openById(spreadsheetId);
}

function ensureSheet_(sheetName, headers) {
  const ss = getSpreadsheet_();
  let sheet = ss.getSheetByName(sheetName);
  if (!sheet) {
    sheet = ss.insertSheet(sheetName);
  }
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(headers);
  } else {
    const currentHeaders = sheet.getRange(1, 1, 1, sheet.getLastColumn()).getValues()[0];
    if (String(currentHeaders.join('|')) !== String(headers.join('|'))) {
      sheet.clearContents();
      sheet.appendRow(headers);
    }
  }
  return sheet;
}

function ensureSheets_() {
  ensureSheet_(SHEET_MACHINES, MACHINE_HEADERS);
  ensureSheet_(SHEET_EVENTS, EVENT_HEADERS);
  ensureSheet_(SHEET_PAYMENTS, PAYMENT_HEADERS);
  ensureSheet_(SHEET_COMMANDS, COMMAND_HEADERS);
  ensureSheet_(SHEET_DAILY, DAILY_HEADERS);
}

function sheetToObjects_(sheet, limit) {
  const values = sheet.getDataRange().getValues();
  if (values.length < 2) {
    return [];
  }
  const headers = values[0];
  const rows = values.slice(1);
  const subset = limit ? rows.slice(Math.max(0, rows.length - limit)) : rows;
  return subset.map(row => {
    const out = {};
    headers.forEach((header, index) => out[header] = row[index]);
    return out;
  }).reverse();
}

function findMachineRow_(sheet, machineKey) {
  const values = sheet.getDataRange().getValues();
  for (let row = 1; row < values.length; row++) {
    if (String(values[row][0]) === String(machineKey) || String(values[row][1]) === String(machineKey)) {
      return row + 1;
    }
  }
  return 0;
}

function upsertMachine_(machine) {
  const sheet = ensureSheet_(SHEET_MACHINES, MACHINE_HEADERS);
  const rowValues = [
    machine.id || '',
    machine.name || '',
    machine.type || '',
    machine.bridge_id || '',
    machine.site_id || '',
    machine.configured_active !== false,
    normalizeCommMode_(machine.comm_mode),
    machine.online === true,
    machine.healthy === true,
    machine.enabled !== false,
    machine.fault_code || machine.fault || '',
    machine.reason || machine.last_reason || '',
    Number(machine.credit_cents || 0),
    Number(machine.current_program || machine.program || 0),
    machine.running === true,
    machine.presostat === true,
    machine.gel === true,
    machine.shock === true,
    machine.temperature === undefined ? '' : machine.temperature,
    machine.humidity === undefined ? '' : machine.humidity,
    new Date(),
    new Date(),
    machine.last_payment_id || '',
    machine.firmware_version || ''
  ];

  const row = findMachineRow_(sheet, machine.id || machine.name);
  if (row > 0) {
    sheet.getRange(row, 1, 1, rowValues.length).setValues([rowValues]);
  } else {
    sheet.appendRow(rowValues);
  }
}

function deactivateMissingMachines_(bridgeId, activeMachineIds) {
  if (!bridgeId) {
    return;
  }
  const sheet = ensureSheet_(SHEET_MACHINES, MACHINE_HEADERS);
  const values = sheet.getDataRange().getValues();
  if (values.length < 2) {
    return;
  }
  for (let row = 1; row < values.length; row++) {
    if (String(values[row][3]) !== String(bridgeId)) {
      continue;
    }
    if (activeMachineIds.indexOf(String(values[row][0])) !== -1) {
      continue;
    }
    const updated = values[row].slice();
    updated[5] = false;
    updated[6] = 'SAFE';
    updated[7] = false;
    updated[8] = false;
    updated[9] = truthy_(updated[9]);
    updated[10] = 'NOT_CONFIGURED';
    updated[11] = 'not_configured';
    updated[12] = 0;
    updated[13] = 0;
    updated[14] = false;
    updated[21] = new Date();
    sheet.getRange(row + 1, 1, 1, updated.length).setValues([updated]);
  }
}

function appendEvent_(event) {
  ensureSheet_(SHEET_EVENTS, EVENT_HEADERS).appendRow([
    new Date(),
    event.machine_id || '',
    event.machine_name || '',
    event.event_type || '',
    event.value_1 || '',
    event.value_2 || '',
    event.source || '',
    event.seq || '',
    event.note || ''
  ]);
}

function upsertCommandLog_(cmd) {
  const sheet = ensureSheet_(SHEET_COMMANDS, COMMAND_HEADERS);
  const values = sheet.getDataRange().getValues();
  let targetRow = 0;
  for (let row = 1; row < values.length; row++) {
    if (String(values[row][1]) === String(cmd.command_id)) {
      targetRow = row + 1;
      break;
    }
  }

  const rowValues = [
    cmd.timestamp || new Date(),
    cmd.command_id || '',
    cmd.target_machine || '',
    cmd.command_type || '',
    cmd.value || '',
    cmd.source || '',
    cmd.status || '',
    cmd.ack_stage || '',
    cmd.note || '',
    new Date()
  ];

  if (targetRow > 0) {
    sheet.getRange(targetRow, 1, 1, rowValues.length).setValues([rowValues]);
  } else {
    sheet.appendRow(rowValues);
  }
}

function getMachineStatusByAny_(machineKey) {
  ensureSheets_();
  const sheet = ensureSheet_(SHEET_MACHINES, MACHINE_HEADERS);
  const objects = sheetToObjects_(sheet).map(normalizeMachineRecord_);
  const machine = objects.find(item => String(item.machine_id) === String(machineKey) || String(item.machine_name) === String(machineKey));
  if (!machine) {
    return { ok: false, error: 'machine_not_found', machine_key: machineKey };
  }
  return { ok: true, machine: machine };
}

function getDashboardData_() {
  ensureSheets_();
  const machines = sheetToObjects_(ensureSheet_(SHEET_MACHINES, MACHINE_HEADERS))
    .map(normalizeMachineRecord_)
    .filter(machine => machine.configured_active !== false);
  return {
    ok: true,
    machines: machines,
    events: sheetToObjects_(ensureSheet_(SHEET_EVENTS, EVENT_HEADERS), 50),
    commands: sheetToObjects_(ensureSheet_(SHEET_COMMANDS, COMMAND_HEADERS), 50),
    payments: sheetToObjects_(ensureSheet_(SHEET_PAYMENTS, PAYMENT_HEADERS), 50),
    dailySummary: sheetToObjects_(ensureSheet_(SHEET_DAILY, DAILY_HEADERS), 20)
  };
}

function ingestStatusSnapshot_(body) {
  const machines = body.machines || [];
  const activeMachineIds = [];
  machines.forEach(machine => {
    activeMachineIds.push(String(machine.id));
    upsertMachine_({
      id: machine.id,
      name: machine.name,
      type: machine.type,
      bridge_id: machine.bridge_id || body.bridge || '',
      site_id: machine.site_id || body.site_id || '',
      configured_active: machine.configured_active !== false,
      comm_mode: machine.comm_mode || 'NORMAL',
      online: machine.online,
      healthy: machine.healthy,
      enabled: machine.enabled,
      fault_code: machine.fault_code || machine.fault,
      reason: machine.reason,
      credit_cents: machine.credit_cents,
      current_program: machine.current_program,
      running: machine.running,
      presostat: machine.presostat,
      gel: machine.gel,
      shock: machine.shock,
      temperature: machine.temperature,
      humidity: machine.humidity,
      firmware_version: machine.firmware_version
    });
    syncStripeAvailabilityForMachine_(machine);
  });
  deactivateMissingMachines_(body.bridge || '', activeMachineIds);
  return { ok: true, updated: machines.length };
}

function getStripeBindingForMachine_(machine) {
  const props = PropertiesService.getScriptProperties();
  const machineName = machine.machine_name || machine.name || ('MACHINE_' + (machine.machine_id || machine.id || 'UNKNOWN'));
  const normalized = normalizeMachinePropertyKey_(machineName);
  return {
    normalized: normalized,
    productId: props.getProperty('STRIPE_PRODUCT_' + normalized) || '',
    paymentLinkId: props.getProperty('STRIPE_PAYMENT_LINK_' + normalized) || ''
  };
}

function stripeApiRequest_(path, payload) {
  const secretKey = getRequiredProperty_('STRIPE_SECRET_KEY', '');
  if (!secretKey) {
    return { ok: false, skipped: true, reason: 'missing_stripe_secret_key' };
  }

  const response = UrlFetchApp.fetch('https://api.stripe.com/v1/' + path, {
    method: 'post',
    headers: {
      Authorization: 'Bearer ' + secretKey
    },
    payload: payload,
    muteHttpExceptions: true
  });

  return {
    ok: response.getResponseCode() >= 200 && response.getResponseCode() < 300,
    code: response.getResponseCode(),
    body: response.getContentText()
  };
}

function syncStripeAvailabilityForMachine_(machine) {
  const binding = getStripeBindingForMachine_(machine);
  if (!binding.productId && !binding.paymentLinkId) {
    return { ok: true, skipped: true, reason: 'no_stripe_binding' };
  }

  const props = PropertiesService.getScriptProperties();
  const desiredActive = machineAvailable_(machine);
  const cacheKey = 'STRIPE_ACTIVE_' + binding.normalized;
  const desiredToken = desiredActive ? '1' : '0';
  if (props.getProperty(cacheKey) === desiredToken) {
    return { ok: true, skipped: true, reason: 'already_synced', active: desiredActive };
  }

  const results = [];
  if (binding.productId) {
    results.push(stripeApiRequest_('products/' + encodeURIComponent(binding.productId), { active: desiredActive ? 'true' : 'false' }));
  }
  if (binding.paymentLinkId) {
    results.push(stripeApiRequest_('payment_links/' + encodeURIComponent(binding.paymentLinkId), { active: desiredActive ? 'true' : 'false' }));
  }

  const ok = results.every(result => result.ok || result.skipped);
  if (ok) {
    props.setProperty(cacheKey, desiredToken);
  }

  appendEvent_({
    machine_id: machine.machine_id || machine.id || '',
    machine_name: machine.machine_name || machine.name || '',
    event_type: 'STRIPE_SYNC',
    value_1: desiredActive ? 'ACTIVE' : 'DISABLED',
    value_2: binding.productId || binding.paymentLinkId || '',
    source: 'apps_script',
    seq: '',
    note: desiredActive ? 'Stripe enabled for machine' : 'Stripe disabled because machine is HORS SERVICE'
  });

  return { ok: ok, active: desiredActive, results: results };
}

function ingestEvent_(body) {
  appendEvent_({
    machine_id: body.machine_id || '',
    machine_name: body.machine || body.machine_name || '',
    event_type: body.event_type || '',
    value_1: body.value_1 || '',
    value_2: body.value_2 || '',
    source: body.source || 'a8',
    seq: body.seq || '',
    note: body.note || ''
  });
  return { ok: true };
}

function ingestCommandAck_(body) {
  upsertCommandLog_({
    timestamp: new Date(),
    command_id: body.command_id,
    target_machine: body.target_machine,
    command_type: body.command_type,
    value: body.value || '',
    source: body.source || 'bridge',
    status: body.status || '',
    ack_stage: body.ack_stage || '',
    note: body.note || ''
  });
  appendEvent_({
    machine_id: '',
    machine_name: body.target_machine || '',
    event_type: 'COMMAND_' + (body.ack_stage || 'UPDATE'),
    value_1: body.command_type || '',
    value_2: body.status || '',
    source: body.bridge || 'a8',
    seq: '',
    note: [body.command_id || '', body.note || ''].filter(Boolean).join(' | ')
  });
  return { ok: true };
}

function publishCommandToEmqx_(cmd) {
  const apiBase = getRequiredProperty_('EMQX_API_BASE').replace(/\/+$/, '');
  const appId = getRequiredProperty_('EMQX_APP_ID');
  const appSecret = getRequiredProperty_('EMQX_APP_SECRET');
  const topic = getRequiredProperty_('MQTT_TOPIC_CMD', 'carwash/site1/bridge/cmd');

  const payload = {
    topic: topic,
    qos: 1,
    retain: false,
    payload: JSON.stringify(cmd),
    payload_encoding: 'plain'
  };

  const response = UrlFetchApp.fetch(apiBase + '/publish', {
    method: 'post',
    contentType: 'application/json',
    headers: {
      Authorization: 'Basic ' + Utilities.base64Encode(appId + ':' + appSecret)
    },
    payload: JSON.stringify(payload),
    muteHttpExceptions: true
  });

  return {
    ok: [200, 202].indexOf(response.getResponseCode()) !== -1,
    code: response.getResponseCode(),
    body: response.getContentText()
  };
}

function publishMachineCommand_(machineKey, commandType, value, extra) {
  const machineStatus = getMachineStatusByAny_(machineKey);
  if (!machineStatus.ok) {
    return machineStatus;
  }
  const machine = machineStatus.machine;
  if (machine.configured_active === false) {
    return { ok: false, error: 'machine_not_configured', machine: machine };
  }
  if (commandType !== 'set_comm_mode' && machine.comm_mode === 'SAFE') {
    return {
      ok: false,
      error: 'machine_safe_mode',
      machine: machine,
      reason: 'Machine is in SAFE mode and accepts only local standalone operation until it is switched back to NORMAL.'
    };
  }
  const cmd = Object.assign({
    id: 'cmd_' + Date.now(),
    source: 'apps_script',
    machine: machine.machine_name,
    machine_id: Number(machine.machine_id),
    type: commandType,
    value: value || '',
    created: Math.floor(Date.now() / 1000)
  }, extra || {});

  const response = publishCommandToEmqx_(cmd);
  upsertCommandLog_({
    timestamp: new Date(),
    command_id: cmd.id,
    target_machine: machine.machine_name,
    command_type: commandType,
    value: value || '',
    source: cmd.source,
    status: response.ok ? 'published' : 'failed',
    ack_stage: 'MQTT',
    note: response.body
  });
  return { ok: response.ok, response: response, command: cmd, machine: machine };
}

function sendAdminCredit(machineKey, amountCents, sessions) {
  const cents = Number(amountCents || 0);
  const sessionCount = Number(sessions || 1);
  const result = publishMachineCommand_(machineKey, 'credit', cents, {
    amount_cents: cents,
    sessions: sessionCount,
    source: 'admin'
  });
  return result;
}

function disableDeviceFromWebapp(machineKey, note) {
  return publishMachineCommand_(machineKey, 'disable', note || 'Remote disable from mobile web app', {
    source: 'admin_mobile',
    note: note || 'Remote disable from mobile web app'
  });
}

function enableDeviceFromWebapp(machineKey, note) {
  return publishMachineCommand_(machineKey, 'enable', note || 'Remote enable from mobile web app', {
    source: 'admin_mobile',
    note: note || 'Remote enable from mobile web app'
  });
}

function sendAdminCommand(machineKey, commandType) {
  if (commandType === 'disable') {
    return disableDeviceFromWebapp(machineKey);
  }
  if (commandType === 'enable') {
    return enableDeviceFromWebapp(machineKey);
  }
  return publishMachineCommand_(machineKey, commandType, '', { source: 'admin' });
}

function refreshMachineStatus(machineKey) {
  return publishMachineCommand_(machineKey, 'refresh_status', '', { source: 'admin' });
}

function setMachineCommMode(machineKey, commMode) {
  const normalizedMode = normalizeCommMode_(commMode);
  return publishMachineCommand_(machineKey, 'set_comm_mode', normalizedMode, {
    source: 'admin',
    comm_mode: normalizedMode,
    note: 'Admin changed communication mode to ' + normalizedMode
  });
}

function logPayment_(payment) {
  ensureSheet_(SHEET_PAYMENTS, PAYMENT_HEADERS).appendRow([
    new Date(),
    payment.stripe_event_id || '',
    payment.machine_id || '',
    payment.machine_name || '',
    payment.amount_cents || 0,
    payment.sessions || 0,
    payment.status || '',
    payment.source || '',
    payment.command_id || ''
  ]);
}

function handleStripeWebhook_(stripeEvent) {
  const eventType = stripeEvent.type || '';
  if (eventType && eventType !== 'checkout.session.completed') {
    return { ok: true, ignored: true, type: eventType };
  }

  const session = stripeEvent.data && stripeEvent.data.object ? stripeEvent.data.object : stripeEvent;
  const metadata = session.metadata || {};
  const machineKey = metadata.machine_id || metadata.machine || metadata.Piste || '';
  const amountCents = Number(session.amount_total || metadata.amount_cents || 0);
  const sessions = Number(metadata.sessions || metadata.NombreDeSessions || 1);

  const machineStatus = getMachineStatusByAny_(machineKey);
  if (!machineStatus.ok) {
    logPayment_({
      stripe_event_id: stripeEvent.id || '',
      machine_id: machineKey,
      machine_name: machineKey,
      amount_cents: amountCents,
      sessions: sessions,
      status: 'machine_not_found',
      source: 'stripe'
    });
    return { ok: false, reason: 'machine_not_found' };
  }

  const machine = machineStatus.machine;
  if (!(machine.online && machine.healthy && machine.enabled)) {
    logPayment_({
      stripe_event_id: stripeEvent.id || '',
      machine_id: machine.machine_id,
      machine_name: machine.machine_name,
      amount_cents: amountCents,
      sessions: sessions,
      status: 'blocked_unavailable',
      source: 'stripe'
    });
    return { ok: false, blocked: true, reason: 'machine_unavailable' };
  }

  const commandResult = publishMachineCommand_(machine.machine_id, 'credit', amountCents, {
    amount_cents: amountCents,
    sessions: sessions,
    source: 'stripe',
    payment_status: session.payment_status || '',
    currency: session.currency || 'eur',
    customer_email: session.customer_details && session.customer_details.email || ''
  });

  logPayment_({
    stripe_event_id: stripeEvent.id || '',
    machine_id: machine.machine_id,
    machine_name: machine.machine_name,
    amount_cents: amountCents,
    sessions: sessions,
    status: commandResult.ok ? 'published' : 'publish_failed',
    source: 'stripe',
    command_id: commandResult.command && commandResult.command.id || ''
  });

  recomputeDailySummary();
  return commandResult;
}

function recomputeDailySummary() {
  const paymentRows = sheetToObjects_(ensureSheet_(SHEET_PAYMENTS, PAYMENT_HEADERS));
  const summarySheet = ensureSheet_(SHEET_DAILY, DAILY_HEADERS);
  summarySheet.clearContents();
  summarySheet.appendRow(DAILY_HEADERS);

  const buckets = {};
  paymentRows.forEach(row => {
    if (row.status !== 'published') {
      return;
    }
    const date = Utilities.formatDate(new Date(row.timestamp), Session.getScriptTimeZone(), 'yyyy-MM-dd');
    const key = [date, row.machine_id].join('|');
    if (!buckets[key]) {
      buckets[key] = {
        date: date,
        machine_id: row.machine_id,
        machine_name: row.machine_name,
        revenue_cents: 0,
        session_count: 0
      };
    }
    buckets[key].revenue_cents += Number(row.amount_cents || 0);
    buckets[key].session_count += Number(row.sessions || 0);
  });

  Object.keys(buckets).sort().forEach(key => {
    const row = buckets[key];
    summarySheet.appendRow([row.date, row.machine_id, row.machine_name, row.revenue_cents, row.session_count]);
  });
}

function checkMachineAvailableForPayment(machineKey) {
  const status = getMachineStatusByAny_(machineKey);
  if (!status.ok) {
    return status;
  }
  const machine = status.machine;
  const available = machineAvailable_(machine);
  return {
    ok: true,
    available: available,
    machine: machine,
    reason: available ? '' : (machine.comm_mode === 'SAFE'
      ? 'Machine is in SAFE mode and stays local-only, so remote Stripe credit is blocked.'
      : 'Machine is offline, faulty, disabled, or not configured.')
  };
}

function buildStripeRedirectUrl(machineKey) {
  const base = getRequiredProperty_('STRIPE_MACHINE_LINK_BASE', '');
  if (!base) {
    return '';
  }
  const separator = base.indexOf('?') === -1 ? '?' : '&';
  return base + separator + 'machine_id=' + encodeURIComponent(machineKey);
}