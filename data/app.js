'use strict';
const debug = location.pathname === '/debug';
document.querySelector('#overview').hidden = debug;
document.querySelector('#debug').hidden = !debug;
const $ = selector => document.querySelector(selector);
let token = '', history = [], lastHistory = 0;
async function get(url) {
  const response = await fetch(url, {cache: 'no-store', signal: AbortSignal.timeout(5000)});
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return response.json();
}
function age(now, value) { return value == null ? 'Noch keine' : `vor ${Math.floor(((now - value) >>> 0) / 1000)} s`; }
function updateStatus(s) {
  const state = s.communicationState;
  $('#status').className = `badge ${state}`;
  $('#status').textContent = {online: 'RS485 online', stale: 'Antwort veraltet', offline: 'RS485 offline'}[state];
  document.querySelectorAll('[data-value]').forEach(element => {
    const value = element.dataset.value.split('.').reduce((obj, key) => obj?.[key], s);
    element.textContent = value == null ? '— unbekannt' : `${new Intl.NumberFormat('de-DE', {maximumFractionDigits: 2}).format(value)} ${element.dataset.unit || ''}`;
  });
  $('#notice').textContent = s.mappingCount ? 'Messwerte basieren auf den eingetragenen Registerdefinitionen. Veraltete Werte anhand der letzten Aktualisierung prüfen.' : 'Registermapping noch nicht bestätigt. Unbekannte Werte werden nicht als Messwerte dargestellt.';
  $('#dataState').textContent = {online: 'Messdaten aktuell', stale: 'Messdaten veraltet', offline: 'Keine aktuellen Messdaten'}[s.dataState];
  $('#responseAge').textContent = age(s.uptime, s.lastResponse);
  $('#dataAge').textContent = age(s.uptime, s.lastUpdate);
  $('#success').textContent = s.stats.successful;
  $('#errors').textContent = s.stats.timeouts + s.stats.crcErrors + s.stats.exceptions + s.stats.invalidFrames + s.stats.overflows;
}
function descriptionList(selector, rows) {
  const list = $(selector); list.replaceChildren();
  for (const [name, value] of rows) {
    const dt = document.createElement('dt'), dd = document.createElement('dd');
    dt.textContent = name; dd.textContent = value; list.append(dt, dd);
  }
}
function updateDebug(r, s) {
  descriptionList('#query', [['UART', 'Initialisiert; Verbindung separat prüfen'], ['Baudrate', r.baud], ['Slave-ID', r.slave], ['Aufgabe', r.job], ['Letzte Abfrage (Boot-ms)', r.lastQuery ?? '—'], ['Abfrage Baud / Slave', `${r.queryBaud} / ${r.querySlave}`], ['Function Code', `0x${r.functionCode.toString(16).padStart(2, '0')}`], ['Register', `0x${r.address.toString(16).padStart(4, '0')}`], ['CRC', r.crc]]);
  descriptionList('#stats', [['Anfragen', s.requests], ['Erfolgreich', s.successful], ['Timeouts', s.timeouts], ['CRC-Fehler', s.crcErrors], ['Exceptions', s.exceptions], ['Ungültige Frames', s.invalidFrames], ['RX-Überläufe', s.overflows]]);
  $('#frameResult').textContent = `${r.result}${r.exceptionName ? ` · ${r.exception.toString(16).padStart(2, '0')} ${r.exceptionName}` : ''}`;
  $('#tx').textContent = r.tx || '—'; $('#rx').textContent = r.rx || '—';
  $('#scanForm button').disabled = r.job !== 'idle'; $('#diagnose').disabled = r.job !== 'idle';
}
async function control(action, values) {
  if (!token) token = (await get('/api/modbus/control')).token;
  const response = await fetch(`/api/modbus/${action}`, {method: 'POST', headers: {'X-Scan-Token': token}, body: new URLSearchParams(values), signal: AbortSignal.timeout(5000)});
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || `HTTP ${response.status}`);
  return result;
}
async function action(name, values = {}) {
  try { await control(name, values); $('#jobMessage').textContent = name === 'stop' ? 'Stop angefordert' : 'Diagnose gestartet. Fortschritt siehe letzte Abfrage und Serial Monitor.'; }
  catch (error) { $('#jobMessage').textContent = error.message; }
}
$('#scanForm').addEventListener('submit', event => { event.preventDefault(); action('scan', Object.fromEntries(new FormData(event.target))); });
$('#stop').addEventListener('click', () => action('stop'));
$('#diagnose').addEventListener('click', () => action('diagnose'));
function drawChart() {
  const canvas = $('#chart'), ctx = canvas.getContext('2d'), width = canvas.clientWidth, height = 230;
  if (!width) return;
  const scale = devicePixelRatio || 1;
  canvas.width = width * scale; canvas.height = height * scale; ctx.scale(scale, scale);
  const key = $('#series').value, values = history.map(s => s[key]).filter(Number.isFinite);
  $('#chartInfo').textContent = values.length ? `${values.length} gültige Punkte · 10 Sekunden Abtastung · Lücken sind unbekannte Messwerte` : 'Noch keine bestätigten Messwerte vorhanden. Es werden keine Beispielwerte gezeichnet.';
  if (!values.length) return;
  const low = Math.min(...values), high = Math.max(...values), range = high - low || 1;
  ctx.font = '12px system-ui'; ctx.fillStyle = '#acbcd0'; ctx.strokeStyle = '#2a384b';
  for (let i = 0; i < 4; i++) { const y = 20 + i * 55; ctx.beginPath(); ctx.moveTo(55, y); ctx.lineTo(width - 10, y); ctx.stroke(); ctx.fillText((high - range * i / 3).toFixed(1), 0, y + 4); }
  ctx.strokeStyle = '#75ddbd'; ctx.lineWidth = 2; ctx.beginPath(); let active = false;
  history.forEach((sample, i) => {
    const value = sample[key]; if (!Number.isFinite(value)) { active = false; return; }
    const x = 55 + i / Math.max(1, history.length - 1) * (width - 65), y = 185 - (value - low) / range * 165;
    if (active) ctx.lineTo(x, y); else ctx.moveTo(x, y); active = true;
  }); ctx.stroke(); ctx.fillText('älteste Probe', 55, 220); ctx.fillText('neueste Probe', Math.max(55, width - 100), 220);
}
$('#series').addEventListener('change', drawChart); window.addEventListener('resize', drawChart);
async function refresh() {
  try {
    const status = await get('/api/status'); updateStatus(status);
    if (debug) { const raw = await get('/api/modbus/raw'); updateDebug(raw, status.stats); }
    else if (Date.now() - lastHistory >= 10000) { history = await get('/api/history'); lastHistory = Date.now(); drawChart(); }
  } catch (error) { $('#status').className = 'badge offline'; $('#status').textContent = 'ESP32 nicht erreichbar'; }
  finally { setTimeout(refresh, 2000); }
}
refresh();
