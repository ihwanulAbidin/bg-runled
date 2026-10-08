// Logika running text: perintah serial, animasi, dan render ke bitmap 1-bit per zona.
// Tidak bergantung pada hardware, sehingga bisa diuji di PC; running-text.ino yang
// meneruskan bitmap ke panel HUB75.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rt {

constexpr int16_t PANEL_W = 64;
constexpr int16_t PANEL_H = 32;
constexpr int16_t ROW_H = PANEL_H / 2;  // satu baris SPLIT / LINES
constexpr size_t MAX_TEXT = 255;        // template, sebelum VAR diisi
constexpr size_t MAX_SHOWN = 511;       // teks tampil, setelah VAR diisi
constexpr uint8_t MAX_LINES = 16;       // baris sumber LINES / credit
constexpr uint8_t MAX_ROWS = 64;        // baris tampil credit (label/nilai, setelah dipecah)
constexpr uint8_t VAR_COUNT = 9;        // {1}..{9}
constexpr size_t MAX_VAR = 40;
constexpr uint8_t SIZE_COUNT = 4;

// Kode warna di teks, mis. "%w%Tarif: %g%Rp.5000" (huruf besar/kecil sama). Berlaku sampai kode berikutnya
// atau akhir baris. Kode k (1..) = huruf COLOR_CODES[k - 1], warna COLOR_CODE_RGB[k].
constexpr char COLOR_CODES[] = "wrgbycmoa";  // putih merah hijau biru kuning toska ungu oranye abu
constexpr uint32_t COLOR_CODE_RGB[] = {0,        0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF,
                                       0xFFFF00, 0x00FFFF, 0xFF00FF, 0xFF5C00, 0x808080};

enum class Layout : uint8_t { Full, Split, Lines };
enum class Dir : uint8_t { Left, Up, Down, Credit, Side, Side2 };
enum class Align : uint8_t { Left, Center, Right };
enum class Phase : uint8_t {
  Idle,
  Static, Scroll,                                    // satu baris, arah kiri
  Enter, HoldScroll, HoldWait, Exit,                 // satu baris, naik/turun
  Roll, RollPause,                                   // credit: bergulir, berhenti PAUSE tiap kelompok
  SideEnter, SidePan, SidePause, SideMove,           // ke samping (SIDE/SIDE2): masuk-diam-keluar per layar
  LinesPause, LinesShift, LinesShiftPause,           // layout LINES: diam, geser, diam di ujung geser,
  LinesReturn, LinesScroll,                          //   balik, naik
  Done
};

struct Settings {
  char text[MAX_TEXT + 1];  // template: boleh berisi {1}..{9}; '\n' = baris baru (LINES: juga '|')
  uint32_t rgb;             // 0xRRGGBB: teks / nilai
  uint32_t labelRgb;        // 0xRRGGBB: label "Label : Nilai" di credit
  uint8_t size;             // 1..SIZE_COUNT
  Dir dir;
  Align align;              // posisi teks yang muat panel
  bool loop;                // true = ulang terus, false = sekali lalu kosong (default tiap TEXT/LINES)
  bool moveUp;              // SIDE2: pindah layar naik seperti credit (MOVE=UP), bukan ke samping
  uint16_t speed;           // pixel per detik
  uint32_t pauseMs;         // lama berhenti (credit per kelompok, naik/turun, LINES)
};

// Isi yang disimpan ke flash: hanya baud UART. Setelan tampilan selalu kembali default saat menyala.
// Naikkan STATE_VERSION bila struktur ini berubah.
constexpr uint32_t STATE_MAGIC = 0x54585452;  // "RTXT"
constexpr uint16_t STATE_VERSION = 2;
struct SavedState {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t baud;
  uint32_t crc;  // CRC32 seluruh isi sebelum field ini
};

// Satu baris tampil credit: potongan dari Zone::shown dengan ukuran font sendiri.
struct Row {
  uint16_t start, len;  // posisi di Zone::shown
  int16_t y;            // baseline: credit = dari atas blok; ke samping = dari atas zona
  int16_t w;            // lebar pixel
  int16_t x;            // SIDE2: posisi dalam barisnya (nilai di kanan label); SIDE2 naik: posisi x di panel
  uint8_t size;
  bool label;           // digambar dengan warna label
};

struct Zone {
  Settings s;
  int16_t y0, h;    // baris panel yang dipakai zona ini
  bool autoStatic;  // SPLIT: teks satu baris yang muat panel diam
  bool isLines;     // zona LINES: 2 baris @16 px, banyak baris bergantian

  // Teks tampil = template setelah VAR diisi dan kode warna dibuang; antar baris dipisah '\0'.
  char shown[MAX_SHOWN + 1];
  uint8_t shownColor[MAX_SHOWN + 1];  // kode warna tiap karakter shown (0 = COLOR / LABELCOLOR)
  uint8_t lineCount;
  uint16_t lineStart[MAX_LINES];
  int16_t lineW[MAX_LINES];

  // Credit: baris tampil (label/nilai, dikecilkan/dipecah agar muat) dan tinggi blok.
  Row rows[MAX_ROWS];
  uint8_t rowCount;
  int16_t blockH;
  int16_t groupTop[MAX_LINES];  // credit: atas tiap kelompok (satu baris sumber) dalam blok
  uint16_t groupStopped;        // credit: kelompok yang sudah berhenti (bit); SIDE2 naik: jumlah layar berhenti
  uint8_t groupRow[MAX_LINES];  // ke samping: baris tampil pertama tiap kelompok
  uint8_t groupRows[MAX_LINES]; // ke samping: jumlah baris tampil (1 = teks, 2 = label + nilai)
  int16_t groupW[MAX_LINES];    // ke samping: lebar kelompok

  Phase phase;
  int16_t textW;  // lebar baris terlebar
  int16_t baseline, x, off, offStart, offEnd;
  int16_t rowX[2];  // LINES: posisi x baris atas & bawah; SIDE2: geseran baris lebar
  uint8_t cur;      // LINES: baris yang tampil di atas; SIDE: kelompok; SIDE2: layar (2 baris sumber)
  uint32_t lastStepUs, holdUntilUs;
  uint8_t buf[PANEL_W / 8 * PANEL_H];     // 1 bit per pixel, 8 byte per baris: warna s.rgb
  uint8_t bufAlt[PANEL_W / 8 * PANEL_H];  // sama, warna s.labelRgb (label credit)
  bool colorCodes;                        // teks memuat kode warna: pakai cbuf
  uint8_t cbuf[PANEL_W * PANEL_H];        // kode warna tiap pixel buf/bufAlt (0 = warna lapisan)
};

class RunningText {
 public:
  void begin(uint32_t nowUs);

  // Maju animasi sesuai waktu; true bila tampilan berubah dan perlu digambar ulang.
  bool update(uint32_t nowUs);

  // Proses satu perintah (tanpa CR penutup). Balasan ditulis ke reply ("OK...", "ERR ...", status).
  void handleLine(char *line, char *reply, size_t replySize, uint32_t nowUs);

  // Teks pembuka saat menyala: diam di tengah selama durationMs, lalu tampilan normal dimulai dari awal.
  // Perintah serial yang mengubah tampilan menghentikannya lebih awal.
  void beginSplash(const char *text, uint8_t size, uint32_t durationMs, uint32_t nowUs);

  uint8_t zoneCount() const { return splash_ || layout_ != Layout::Split ? 1 : 2; }
  const Zone &zone(uint8_t i) const {
    if (splash_) return splashZone_;
    if (layout_ == Layout::Full) return zones_[0];
    if (layout_ == Layout::Lines) return zones_[3];
    return zones_[1 + i];
  }

  uint32_t baud() const { return baud_; }  // baud UART0 (GP0/GP1)
  // Naik setiap kali isi yang disimpan (baud) benar-benar berubah.
  uint32_t revision() const { return revision_; }

  void exportState(SavedState &out) const;
  // false bila isi tidak valid (magic/versi/CRC/baud); baud sekarang tidak diubah.
  bool importState(const SavedState &in);
  // Info diagnosa untuk @STATUS (diisi running-text.ino).
  void setDiagInfo(uint32_t saveCount, uint32_t lastSaveMs, uint32_t refreshHz) {
    saveCount_ = saveCount;
    lastSaveMs_ = lastSaveMs;
    refreshHz_ = refreshHz;
  }

 private:
  void restart(Zone &z, uint32_t nowUs);
  void refreshShown(Zone &z, uint32_t nowUs);
  void step(Zone &z, uint32_t nowUs);
  void linesSetRows(Zone &z);
  void linesAfterPause(Zone &z, uint32_t nowUs);
  void linesAfterView(Zone &z, uint32_t nowUs);
  void render(Zone &z);
  void endSplash(uint32_t nowUs);

  Zone zones_[4];  // 0 = FULL, 1 = baris atas, 2 = baris bawah, 3 = LINES
  Zone splashZone_;
  char vars_[VAR_COUNT][MAX_VAR + 1];
  bool splash_ = false;
  bool linesModeArmed_ = false;  // MODE= dikirim untuk LINES tanpa teks: berlaku untuk LINE1= berikutnya
  uint32_t splashUntilUs_ = 0;
  Layout layout_ = Layout::Full;
  bool dirty_ = true;
  uint32_t baud_ = 0;
  uint32_t revision_ = 0;
  uint32_t saveCount_ = 0, lastSaveMs_ = 0, refreshHz_ = 0;
};

}  // namespace rt
