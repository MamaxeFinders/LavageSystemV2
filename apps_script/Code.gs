const SHEET_SYSTEM_CONFIG = 'System_Config';
const SHEET_MACHINES = 'Machines';
const SHEET_ASSIGNMENTS = 'Assignments';
const SHEET_PAYMENTS = 'Payments';
const SHEET_COMMANDS = 'Commands';
const SHEET_EVENTS = 'Events';
const SHEET_FAULTS = 'Faults';
const SHEET_DAILY = 'Daily_Summary';
const SHEET_SETUP_LOG = 'Setup_Log';

const MACHINE_HEADERS = [
  'machine_id',
  'device_type',
  'rs485_id',
  'board_uid',
  'enabled',
  'maintenance_mode',
  'healthy',
  'last_seen',
  'status_age_seconds',
  'status',
  'last_fault',
  'current_credit',
  'active_program',
  'running_state',
  'presostat',
  'gel',
  'shock',
  'temperature',
  'humidity',
  'stripe_payment_link_1',
  'stripe_payment_link_2',
  'stripe_link_active',
  'notes',
  // Compatibility fields used by the current web dashboard.
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
  'severity',
  'event_type',
  'message',
  'resolved',
  'resolved_at',
  // Compatibility fields
  'machine_name',
  'value_1',
  'value_2',
  'source',
  'seq',
  'note'
];

const ASSIGNMENT_HEADERS = [
  'board_uid',
  'detected_device_type',
  'assigned_machine_id',
  'assigned_rs485_id',
  'assignment_status',
  'assigned_at',
  'last_seen'
];

const PAYMENT_HEADERS = [
  'timestamp',
  'stripe_event_id',
  'payment_link_id',
  'machine_id',
  'amount',
  'currency',
  'stripe_status',
  'mqtt_publish_status',
  'f4_ack_status',
  'rs485_ack_status',
  'final_status',
  'manual_review_required',
  'notes',
  // Compatibility fields
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
  'source',
  'target_machine_id',
  'command_type',
  'amount_or_credit',
  'status',
  'ack_stage',
  'retries',
  'error_message',
  // Compatibility fields
  'target_machine',
  'value',
  'note',
  'updated_at'
];

const FAULT_HEADERS = ['timestamp', 'machine_id', 'severity', 'event_type', 'message', 'resolved', 'resolved_at'];
const DAILY_HEADERS = ['date', 'machine_id', 'machine_name', 'revenue_cents', 'session_count'];
const SYSTEM_CONFIG_HEADERS = ['key', 'value', 'updated_at'];
const SETUP_LOG_HEADERS = ['timestamp', 'action', 'result', 'details'];
const PARIS_TIMEZONE = 'Europe/Paris';
const EFINDERS_LAVAGE_FOLDER_ID = '1nRXyDgha3opObmQV1VdYSwIO06ucAbL8';
const PRIMARY_SPREADSHEET_ID = '1bb8spIX9BdwmkA-crni0Jk1OEpsrDHqu1ENX05W30sA';

function formatParisDateTime_(dateValue) {
  return Utilities.formatDate(new Date(dateValue || new Date()), PARIS_TIMEZONE, 'yyyy-MM-dd HH:mm:ss');
}

function ensureSpreadsheetTimezone_() {
  const spreadsheet = getSpreadsheet_();
  if (spreadsheet.getSpreadsheetTimeZone() !== PARIS_TIMEZONE) {
    spreadsheet.setSpreadsheetTimeZone(PARIS_TIMEZONE);
  }
}

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

  if (action === 'bootstrap_lavage') {
    return jsonOut_(bootstrapLavageSheetForEfinders());
  }
  if (action === 'quick_access') {
    return jsonOut_(getQuickAccessLinks());
  }

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
  if (action === 'provisioning_seen') {
    return jsonOut_(registerProvisioningSeen_(body));
  }
  if (action === 'assign_device') {
    return jsonOut_(assignDeviceFromProvisioning_(body));
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

function onOpen() {
  SpreadsheetApp.getUi()
    .createMenu('Carwash Setup')
    .addItem('Link Primary Program Sheet', 'bootstrapLavageSheetForEfinders')
    .addItem('Show Quick Access Links', 'showQuickAccessLinks')
    .addItem('Open Setup Panel', 'openSetupSidebar')
    .addItem('Open Stripe Mapping Panel', 'openStripeMappingSidebar')
    .addItem('Open QR & URL Generator', 'openPaymentQrSidebar')
    .addItem('Run Configuration Test', 'testSystemConfiguration')
    .addToUi();
}

function bootstrapLavageSheetForEfinders() {
  return bindSpreadsheetById_(PRIMARY_SPREADSHEET_ID, 'primary_program_sheet');
}

function bindSpreadsheetById_(spreadsheetId, sourceTag) {
  const props = PropertiesService.getScriptProperties();
  const spreadsheet = SpreadsheetApp.openById(spreadsheetId);
  props.setProperty('SPREADSHEET_ID', spreadsheet.getId());
  ensureSheets_();
  logSetup_(sourceTag || 'bind_spreadsheet', 'ok', spreadsheet.getId());
  return {
    ok: true,
    source: sourceTag || 'bind_spreadsheet',
    spreadsheetId: spreadsheet.getId(),
    spreadsheetUrl: spreadsheet.getUrl(),
    message: 'Script is now linked to the primary program spreadsheet.'
  };
}

function getQuickAccessLinks() {
  const spreadsheet = getSpreadsheet_();
  const webAppUrl = ScriptApp.getService().getUrl() || '';
  return {
    ok: true,
    spreadsheetId: spreadsheet.getId(),
    spreadsheetUrl: spreadsheet.getUrl(),
    webAppUrl: webAppUrl,
    setupHint: 'Open the linked sheet, then use menu Carwash Setup > Open Setup Panel to add keys.'
  };
}

function showQuickAccessLinks() {
  const links = getQuickAccessLinks();
  const ui = SpreadsheetApp.getUi();
  ui.alert(
    'Carwash Quick Access',
    'Spreadsheet:\n' + links.spreadsheetUrl + '\n\nWeb App:\n' + (links.webAppUrl || 'Deploy web app to get URL') + '\n\n' + links.setupHint,
    ui.ButtonSet.OK
  );
  return links;
}

function bootstrapLavageSheet_(folderName, spreadsheetName, folderId) {
  const props = PropertiesService.getScriptProperties();
  let folder = null;
  if (folderId) {
    try {
      folder = DriveApp.getFolderById(folderId);
    } catch (error) {
      folder = null;
    }
  }
  if (!folder) {
    const folderIterator = DriveApp.getFoldersByName(folderName);
    if (folderIterator.hasNext()) {
      folder = folderIterator.next();
    }
  }
  if (!folder) {
    return {
      ok: false,
      error: 'folder_not_found',
      folder: folderName,
      folderId: folderId || '',
      message: 'Google Drive folder not found: ' + folderName
    };
  }

  const existingId = getRequiredProperty_('SPREADSHEET_ID', '');
  let spreadsheet = null;
  let created = false;

  if (existingId) {
    try {
      spreadsheet = SpreadsheetApp.openById(existingId);
    } catch (error) {
      spreadsheet = null;
    }
  }

  if (!spreadsheet) {
    spreadsheet = SpreadsheetApp.create(spreadsheetName);
    created = true;
  }

  const file = DriveApp.getFileById(spreadsheet.getId());
  folder.addFile(file);
  try {
    DriveApp.getRootFolder().removeFile(file);
  } catch (error) {
    // Ignore if file is already not in root or permission does not allow removal.
  }

  props.setProperty('SPREADSHEET_ID', spreadsheet.getId());
  ensureSheets_();
  logSetup_('bootstrap_lavage_sheet', 'ok', (created ? 'created' : 'linked') + ':' + spreadsheet.getId());

  return {
    ok: true,
    created: created,
    folder: folderName,
    folderId: folder.getId(),
    spreadsheetId: spreadsheet.getId(),
    spreadsheetUrl: spreadsheet.getUrl(),
    message: created
      ? 'Spreadsheet created, linked, and initialized in folder ' + folderName
      : 'Existing spreadsheet linked and initialized in folder ' + folderName
  };
}

function openSetupSidebar() {
  ensureSheets_();
  const html = HtmlService.createTemplateFromFile('SetupSidebar').evaluate()
    .setTitle('Carwash Setup')
    .setWidth(420);
  SpreadsheetApp.getUi().showSidebar(html);
}

function openStripeMappingSidebar() {
  ensureSheets_();
  const html = HtmlService.createTemplateFromFile('StripeMappingSidebar').evaluate()
    .setTitle('Stripe Mapping')
    .setWidth(470);
  SpreadsheetApp.getUi().showSidebar(html);
}

function openPaymentQrSidebar() {
  ensureSheets_();
  const html = HtmlService.createTemplateFromFile('PaymentQrSidebar').evaluate()
    .setTitle('QR & Payment URLs')
    .setWidth(500);
  SpreadsheetApp.getUi().showSidebar(html);
}

function buildStripeEntryUrl(machineKey) {
  const webAppUrl = ScriptApp.getService().getUrl() || getRequiredProperty_('WEB_APP_URL', '');
  if (!webAppUrl) {
    return '';
  }
  const separator = webAppUrl.indexOf('?') === -1 ? '?' : '&';
  return webAppUrl + separator + 'action=stripe_entry&machine_id=' + encodeURIComponent(machineKey);
}

function buildQrImageUrl_(targetUrl, size) {
  const qrSize = size || 320;
  return 'https://chart.googleapis.com/chart?cht=qr&chs=' + qrSize + 'x' + qrSize + '&chl=' + encodeURIComponent(targetUrl);
}

function getPaymentQrCatalog() {
  ensureSheets_();
  const machines = sheetToObjects_(ensureSheet_(SHEET_MACHINES, MACHINE_HEADERS))
    .map(normalizeMachineRecord_)
    .filter(machine => machine.configured_active !== false);

  const rows = machines.map(machine => {
    const machineKey = String(machine.machine_id || machine.machine_name || '');
    const entryUrl = buildStripeEntryUrl(machineKey);
    const stripeRedirectUrl = buildStripeRedirectUrl(machineKey);
    return {
      machine_id: machine.machine_id,
      machine_name: machine.machine_name,
      machine_type: machine.machine_type,
      entry_url: entryUrl,
      qr_url: entryUrl ? buildQrImageUrl_(entryUrl, 320) : '',
      stripe_redirect_url: stripeRedirectUrl,
      stripe_payment_link_1: machine.stripe_payment_link_1 || '',
      stripe_payment_link_2: machine.stripe_payment_link_2 || ''
    };
  });

  return {
    ok: true,
    web_app_url: ScriptApp.getService().getUrl() || getRequiredProperty_('WEB_APP_URL', ''),
    stripe_machine_link_base: getRequiredProperty_('STRIPE_MACHINE_LINK_BASE', ''),
    machines: rows
  };
}

function getStripeMappingData() {
  ensureSheets_();
  const sheet = ensureSheet_(SHEET_MACHINES, MACHINE_HEADERS);
  const rows = sheetToObjects_(sheet)
    .map(normalizeMachineRecord_)
    .filter(machine => machine.configured_active !== false)
    .map(machine => {
      const link1 = String(machine.stripe_payment_link_1 || '').trim();
      const link2 = String(machine.stripe_payment_link_2 || '').trim();
      const mappingStatus = !link1 && !link2
        ? 'missing'
        : (link1 && link2 ? 'complete' : 'partial');
      return {
        machine_id: machine.machine_id,
        machine_name: machine.machine_name,
        machine_type: machine.machine_type,
        stripe_payment_link_1: link1,
        stripe_payment_link_2: link2,
        mapping_status: mappingStatus
      };
    });
  return { ok: true, machines: rows };
}

function saveStripeMachineMapping(input) {
  ensureSheets_();
  const machineKey = String(input.machineKey || '').trim();
  if (!machineKey) {
    return { ok: false, error: 'missing_machine_key' };
  }

  const link1 = String(input.stripePaymentLink1 || '').trim();
  const link2 = String(input.stripePaymentLink2 || '').trim();
  const sheet = ensureSheet_(SHEET_MACHINES, MACHINE_HEADERS);
  const values = sheet.getDataRange().getValues();
  if (values.length < 2) {
    return { ok: false, error: 'machines_sheet_empty' };
  }

  const headers = values[0].map(String);
  const map = headerIndexMap_(headers);
  let targetRow = -1;

  for (let row = 1; row < values.length; row++) {
    if (String(values[row][map.machine_id]) === machineKey || String(values[row][map.machine_name]) === machineKey) {
      targetRow = row;
      break;
    }
  }

  if (targetRow < 0) {
    return { ok: false, error: 'machine_not_found', machineKey: machineKey };
  }

  const updated = values[targetRow].slice();
  if (map.stripe_payment_link_1 != null) {
    updated[map.stripe_payment_link_1] = link1;
  }
  if (map.stripe_payment_link_2 != null) {
    updated[map.stripe_payment_link_2] = link2;
  }
  if (map.notes != null) {
    const note = link1 || link2
      ? 'Stripe mapping updated via sidebar on ' + formatParisDateTime_(new Date()) + ' (Europe/Paris)'
      : 'Stripe mapping cleared via sidebar on ' + formatParisDateTime_(new Date()) + ' (Europe/Paris)';
    updated[map.notes] = note;
  }

  sheet.getRange(targetRow + 1, 1, 1, updated.length).setValues([updated]);
  logSetup_('save_stripe_mapping', 'ok', machineKey + ' => ' + [link1, link2].filter(Boolean).join(','));
  return { ok: true, machineKey: machineKey, stripe_payment_link_1: link1, stripe_payment_link_2: link2 };
}

function setupStatus_() {
  const props = PropertiesService.getScriptProperties();
  const keys = [
    'EMQX_API_BASE',
    'EMQX_APP_ID',
    'EMQX_APP_SECRET',
    'MQTT_TOPIC_CMD',
    'MQTT_TOPIC_STATUS',
    'MQTT_TOPIC_ACK',
    'STRIPE_WEBHOOK_SECRET',
    'STRIPE_SECRET_KEY',
    'WEB_APP_URL',
    'ADMIN_ALLOWLIST'
  ];
  const status = {};
  keys.forEach(key => status[key] = (props.getProperty(key) || '').length > 0 ? 'configured' : 'missing');
  return status;
}

function logSetup_(action, result, details) {
  ensureSheet_(SHEET_SETUP_LOG, SETUP_LOG_HEADERS).appendRow([
    new Date(),
    action,
    result,
    details || ''
  ]);
}

function getSetupConfigStatus() {
  ensureSheets_();
  return { ok: true, status: setupStatus_() };
}

function saveSetupConfig(input) {
  ensureSheets_();
  const props = PropertiesService.getScriptProperties();
  const entries = {
    EMQX_API_BASE: String(input.emqxApiBase || '').trim(),
    EMQX_APP_ID: String(input.emqxAppId || '').trim(),
    EMQX_APP_SECRET: String(input.emqxAppSecret || '').trim(),
    MQTT_TOPIC_CMD: String(input.mqttTopicCmd || '').trim(),
    MQTT_TOPIC_STATUS: String(input.mqttTopicStatus || '').trim(),
    MQTT_TOPIC_ACK: String(input.mqttTopicAck || '').trim(),
    STRIPE_WEBHOOK_SECRET: String(input.stripeWebhookSecret || '').trim(),
    STRIPE_SECRET_KEY: String(input.stripeSecretKey || '').trim(),
    WEB_APP_URL: String(input.webAppUrl || '').trim(),
    ADMIN_ALLOWLIST: String(input.adminAllowlist || '').trim()
  };

  Object.keys(entries).forEach(key => {
    if (entries[key]) {
      props.setProperty(key, entries[key]);
    }
  });

  logSetup_('save_setup_config', 'ok', 'Setup properties updated');
  return { ok: true, status: setupStatus_() };
}

function testEmqxPublish() {
  try {
    const ping = {
      id: 'test_' + Date.now(),
      type: 'refresh_status',
      source: 'setup_test',
      machine_id: 1,
      machine: 'CAISSE_1'
    };
    const result = publishCommandToEmqx_(ping);
    logSetup_('test_emqx_publish', result.ok ? 'ok' : 'failed', JSON.stringify(result));
    return { ok: result.ok, result: result };
  } catch (error) {
    logSetup_('test_emqx_publish', 'failed', String(error));
    return { ok: false, error: String(error) };
  }
}

function testStripeConnection() {
  try {
    const result = stripeApiRequest_('balance', {});
    logSetup_('test_stripe_connection', result.ok ? 'ok' : 'failed', JSON.stringify(result));
    return { ok: result.ok, result: result };
  } catch (error) {
    logSetup_('test_stripe_connection', 'failed', String(error));
    return { ok: false, error: String(error) };
  }
}

function testSheetWrite() {
  try {
    logSetup_('test_sheet_write', 'ok', 'Sheet write test succeeded');
    return { ok: true };
  } catch (error) {
    return { ok: false, error: String(error) };
  }
}

function testGoogleSetup() {
  const props = PropertiesService.getScriptProperties();
  const webAppUrl = String(props.getProperty('WEB_APP_URL') || '').trim();
  const adminAllowlist = String(props.getProperty('ADMIN_ALLOWLIST') || '').trim();

  const webAppOk = /^https:\/\/script\.google\.com\//i.test(webAppUrl) && /\/exec\b/i.test(webAppUrl);
  const allowlistEmails = adminAllowlist
    .split(',')
    .map(v => v.trim())
    .filter(Boolean);
  const invalidEmails = allowlistEmails.filter(email => !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email));
  const allowlistOk = allowlistEmails.length > 0 && invalidEmails.length === 0;

  const result = {
    ok: webAppOk && allowlistOk,
    checks: {
      WEB_APP_URL: webAppOk,
      ADMIN_ALLOWLIST: allowlistOk
    },
    details: {
      web_app_url: webAppUrl,
      allowlist_count: allowlistEmails.length,
      invalid_emails: invalidEmails
    }
  };

  logSetup_('test_google_setup', result.ok ? 'ok' : 'warning', JSON.stringify(result.checks));
  return result;
}

function testSystemConfiguration() {
  ensureSheets_();
  const status = setupStatus_();
  const missing = Object.keys(status).filter(key => status[key] === 'missing');
  const result = { ok: missing.length === 0, missing: missing };
  logSetup_('test_system_configuration', result.ok ? 'ok' : 'warning', missing.join(',') || 'all configured');
  return result;
}

function jsonOut_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}

function getRequiredProperty_(name, fallback) {
  const value = PropertiesService.getScriptProperties().getProperty(name);
  return value || fallback || '';
}

function getSpreadsheet_() {
  const spreadsheetId = getRequiredProperty_('SPREADSHEET_ID', PRIMARY_SPREADSHEET_ID);
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
    const currentHeaders = sheet.getRange(1, 1, 1, sheet.getLastColumn()).getValues()[0].map(String);
    let changed = false;
    headers.forEach(header => {
      if (currentHeaders.indexOf(header) === -1) {
        currentHeaders.push(header);
        changed = true;
      }
    });
    if (changed) {
      sheet.getRange(1, 1, 1, currentHeaders.length).setValues([currentHeaders]);
    }
  }
  return sheet;
}

function ensureSheets_() {
  ensureSpreadsheetTimezone_();
  ensureSheet_(SHEET_SYSTEM_CONFIG, SYSTEM_CONFIG_HEADERS);
  ensureSheet_(SHEET_MACHINES, MACHINE_HEADERS);
  ensureSheet_(SHEET_ASSIGNMENTS, ASSIGNMENT_HEADERS);
  ensureSheet_(SHEET_EVENTS, EVENT_HEADERS);
  ensureSheet_(SHEET_FAULTS, FAULT_HEADERS);
  ensureSheet_(SHEET_PAYMENTS, PAYMENT_HEADERS);
  ensureSheet_(SHEET_COMMANDS, COMMAND_HEADERS);
  ensureSheet_(SHEET_DAILY, DAILY_HEADERS);
  ensureSheet_(SHEET_SETUP_LOG, SETUP_LOG_HEADERS);
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

function headerIndexMap_(headers) {
  const map = {};
  headers.forEach((header, index) => map[String(header)] = index);
  return map;
}

function buildRowFromObject_(headers, objectLike) {
  return headers.map(header => objectLike[header] == null ? '' : objectLike[header]);
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
  const headers = sheet.getRange(1, 1, 1, sheet.getLastColumn()).getValues()[0].map(String);
  const statusAge = machine.last_seen_ms_ago == null ? '' : Math.floor(Number(machine.last_seen_ms_ago) / 1000);
  const machineStatus = machine.maintenance_mode ? 'MAINTENANCE'
    : (machine.enabled === false ? 'DISABLED'
      : (machine.online ? (machine.healthy ? 'OK' : 'FAULT') : 'STALE'));
  const rowValues = buildRowFromObject_(headers, {
    machine_id: machine.id || machine.machine_id || '',
    device_type: machine.type || machine.machine_type || '',
    rs485_id: machine.id || machine.machine_id || '',
    board_uid: machine.board_uid || '',
    enabled: machine.enabled !== false,
    maintenance_mode: machine.maintenance_mode === true,
    healthy: machine.healthy === true,
    last_seen: new Date(),
    status_age_seconds: statusAge,
    status: machineStatus,
    last_fault: machine.fault_code || machine.fault || '',
    current_credit: Number(machine.credit_cents || 0),
    active_program: Number(machine.current_program || machine.program || 0),
    running_state: machine.running === true,
    presostat: machine.presostat === true,
    gel: machine.gel === true,
    shock: machine.shock === true,
    temperature: machine.temperature === undefined ? '' : machine.temperature,
    humidity: machine.humidity === undefined ? '' : machine.humidity,
    stripe_payment_link_1: machine.stripe_payment_link_1 || '',
    stripe_payment_link_2: machine.stripe_payment_link_2 || '',
    stripe_link_active: machine.stripe_link_active === true,
    notes: machine.notes || '',
    machine_name: machine.name || machine.machine_name || '',
    machine_type: machine.type || machine.machine_type || '',
    bridge_id: machine.bridge_id || '',
    site_id: machine.site_id || '',
    configured_active: machine.configured_active !== false,
    comm_mode: normalizeCommMode_(machine.comm_mode),
    online: machine.online === true,
    fault_code: machine.fault_code || machine.fault || '',
    last_reason: machine.reason || machine.last_reason || '',
    credit_cents: Number(machine.credit_cents || 0),
    current_program: Number(machine.current_program || machine.program || 0),
    running: machine.running === true,
    last_snapshot_at: new Date(),
    last_payment_id: machine.last_payment_id || '',
    firmware_version: machine.firmware_version || ''
  });

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
  const headers = values[0].map(String);
  const map = headerIndexMap_(headers);
  for (let row = 1; row < values.length; row++) {
    if (String(values[row][map.bridge_id]) !== String(bridgeId)) {
      continue;
    }
    if (activeMachineIds.indexOf(String(values[row][map.machine_id])) !== -1) {
      continue;
    }
    const updated = values[row].slice();
    updated[map.configured_active] = false;
    updated[map.comm_mode] = 'SAFE';
    updated[map.online] = false;
    updated[map.healthy] = false;
    updated[map.enabled] = true;
    updated[map.fault_code] = 'NOT_CONFIGURED';
    updated[map.last_reason] = 'not_configured';
    updated[map.credit_cents] = 0;
    updated[map.current_program] = 0;
    updated[map.running] = false;
    if (map.status != null) {
      updated[map.status] = 'STALE';
    }
    if (map.last_snapshot_at != null) {
      updated[map.last_snapshot_at] = new Date();
    }
    sheet.getRange(row + 1, 1, 1, updated.length).setValues([updated]);
  }
}

function appendEvent_(event) {
  ensureSheet_(SHEET_EVENTS, EVENT_HEADERS).appendRow([
    new Date(),
    event.machine_id || '',
    event.severity || 'INFO',
    event.event_type || '',
    event.message || event.note || '',
    event.resolved === true,
    event.resolved_at || '',
    // Compatibility fields
    event.machine_name || '',
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
    cmd.source || '',
    cmd.target_machine_id || cmd.target_machine || '',
    cmd.command_type || '',
    cmd.amount_or_credit || cmd.value || '',
    cmd.status || '',
    cmd.ack_stage || '',
    cmd.retries || 0,
    cmd.error_message || '',
    // Compatibility fields
    cmd.target_machine || '',
    cmd.value || '',
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
    assignments: sheetToObjects_(ensureSheet_(SHEET_ASSIGNMENTS, ASSIGNMENT_HEADERS), 100),
    events: sheetToObjects_(ensureSheet_(SHEET_EVENTS, EVENT_HEADERS), 50),
    commands: sheetToObjects_(ensureSheet_(SHEET_COMMANDS, COMMAND_HEADERS), 50),
    payments: sheetToObjects_(ensureSheet_(SHEET_PAYMENTS, PAYMENT_HEADERS), 50),
    dailySummary: sheetToObjects_(ensureSheet_(SHEET_DAILY, DAILY_HEADERS), 20),
    setupLog: sheetToObjects_(ensureSheet_(SHEET_SETUP_LOG, SETUP_LOG_HEADERS), 50)
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
    productId: machine.stripe_product_id || props.getProperty('STRIPE_PRODUCT_' + normalized) || '',
    paymentLinkId: machine.stripe_payment_link_1 || machine.stripe_payment_link_2 || props.getProperty('STRIPE_PAYMENT_LINK_' + normalized) || ''
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
    severity: body.ack_stage === 'FAULT' ? 'CRITICAL' : 'INFO',
    event_type: 'COMMAND_' + (body.ack_stage || 'UPDATE'),
    value_1: body.command_type || '',
    value_2: body.status || '',
    source: body.bridge || 'a8',
    seq: '',
    note: [body.command_id || '', body.note || ''].filter(Boolean).join(' | ')
  });
  updatePaymentAckStatus_(body.command_id || '', body.ack_stage || '', body.status || '', body.note || '');
  return { ok: true };
}

function updatePaymentAckStatus_(commandId, ackStage, status, note) {
  if (!commandId) {
    return;
  }
  const sheet = ensureSheet_(SHEET_PAYMENTS, PAYMENT_HEADERS);
  const values = sheet.getDataRange().getValues();
  if (values.length < 2) {
    return;
  }
  const headers = values[0].map(String);
  const map = headerIndexMap_(headers);
  for (let row = values.length - 1; row >= 1; row--) {
    if (String(values[row][map.command_id]) !== String(commandId)) {
      continue;
    }
    const updated = values[row].slice();
    updated[map.f4_ack_status] = ackStage || updated[map.f4_ack_status];
    updated[map.rs485_ack_status] = ackStage || updated[map.rs485_ack_status];
    if (ackStage === 'COMPLETED' || status === 'completed') {
      updated[map.final_status] = 'PAID_MACHINE_ACK_OK';
      updated[map.manual_review_required] = false;
    } else if (ackStage === 'FAULT' || status === 'failed' || status === 'fault') {
      updated[map.final_status] = 'PAID_NO_MACHINE_ACK';
      updated[map.manual_review_required] = true;
      updated[map.notes] = note || 'Command failed in F4/RS485 ack path';
    }
    sheet.getRange(row + 1, 1, 1, updated.length).setValues([updated]);
    break;
  }
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

function disableDeviceFromWebapp(machineKey, note, source) {
  return publishMachineCommand_(machineKey, 'disable', note || 'Remote disable from mobile web app', {
    source: source || 'admin_mobile',
    note: note || 'Remote disable from mobile web app'
  });
}

function enableDeviceFromWebapp(machineKey, note, source) {
  return publishMachineCommand_(machineKey, 'enable', note || 'Remote enable from mobile web app', {
    source: source || 'admin_mobile',
    note: note || 'Remote enable from mobile web app'
  });
}

function startMachineMaintenance(machineKey, note) {
  return disableDeviceFromWebapp(
    machineKey,
    note || 'Remote maintenance enabled: machine disabled from dashboard',
    'admin_maintenance'
  );
}

function endMachineMaintenance(machineKey, note) {
  return enableDeviceFromWebapp(
    machineKey,
    note || 'Remote maintenance ended: machine re-enabled from dashboard',
    'admin_maintenance'
  );
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

function registerProvisioningSeen_(body) {
  const sheet = ensureSheet_(SHEET_ASSIGNMENTS, ASSIGNMENT_HEADERS);
  const values = sheet.getDataRange().getValues();
  const boardUid = String(body.board_uid || '').trim();
  if (!boardUid) {
    return { ok: false, error: 'missing_board_uid' };
  }

  let rowIndex = -1;
  for (let row = 1; row < values.length; row++) {
    if (String(values[row][0]) === boardUid) {
      rowIndex = row + 1;
      break;
    }
  }

  const rowValues = [
    boardUid,
    body.detected_device_type || '',
    body.assigned_machine_id || '',
    body.assigned_rs485_id || '',
    body.assignment_status || 'UNASSIGNED',
    body.assigned_at || '',
    new Date()
  ];

  if (rowIndex > 0) {
    sheet.getRange(rowIndex, 1, 1, rowValues.length).setValues([rowValues]);
  } else {
    sheet.appendRow(rowValues);
  }
  return { ok: true };
}

function getProvisioningData() {
  ensureSheets_();
  return {
    ok: true,
    assignments: sheetToObjects_(ensureSheet_(SHEET_ASSIGNMENTS, ASSIGNMENT_HEADERS), 200),
    machines: sheetToObjects_(ensureSheet_(SHEET_MACHINES, MACHINE_HEADERS), 200)
  };
}

function assignDeviceFromProvisioning_(body) {
  const boardUid = String(body.board_uid || '').trim();
  const machineId = String(body.assigned_machine_id || '').trim();
  const rs485Id = Number(body.assigned_rs485_id || 0);
  if (!boardUid || !machineId || !rs485Id) {
    return { ok: false, error: 'missing_assignment_fields' };
  }

  registerProvisioningSeen_({
    board_uid: boardUid,
    detected_device_type: body.detected_device_type || '',
    assigned_machine_id: machineId,
    assigned_rs485_id: rs485Id,
    assignment_status: 'PENDING_ASSIGN',
    assigned_at: new Date()
  });

  const cmd = {
    id: 'assign_' + Date.now(),
    source: 'admin_setup',
    type: 'assign_device',
    machine: machineId,
    machine_id: rs485Id,
    assigned_machine_id: machineId,
    assigned_rs485_id: rs485Id,
    board_uid: boardUid,
    detected_device_type: body.detected_device_type || ''
  };

  const publish = publishCommandToEmqx_(cmd);
  upsertCommandLog_({
    timestamp: new Date(),
    command_id: cmd.id,
    source: 'admin_setup',
    target_machine_id: machineId,
    target_machine: machineId,
    command_type: cmd.type,
    amount_or_credit: rs485Id,
    value: String(rs485Id),
    status: publish.ok ? 'published' : 'failed',
    ack_stage: 'MQTT',
    retries: 0,
    error_message: publish.ok ? '' : (publish.body || ''),
    note: publish.body || ''
  });

  logSetup_('assign_device', publish.ok ? 'ok' : 'failed', machineId + ' <= ' + boardUid);
  return { ok: publish.ok, publish: publish, command: cmd };
}

function assignDeviceFromProvisioning(boardUid, machineId, rs485Id, detectedType) {
  return assignDeviceFromProvisioning_({
    board_uid: boardUid,
    assigned_machine_id: machineId,
    assigned_rs485_id: rs485Id,
    detected_device_type: detectedType || ''
  });
}

function logPayment_(payment) {
  ensureSheet_(SHEET_PAYMENTS, PAYMENT_HEADERS).appendRow([
    new Date(),
    payment.stripe_event_id || '',
    payment.payment_link_id || '',
    payment.machine_id || '',
    payment.amount || payment.amount_cents || 0,
    payment.currency || 'eur',
    payment.stripe_status || '',
    payment.mqtt_publish_status || '',
    payment.f4_ack_status || '',
    payment.rs485_ack_status || '',
    payment.final_status || payment.status || '',
    payment.manual_review_required === true,
    payment.notes || '',
    // Compatibility fields
    payment.machine_name || '',
    payment.amount_cents || 0,
    payment.sessions || 0,
    payment.status || '',
    payment.source || '',
    payment.command_id || ''
  ]);
}

function findMachineByStripePaymentLink_(paymentLinkId) {
  const link = String(paymentLinkId || '').trim();
  if (!link) {
    return { ok: false, error: 'missing_payment_link_id' };
  }

  const machines = sheetToObjects_(ensureSheet_(SHEET_MACHINES, MACHINE_HEADERS))
    .map(normalizeMachineRecord_)
    .filter(machine => machine.configured_active !== false);

  const matches = machines.filter(machine => {
    const link1 = String(machine.stripe_payment_link_1 || '').trim();
    const link2 = String(machine.stripe_payment_link_2 || '').trim();
    return link === link1 || link === link2;
  });

  if (matches.length === 1) {
    return { ok: true, machine: matches[0] };
  }
  if (matches.length > 1) {
    return { ok: false, error: 'multiple_machine_match_for_payment_link', payment_link_id: link };
  }
  return { ok: false, error: 'payment_link_not_mapped', payment_link_id: link };
}

function resolveMachineForStripeSession_(session, metadata) {
  const paymentLinkId = String(metadata.payment_link_id || session.payment_link || '').trim();
  const machineKey = String(metadata.machine_id || metadata.machine || metadata.Piste || '').trim();

  const byMachineKey = machineKey ? getMachineStatusByAny_(machineKey) : null;
  const byPaymentLink = paymentLinkId ? findMachineByStripePaymentLink_(paymentLinkId) : null;

  if (byMachineKey && byMachineKey.ok && byPaymentLink && byPaymentLink.ok) {
    if (String(byMachineKey.machine.machine_id) !== String(byPaymentLink.machine.machine_id)) {
      return {
        ok: false,
        error: 'machine_payment_link_mismatch',
        machine_from_metadata: byMachineKey.machine.machine_id,
        machine_from_payment_link: byPaymentLink.machine.machine_id,
        payment_link_id: paymentLinkId
      };
    }
    return { ok: true, machine: byMachineKey.machine, payment_link_id: paymentLinkId };
  }

  if (byPaymentLink && byPaymentLink.ok) {
    return { ok: true, machine: byPaymentLink.machine, payment_link_id: paymentLinkId };
  }

  if (byMachineKey && byMachineKey.ok) {
    return { ok: true, machine: byMachineKey.machine, payment_link_id: paymentLinkId };
  }

  return {
    ok: false,
    error: byPaymentLink && !byPaymentLink.ok ? byPaymentLink.error : 'machine_not_found',
    payment_link_id: paymentLinkId,
    machine_key: machineKey
  };
}

function normalizeStripeAmountToCents_(session, metadata) {
  const currency = String(session.currency || metadata.currency || 'eur').trim().toLowerCase();
  const rawAmount = Number(session.amount_total);
  const fallbackAmount = Number(metadata.amount_cents || 0);

  // Stripe checkout.session.amount_total is in the smallest currency unit.
  // For EUR this means cents, e.g. 1.00 EUR => 100.
  const amountCents = Number.isFinite(rawAmount) && rawAmount > 0 ? Math.round(rawAmount) : Math.round(fallbackAmount);

  return {
    currency: currency,
    amountCents: amountCents,
    valid: amountCents > 0
  };
}

function handleStripeWebhook_(stripeEvent) {
  const eventType = stripeEvent.type || '';
  if (eventType && eventType !== 'checkout.session.completed') {
    return { ok: true, ignored: true, type: eventType };
  }

  const session = stripeEvent.data && stripeEvent.data.object ? stripeEvent.data.object : stripeEvent;
  const metadata = session.metadata || {};
  const normalizedAmount = normalizeStripeAmountToCents_(session, metadata);
  const amountCents = normalizedAmount.amountCents;
  const currency = normalizedAmount.currency || 'eur';
  const sessions = Number(metadata.sessions || metadata.NombreDeSessions || 1);
  const paymentLinkId = String(metadata.payment_link_id || session.payment_link || '').trim();

  if (currency !== 'eur' || !normalizedAmount.valid) {
    logPayment_({
      stripe_event_id: stripeEvent.id || '',
      payment_link_id: paymentLinkId,
      machine_id: metadata.machine_id || metadata.machine || metadata.Piste || '',
      machine_name: metadata.machine || metadata.machine_id || metadata.Piste || '',
      amount_cents: amountCents,
      sessions: sessions,
      currency: currency,
      status: 'invalid_currency_or_amount',
      source: 'stripe',
      final_status: 'PAID_MANUAL_REVIEW',
      manual_review_required: true,
      notes: 'Blocked automatic credit: expected EUR and positive amount in cents from Stripe amount_total'
    });
    return { ok: false, reason: 'invalid_currency_or_amount', currency: currency, amount_cents: amountCents };
  }

  const machineResolution = resolveMachineForStripeSession_(session, metadata);
  if (!machineResolution.ok) {
    logPayment_({
      stripe_event_id: stripeEvent.id || '',
      payment_link_id: paymentLinkId,
      machine_id: metadata.machine_id || metadata.machine || metadata.Piste || '',
      machine_name: metadata.machine || metadata.machine_id || metadata.Piste || '',
      amount_cents: amountCents,
      sessions: sessions,
      status: 'machine_not_found',
      source: 'stripe',
      final_status: 'PAID_MACHINE_NOT_FOUND',
      manual_review_required: true,
      notes: 'Payment received but machine mapping invalid: ' + (machineResolution.error || 'unknown')
    });
    return { ok: false, reason: machineResolution.error || 'machine_not_found' };
  }

  const machine = machineResolution.machine;
  if (!(machine.online && machine.healthy && machine.enabled)) {
    logPayment_({
      stripe_event_id: stripeEvent.id || '',
      payment_link_id: paymentLinkId,
      machine_id: machine.machine_id,
      machine_name: machine.machine_name,
      amount_cents: amountCents,
      sessions: sessions,
      status: 'blocked_unavailable',
      source: 'stripe',
      final_status: 'PAID_BLOCKED_MACHINE_UNAVAILABLE',
      manual_review_required: true,
      notes: 'Payment arrived but machine was unavailable for remote credit'
    });
    return { ok: false, blocked: true, reason: 'machine_unavailable' };
  }

  const commandResult = publishMachineCommand_(machine.machine_id, 'credit', amountCents, {
    amount_cents: amountCents,
    sessions: sessions,
    source: 'stripe',
    payment_status: session.payment_status || '',
    currency: currency,
    customer_email: session.customer_details && session.customer_details.email || ''
  });

  logPayment_({
    stripe_event_id: stripeEvent.id || '',
    payment_link_id: paymentLinkId,
    machine_id: machine.machine_id,
    machine_name: machine.machine_name,
    amount: amountCents,
    amount_cents: amountCents,
    currency: currency,
    stripe_status: session.payment_status || '',
    mqtt_publish_status: commandResult.ok ? 'published' : 'publish_failed',
    f4_ack_status: 'pending',
    rs485_ack_status: 'pending',
    sessions: sessions,
    status: commandResult.ok ? 'published' : 'publish_failed',
    source: 'stripe',
    command_id: commandResult.command && commandResult.command.id || '',
    final_status: commandResult.ok ? 'PAID_MQTT_PUBLISHED' : 'PAID_CREDIT_FAILED',
    manual_review_required: !commandResult.ok,
    notes: commandResult.ok ? 'Awaiting F4/RS485 ack chain' : 'MQTT publish failed, manual review required'
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
    const date = Utilities.formatDate(new Date(row.timestamp), PARIS_TIMEZONE, 'yyyy-MM-dd');
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