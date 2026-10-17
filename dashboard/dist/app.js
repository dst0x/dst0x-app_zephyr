const $ = (id) => document.getElementById(id);
const environment = window.__DST0_CONFIG__ || {};
const DEVICE_TIME_ZONE = environment.timezone || 'Asia/Jakarta';

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
const PARAMETER_CONFIG = {
  temperature: { label: 'Suhu', unit: '°C', decimals: 1, color: '#078fe8', value: (item) => numberValue(item['DST0-tem']) },
  humidity: { label: 'Kelembapan', unit: '%RH', decimals: 1, color: '#16b9ed', value: (item) => numberValue(item['DST0-hum']) },
  pressure: { label: 'Tekanan', unit: 'hPa', decimals: 1, color: '#163d83', value: (item) => numberValue(item['DST0-pre']) },
  wind: { label: 'Kecepatan angin', unit: 'm/s', decimals: 2, color: '#15a777', value: (item) => numberValue(item['DST0-wsp']) },
  direction: { label: 'Arah angin', unit: '°', decimals: 0, color: '#ef8d32', value: (item) => numberValue(item['DST0-dir']) },
  rainfall: { label: 'Rainfall', unit: 'mm', decimals: 1, color: '#6a72df', value: (item) => firstNumber(item, ['DST0-rain', 'DST0-rnf', 'DST0-rainfall', 'rainfall', 'rain']) },
  signal: { label: 'Sinyal', unit: '%', decimals: 0, color: '#8191a7', value: (item) => numberValue(item['DST0-sig']) }
};

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
  $('analyticsConnectionBadge').className = `monitor-status ${status}`;
  $('analyticsConnectionBadge').textContent = status === 'online' ? 'Online' : status === 'connecting' ? 'Connecting' : 'Offline';
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
  const rainfall = firstNumber(data, ['DST0-rain', 'DST0-rnf', 'DST0-rainfall', 'rainfall', 'rain']);

  $('analyticsDeviceId').textContent = data.id || '—';
  $('firmwareVersion').textContent = data.ver || '—';
  setMetric('stationTemperature', temp, 1);
  setMetric('stationHumidity', humidity, 1);
  setMetric('stationPressure', pressure, 1);
  setMetric('stationWind', wind, 2);
  setMetric('stationRainfall', rainfall, 1);
  $('rainfallStatus').textContent = !Number.isFinite(rainfall) ? 'Menunggu data hujan' : rainfall === 0 ? 'Tidak ada hujan terdeteksi' : 'Hujan terdeteksi';
  updateStationWeatherEffects(wind, rainfall, direction);

  if (Number.isFinite(direction)) {
    const degree = ((direction % 360) + 360) % 360;
    $('stationDirection').textContent = `${directionName(degree)} · ${degree.toFixed(0)}°`;
  } else {
    $('stationDirection').textContent = '—';
  }

  updateSignal(signal);
  if (saveToHistory) saveReading(data);
}

function historyStorageKey() {
  /* v2 excludes cached records created before firmware timestamps were UTC. */
  return `dst0WeatherHistory:v2:${state.config.topic}`;
}

function saveReading(data) {
  const reading = { ...data, _receivedAt: Date.now() };
  const duplicateIndex = state.history.findIndex((item) =>
    item.id === reading.id && Number(item.timestamp) === Number(reading.timestamp)
  );
  if (duplicateIndex >= 0) state.history[duplicateIndex] = reading;
  else state.history.push(reading);
  state.history = sortHistory(state.history).slice(-10000);

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
    state.history = Array.isArray(stored) ? sortHistory(stored).slice(-10000) : [];
  } catch {
    state.history = [];
  }

  if (state.history.length) displayData(state.history.at(-1), false);
  refreshHistoryViews();
}

function sortHistory(history) {
  return [...history].sort((a, b) => {
    const aTime = Number.isFinite(Number(a.timestamp)) ? Number(a.timestamp) * 1000 : Number(a._receivedAt) || 0;
    const bTime = Number.isFinite(Number(b.timestamp)) ? Number(b.timestamp) * 1000 : Number(b._receivedAt) || 0;
    return aTime - bTime;
  });
}

function refreshHistoryViews() {
  state.messages = state.history.length;
  $('messageCount').textContent = `${state.history.length} data tersimpan`;
  setDefaultDateRange();
  state.analysisRows = aggregateHistory();
  $('historyCount').textContent = `${state.analysisRows.length} DATA`;
  $('filteredCount').textContent = String(state.analysisRows.length);
  renderHistoryTable();
  drawAnalyticsChart();
}

function selectedParameters() {
  return [...document.querySelectorAll('input[name="sensorParameter"]:checked')].map((input) => input.value);
}

function setDefaultDateRange() {
  const dates = state.history.map(deviceDate).filter(Boolean).map(localDateKey).sort();
  if (!dates.length) return;
  if (!$('rangeStart').value) $('rangeStart').value = dates[0];
  if (!$('rangeEnd').value) $('rangeEnd').value = dates.at(-1);
}

function localDateParts(date) {
  const parts = new Intl.DateTimeFormat('en-CA', {
    timeZone: DEVICE_TIME_ZONE, year: 'numeric', month: '2-digit', day: '2-digit'
  }).formatToParts(date);
  return Object.fromEntries(parts.filter((part) => part.type !== 'literal').map((part) => [part.type, Number(part.value)]));
}

function localDateKey(date) {
  const { year, month, day } = localDateParts(date);
  return `${year}-${String(month).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
}

function periodBucket(date, period) {
  if (period === 'realtime') {
    const sort = date.getTime();
    const label = date.toLocaleString('id-ID', {
      timeZone: DEVICE_TIME_ZONE, day: '2-digit', month: 'short', year: 'numeric',
      hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23'
    });
    return { key: String(sort), sort, label };
  }
  if (period === 'minute' || period === 'hour') {
    const size = period === 'minute' ? 60000 : 3600000;
    const sort = Math.floor(date.getTime() / size) * size;
    const label = new Date(sort).toLocaleString('id-ID', {
      timeZone: DEVICE_TIME_ZONE, day: '2-digit', month: 'short', year: 'numeric',
      hour: '2-digit', minute: '2-digit', hourCycle: 'h23'
    });
    return { key: String(sort), sort, label };
  }

  const { year, month, day } = localDateParts(date);
  let sort = Date.UTC(year, month - 1, day);
  if (period === 'week') {
    const weekDay = new Date(sort).getUTCDay() || 7;
    sort -= (weekDay - 1) * 86400000;
  }
  const bucketDate = new Date(sort);
  const label = period === 'week'
    ? `Minggu ${bucketDate.toLocaleDateString('id-ID', { day: '2-digit', month: 'short', year: 'numeric', timeZone: 'UTC' })}`
    : bucketDate.toLocaleDateString('id-ID', { day: '2-digit', month: 'short', year: 'numeric', timeZone: 'UTC' });
  return { key: String(sort), sort, label };
}

function aggregateHistory() {
  const period = $('aggregationPeriod').value;
  const start = $('rangeStart').value;
  const end = $('rangeEnd').value;
  const parameters = selectedParameters();
  const groups = new Map();

  state.history.forEach((item) => {
    const date = deviceDate(item);
    if (!date) return;
    const dateKey = localDateKey(date);
    if ((start && dateKey < start) || (end && dateKey > end)) return;
    const bucket = periodBucket(date, period);
    if (!groups.has(bucket.key)) groups.set(bucket.key, { ...bucket, items: [] });
    groups.get(bucket.key).items.push(item);
  });

  return [...groups.values()].sort((a, b) => a.sort - b.sort).map((group) => {
    const row = { label: group.label, sort: group.sort, count: group.items.length };
    parameters.forEach((key) => {
      const values = group.items.map(PARAMETER_CONFIG[key].value).filter(Number.isFinite);
      if (!values.length) row[key] = NaN;
      else if (key === 'direction') {
        const sin = values.reduce((sum, value) => sum + Math.sin(value * Math.PI / 180), 0);
        const cos = values.reduce((sum, value) => sum + Math.cos(value * Math.PI / 180), 0);
        row[key] = (Math.atan2(sin / values.length, cos / values.length) * 180 / Math.PI + 360) % 360;
      } else row[key] = values.reduce((sum, value) => sum + value, 0) / values.length;
    });
    return row;
  });
}

function deviceDate(item) {
  const timestamp = Number(item.timestamp);
  if (!Number.isFinite(timestamp)) return null;
  const date = new Date(timestamp * 1000);
  return Number.isNaN(date.getTime()) ? null : date;
}

function formatDeviceTime(date) {
  return date.toLocaleTimeString('id-ID', {
    timeZone: DEVICE_TIME_ZONE,
    hour: '2-digit',
    minute: '2-digit',
    hourCycle: 'h23'
  });
}

function formatDeviceTimestamp(date, includeSeconds) {
  return date.toLocaleString('id-ID', {
    timeZone: DEVICE_TIME_ZONE,
    day: '2-digit',
    month: 'short',
    year: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    second: includeSeconds ? '2-digit' : undefined,
    hourCycle: 'h23',
    timeZoneName: 'short'
  });
}

function renderHistoryTable() {
  const head = $('historyHead');
  const body = $('historyBody');
  const parameters = selectedParameters();
  const headerRow = document.createElement('tr');
  ['Waktu Pemantauan', 'Jumlah Data', ...parameters].forEach((key) => {
    const cell = document.createElement('th');
    cell.textContent = PARAMETER_CONFIG[key] ? `${PARAMETER_CONFIG[key].label} (${PARAMETER_CONFIG[key].unit})` : key;
    headerRow.appendChild(cell);
  });
  head.replaceChildren(headerRow);
  body.replaceChildren();
  const rows = state.analysisRows || [];
  if (!rows.length) {
    const row = document.createElement('tr');
    row.className = 'empty-row';
    const cell = document.createElement('td');
    cell.colSpan = parameters.length + 2;
    cell.textContent = state.history.length ? 'Tidak ada data pada rentang yang dipilih' : 'Belum ada data tersimpan';
    row.appendChild(cell);
    body.appendChild(row);
    return;
  }

  rows.slice().reverse().forEach((item) => {
    const row = document.createElement('tr');
    const values = [
      item.label,
      String(item.count),
      ...parameters.map((key) => formatHistoryValue(item[key], PARAMETER_CONFIG[key].decimals, ` ${PARAMETER_CONFIG[key].unit}`))
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

function firstNumber(data, keys) {
  for (const key of keys) {
    const value = numberValue(data[key]);
    if (Number.isFinite(value)) return value;
  }
  return NaN;
}

function setMetric(id, value, decimals) {
  $(id).textContent = Number.isFinite(value) ? value.toFixed(decimals) : '—';
}

function directionName(degree) {
  return ['Utara', 'Timur Laut', 'Timur', 'Tenggara', 'Selatan', 'Barat Daya', 'Barat', 'Barat Laut'][Math.round(degree / 45) % 8];
}

function updateStationWeatherEffects(wind, rainfall, direction) {
  const effects = $('stationWeatherEffects');
  const windActive = Number.isFinite(wind) && wind >= 0.25;
  const rainActive = Number.isFinite(rainfall) && rainfall > 0;
  effects.classList.toggle('has-wind', windActive);
  effects.classList.toggle('has-rain', rainActive);
  if (Number.isFinite(direction)) {
    const normalizedDirection = ((direction % 360) + 360) % 360;
    // Arah meteorologi menyatakan asal angin. Pertahankan lintasan visual
    // horizontal dan balikkan gerak saat angin datang dari sisi timur.
    effects.classList.toggle('wind-reverse', normalizedDirection > 0 && normalizedDirection < 180);
  }
  if (windActive) effects.style.setProperty('--wind-duration', `${Math.max(.45, 2.1 - Math.min(wind, 10) * .15).toFixed(2)}s`);
  if (rainActive) {
    effects.style.setProperty('--rain-duration', `${Math.max(.38, 1.05 - Math.min(rainfall, 8) * .07).toFixed(2)}s`);
    effects.style.setProperty('--rain-opacity', String(Math.min(.95, .48 + rainfall * .08)));
  }
}

function updateSignal(signal) {
  const active = Number.isFinite(signal) ? Math.ceil(signal / 25) : 0;
  document.querySelectorAll('.signal i').forEach((bar, index) => bar.classList.toggle('active', index < active));
}

function parameterChartType(key) {
  return document.querySelector(`select[data-chart-parameter="${key}"]`)?.value || 'line';
}

function drawAnalyticsChart() {
  const canvas = $('analyticsChart');
  if (!canvas || canvas.closest('[hidden]')) return;
  const box = canvas.getBoundingClientRect();
  if (!box.width || !box.height) return;
  const ratio = window.devicePixelRatio || 1;
  canvas.width = box.width * ratio;
  canvas.height = box.height * ratio;
  const ctx = canvas.getContext('2d');
  ctx.scale(ratio, ratio);
  const w = box.width, h = box.height, pad = { top: 15, right: 12, bottom: 32, left: 38 };
  const rows = (state.analysisRows || []).slice(-80);
  const parameters = selectedParameters();
  const directionOnly = parameters.length === 1 && parameterChartType(parameters[0]) === 'direction';
  const barParameters = parameters.filter((key) => parameterChartType(key) === 'bar');
  const singleKey = parameters.length === 1 ? parameters[0] : null;
  const singleValues = singleKey ? rows.map((row) => row[singleKey]).filter(Number.isFinite) : [];
  let singleMin = singleValues.length ? Math.min(...singleValues) : 0;
  let singleMax = singleValues.length ? Math.max(...singleValues) : 1;
  if (singleKey && parameterChartType(singleKey) === 'bar' && singleMin >= 0) singleMin = 0;
  if (singleMin === singleMax) {
    if (singleKey && parameterChartType(singleKey) === 'bar' && singleMin === 0) singleMax = 1;
    else { singleMin -= .5; singleMax += .5; }
  }
  const labels = rows.map((row) => row.label);
  ctx.clearRect(0, 0, w, h);

  ctx.font = '9px Inter';
  ctx.fillStyle = '#91a0b4';
  ctx.strokeStyle = 'rgba(18,72,132,.08)';
  ctx.lineWidth = 1;
  const directionLabels = ['360° U', '270° B', '180° S', '90° T', '0° U'];
  for (let i = 0; i < 5; i++) {
    const y = pad.top + ((h - pad.top - pad.bottom) / 4) * i;
    ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(w - pad.right, y); ctx.stroke();
    const axisLabel = directionOnly
      ? directionLabels[i]
      : singleKey ? (singleMax - (singleMax - singleMin) * i / 4).toFixed(PARAMETER_CONFIG[singleKey].decimals) : `${100 - i * 25}%`;
    ctx.fillText(axisLabel, 5, y + 3);
  }

  if (!rows.length || !parameters.length) {
    ctx.fillStyle = '#718198';
    ctx.textAlign = 'center';
    ctx.fillText(rows.length ? 'Pilih minimal satu parameter' : 'Belum ada data pada rentang ini', w / 2, h / 2);
    ctx.textAlign = 'start';
  }
  const x = (i) => rows.length <= 1 ? (pad.left + w - pad.right) / 2 : pad.left + i * (w - pad.left - pad.right) / (rows.length - 1);
  parameters.forEach((key) => {
    const chartType = parameterChartType(key);
    const values = rows.map((row) => row[key]);
    const finite = values.filter(Number.isFinite);
    if (!finite.length) return;
    let min = chartType === 'bar' && Math.min(...finite) >= 0 ? 0 : Math.min(...finite), max = Math.max(...finite);
    if (min === max) {
      if (chartType === 'bar' && min === 0) max = 1;
      else { min -= .5; max += .5; }
    }
    const y = (value) => pad.top + (max - value) * (h - pad.top - pad.bottom) / (max - min);
    if (chartType === 'bar') {
      const slot = (w - pad.left - pad.right) / Math.max(rows.length, 1);
      const groupWidth = Math.min(30, slot * .72);
      const barWidth = Math.max(2, groupWidth / Math.max(barParameters.length, 1));
      values.forEach((value, i) => {
        if (!Number.isFinite(value)) return;
        const left = x(i) - groupWidth / 2 + barParameters.indexOf(key) * barWidth;
        const top = y(value);
        ctx.fillStyle = PARAMETER_CONFIG[key].color;
        ctx.fillRect(left, top, Math.max(1, barWidth - 1), h - pad.bottom - top);
      });
    } else if (chartType === 'direction') {
      const step = Math.max(1, Math.ceil(values.length / 32));
      values.forEach((value, i) => {
        if (!Number.isFinite(value) || i % step) return;
        const degree = ((value % 360) + 360) % 360;
        const arrowY = pad.top + (360 - degree) * (h - pad.top - pad.bottom) / 360;
        ctx.save();
        ctx.translate(x(i), arrowY);
        ctx.rotate(degree * Math.PI / 180);
        ctx.strokeStyle = PARAMETER_CONFIG[key].color;
        ctx.fillStyle = PARAMETER_CONFIG[key].color;
        ctx.lineWidth = 1.5;
        ctx.beginPath(); ctx.moveTo(0, 7); ctx.lineTo(0, -7); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(0, -9); ctx.lineTo(-4, -3); ctx.lineTo(4, -3); ctx.closePath(); ctx.fill();
        ctx.restore();
      });
    } else {
      ctx.beginPath();
      let started = false;
      values.forEach((value, i) => {
        if (!Number.isFinite(value)) { started = false; return; }
        if (!started) { ctx.moveTo(x(i), y(value)); started = true; } else ctx.lineTo(x(i), y(value));
      });
      ctx.strokeStyle = PARAMETER_CONFIG[key].color;
      ctx.lineWidth = 2;
      ctx.lineJoin = 'round';
      ctx.stroke();
    }
  });
  if (labels.length) {
    ctx.fillStyle = '#91a0b4';
    const indexes = [...new Set([0, Math.floor((labels.length - 1) / 2), labels.length - 1])];
    indexes.forEach((i) => { if (labels[i]) ctx.fillText(shortChartLabel(labels[i]), Math.max(pad.left, Math.min(x(i) - 20, w - 65)), h - 8); });
  }
  renderAnalyticsLegend(parameters, rows);
}

function shortChartLabel(label) {
  return label.length > 16 ? `${label.slice(0, 15)}…` : label;
}

function renderAnalyticsLegend(parameters, rows) {
  const legend = $('analyticsLegend');
  legend.replaceChildren();
  parameters.forEach((key) => {
    const config = PARAMETER_CONFIG[key];
    const values = rows.map((row) => row[key]).filter(Number.isFinite);
    const item = document.createElement('span');
    const swatch = document.createElement('i');
    swatch.style.background = config.color;
    const range = values.length ? ` ${Math.min(...values).toFixed(config.decimals)}–${Math.max(...values).toFixed(config.decimals)} ${config.unit}` : ' tidak tersedia';
    const typeLabels = { line: 'Line', bar: 'Batang', direction: 'Arah' };
    item.append(swatch, `${config.label} · ${typeLabels[parameterChartType(key)]}:${range}`);
    legend.appendChild(item);
  });
}

function refreshAnalytics() {
  state.analysisRows = aggregateHistory();
  const periodLabels = { minute: 'menit', hour: 'jam', day: 'hari', week: 'minggu' };
  const period = $('aggregationPeriod').value;
  const timeLabel = period === 'realtime' ? 'Setiap pembacaan realtime' : `Rata-rata per ${periodLabels[period]}`;
  $('chartSubtitle').textContent = `${timeLabel} · tipe grafik per parameter`;
  $('historyCount').textContent = `${state.analysisRows.length} DATA`;
  $('filteredCount').textContent = String(state.analysisRows.length);
  $('downloadExcel').disabled = !state.analysisRows.length;
  renderHistoryTable();
  drawAnalyticsChart();
}

function escapeXml(value) {
  return String(value).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function downloadExcel() {
  const rows = state.analysisRows || [];
  if (!rows.length) return;
  const parameters = selectedParameters();
  const headers = ['Periode', 'Jumlah data', ...parameters.map((key) => `${PARAMETER_CONFIG[key].label} (${PARAMETER_CONFIG[key].unit})`)];
  const xmlRows = [headers.map((value) => `<Cell ss:StyleID="Header"><Data ss:Type="String">${escapeXml(value)}</Data></Cell>`).join('')];
  rows.forEach((row) => {
    const cells = [
      `<Cell><Data ss:Type="String">${escapeXml(row.label)}</Data></Cell>`,
      `<Cell><Data ss:Type="Number">${row.count}</Data></Cell>`,
      ...parameters.map((key) => Number.isFinite(row[key])
        ? `<Cell><Data ss:Type="Number">${row[key]}</Data></Cell>`
        : '<Cell><Data ss:Type="String">n.a.</Data></Cell>')
    ];
    xmlRows.push(cells.join(''));
  });
  const xml = `<?xml version="1.0"?><Workbook xmlns="urn:schemas-microsoft-com:office:spreadsheet" xmlns:ss="urn:schemas-microsoft-com:office:spreadsheet"><Styles><Style ss:ID="Default"><Font ss:FontName="Aptos" ss:Size="10"/></Style><Style ss:ID="Header"><Font ss:Bold="1" ss:Color="#FFFFFF"/><Interior ss:Color="#0A4D9C" ss:Pattern="Solid"/></Style></Styles><Worksheet ss:Name="Data Sensor"><Table>${xmlRows.map((row) => `<Row>${row}</Row>`).join('')}</Table></Worksheet></Workbook>`;
  const blob = new Blob([xml], { type: 'application/vnd.ms-excel;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = `dst0-data-${$('rangeStart').value || 'awal'}-${$('rangeEnd').value || 'akhir'}.xls`;
  link.click();
  URL.revokeObjectURL(url);
}

let sensorSelectionSnapshot = [];
let sensorChartTypeSnapshot = {};

function sensorInputs() {
  return [...document.querySelectorAll('input[name="sensorParameter"]')];
}

function updateSensorPickerControls() {
  const inputs = sensorInputs();
  const selected = inputs.filter((input) => input.checked);
  $('selectAllSensors').checked = selected.length === inputs.length;
  $('selectAllSensors').indeterminate = selected.length > 0 && selected.length < inputs.length;
  $('applySensorSelection').disabled = selected.length === 0;
  inputs.forEach((input) => {
    const select = document.querySelector(`select[data-chart-parameter="${input.value}"]`);
    if (select) select.disabled = !input.checked;
  });
}

function updateSensorPickerLabel() {
  const parameters = selectedParameters();
  $('sensorPickerLabel').textContent = 'Pilih Sensor';
  const badges = $('selectedSensorBadges');
  badges.replaceChildren();
  parameters.forEach((key) => {
    const badge = document.createElement('span');
    badge.className = 'selected-sensor-badge';
    const color = document.createElement('i');
    color.style.background = PARAMETER_CONFIG[key].color;
    badge.append(color, PARAMETER_CONFIG[key].label);
    badges.appendChild(badge);
  });
}

function openSensorPicker() {
  sensorSelectionSnapshot = selectedParameters();
  sensorChartTypeSnapshot = Object.fromEntries(sensorInputs().map((input) => [input.value, parameterChartType(input.value)]));
  $('sensorPickerMenu').hidden = false;
  $('sensorPickerToggle').setAttribute('aria-expanded', 'true');
  $('sensorSearch').value = '';
  document.querySelectorAll('#sensorOptions [data-search]').forEach((row) => { row.hidden = false; });
  updateSensorPickerControls();
  $('sensorSearch').focus();
}

function closeSensorPicker(commit = false) {
  const typeChanged = sensorInputs().some((input) => parameterChartType(input.value) !== sensorChartTypeSnapshot[input.value]);
  const selectionChanged = selectedParameters().join(',') !== sensorSelectionSnapshot.join(',') || typeChanged;
  if (!commit) {
    sensorInputs().forEach((input) => { input.checked = sensorSelectionSnapshot.includes(input.value); });
    Object.entries(sensorChartTypeSnapshot).forEach(([key, value]) => {
      const select = document.querySelector(`select[data-chart-parameter="${key}"]`);
      if (select) select.value = value;
    });
  }
  $('sensorPickerMenu').hidden = true;
  $('sensorPickerToggle').setAttribute('aria-expanded', 'false');
  updateSensorPickerControls();
  if (!commit && selectionChanged) refreshAnalytics();
}

function applySensorSelection() {
  const parameters = selectedParameters();
  if (!parameters.length) return;
  updateSensorPickerLabel();
  closeSensorPicker(true);
  refreshAnalytics();
}

$('activeTopic').textContent = state.config.topic;
document.querySelectorAll('.nav-button').forEach((button) => button.addEventListener('click', () => {
  document.querySelectorAll('.nav-button').forEach((item) => item.classList.toggle('active', item === button));
  document.querySelectorAll('.app-page').forEach((page) => { page.hidden = page.id !== button.dataset.page; });
  if (button.dataset.page === 'analyticsPage') requestAnimationFrame(drawAnalyticsChart);
}));
['aggregationPeriod', 'rangeStart', 'rangeEnd'].forEach((id) => $(id).addEventListener('change', refreshAnalytics));
$('sensorPickerToggle').addEventListener('click', () => {
  if ($('sensorPickerMenu').hidden) openSensorPicker(); else closeSensorPicker();
});
$('closeSensorPicker').addEventListener('click', () => closeSensorPicker());
$('selectAllSensors').addEventListener('change', (event) => {
  sensorInputs().forEach((input) => { input.checked = event.target.checked; });
  updateSensorPickerControls();
});
sensorInputs().forEach((input) => input.addEventListener('change', updateSensorPickerControls));
document.querySelectorAll('select[data-chart-parameter]').forEach((select) => select.addEventListener('change', updateSensorPickerControls));
$('sensorSearch').addEventListener('input', (event) => {
  const query = event.target.value.trim().toLowerCase();
  document.querySelectorAll('#sensorOptions [data-search]').forEach((row) => {
    row.hidden = Boolean(query) && !row.dataset.search.includes(query);
  });
});
$('applySensorSelection').addEventListener('click', applySensorSelection);
document.addEventListener('click', (event) => {
  if (!$('sensorPickerMenu').hidden && !$('sensorPickerMenu').contains(event.target) && !$('sensorPickerToggle').contains(event.target)) closeSensorPicker();
});
document.addEventListener('keydown', (event) => {
  if (event.key === 'Escape' && !$('sensorPickerMenu').hidden) closeSensorPicker();
});
$('downloadExcel').addEventListener('click', downloadExcel);
$('fullscreenAnalytics').addEventListener('click', () => {
  if (document.fullscreenElement) document.exitFullscreen?.();
  else $('analyticsPage').requestFullscreen?.();
});
document.addEventListener('fullscreenchange', () => {
  $('fullscreenAnalytics').firstElementChild.textContent = document.fullscreenElement ? '×' : '⛶';
  requestAnimationFrame(drawAnalyticsChart);
});
window.addEventListener('resize', drawAnalyticsChart);
updateSensorPickerLabel();
updateSensorPickerControls();
restoreHistory();
connect();
