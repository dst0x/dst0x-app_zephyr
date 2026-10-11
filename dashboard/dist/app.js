const $ = (id) => document.getElementById(id);
const environment = window.__DST0_CONFIG__ || {};

const state = {
  socket: null,
  buffer: new Uint8Array(0),
  connected: false,
  packetId: 1,
  reconnectTimer: null,
  connectTimer: null,
  pingTimer: null,
  messages: 0,
  temperatures: [],
  labels: [],
  history: [],
  config: {
    url: environment.url || 'wss://broker.avisha.id:8084/mqtt',
    topic: environment.topic || 'dstxlabs/signal',
    username: environment.username || '',
    password: environment.password || ''
  }
};

const utf8 = new TextEncoder();
const decoder = new TextDecoder();

function encodeString(value) {
  const bytes = utf8.encode(value || '');
  return new Uint8Array([bytes.length >> 8, bytes.length & 255, ...bytes]);
}

function remainingLength(length) {
  const bytes = [];
  do {
    let digit = length % 128;
    length = Math.floor(length / 128);
    if (length > 0) digit |= 128;
    bytes.push(digit);
  } while (length > 0);
  return bytes;
}

function mqttPacket(header, body) {
  return new Uint8Array([header, ...remainingLength(body.length), ...body]);
}

function connectPacket(clientId, username, password) {
  const protocol = encodeString('MQTT');
  let flags = 0x02;
  if (username) flags |= 0x80;
  if (password) flags |= 0x40;
  const variable = new Uint8Array([...protocol, 0x04, flags, 0x00, 0x3c]);
  const payload = [encodeString(clientId)];
  if (username) payload.push(encodeString(username));
  if (password) payload.push(encodeString(password));
  const body = concatBytes(variable, ...payload);
  return mqttPacket(0x10, body);
}

function subscribePacket(topic) {
  state.packetId = state.packetId >= 65535 ? 1 : state.packetId + 1;
  const body = new Uint8Array([
    state.packetId >> 8, state.packetId & 255,
    ...encodeString(topic), 0x00
  ]);
  return mqttPacket(0x82, body);
}

function concatBytes(...arrays) {
  const output = new Uint8Array(arrays.reduce((sum, array) => sum + array.length, 0));
  let offset = 0;
  arrays.forEach((array) => { output.set(array, offset); offset += array.length; });
  return output;
}

function setConnection(status, message) {
  state.connected = status === 'online';
  $('connectionBadge').className = `status-pill ${status}`;
  $('connectionText').textContent = message;
}

function disconnect() {
  clearInterval(state.pingTimer);
  clearTimeout(state.reconnectTimer);
  clearTimeout(state.connectTimer);
  if (state.socket) {
    state.socket.onclose = null;
    state.socket.close();
  }
  state.socket = null;
  state.buffer = new Uint8Array(0);
  setConnection('offline', 'Terputus');
}

function connect() {
  disconnect();
  setConnection('connecting', 'Menghubungkan…');

  let socket;
  try {
    socket = new WebSocket(state.config.url, ['mqtt']);
  } catch (error) {
    connectionFailed(`URL WebSocket tidak valid: ${error.message}`);
    return;
  }

  state.socket = socket;
  socket.binaryType = 'arraybuffer';
  state.connectTimer = setTimeout(() => {
    if (!state.connected) {
      connectionFailed('Koneksi ke broker melewati batas waktu. Periksa endpoint WebSocket dan jaringan.');
      socket.close();
    }
  }, 10000);
  socket.onopen = () => {
    const clientId = `dst0-web-${Math.random().toString(16).slice(2, 10)}`;
    socket.send(connectPacket(clientId, state.config.username, state.config.password));
  };
  socket.onmessage = (event) => {
    state.buffer = concatBytes(state.buffer, new Uint8Array(event.data));
    parsePackets();
  };
  socket.onerror = () => connectionFailed('Endpoint WebSocket tidak dapat dihubungi. Periksa URL, port, dan konfigurasi WS/WSS broker.');
  socket.onclose = () => {
    clearInterval(state.pingTimer);
    if (state.connected) {
      setConnection('connecting', 'Menyambung ulang…');
      state.reconnectTimer = setTimeout(connect, 4000);
    } else {
      setConnection('offline', 'Terputus');
    }
  };
}

function connectionFailed(message) {
  setConnection('offline', 'Gagal terhubung');
  $('connectionBadge').title = message;
  $('lastUpdate').textContent = message;
  console.error(`[MQTT] ${message}`);
}

function parsePackets() {
  let offset = 0;
  while (offset + 2 <= state.buffer.length) {
    const header = state.buffer[offset];
    let multiplier = 1, remaining = 0, index = offset + 1, digit;
    do {
      if (index >= state.buffer.length) return;
      digit = state.buffer[index++];
      remaining += (digit & 127) * multiplier;
      multiplier *= 128;
    } while ((digit & 128) && multiplier <= 128 * 128 * 128);
    if (index + remaining > state.buffer.length) return;
    handlePacket(header, state.buffer.slice(index, index + remaining));
    offset = index + remaining;
  }
  state.buffer = state.buffer.slice(offset);
}

function handlePacket(header, body) {
  const type = header >> 4;
  if (type === 2) {
    const code = body[1];
    if (code !== 0) {
      connectionFailed(code === 4 || code === 5 ? 'Login ditolak broker. Periksa username, password, dan izin akun.' : `Broker menolak koneksi (kode ${code}).`);
      state.socket?.close();
      return;
    }
    state.socket.send(subscribePacket(state.config.topic));
  } else if (type === 9) {
    const granted = body[2];
    if (granted === 0x80) {
      connectionFailed(`Akun tidak memiliki izin subscribe ke ${state.config.topic}. Periksa ACL broker.`);
      return;
    }
    setConnection('online', 'Terhubung');
    clearTimeout(state.connectTimer);
    $('connectionBadge').title = `Subscribe: ${state.config.topic}`;
    clearInterval(state.pingTimer);
    state.pingTimer = setInterval(() => {
      if (state.socket?.readyState === WebSocket.OPEN) state.socket.send(new Uint8Array([0xc0, 0x00]));
    }, 30000);
  } else if (type === 3) {
    let index = 0;
    const topicLength = (body[index++] << 8) | body[index++];
    const topic = decoder.decode(body.slice(index, index + topicLength));
    index += topicLength;
    const qos = (header >> 1) & 3;
    let packetId;
    if (qos > 0) {
      packetId = (body[index++] << 8) | body[index++];
    }
    const payload = decoder.decode(body.slice(index));
    if (topic === state.config.topic) processPayload(payload);
    if (qos === 1 && packetId) state.socket.send(new Uint8Array([0x40, 0x02, packetId >> 8, packetId & 255]));
  }
}

function processPayload(raw) {
  let data;
  try { data = JSON.parse(raw); }
  catch { return; }

  displayData(data, true);
}

function displayData(data, saveToHistory) {

  const temp = numberValue(data['DST0-tem']);
  const humidity = numberValue(data['DST0-hum']);
  const pressure = numberValue(data['DST0-pre']);
  const wind = numberValue(data['DST0-wsp']);
  const direction = numberValue(data['DST0-dir']);
  const signal = numberValue(data['DST0-sig']);

  $('deviceId').textContent = data.id || '—';
  $('firmwareVersion').textContent = data.ver || '—';
  setMetric('temperature', temp, 1);
  setMetric('humidity', humidity, 1);
  setMetric('pressure', pressure, 1);
  setMetric('windSpeed', wind, 2);

  if (Number.isFinite(direction)) {
    const degree = ((direction % 360) + 360) % 360;
    $('windDirection').textContent = `${degree.toFixed(0)}°`;
    $('directionLabel').textContent = directionName(degree);
    $('compassNeedle').style.transform = `rotate(${degree}deg)`;
  }

  updateSignal(signal);
  updateNotes(temp, humidity, wind);
  const date = Number.isFinite(Number(data.timestamp)) ? new Date(Number(data.timestamp) * 1000) : new Date();
  $('lastUpdate').textContent = `Diperbarui ${date.toLocaleDateString('id-ID', { day: 'numeric', month: 'long' })}, ${date.toLocaleTimeString('id-ID', { hour: '2-digit', minute: '2-digit', second: '2-digit' })} WIB`;

  if (saveToHistory) saveReading(data, date);
}

function historyStorageKey() {
  return `dst0WeatherHistory:v1:${state.config.topic}`;
}

function saveReading(data, date) {
  const reading = { ...data, _receivedAt: date.getTime() };
  const last = state.history.at(-1);
  const isDuplicate = last && last.id === reading.id && Number(last.timestamp) === Number(reading.timestamp);
  if (isDuplicate) state.history[state.history.length - 1] = reading;
  else state.history.push(reading);
  state.history = state.history.slice(-200);

  try {
    localStorage.setItem(historyStorageKey(), JSON.stringify(state.history));
  } catch (error) {
    console.warn('[DASHBOARD] Riwayat tidak dapat disimpan:', error);
  }
  refreshHistoryViews();
}

function restoreHistory() {
  try {
    const stored = JSON.parse(localStorage.getItem(historyStorageKey()) || '[]');
    state.history = Array.isArray(stored) ? stored.slice(-200) : [];
  } catch {
    state.history = [];
  }

  if (state.history.length) displayData(state.history.at(-1), false);
  refreshHistoryViews();
}

function refreshHistoryViews() {
  const chartData = state.history
    .filter((item) => Number.isFinite(Number(item['DST0-tem'])))
    .slice(-12);
  state.temperatures = chartData.map((item) => Number(item['DST0-tem']));
  state.labels = chartData.map((item) => readingDate(item).toLocaleTimeString('id-ID', { hour: '2-digit', minute: '2-digit' }));
  state.messages = state.history.length;
  $('messageCount').textContent = `${state.history.length} data tersimpan`;
  $('historyCount').textContent = `${state.history.length} DATA`;
  renderHistoryTable();
  drawChart();
}

function readingDate(item) {
  const timestamp = Number(item.timestamp);
  const millis = Number.isFinite(timestamp) ? timestamp * 1000 : Number(item._receivedAt);
  const date = new Date(millis);
  return Number.isNaN(date.getTime()) ? new Date() : date;
}

function renderHistoryTable() {
  const body = $('historyBody');
  body.replaceChildren();
  const readings = state.history.slice(-20).reverse();
  if (!readings.length) {
    const row = document.createElement('tr');
    row.className = 'empty-row';
    const cell = document.createElement('td');
    cell.colSpan = 7;
    cell.textContent = 'Belum ada data tersimpan';
    row.appendChild(cell);
    body.appendChild(row);
    return;
  }

  readings.forEach((item) => {
    const row = document.createElement('tr');
    const date = readingDate(item);
    const values = [
      date.toLocaleString('id-ID', { day: '2-digit', month: 'short', hour: '2-digit', minute: '2-digit' }),
      formatHistoryValue(item['DST0-tem'], 1, ' °C'),
      formatHistoryValue(item['DST0-hum'], 1, ' %'),
      formatHistoryValue(item['DST0-pre'], 1, ' hPa'),
      formatHistoryValue(item['DST0-wsp'], 2, ' m/s'),
      formatHistoryValue(item['DST0-dir'], 0, '°'),
      formatHistoryValue(item['DST0-sig'], 0, '%')
    ];
    values.forEach((value) => {
      const cell = document.createElement('td');
      cell.textContent = value;
      row.appendChild(cell);
    });
    body.appendChild(row);
  });
}

function formatHistoryValue(value, decimals, unit) {
  const number = Number(value);
  return Number.isFinite(number) ? `${number.toFixed(decimals)}${unit}` : '—';
}

function numberValue(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : NaN;
}

function setMetric(id, value, decimals) {
  $(id).textContent = Number.isFinite(value) ? value.toFixed(decimals) : '—';
}

function updateNotes(temp, humidity, wind) {
  $('temperatureNote').textContent = !Number.isFinite(temp) ? 'Belum ada data' : temp < 20 ? 'Udara terasa sejuk' : temp < 30 ? 'Suhu dalam rentang nyaman' : 'Udara terasa hangat';
  $('humidityNote').textContent = !Number.isFinite(humidity) ? 'Belum ada data' : humidity < 40 ? 'Udara cukup kering' : humidity <= 70 ? 'Kelembapan ideal' : 'Udara cukup lembap';
  $('windNote').textContent = !Number.isFinite(wind) ? 'Belum ada data' : wind < .5 ? 'Angin tenang' : wind < 5.5 ? 'Hembusan ringan' : 'Angin cukup kuat';
}

function directionName(degree) {
  return ['Utara', 'Timur Laut', 'Timur', 'Tenggara', 'Selatan', 'Barat Daya', 'Barat', 'Barat Laut'][Math.round(degree / 45) % 8];
}

function updateSignal(signal) {
  $('signalHeader').textContent = Number.isFinite(signal) ? `${Math.round(signal)}%` : '—';
  const active = Number.isFinite(signal) ? Math.ceil(signal / 25) : 0;
  document.querySelectorAll('.signal i').forEach((bar, index) => bar.classList.toggle('active', index < active));
}

function drawChart() {
  const canvas = $('temperatureChart');
  const box = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  canvas.width = box.width * ratio;
  canvas.height = box.height * ratio;
  const ctx = canvas.getContext('2d');
  ctx.scale(ratio, ratio);
  const w = box.width, h = box.height, pad = { top: 15, right: 10, bottom: 28, left: 34 };
  const values = state.temperatures.length ? state.temperatures : [24, 25, 24.7, 25.6, 26, 25.7, 26.4];
  const labels = state.labels.length ? state.labels : ['', '', '', '', '', '', ''];
  let min = Math.floor(Math.min(...values) - 1), max = Math.ceil(Math.max(...values) + 1);
  if (max === min) max += 1;

  ctx.font = '9px DM Mono';
  ctx.fillStyle = '#98a09b';
  ctx.strokeStyle = 'rgba(28,45,37,.08)';
  ctx.lineWidth = 1;
  for (let i = 0; i < 4; i++) {
    const y = pad.top + ((h - pad.top - pad.bottom) / 3) * i;
    ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(w - pad.right, y); ctx.stroke();
    const label = (max - ((max - min) / 3) * i).toFixed(0);
    ctx.fillText(label, 5, y + 3);
  }

  const x = (i) => pad.left + (values.length === 1 ? 0 : i * (w - pad.left - pad.right) / (values.length - 1));
  const y = (value) => pad.top + (max - value) * (h - pad.top - pad.bottom) / (max - min);
  const gradient = ctx.createLinearGradient(0, pad.top, 0, h - pad.bottom);
  gradient.addColorStop(0, 'rgba(227,135,87,.20)'); gradient.addColorStop(1, 'rgba(227,135,87,0)');
  ctx.beginPath(); values.forEach((value, i) => i ? ctx.lineTo(x(i), y(value)) : ctx.moveTo(x(i), y(value)));
  ctx.lineTo(x(values.length - 1), h - pad.bottom); ctx.lineTo(x(0), h - pad.bottom); ctx.closePath(); ctx.fillStyle = gradient; ctx.fill();
  ctx.beginPath(); values.forEach((value, i) => i ? ctx.lineTo(x(i), y(value)) : ctx.moveTo(x(i), y(value)));
  ctx.strokeStyle = '#e38757'; ctx.lineWidth = 2; ctx.lineJoin = 'round'; ctx.stroke();
  values.forEach((value, i) => { ctx.beginPath(); ctx.arc(x(i), y(value), 3, 0, Math.PI * 2); ctx.fillStyle = '#fff'; ctx.fill(); ctx.strokeStyle = '#e38757'; ctx.stroke(); });
  if (labels.length) {
    ctx.fillStyle = '#98a09b';
    [0, Math.floor((labels.length - 1) / 2), labels.length - 1].forEach((i) => { if (labels[i]) ctx.fillText(labels[i], Math.max(pad.left, Math.min(x(i) - 14, w - 38)), h - 7); });
  }
  $('chartRange').textContent = `${min}–${max} °C`;
}

$('activeTopic').textContent = state.config.topic;
window.addEventListener('resize', drawChart);
restoreHistory();
connect();
