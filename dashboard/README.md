# DST0 Weather Monitor

Dashboard web statis untuk menampilkan payload MQTT dari firmware DST0.

## Konfigurasi

Salin `.env.example` menjadi `.env`, lalu isi konfigurasi MQTT:

```env
MQTT_URL=wss://broker.avisha.id:8084/mqtt
MQTT_TOPIC=dstxlabs/signal
MQTT_USERNAME=dstxlabs_viewer
MQTT_PASSWORD=password-subscriber
DASHBOARD_TIMEZONE=Asia/Jakarta
```

Hasil konfigurasi dibuat dengan:

```bash
node build.mjs
```

File `.env` dan `dist/env.js` diabaikan Git. Karena dashboard berjalan di browser, kredensial pada hasil build tetap dapat dilihat oleh pengguna website. Gunakan akun MQTT read-only.

## Menjalankan lokal

```bash
cd dashboard/dist
python3 -m http.server 8080
```

Buka `http://localhost:8080`. Dashboard otomatis menghubungi broker dan subscribe ke topic dari `.env`. Pengaturan koneksi tidak ditampilkan di dashboard.

Dashboard menyimpan maksimal 200 pembacaan terakhir di `localStorage` browser. Data terakhir langsung ditampilkan ketika halaman dimuat, dan 20 pembacaan terbaru tersedia pada tabel riwayat. Penyimpanan ini bersifat lokal per browser; untuk riwayat lintas perangkat diperlukan backend/database.

## Deployment Vercel

Atur **Root Directory** project Vercel menjadi `dashboard`. Konfigurasi `dashboard/vercel.json` akan menjalankan `node build.mjs` dan mempublikasikan folder `dist`.

Tambahkan Environment Variables berikut di **Vercel → Project Settings → Environment Variables**:

```text
MQTT_URL
MQTT_TOPIC
MQTT_USERNAME
MQTT_PASSWORD
DASHBOARD_TIMEZONE
```

Setelah variabel ditambahkan, lakukan redeploy. File `.env` lokal tidak perlu dan tidak boleh diunggah.

> Browser tidak dapat terhubung langsung ke MQTT TCP port `1883`. Broker harus menyediakan listener WebSocket, biasanya `ws://host:8083/mqtt` atau `wss://host:8084/mqtt`.

## Payload

```json
{
  "id": "DST0X-001",
  "ver": "1.0.0",
  "timestamp": 1791753498,
  "DST0-sig": 44,
  "DST0-tem": 28.7,
  "DST0-hum": 50.3,
  "DST0-pre": 993.7,
  "DST0-wsp": 0.0,
  "DST0-dir": 0
}
```

Untuk mengubah koneksi, edit `.env` dan jalankan kembali `node build.mjs`.
