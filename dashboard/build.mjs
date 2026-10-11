import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';

const root = new URL('.', import.meta.url);
const envPath = resolve(root.pathname, '.env');
const outputPath = resolve(root.pathname, 'dist/env.js');

function parseEnv(source) {
  const values = {};
  for (const sourceLine of source.split(/\r?\n/)) {
    const line = sourceLine.trim();
    if (!line || line.startsWith('#')) continue;
    const separator = line.indexOf('=');
    if (separator < 0) continue;
    const key = line.slice(0, separator).trim();
    let value = line.slice(separator + 1).trim();
    if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith("'") && value.endsWith("'"))) {
      value = value.slice(1, -1);
    }
    values[key] = value;
  }
  return values;
}

let fileEnv = {};

try {
  fileEnv = parseEnv(await readFile(envPath, 'utf8'));
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
}

const env = {
  ...fileEnv,
  ...Object.fromEntries(
    ['MQTT_URL', 'MQTT_TOPIC', 'MQTT_USERNAME', 'MQTT_PASSWORD', 'DASHBOARD_TIMEZONE']
      .filter((key) => process.env[key] !== undefined)
      .map((key) => [key, process.env[key]])
  )
};
const config = {
  url: env.MQTT_URL || '',
  topic: env.MQTT_TOPIC || '',
  username: env.MQTT_USERNAME || '',
  password: env.MQTT_PASSWORD || '',
  timezone: env.DASHBOARD_TIMEZONE || 'Asia/Jakarta'
};

await writeFile(outputPath, `window.__DST0_CONFIG__ = ${JSON.stringify(config)};\n`, 'utf8');
console.log(`Generated ${outputPath}`);
