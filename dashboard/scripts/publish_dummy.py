#!/usr/bin/env python3
"""Publish continuous dummy weather readings to the DST0 MQTT topic."""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import signal
import sys
import threading
import time
from pathlib import Path
from urllib.parse import urlparse


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_ENV_FILE = SCRIPT_DIR.parent / ".env"


def load_env_file(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for source_line in path.read_text(encoding="utf-8").splitlines():
        line = source_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key.strip()] = value
    return values


def bounded_walk(value: float, low: float, high: float, step: float) -> float:
    return min(high, max(low, value + random.uniform(-step, step)))


class WeatherGenerator:
    def __init__(self, device_id: str) -> None:
        self.device_id = device_id
        self.temperature = 27.2
        self.humidity = 46.8
        self.pressure = 993.5
        self.wind_speed = 0.4
        self.wind_direction = 15.0
        self.signal_strength = 82.0
        self.rain_cycles = 0

    def next_payload(self) -> dict[str, object]:
        hour = time.localtime().tm_hour + time.localtime().tm_min / 60
        target_temperature = 26.5 + 3.0 * math.sin((hour - 8) * math.pi / 12)
        self.temperature += (target_temperature - self.temperature) * 0.08
        self.temperature = bounded_walk(self.temperature, 20.0, 35.0, 0.18)
        self.humidity = bounded_walk(self.humidity, 35.0, 90.0, 0.8)
        self.pressure = bounded_walk(self.pressure, 985.0, 1015.0, 0.25)
        self.wind_speed = bounded_walk(self.wind_speed, 0.0, 12.0, 0.35)
        self.wind_direction = (self.wind_direction + random.uniform(-12.0, 12.0)) % 360
        self.signal_strength = bounded_walk(self.signal_strength, 45.0, 100.0, 2.0)

        if self.rain_cycles <= 0 and random.random() < 0.025:
            self.rain_cycles = random.randint(5, 30)
        rainfall = random.uniform(0.1, 4.5) if self.rain_cycles > 0 else 0.0
        self.rain_cycles = max(0, self.rain_cycles - 1)

        return {
            "id": self.device_id,
            "ver": "dummy-1.0.0",
            "timestamp": int(time.time()),
            "DST0-sig": round(self.signal_strength),
            "DST0-tem": round(self.temperature, 1),
            "DST0-hum": round(self.humidity, 1),
            "DST0-pre": round(self.pressure, 1),
            "DST0-wsp": round(self.wind_speed, 2),
            "DST0-dir": round(self.wind_direction),
            "DST0-rain": round(rainfall, 1),
        }


def mqtt_client(broker_url: str, client_id: str, username: str, password: str):
    try:
        import paho.mqtt.client as mqtt
    except ImportError as error:
        raise SystemExit(
            "Dependensi belum terpasang. Jalankan: "
            "python3 -m pip install -r dashboard/scripts/requirements.txt"
        ) from error

    parsed = urlparse(broker_url)
    if parsed.scheme not in {"ws", "wss", "mqtt", "mqtts"} or not parsed.hostname:
        raise SystemExit("MQTT_URL harus menggunakan ws://, wss://, mqtt://, atau mqtts://")

    transport = "websockets" if parsed.scheme in {"ws", "wss"} else "tcp"
    if hasattr(mqtt, "CallbackAPIVersion"):
        client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=client_id,
            protocol=mqtt.MQTTv311,
            transport=transport,
        )
    else:
        client = mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311, transport=transport)

    if username:
        client.username_pw_set(username, password)
    if transport == "websockets":
        path = parsed.path or "/mqtt"
        if parsed.query:
            path = f"{path}?{parsed.query}"
        client.ws_set_options(path=path)
    if parsed.scheme in {"wss", "mqtts"}:
        client.tls_set()

    default_ports = {"ws": 80, "wss": 443, "mqtt": 1883, "mqtts": 8883}
    return client, parsed.hostname, parsed.port or default_ports[parsed.scheme]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Kirim data cuaca dummy ke dashboard DST0.")
    parser.add_argument("--env-file", type=Path, default=DEFAULT_ENV_FILE)
    parser.add_argument("--url", help="URL broker MQTT; default membaca MQTT_URL")
    parser.add_argument("--topic", help="Topik tujuan; default membaca MQTT_TOPIC")
    parser.add_argument("--username", help="Username publisher")
    parser.add_argument("--password", help="Password publisher")
    parser.add_argument("--device-id", default="DST0X-DUMMY")
    parser.add_argument("--interval", type=float, default=2.0, help="Jeda kirim dalam detik")
    parser.add_argument("--count", type=int, default=0, help="Jumlah payload; 0 berarti terus-menerus")
    parser.add_argument("--qos", type=int, choices=(0, 1, 2), default=0)
    parser.add_argument("--retain", action="store_true")
    parser.add_argument("--seed", type=int, help="Seed agar urutan data dapat direproduksi")
    parser.add_argument("--dry-run", action="store_true", help="Cetak payload tanpa terhubung ke broker")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.interval < 1:
        raise SystemExit("--interval minimal 1 detik karena timestamp payload menggunakan satuan detik")
    if args.count < 0:
        raise SystemExit("--count tidak boleh negatif")
    if args.seed is not None:
        random.seed(args.seed)

    file_env = load_env_file(args.env_file)
    config = {**file_env, **os.environ}
    broker_url = args.url or config.get("MQTT_URL", "")
    topic = args.topic or config.get("MQTT_TOPIC", "dstxlabs/signal")
    username = args.username or config.get("MQTT_PUBLISH_USERNAME") or config.get("MQTT_USERNAME", "")
    password = args.password or config.get("MQTT_PUBLISH_PASSWORD") or config.get("MQTT_PASSWORD", "")
    generator = WeatherGenerator(args.device_id)
    stop_event = threading.Event()

    def request_stop(_signum=None, _frame=None) -> None:
        stop_event.set()

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    client = None
    connected = threading.Event()
    if not args.dry_run:
        if not broker_url:
            raise SystemExit("MQTT_URL belum tersedia di .env atau argumen --url")
        client, host, port = mqtt_client(
            broker_url, f"dst0-dummy-{random.randrange(0x1000000):06x}", username, password
        )

        def on_connect(_client, _userdata, _flags, reason_code, _properties=None):
            if reason_code == 0:
                connected.set()
                print(f"Terhubung ke {host}:{port}; publish ke {topic}")
            else:
                print(f"Koneksi ditolak broker: {reason_code}", file=sys.stderr)

        client.on_connect = on_connect
        client.connect(host, port, keepalive=60)
        client.loop_start()
        if not connected.wait(timeout=12):
            client.loop_stop()
            client.disconnect()
            raise SystemExit("Tidak berhasil terhubung ke broker dalam 12 detik")
    else:
        print(f"Dry run; payload ditujukan ke {topic}")

    sent = 0
    try:
        while not stop_event.is_set() and (args.count == 0 or sent < args.count):
            payload = generator.next_payload()
            encoded = json.dumps(payload, separators=(",", ":"))
            if client:
                result = client.publish(topic, encoded, qos=args.qos, retain=args.retain)
                if result.rc != 0:
                    print(f"Publish gagal, kode {result.rc}", file=sys.stderr)
                elif args.qos > 0:
                    result.wait_for_publish(timeout=10)
            print(encoded, flush=True)
            sent += 1
            if args.count == 0 or sent < args.count:
                stop_event.wait(args.interval)
    finally:
        if client:
            client.loop_stop()
            client.disconnect()

    print(f"Selesai. {sent} payload diproses.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
