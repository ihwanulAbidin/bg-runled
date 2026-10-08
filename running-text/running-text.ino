// Running text untuk Raspberry Pi Pico (RP2040) + panel HUB75 P5 64x32.
// Teks dan tampilan diatur lewat serial (USB, atau UART0 GP0/GP1 default 115200); tiap perintah diakhiri CR.
// Ketik @HELP untuk daftar perintah.
// Saat menyala: READY sebentar, lalu kosong dengan setelan default. Hanya baud UART yang disimpan ke flash (LittleFS).
// Menggantikan code.py CircuitPython lama (padelv1.3.py); pin panel tidak berubah.
// Build: FQBN rp2040:rp2040:rpipico:flash=2097152_1048576 (FS 1 MB untuk penyimpanan baud).
#include <Adafruit_Protomatter.h>
#include <LittleFS.h>
#include <hardware/irq.h>

#include "src/RunningText.h"

constexpr uint8_t UART_TX_PIN = 0;  // GP0
constexpr uint8_t UART_RX_PIN = 1;  // GP1
constexpr size_t LINE_MAX = 320;
constexpr size_t REPLY_MAX = 2600;  // @STATUS: 4 zona x teks 255 + VAR
constexpr uint32_t SAVE_DELAY_MS = 5000;  // simpan setelah 5 detik tanpa perubahan baru
const char *const STATE_FILE = "/state.bin";
const char *const STATE_TMP = "/state.tmp";

// Pin HUB75 sama dengan code.py lama.
uint8_t rgbPins[] = {2, 3, 6, 7, 8, 9};  // R1 G1 B1 R2 G2 B2
uint8_t addrPins[] = {10, 16, 18, 20};   // A B C D
constexpr uint8_t CLOCK_PIN = 11, LATCH_PIN = 12, OE_PIN = 13;

Adafruit_Protomatter matrix(rt::PANEL_W, 6, 1, rgbPins, 4, addrPins, CLOCK_PIN, LATCH_PIN, OE_PIN, true);
rt::RunningText runningText;

struct LineReader {
  Stream &port;
  char buf[LINE_MAX + 1];
  size_t len;
  bool overflow;
};
LineReader usbIn{Serial, {}, 0, false};
LineReader uartIn{Serial1, {}, 0, false};
char reply[REPLY_MAX];
uint32_t uartBaud;

// Penyimpanan baud UART
bool fsReady;
rt::SavedState stored;  // isi file di flash saat ini (untuk tulis hanya bila berubah)
uint32_t seenRevision, savedRevision, lastChangeMs, saveCount, lastSaveMs;

volatile int matrixStatus = -1;  // hasil matrix.begin() dari core 1; -1 = belum selesai
uint32_t refreshHz, lastRefreshMs;

void beginUart(uint32_t baud) {
  if (uartBaud) {
    Serial1.flush();  // balasan OK terkirim dulu dengan baud lama
    Serial1.end();
  }
  Serial1.begin(baud);
  uartBaud = baud;
}

// Perintah diakhiri CR ("\r" atau "\r\n"). LF di tengah perintah = baris baru di teks;
// LF sisa "\r\n" (di awal perintah berikutnya) diabaikan.
void pollSerial(LineReader &in) {
  while (in.port.available()) {
    char c = in.port.read();
    if (c != '\r') {
      if (c == '\n' && !in.len) continue;
      if (in.len < LINE_MAX) in.buf[in.len++] = c;
      else in.overflow = true;
      continue;
    }
    if (in.overflow) {
      in.port.println("ERR baris terlalu panjang (maks 320 karakter)");
    } else if (in.len) {
      in.buf[in.len] = 0;
      runningText.handleLine(in.buf, reply, sizeof reply, micros());
      if (reply[0]) in.port.println(reply);
      if (runningText.baud() != uartBaud) beginUart(runningText.baud());
    }
    in.len = 0;
    in.overflow = false;
  }
}

void loadState() {
  fsReady = LittleFS.begin();  // area FS kosong/rusak (mis. sisa CIRCUITPY) diformat otomatis
  if (!fsReady) return;
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) return;
  bool ok = f.size() == sizeof stored && f.read((uint8_t *)&stored, sizeof stored) == sizeof stored &&
            runningText.importState(stored);
  f.close();
  if (!ok) memset(&stored, 0, sizeof stored);  // file tidak valid: setelan default, ditimpa saat ada perubahan
}

void maybeSave() {
  uint32_t now = millis();
  if (runningText.revision() != seenRevision) {
    seenRevision = runningText.revision();
    lastChangeMs = now;
  }
  if (!fsReady || savedRevision == seenRevision || now - lastChangeMs < SAVE_DELAY_MS) return;
  savedRevision = seenRevision;

  rt::SavedState st;
  runningText.exportState(st);
  if (!memcmp(&st, &stored, sizeof st)) return;  // isi sama dengan di flash: tidak perlu tulis

  // Tulis ke file sementara lalu rename (atomik): listrik mati di tengah jalan tidak merusak setelan lama.
  uint32_t t0 = millis();
  File f = LittleFS.open(STATE_TMP, "w");
  bool ok = f && f.write((const uint8_t *)&st, sizeof st) == sizeof st;
  if (f) f.close();
  ok = ok && LittleFS.rename(STATE_TMP, STATE_FILE);
  if (ok) {
    stored = st;
    saveCount++;
    lastSaveMs = millis() - t0;
    runningText.setDiagInfo(saveCount, lastSaveMs, refreshHz);
  } else {
    savedRevision = seenRevision - 1;  // coba lagi pada putaran berikutnya
    lastChangeMs = now;
  }
}

void draw() {
  matrix.fillScreen(0);
  for (uint8_t i = 0; i < runningText.zoneCount(); i++) {
    const rt::Zone &z = runningText.zone(i);
    uint32_t c = z.s.rgb, l = z.s.labelRgb;
    if (z.colorCodes) {  // kode warna %x% di teks: warna per pixel (cbuf), 0 = warna lapisannya
      for (int16_t y = 0; y < z.h; y++) {
        for (int16_t x = 0; x < rt::PANEL_W; x++) {
          int b = y * (rt::PANEL_W / 8) + x / 8;
          uint8_t m = 0x80 >> (x & 7);
          bool alt = z.bufAlt[b] & m;
          if (!alt && !(z.buf[b] & m)) continue;
          uint8_t k = z.cbuf[y * rt::PANEL_W + x];
          uint32_t rgb = k ? rt::COLOR_CODE_RGB[k] : alt ? l : c;
          matrix.drawPixel(x, z.y0 + y, matrix.color565(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF));
        }
      }
    } else {
      matrix.drawBitmap(0, z.y0, z.buf, rt::PANEL_W, z.h, matrix.color565(c >> 16, (c >> 8) & 0xFF, c & 0xFF));
      matrix.drawBitmap(0, z.y0, z.bufAlt, rt::PANEL_W, z.h, matrix.color565(l >> 16, (l >> 8) & 0xFF, l & 0xFF));
    }
  }
  matrix.show();
}

// Refresh panel per detik (dari penghitung frame Protomatter), untuk @STATUS.
void measureRefresh() {
  uint32_t now = millis();
  if (now - lastRefreshMs < 1000) return;
  uint32_t frames = matrix.getFrameCount();  // sekaligus me-reset penghitung
  refreshHz = frames * 1000 / (now - lastRefreshMs);
  lastRefreshMs = now;
  runningText.setDiagInfo(saveCount, lastSaveMs, refreshHz);
}

// Core 1 khusus refresh panel. Interrupt Protomatter aktif di core yang memanggil begin(), jadi di sini
// ia tidak tertunda oleh interrupt USB/serial di core 0 (penyebab panel berkedip pada versi pertama).
void setup1() {
  ProtomatterStatus status = matrix.begin();
  irq_set_priority(PWM_IRQ_WRAP, PICO_HIGHEST_IRQ_PRIORITY);
  matrixStatus = status;
}

void loop1() {
  __wfi();  // tidur sampai interrupt panel berikutnya
}

void setup() {
  Serial.begin(115200);  // USB: baud tidak berpengaruh

  while (matrixStatus < 0) delay(1);
  if (matrixStatus != PROTOMATTER_OK) {
    for (;;) {
      Serial.print("ERR panel gagal mulai, status Protomatter ");
      Serial.println(matrixStatus);
      delay(2000);
    }
  }

  runningText.begin(micros());
  loadState();
  runningText.beginSplash("READY", 2, 2000, micros());  // helvB12: READY 57 px, muat diam di 64 px
  seenRevision = savedRevision = runningText.revision();

  Serial1.setTX(UART_TX_PIN);
  Serial1.setRX(UART_RX_PIN);
  Serial1.setFIFOSize(256);  // tampung ±22 ms data 115200 selama satu frame digambar
  beginUart(runningText.baud());

  runningText.update(micros());
  draw();
}

void loop() {
  pollSerial(usbIn);
  pollSerial(uartIn);
  if (runningText.update(micros())) draw();
  maybeSave();
  measureRefresh();
}
