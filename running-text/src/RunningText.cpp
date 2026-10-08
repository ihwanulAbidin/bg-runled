#include "RunningText.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fonts/helvB08.h"
#include "fonts/helvB12.h"
#include "fonts/helvB22.h"
#include "fonts/helvB24.h"

namespace rt {
namespace {

constexpr uint8_t Z_FULL = 0, Z_LINES = 3, ZONES = 4;

// Nilai saat menyala. READY hanya tampil sebagai pembuka (beginSplash).
constexpr uint8_t DEFAULT_SIZE_FULL = 3;   // helvB22, sama dengan banner WINS lama
constexpr uint8_t DEFAULT_SIZE_ROW = 2;    // helvB12, pas 16 px (SPLIT, LINES)
constexpr uint16_t DEFAULT_SPEED = 33;     // 1 px tiap 30 ms, sama dengan banner WINS lama
constexpr uint32_t DEFAULT_PAUSE_MS = 2000;
constexpr uint32_t DEFAULT_LABEL_RGB = 0x808080;  // abu-abu, seperti label di layar parkir

constexpr uint32_t DEFAULT_BAUD = 115200;
const uint32_t BAUDS[] = {9600, 19200, 38400, 57600, 115200};

constexpr uint16_t SPEED_MIN = 1, SPEED_MAX = 200;
constexpr uint32_t PAUSE_MAX_MS = 60000;
constexpr size_t LINES_JOIN_MAX = 400;      // LINE1..LINE16 digabung sebelum dicek batas 255
constexpr uint32_t MAX_CATCHUP_STEPS = 8;  // lebih dari ini (loop sempat macet) = lanjut tanpa melompat
constexpr uint8_t RETURN_SPEEDUP = 4;      // LINES: geser balik ke posisi awal 4x SPEED

// Credit
constexpr uint8_t LABEL_SIZE = 1;  // label "Label : Nilai" selalu helvB08
constexpr int16_t ROW_GAP = 2;     // jarak antar baris tampil
constexpr int16_t GROUP_GAP = 4;   // tambahan jarak antar baris sumber (mis. antar pasangan label/nilai)
constexpr int16_t SIDE_GAP = 16;   // ke samping: jarak kelompok yang keluar dengan kelompok berikutnya
constexpr int16_t LABEL_GAP = 4;   // SIDE2: jarak label dengan nilai di satu baris

struct FontInfo {
  const GFXfont *font;
  const char *name;
  int16_t cap, desc;  // tinggi kapital 'M'; tinggi maksimum di bawah baseline
};

FontInfo fonts[SIZE_COUNT] = {
    {&helvB08, "helvB08", 0, 0},
    {&helvB12, "helvB12", 0, 0},
    {&helvB22, "helvB22", 0, 0},
    {&helvB24, "helvB24", 0, 0},
};

const GFXglyph *glyphOf(const GFXfont *f, char c) { return &f->glyph[(uint8_t)c - f->first]; }

void measureFonts() {
  for (FontInfo &fi : fonts) {
    fi.cap = -glyphOf(fi.font, 'M')->yOffset;
    fi.desc = 0;
    for (uint16_t c = fi.font->first; c <= fi.font->last; c++) {
      const GFXglyph *g = &fi.font->glyph[c - fi.font->first];
      if (g->height && g->yOffset + g->height > fi.desc) fi.desc = g->yOffset + g->height;
    }
  }
}

const FontInfo &fontFor(uint8_t size) { return fonts[size - 1]; }

// Huruf kapital + descender harus muat di tinggi zona.
bool fits(uint8_t size, int16_t h) { return fontFor(size).cap + fontFor(size).desc <= h; }

int16_t textWidth(const GFXfont *f, const char *t, size_t len) {
  int16_t w = 0;
  for (size_t i = 0; i < len; i++) w += glyphOf(f, t[i])->xAdvance;
  return w;
}

int16_t alignX(Align a, int16_t w) {
  if (w > PANEL_W || a == Align::Left) return 0;  // teks lebar selalu mulai rata kiri
  return a == Align::Center ? (PANEL_W - w) / 2 : PANEL_W - w;
}

void drawGlyph(uint8_t *buf, uint8_t *cbuf, uint8_t color, int16_t h, const GFXfont *f, const GFXglyph *g, int16_t gx,
               int16_t gy) {
  const uint8_t *bits = f->bitmap + g->bitmapOffset;
  uint16_t bit = 0;
  for (int16_t yy = 0; yy < g->height; yy++) {
    int16_t py = gy + yy;
    if (py < 0 || py >= h) {
      bit += g->width;
      continue;
    }
    uint8_t *row = buf + py * (PANEL_W / 8);
    for (int16_t xx = 0; xx < g->width; xx++, bit++) {
      int16_t px = gx + xx;
      if (px < 0 || px >= PANEL_W) continue;
      if (bits[bit >> 3] & (0x80 >> (bit & 7))) {
        row[px >> 3] |= 0x80 >> (px & 7);
        cbuf[py * PANEL_W + px] = color;
      }
    }
  }
}

// t menunjuk ke dalam z.shown; warna tiap huruf dari z.shownColor di posisi yang sama.
void drawText(Zone &z, uint8_t *buf, const GFXfont *f, const char *t, size_t len, int16_t x, int16_t base) {
  const uint8_t *color = z.shownColor + (t - z.shown);
  for (size_t i = 0; i < len; i++) {
    const GFXglyph *g = glyphOf(f, t[i]);
    int16_t gx = x + g->xOffset;
    if (gx >= PANEL_W) break;  // sisa teks masih di luar kanan panel
    if (gx + g->width > 0) drawGlyph(buf, z.cbuf, color[i], z.h, f, g, gx, base + g->yOffset);
    x += g->xAdvance;
  }
}

bool isMoving(Phase p) {
  return p == Phase::Scroll || p == Phase::Enter || p == Phase::HoldScroll || p == Phase::Exit || p == Phase::Roll ||
         p == Phase::LinesShift || p == Phase::LinesReturn || p == Phase::LinesScroll || p == Phase::SideEnter ||
         p == Phase::SidePan || p == Phase::SideMove;
}

bool isSidePhase(Phase p) {
  return p == Phase::SideEnter || p == Phase::SidePan || p == Phase::SidePause || p == Phase::SideMove;
}

bool isEmpty(const Zone &z) { return z.lineCount == 1 && !z.shown[0]; }

// ONCE dan tampilan 2 baris ini yang terakhir: setelahnya zona kosong.
bool linesLastView(const Zone &z) { return !z.s.loop && z.cur + 2 >= z.lineCount; }

// Credit ke samping: arah SIDE / SIDE2 (juga untuk teks berisi baris baru).
bool isSide(const Zone &z) { return !z.isLines && z.s.dir == Dir::Side; }
bool isSide2(const Zone &z) { return !z.isLines && z.s.dir == Dir::Side2; }

// Credit: arah CREDIT, atau teks yang berisi baris baru (arah apa pun selain LINES dan SIDE/SIDE2).
bool isCredit(const Zone &z) {
  return !z.isLines && z.s.dir != Dir::Side && z.s.dir != Dir::Side2 &&
         (z.s.dir == Dir::Credit || z.lineCount > 1);
}

// Posisi pemisah "Label : Nilai": titik dua yang diikuti spasi, dengan label tidak kosong.
// len bila bukan baris label (mis. "09:23", "http://", ": x").
uint16_t labelSep(const char *t, uint16_t len) {
  for (uint16_t i = 0; i + 1 < len; i++) {
    if (t[i] != ':' || t[i + 1] != ' ') continue;
    for (uint16_t j = 0; j < i; j++)
      if (t[j] != ' ') return i;
    return len;
  }
  return len;
}

void trimRange(const char *s, uint16_t &start, uint16_t &len) {
  while (len && s[start] == ' ') start++, len--;
  while (len && s[start + len - 1] == ' ') len--;
}

// Batas tinta huruf yang benar-benar ada di teks (bukan seluruh font), agar teks kapital semua tidak
// punya jeda kosong selebar ruang descender saat masuk/keluar.
void inkExtent(const GFXfont *f, const char *t, int16_t &asc, int16_t &desc) {
  asc = desc = 0;
  for (; *t; t++) {
    const GFXglyph *g = glyphOf(f, *t);
    if (!g->height) continue;
    if (-g->yOffset > asc) asc = -g->yOffset;
    if (g->yOffset + g->height > desc) desc = g->yOffset + g->height;
  }
}

enum ExpandResult { EXPAND_OK, EXPAND_TOO_MANY_LINES, EXPAND_TOO_LONG };

// "%w%" dst. di p -> kode warna 1.., selain itu 0.
uint8_t colorCodeAt(const char *p) {
  if (p[0] != '%' || !p[1] || p[2] != '%') return 0;
  const char *c = strchr(COLOR_CODES, tolower((unsigned char)p[1]));
  return c ? c - COLOR_CODES + 1 : 0;
}

// Template -> teks tampil: {1}..{9} diisi VAR, pemisah baris jadi '\0'. Pemisah: '\n'; LINES juga '|'.
// Isi VAR tidak pernah dianggap pemisah. Kode warna (di template maupun VAR) dibuang dari teks dan dicatat di
// colors per karakter; tiap baris mulai lagi dengan warna 0. anyColor = ada kode warna.
ExpandResult expandText(const char *tpl, const char (*vars)[MAX_VAR + 1], bool linesMode, char *out, uint8_t *colors,
                        uint16_t *starts, uint8_t &count, bool &anyColor) {
  size_t n = 0;
  uint8_t cur = 0;
  count = 1;
  starts[0] = 0;
  anyColor = false;
  auto put = [&](char c) {
    if (n >= MAX_SHOWN) return false;
    colors[n] = cur;
    out[n++] = c;
    return true;
  };
  // Satu karakter teks biasa (template atau VAR); kode warna dilewati. Mengembalikan panjang yang dipakai.
  auto putChar = [&](const char *p) -> int {
    if (uint8_t k = colorCodeAt(p)) {
      cur = k;
      anyColor = true;
      return 3;
    }
    return put(*p) ? 1 : 0;
  };
  for (const char *p = tpl; *p;) {
    if (*p == '\n' || (linesMode && *p == '|')) {
      if (count >= MAX_LINES) return EXPAND_TOO_MANY_LINES;
      cur = 0;
      if (!put(0)) return EXPAND_TOO_LONG;
      starts[count++] = n;
      p++;
    } else if (p[0] == '{' && p[1] >= '1' && p[1] <= '9' && p[2] == '}') {
      for (const char *v = vars[p[1] - '1']; *v;) {
        int used = putChar(v);
        if (!used) return EXPAND_TOO_LONG;
        v += used;
      }
      p += 3;
    } else {
      int used = putChar(p);
      if (!used) return EXPAND_TOO_LONG;
      p += used;
    }
  }
  out[n] = 0;
  colors[n] = 0;
  return EXPAND_OK;
}

// Isi teks tampil zona + lebar tiap baris.
ExpandResult layoutText(Zone &z, const char (*vars)[MAX_VAR + 1]) {
  ExpandResult r = expandText(z.s.text, vars, z.isLines, z.shown, z.shownColor, z.lineStart, z.lineCount, z.colorCodes);
  if (r != EXPAND_OK) {
    z.shown[0] = 0;
    z.shownColor[0] = 0;
    z.colorCodes = false;
    z.lineCount = 1;
    z.lineStart[0] = 0;
  }
  const GFXfont *f = fontFor(z.s.size).font;
  z.textW = 0;
  for (uint8_t k = 0; k < z.lineCount; k++) {
    const char *t = z.shown + z.lineStart[k];
    z.lineW[k] = textWidth(f, t, strlen(t));
    if (z.lineW[k] > z.textW) z.textW = z.lineW[k];
  }
  return r;
}

// Pecah teks (start, len di s) menjadi potongan yang muat PANEL_W dengan font f: di spasi, kata yang tetap
// lebih lebar dari panel dipotong per huruf. emit(start, len) dipanggil tiap potongan; false = berhenti.
template <typename Emit>
bool wrapWords(const char *s, uint16_t start, uint16_t len, const GFXfont *f, Emit emit) {
  uint16_t i = 0;
  while (i < len) {
    while (i < len && s[start + i] == ' ') i++;
    if (i >= len) break;
    uint16_t rowStart = i, end = i, j = i;
    while (j < len) {  // tambah kata selama masih muat
      uint16_t wordEnd = j;
      while (wordEnd < len && s[start + wordEnd] != ' ') wordEnd++;
      if (textWidth(f, s + start + rowStart, wordEnd - rowStart) > PANEL_W) break;
      end = j = wordEnd;
      while (j < len && s[start + j] == ' ') j++;
    }
    if (end == rowStart) {  // satu kata lebih lebar dari panel: potong per huruf
      int16_t w = 0;
      while (end < len && s[start + end] != ' ') {
        int16_t a = glyphOf(f, s[start + end])->xAdvance;
        if (w + a > PANEL_W && end > rowStart) break;
        w += a;
        end++;
      }
      j = end;
    }
    if (!emit(start + rowStart, (uint16_t)(end - rowStart))) return false;
    i = j;
  }
  return true;
}

// Credit: susun baris tampil. Baris "Label : Nilai" (titik dua diikuti spasi) jadi label kecil + nilai.
// Baris yang tidak muat dikecilkan sampai helvB08; kalau masih tidak muat dipecah di spasi (kata yang
// tetap terlalu panjang dipotong per huruf). false bila lebih dari MAX_ROWS baris tampil.
bool layoutCredit(Zone &z) {
  z.rowCount = 0;
  int16_t top = 0;
  const char *s = z.shown;

  auto addRow = [&](uint16_t start, uint16_t len, uint8_t size, bool label) {
    if (z.rowCount >= MAX_ROWS) return false;
    const FontInfo &f = fontFor(size);
    Row &r = z.rows[z.rowCount++];
    r.start = start;
    r.len = len;
    r.size = size;
    r.label = label;
    r.y = top + f.cap;
    r.x = 0;
    r.w = textWidth(f.font, s + start, len);
    top += f.cap + f.desc + ROW_GAP;
    return true;
  };

  auto addSegment = [&](uint16_t start, uint16_t len, uint8_t size, bool label) {
    while (len && s[start] == ' ') start++, len--;
    while (len && s[start + len - 1] == ' ') len--;
    for (uint8_t sz = size; sz >= 1; sz--)
      if (textWidth(fontFor(sz).font, s + start, len) <= PANEL_W) return addRow(start, len, sz, label);

    // helvB08 pun tidak muat: pecah
    return wrapWords(s, start, len, fontFor(1).font, [&](uint16_t st, uint16_t ln) { return addRow(st, ln, 1, label); });
  };

  for (uint8_t k = 0; k < z.lineCount; k++) {
    uint16_t start = z.lineStart[k];
    uint16_t len = strlen(s + start);
    if (k) top += GROUP_GAP;
    z.groupTop[k] = top;
    uint16_t sep = labelSep(s + start, len);
    if (sep < len) {
      if (!addSegment(start, sep, LABEL_SIZE, true)) return false;
      uint16_t v = sep + 2;
      bool hasValue = false;
      for (uint16_t i = v; i < len; i++) hasValue |= s[start + i] != ' ';
      if (hasValue && !addSegment(start + v, len - v, z.s.size, false)) return false;
    } else if (!addSegment(start, len, z.s.size, false)) {
      return false;
    }
  }
  z.blockH = top > ROW_GAP ? top - ROW_GAP : 0;
  return true;
}

// Credit ke samping: tiap kelompok = 1 baris (teks) atau label kecil + nilai bertumpuk, di tengah zona.
// Baris yang tidak muat dikecilkan sampai helvB08; kalau tetap lebih lebar dari panel, kelompok digeser
// saat berhenti (bukan dipecah). Zona 16 px tidak muat label + nilai: baris ditampilkan utuh.
void layoutSide(Zone &z) {
  const char *s = z.shown;
  const FontInfo &lf = fontFor(LABEL_SIZE);
  const int16_t labelH = lf.cap + lf.desc + ROW_GAP;
  z.rowCount = 0;

  auto shrink = [&](uint16_t start, uint16_t len, uint8_t size, int16_t maxH) {
    uint8_t sz = size;
    while (sz > 1 && (textWidth(fontFor(sz).font, s + start, len) > PANEL_W ||
                      fontFor(sz).cap + fontFor(sz).desc > maxH))
      sz--;
    return sz;
  };
  auto addRow = [&](uint16_t start, uint16_t len, uint8_t size, bool label, int16_t y) {
    Row &r = z.rows[z.rowCount++];
    r.start = start;
    r.len = len;
    r.size = size;
    r.label = label;
    r.y = y;
    r.x = 0;
    r.w = textWidth(fontFor(size).font, s + start, len);
  };

  for (uint8_t k = 0; k < z.lineCount; k++) {
    uint16_t start = z.lineStart[k];
    uint16_t len = strlen(s + start);
    uint16_t sep = labelSep(s + start, len);
    z.groupRow[k] = z.rowCount;
    if (sep < len && z.h >= labelH + fontFor(1).cap + fontFor(1).desc) {
      uint16_t ls = start, ll = sep, vs = start + sep + 2, vl = len - sep - 2;
      trimRange(s, ls, ll);
      trimRange(s, vs, vl);
      uint8_t vsz = vl ? shrink(vs, vl, z.s.size, z.h - labelH) : LABEL_SIZE;
      int16_t total = lf.cap + lf.desc + (vl ? ROW_GAP + fontFor(vsz).cap : 0);  // ekor nilai boleh terpotong
      int16_t top = (z.h - total) / 2;
      addRow(ls, ll, LABEL_SIZE, true, top + lf.cap);
      if (vl) addRow(vs, vl, vsz, false, top + labelH + fontFor(vsz).cap);
    } else {
      trimRange(s, start, len);
      uint8_t sz = shrink(start, len, z.s.size, z.h);
      addRow(start, len, sz, false, (z.h - fontFor(sz).cap) / 2 + fontFor(sz).cap);
    }
    z.groupRows[k] = z.rowCount - z.groupRow[k];
    z.groupW[k] = 0;
    for (uint8_t r = z.groupRow[k]; r < z.rowCount; r++)
      if (z.rows[r].w > z.groupW[k]) z.groupW[k] = z.rows[r].w;
  }
}

int16_t sideHoldX(const Zone &z, uint8_t g) { return alignX(z.s.align, z.groupW[g]); }

// Kelompok yang masuk setelah kelompok sekarang; -1 bila sudah terakhir (ONCE).
int sideNext(const Zone &z) { return z.cur + 1 < z.lineCount ? z.cur + 1 : z.s.loop ? 0 : -1; }

// Kelompok sampai di posisinya: geser dulu bila lebih lebar dari panel, lalu diam PAUSE.
void sideHold(Zone &z, uint32_t nowUs) {
  if (z.groupW[z.cur] > PANEL_W) {
    z.phase = Phase::SidePan;
  } else {
    z.phase = Phase::SidePause;
    z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
  }
}

void drawSideGroup(Zone &z, uint8_t g, int16_t x, int16_t top = 0) {
  for (uint8_t k = z.groupRow[g]; k < z.groupRow[g] + z.groupRows[g]; k++) {
    const Row &r = z.rows[k];
    drawText(z, r.label ? z.bufAlt : z.buf, fontFor(r.size).font, z.shown + r.start, r.len, x + r.x, top + r.y);
  }
}

// SIDE2: tiap baris sumber = satu baris 16 px; dua baris sumber = satu layar. "Label : Nilai" jadi label
// abu (helvB08) lalu nilai di kanannya; nilai dikecilkan (maks helvB12) sampai satu baris muat 64 px.
// Kalau di helvB08 pun tidak muat, baris digeser saat layar berhenti.
void layoutSide2(Zone &z) {
  const char *s = z.shown;
  uint8_t maxSize = z.s.size;
  while (maxSize > 1 && !fits(maxSize, ROW_H)) maxSize--;
  z.rowCount = 0;

  auto addRow = [&](uint16_t start, uint16_t len, uint8_t size, bool label, int16_t x, int16_t y) {
    Row &r = z.rows[z.rowCount++];
    r.start = start;
    r.len = len;
    r.size = size;
    r.label = label;
    r.x = x;
    r.y = y;
    r.w = textWidth(fontFor(size).font, s + start, len);
    return r.w;
  };
  auto baseline = [](uint8_t size) { return (ROW_H - fontFor(size).cap) / 2 + fontFor(size).cap; };

  for (uint8_t k = 0; k < z.lineCount; k++) {
    uint16_t start = z.lineStart[k];
    uint16_t len = strlen(s + start);
    uint16_t sep = labelSep(s + start, len);
    z.groupRow[k] = z.rowCount;
    if (sep < len) {
      uint16_t ls = start, ll = sep, vs = start + sep + 2, vl = len - sep - 2;
      trimRange(s, ls, ll);
      trimRange(s, vs, vl);
      int16_t lw = textWidth(fontFor(LABEL_SIZE).font, s + ls, ll);
      uint8_t vsz = maxSize;
      while (vsz > 1 && lw + LABEL_GAP + textWidth(fontFor(vsz).font, s + vs, vl) > PANEL_W) vsz--;
      int16_t y = baseline(vl ? vsz : LABEL_SIZE);
      addRow(ls, ll, LABEL_SIZE, true, 0, y);
      z.groupW[k] = vl ? lw + LABEL_GAP + addRow(vs, vl, vsz, false, lw + LABEL_GAP, y) : lw;
    } else {
      trimRange(s, start, len);
      uint8_t sz = maxSize;
      while (sz > 1 && textWidth(fontFor(sz).font, s + start, len) > PANEL_W) sz--;
      z.groupW[k] = addRow(start, len, sz, false, 0, baseline(sz));
    }
    z.groupRows[k] = z.rowCount - z.groupRow[k];
  }
}

// SIDE2: baris sumber untuk baris r (0 atas, 1 bawah) layar sc; -1 bila tidak ada (layar terakhir ganjil).
int side2Line(const Zone &z, uint8_t sc, uint8_t r) { return 2 * sc + r < z.lineCount ? 2 * sc + r : -1; }
uint8_t side2Screens(const Zone &z) { return (z.lineCount + 1) / 2; }
int side2Next(const Zone &z) { return z.cur + 1 < side2Screens(z) ? z.cur + 1 : z.s.loop ? 0 : -1; }

// Tepi kanan layar sekarang (x = posisi dasar layar, rowX = geseran baris lebar).
int16_t side2Right(const Zone &z) {
  int16_t right = z.x;
  for (uint8_t r = 0; r < 2; r++) {
    int k = side2Line(z, z.cur, r);
    if (k < 0) continue;
    int16_t e = z.x + alignX(z.s.align, z.groupW[k]) + z.rowX[r] + z.groupW[k];
    if (e > right) right = e;
  }
  return right;
}

// Layar sampai di posisinya: baris lebar digeser dulu, lalu diam PAUSE.
void side2Hold(Zone &z, uint32_t nowUs) {
  z.rowX[0] = z.rowX[1] = 0;
  bool wide = false;
  for (uint8_t r = 0; r < 2; r++) {
    int k = side2Line(z, z.cur, r);
    if (k >= 0 && z.groupW[k] > PANEL_W) wide = true;
  }
  if (wide) {
    z.phase = Phase::SidePan;
  } else {
    z.phase = Phase::SidePause;
    z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
  }
}

// SIDE2 naik (MOVE=UP): baris 16 px gaya SIDE2 (label + nilai berdampingan) bergulir naik seperti credit.
// Baris yang di helvB08 pun tidak muat: label di barisnya sendiri, nilai (atau teks biasa) dipecah per kata ke
// baris berikutnya. Row.x = posisi x di panel (sudah termasuk ALIGN). false bila lebih dari MAX_ROWS potongan.
bool layoutSide2Up(Zone &z) {
  const char *s = z.shown;
  const GFXfont *small = fontFor(1).font;
  uint8_t maxSize = z.s.size;
  while (maxSize > 1 && !fits(maxSize, ROW_H)) maxSize--;
  z.rowCount = 0;
  int16_t row = 0;

  auto add = [&](uint16_t start, uint16_t len, uint8_t size, bool label, int16_t x, int16_t y) {
    if (z.rowCount >= MAX_ROWS) return false;
    Row &r = z.rows[z.rowCount++];
    r.start = start;
    r.len = len;
    r.size = size;
    r.label = label;
    r.x = x;
    r.y = row * ROW_H + y;
    r.w = textWidth(fontFor(size).font, s + start, len);
    return true;
  };
  auto baseline = [](uint8_t size) { return (ROW_H - fontFor(size).cap) / 2 + fontFor(size).cap; };
  auto wrapRows = [&](uint16_t start, uint16_t len, bool label) {
    return wrapWords(s, start, len, small, [&](uint16_t st, uint16_t ln) {
      bool ok = add(st, ln, 1, label, alignX(z.s.align, textWidth(small, s + st, ln)), baseline(1));
      row++;
      return ok;
    });
  };

  for (uint8_t k = 0; k < z.lineCount; k++) {
    uint16_t start = z.lineStart[k];
    uint16_t len = strlen(s + start);
    uint16_t sep = labelSep(s + start, len);
    if (sep < len) {
      uint16_t ls = start, ll = sep, vs = start + sep + 2, vl = len - sep - 2;
      trimRange(s, ls, ll);
      trimRange(s, vs, vl);
      int16_t lw = textWidth(small, s + ls, ll);
      uint8_t vsz = maxSize;
      while (vsz > 1 && lw + LABEL_GAP + textWidth(fontFor(vsz).font, s + vs, vl) > PANEL_W) vsz--;
      int16_t vw = vl ? textWidth(fontFor(vsz).font, s + vs, vl) : 0;
      int16_t total = vl ? lw + LABEL_GAP + vw : lw;
      if (total <= PANEL_W) {  // label + nilai satu baris
        int16_t x = alignX(z.s.align, total), y = baseline(vl ? vsz : 1);
        if (!add(ls, ll, 1, true, x, y)) return false;
        if (vl && !add(vs, vl, vsz, false, x + lw + LABEL_GAP, y)) return false;
        row++;
      } else if (!wrapRows(ls, ll, true) || !wrapRows(vs, vl, false)) {
        return false;
      }
    } else {
      trimRange(s, start, len);
      uint8_t sz = maxSize;
      while (sz > 1 && textWidth(fontFor(sz).font, s + start, len) > PANEL_W) sz--;
      int16_t w = textWidth(fontFor(sz).font, s + start, len);
      if (w <= PANEL_W) {
        if (!add(start, len, sz, false, alignX(z.s.align, w), baseline(sz))) return false;
        row++;
      } else if (!wrapRows(start, len, false)) {
        return false;
      }
    }
  }
  z.blockH = row * ROW_H;
  return true;
}

void drawSide2Screen(Zone &z, uint8_t sc, int16_t x, const int16_t *rowX) {
  for (uint8_t r = 0; r < 2; r++) {
    int k = side2Line(z, sc, r);
    if (k >= 0) drawSideGroup(z, k, x + alignX(z.s.align, z.groupW[k]) + rowX[r], r * ROW_H);
  }
}

// ---------------------------------------------------------------- parsing perintah

char *trim(char *s) {
  while (*s == ' ' || *s == '\t') s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
  return s;
}

void upper(char *s) {
  for (; *s; s++) *s = toupper((unsigned char)*s);
}

bool is(const char *v, const char *a, const char *b = nullptr) { return !strcmp(v, a) || (b && !strcmp(v, b)); }

// Salin teks dari serial. "\n" (backslash-n) atau LF asli = baris baru, "\\" = backslash.
// Karakter di luar ASCII tampil '?'; satu karakter UTF-8 multi-byte = satu '?'.
bool setText(char *dst, size_t cap, const char *src, bool allowNewline) {
  size_t n = 0;
  for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
    unsigned char c = *p;
    if ((c & 0xC0) == 0x80 || c == '\r') continue;
    if (c == '\\' && (p[1] == 'n' || p[1] == '\\')) c = *++p == 'n' ? '\n' : '\\';
    if (c == '\n' ? !allowNewline : (c < 0x20 || c > 0x7E)) c = '?';
    if (n >= cap) return false;
    dst[n++] = c;
  }
  dst[n] = 0;
  return true;
}

bool validBaud(uint32_t b) {
  for (uint32_t v : BAUDS)
    if (v == b) return true;
  return false;
}

uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1));
  }
  return ~c;
}

bool parseColor(const char *v, uint32_t &out) {
  static const struct {
    const char *en, *id;
    uint32_t rgb;
  } names[] = {
      {"RED", "MERAH", 0xFF0000},    {"GREEN", "HIJAU", 0x00FF00},   {"BLUE", "BIRU", 0x0000FF},
      {"WHITE", "PUTIH", 0xFFFFFF},  {"YELLOW", "KUNING", 0xFFFF00}, {"CYAN", "TOSKA", 0x00FFFF},
      {"MAGENTA", "UNGU", 0xFF00FF}, {"ORANGE", "ORANYE", 0xFF5C00}, {"GRAY", "ABU", 0x808080},
      {"GREY", nullptr, 0x808080},
  };
  for (const auto &n : names) {
    if (is(v, n.en, n.id)) {
      out = n.rgb;
      return true;
    }
  }
  if (*v == '#') v++;
  if (strlen(v) != 6) return false;
  for (const char *p = v; *p; p++)
    if (!isxdigit((unsigned char)*p)) return false;
  out = strtoul(v, nullptr, 16);
  return true;
}

bool parseLong(const char *v, long lo, long hi, long &out) {
  char *end;
  out = strtol(v, &end, 10);
  return *v && !*end && out >= lo && out <= hi;
}

bool parseSize(const char *v, uint8_t &out) {
  static const char *const names[SIZE_COUNT] = {"S", "M", "L", "XL"};
  for (uint8_t i = 0; i < SIZE_COUNT; i++) {
    if (!strcmp(v, names[i])) {
      out = i + 1;
      return true;
    }
  }
  long n;
  if (!parseLong(v, 1, SIZE_COUNT, n)) return false;
  out = n;
  return true;
}

bool parsePause(const char *v, uint32_t &outMs) {
  char *end;
  double sec = strtod(v, &end);
  if (!*v || *end || sec < 0 || sec * 1000 > PAUSE_MAX_MS) return false;
  outMs = (uint32_t)(sec * 1000 + 0.5);
  return true;
}

// "VAR1".."VAR9" -> 0..8, selain itu -1
int varIndex(const char *key) {
  return !strncmp(key, "VAR", 3) && key[3] >= '1' && key[3] <= '9' && !key[4] ? key[3] - '1' : -1;
}

// "LINE1".."LINE16" (alias BARIS1..) -> 0..15, selain itu -1
int lineKeyIndex(const char *key) {
  const char *n = !strncmp(key, "LINE", 4) ? key + 4 : !strncmp(key, "BARIS", 5) ? key + 5 : nullptr;
  if (!n || !isdigit((unsigned char)n[0]) || n[0] == '0') return -1;
  long k;
  return parseLong(n, 1, MAX_LINES, k) ? k - 1 : -1;
}

// Kunci yang nilainya dipakai apa adanya (tidak diubah ke huruf besar).
bool isRawKey(const char *key) { return is(key, "TEXT", "LINES") || varIndex(key) >= 0 || lineKeyIndex(key) >= 0; }

// Kunci yang mengambil seluruh sisa baris (boleh berisi ';').
bool isRestKey(const char *key) { return is(key, "TEXT", "LINES"); }

bool keyEquals(const char *k, size_t len, const char *name) {
  while (len && (*k == ' ' || *k == '\t')) k++, len--;
  while (len && (k[len - 1] == ' ' || k[len - 1] == '\t')) len--;
  if (len != strlen(name)) return false;
  for (size_t i = 0; i < len; i++)
    if (toupper((unsigned char)k[i]) != name[i]) return false;
  return true;
}

// Pemisah antar KUNCI=nilai: ';' atau baris baru (LF di dalam perintah).
bool isSep(char c) { return c == ';' || c == '\n'; }

// Perintah tanpa awalan zona yang memuat LINES= / LINEn= ditujukan ke zona LINES (mis. @COLOR=RED;LINES=a|b).
// Kunci k..end (belum dipotong spasi, huruf apa saja) adalah LINEn / BARISn.
bool isLineKey(const char *k, const char *end) {
  char key[8] = {};
  while (k < end && (*k == ' ' || *k == '\t')) k++;
  while (end > k && (end[-1] == ' ' || end[-1] == '\t')) end--;
  if (end - k >= (ptrdiff_t)sizeof key) return false;
  for (size_t i = 0; k < end; i++) key[i] = toupper((unsigned char)*k++);
  return lineKeyIndex(key) >= 0;
}

// Perintah diawali LINEn= (boleh tanpa '@').
bool firstKeyIsLine(const char *p) {
  const char *k = p;
  while (*p && *p != '=' && !isSep(*p)) p++;
  return *p == '=' && isLineKey(k, p);
}

bool hasLinesKey(const char *p) {
  while (*p) {
    const char *k = p;
    while (*p && *p != '=' && !isSep(*p)) p++;
    if (keyEquals(k, p - k, "TEXT")) return false;  // sisa baris = teks
    if (keyEquals(k, p - k, "LINES")) return true;
    if (isLineKey(k, p)) return true;
    if (*p == '=')
      while (*p && !isSep(*p)) p++;
    if (*p) p++;
  }
  return false;
}

const char *dirName(Dir d) {
  return d == Dir::Left     ? "LEFT"
         : d == Dir::Up     ? "UP"
         : d == Dir::Down   ? "DOWN"
         : d == Dir::Credit ? "CREDIT"
         : d == Dir::Side   ? "SIDE"
                            : "SIDE2";
}
const char *alignName(Align a) { return a == Align::Left ? "LEFT" : a == Align::Center ? "CENTER" : "RIGHT"; }
const char *layoutName(Layout l) { return l == Layout::Full ? "FULL" : l == Layout::Split ? "SPLIT" : "LINES"; }

bool zoneInLayout(uint8_t i, Layout l) {
  return l == Layout::Full ? i == 0 : l == Layout::Lines ? i == Z_LINES : (i == 1 || i == 2);
}

struct Pending {
  Settings s[ZONES];
  char vars[VAR_COUNT][MAX_VAR + 1];
  Layout layout;
  uint32_t baud;
  bool restart[ZONES];
  bool layoutGiven, status, help;
  bool display;  // ada perintah tampilan (selain STATUS/HELP/BAUD): menghentikan pembuka READY
  bool linesSet, varsChanged;
  bool textGiven[ZONES], modeGiven[ZONES];  // TEXT/LINES tanpa MODE di perintah yang sama = ONCE
  // LINEn=: isi LINES yang sedang disusun. Mulai dari isi sekarang (salinan di lineOld), LINEn= mengisi baris n
  // dan membuang baris sesudahnya, jadi LINE1, LINE2, ... yang datang sebagai perintah terpisah tetap tersambung.
  bool lineEdit;
  bool lineNew;  // LINE1= ada di perintah ini: blok LINES baru
  char lineOld[MAX_TEXT + 1];
  const char *lineVal[MAX_LINES];  // menunjuk ke lineOld atau ke baris perintah
  uint8_t lineCount;
  char err[96];
};

const char *const HELP_TEXT =
    "Perintah diakhiri CR (\\r atau \\r\\n). LF / \\n di dalam teks = baris baru.\n"
    "  <teks>             teks FULL panel (default: credit, bergulir ke atas)\n"
    "  @TEXT=<teks>       sama (teks boleh diawali @)\n"
    "  @1:TEXT=<teks>     baris atas, otomatis SPLIT (@2: = baris bawah)\n"
    "  @LINES=a|b|c       banyak baris, 2 tampil, bergantian naik (setelan: awalan @L:)\n"
    "  @LINE1=a;LINE2=b   sama, satu kunci per baris (pisah ; atau baris baru)\n"
    "  %w% %r% %g% %b% %y% %c% %m% %o% %a%  kode warna di teks: putih merah hijau biru\n"
    "                     kuning toska ungu oranye abu, sampai kode berikut / akhir baris\n"
    "  Label : Nilai      di credit: label kecil (LABELCOLOR) + nilai (COLOR)\n"
    "  {1}..{9} di teks   diisi @VAR1=..@VAR9= (maks 40 karakter)\n"
    "  @LAYOUT=FULL|SPLIT|LINES\n"
    "  @COLOR=RED|GREEN|BLUE|WHITE|YELLOW|CYAN|MAGENTA|ORANGE|GRAY|RRGGBB\n"
    "  @LABELCOLOR=<warna>  warna label credit (default GRAY)\n"
    "  @SIZE=1..4         1=helvB08 2=helvB12 3=helvB22 4=helvB24 (SPLIT/LINES maks 2)\n"
    "  @DIR=CREDIT|SIDE|SIDE2|LEFT|UP|DOWN  credit naik / ke samping / ke samping 2 baris\n"
    "                     per layar (FULL) / kiri / naik / turun\n"
    "  @MOVE=SIDE|UP      SIDE2: pindah layar ke samping / naik seperti credit\n"
    "  @ALIGN=LEFT|CENTER|RIGHT  posisi teks yang muat panel\n"
    "  @MODE=LOOP|ONCE    ulang terus / sekali lalu kosong. Tiap TEXT/LINES = ONCE,\n"
    "                     kecuali MODE=LOOP di perintah yang sama: @MODE=LOOP;TEXT=...\n"
    "  @SPEED=1..200      pixel per detik\n"
    "  @PAUSE=<detik>     lama berhenti (credit tiap kelompok, UP/DOWN, LINES)\n"
    "  @BAUD=9600|19200|38400|57600|115200  baud UART GP0/GP1 (USB tidak terpengaruh)\n"
    "  @CLEAR  @1:CLEAR  @L:CLEAR  @STATUS  @HELP\n"
    "Credit: baris yang tidak muat dikecilkan sampai helvB08, lalu dipecah di spasi.\n"
    "Batas: teks 255 karakter, setelah VAR 511, LINES/credit maks 16 baris sumber.\n"
    "Saat menyala semua setelan kembali default, kecuali BAUD (disimpan).\n"
    "Gabung dengan ';', TEXT/LINES harus terakhir: @L:COLOR=YELLOW;LINES=Tarif : {1}|Saldo : {2}\n";

bool applyKey(Pending &pd, uint8_t zi, const char *key, char *val) {
  auto fail = [&](const char *fmt, const char *arg) {
    snprintf(pd.err, sizeof pd.err, fmt, arg);
    return false;
  };

  if (is(key, "STATUS")) return pd.status = true;
  if (is(key, "HELP")) return pd.help = true;
  if (!is(key, "BAUD")) pd.display = true;
  if (is(key, "CLEAR")) {
    for (uint8_t i = 0; i < ZONES; i++) {
      if (zi == Z_FULL || i == zi) {  // tanpa awalan = semua zona
        pd.s[i].text[0] = 0;
        pd.restart[i] = true;
      }
    }
    return true;
  }

  if (!val) return fail("%s butuh nilai (tulis KUNCI=nilai)", key);
  if (!isRawKey(key)) upper(val);

  if (is(key, "LAYOUT")) {
    if (is(val, "FULL", "PENUH")) pd.layout = Layout::Full;
    else if (is(val, "SPLIT", "BAGI2")) pd.layout = Layout::Split;
    else if (is(val, "LINES", "BARIS")) pd.layout = Layout::Lines;
    else return fail("LAYOUT tidak dikenal: %s (FULL, SPLIT, LINES)", val);
    pd.layoutGiven = true;
    return true;
  }
  if (is(key, "BAUD")) {
    long n;
    if (!parseLong(val, 0, 1000000, n) || !validBaud(n))
      return fail("BAUD harus 9600, 19200, 38400, 57600, atau 115200: %s", val);
    pd.baud = n;
    return true;
  }
  int vi = varIndex(key);
  if (vi >= 0) {
    char v[MAX_VAR + 1];
    if (!setText(v, MAX_VAR, trim(val), false)) return fail("%s maks 40 karakter", key);
    if (strcmp(v, pd.vars[vi])) {
      strcpy(pd.vars[vi], v);
      pd.varsChanged = true;
    }
    return true;
  }

  Settings &s = pd.s[zi];
  int16_t zoneH = zi == Z_FULL ? PANEL_H : ROW_H;

  if (is(key, "TEXT")) {
    if (zi == Z_LINES) return fail("TEXT tidak berlaku untuk @L:, pakai LINES=%s", "");
    if (!setText(s.text, MAX_TEXT, val, true)) return fail("TEXT terlalu panjang (maks 255 karakter)%s", "");
    pd.layout = zi == Z_FULL ? Layout::Full : Layout::Split;
    pd.restart[zi] = true;
    pd.textGiven[zi] = true;
  } else if (is(key, "LINES")) {
    if (zi != Z_LINES) return fail("LINES tidak bisa dengan awalan @1:/@2:%s", "");
    if (!setText(s.text, MAX_TEXT, val, true)) return fail("LINES terlalu panjang (maks 255 karakter)%s", "");
    pd.layout = Layout::Lines;
    pd.restart[zi] = true;
    pd.linesSet = true;
    pd.textGiven[zi] = true;
  } else if (is(key, "COLOR", "WARNA")) {
    if (!parseColor(val, s.rgb)) return fail("COLOR tidak dikenal: %s (nama warna atau RRGGBB)", val);
  } else if (lineKeyIndex(key) >= 0) {
    if (zi != Z_LINES) return fail("%s tidak bisa dengan awalan @1:/@2:", key);
    uint8_t k = lineKeyIndex(key);
    if (!pd.lineEdit) {
      pd.lineEdit = true;
      strcpy(pd.lineOld, s.text);
      for (char *q = pd.lineOld; *pd.lineOld && pd.lineCount < MAX_LINES;) {
        pd.lineVal[pd.lineCount++] = q;
        q += strcspn(q, "\n|");
        if (!*q) break;
        *q++ = 0;
      }
    }
    while (pd.lineCount < k) pd.lineVal[pd.lineCount++] = "";  // baris yang dilewati: kosong
    pd.lineVal[k] = val;
    pd.lineCount = k + 1;
    if (k == 0) pd.lineNew = true;
  } else if (is(key, "LABELCOLOR", "WARNALABEL")) {
    if (zi == Z_LINES) return fail("LABELCOLOR tidak berlaku untuk LINES%s", "");
    if (!parseColor(val, s.labelRgb)) return fail("LABELCOLOR tidak dikenal: %s (nama warna atau RRGGBB)", val);
  } else if (is(key, "SPEED")) {
    long n;
    if (!parseLong(val, SPEED_MIN, SPEED_MAX, n)) return fail("SPEED harus 1..200 (px/detik): %s", val);
    s.speed = n;
  } else if (is(key, "SIZE")) {
    uint8_t size;
    if (!parseSize(val, size)) return fail("SIZE harus 1..4 atau S/M/L/XL: %s", val);
    if (!fits(size, zoneH)) return fail("SIZE %s terlalu besar untuk baris 16 px (SPLIT/LINES maks 2)", val);
    s.size = size;
    pd.restart[zi] = true;
  } else if (is(key, "DIR", "ARAH")) {
    if (zi == Z_LINES) return fail("DIR tidak berlaku untuk LINES%s", "");
    if (is(val, "LEFT", "KIRI")) s.dir = Dir::Left;
    else if (is(val, "UP", "NAIK")) s.dir = Dir::Up;
    else if (is(val, "DOWN", "TURUN")) s.dir = Dir::Down;
    else if (is(val, "CREDIT", "KREDIT")) s.dir = Dir::Credit;
    else if (is(val, "SIDE", "SAMPING")) s.dir = Dir::Side;
    else if (is(val, "SIDE2", "SAMPING2")) {
      if (zi != Z_FULL) return fail("DIR=%s hanya untuk FULL (2 baris tidak muat di baris SPLIT)", val);
      s.dir = Dir::Side2;
    }
    else return fail("DIR tidak dikenal: %s (CREDIT, SIDE, SIDE2, LEFT, UP, DOWN)", val);
    pd.restart[zi] = true;
  } else if (is(key, "MOVE", "PINDAH")) {
    if (zi == Z_LINES) return fail("MOVE tidak berlaku untuk LINES%s", "");
    if (is(val, "SIDE", "SAMPING")) s.moveUp = false;
    else if (is(val, "UP", "NAIK")) s.moveUp = true;
    else return fail("MOVE tidak dikenal: %s (SIDE atau UP)", val);
    pd.restart[zi] = true;
  } else if (is(key, "ALIGN", "RATA")) {
    if (is(val, "LEFT", "KIRI")) s.align = Align::Left;
    else if (is(val, "CENTER", "TENGAH")) s.align = Align::Center;
    else if (is(val, "RIGHT", "KANAN")) s.align = Align::Right;
    else return fail("ALIGN tidak dikenal: %s (LEFT, CENTER, RIGHT)", val);
    pd.restart[zi] = true;
  } else if (is(key, "MODE")) {
    if (is(val, "LOOP", "ULANG")) s.loop = true;
    else if (is(val, "ONCE", "SEKALI")) s.loop = false;
    else return fail("MODE tidak dikenal: %s (LOOP atau ONCE)", val);
    pd.restart[zi] = true;
    pd.modeGiven[zi] = true;
  } else if (is(key, "PAUSE")) {
    if (!parsePause(val, s.pauseMs)) return fail("PAUSE harus 0..60 detik: %s", val);
  } else {
    return fail("perintah tidak dikenal: %s (ketik @HELP)", key);
  }
  return true;
}

void append(char *out, size_t size, const char *fmt, ...) {
  size_t len = strlen(out);
  if (len >= size) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(out + len, size - len, fmt, ap);
  va_end(ap);
}

// Teks untuk @STATUS: baris baru ditulis \n agar satu zona tetap satu baris balasan.
void appendEscaped(char *out, size_t size, const char *t) {
  for (; *t; t++) {
    if (*t == '\n') append(out, size, "\\n");
    else append(out, size, "%c", *t);
  }
}

}  // namespace

// ---------------------------------------------------------------- RunningText

void RunningText::begin(uint32_t nowUs) {
  measureFonts();
  memset(zones_, 0, sizeof zones_);
  memset(vars_, 0, sizeof vars_);
  const struct {
    int16_t y0, h;
    bool autoStatic, isLines;
    uint32_t rgb;
    uint8_t size;
    Dir dir;
    Align align;
  } init[ZONES] = {
      {0, PANEL_H, false, false, 0xFFFFFF, DEFAULT_SIZE_FULL, Dir::Credit, Align::Center},  // credit default
      {0, ROW_H, true, false, 0xFF0000, DEFAULT_SIZE_ROW, Dir::Left, Align::Center},        // atas: merah
      {ROW_H, ROW_H, true, false, 0x0000FF, DEFAULT_SIZE_ROW, Dir::Left, Align::Center},    // bawah: biru
      {0, PANEL_H, false, true, 0xFFFFFF, DEFAULT_SIZE_ROW, Dir::Left, Align::Left},        // LINES
  };
  for (uint8_t i = 0; i < ZONES; i++) {
    Zone &z = zones_[i];
    z.y0 = init[i].y0;
    z.h = init[i].h;
    z.autoStatic = init[i].autoStatic;
    z.isLines = init[i].isLines;
    z.s.rgb = init[i].rgb;
    z.s.labelRgb = DEFAULT_LABEL_RGB;
    z.s.size = init[i].size;
    z.s.dir = init[i].dir;
    z.s.align = init[i].align;
    z.s.loop = false;  // MODE default ONCE
    z.s.speed = DEFAULT_SPEED;
    z.s.pauseMs = DEFAULT_PAUSE_MS;
    restart(z, nowUs);
  }
  layout_ = Layout::Full;
  baud_ = DEFAULT_BAUD;
  splash_ = false;
  linesModeArmed_ = false;
  dirty_ = true;
}

void RunningText::beginSplash(const char *text, uint8_t size, uint32_t durationMs, uint32_t nowUs) {
  Zone &z = splashZone_;
  memset(&z, 0, sizeof z);
  z.h = PANEL_H;
  z.autoStatic = true;  // muat panel = diam di tengah
  setText(z.s.text, MAX_TEXT, text, false);
  z.s.rgb = 0xFFFFFF;
  z.s.labelRgb = DEFAULT_LABEL_RGB;
  z.s.size = size;
  z.s.align = Align::Center;
  z.s.dir = Dir::Left;
  z.s.loop = true;
  z.s.speed = DEFAULT_SPEED;
  restart(z, nowUs);
  splash_ = true;
  splashUntilUs_ = nowUs + durationMs * 1000;
}

void RunningText::endSplash(uint32_t nowUs) {
  splash_ = false;
  for (Zone &z : zones_) restart(z, nowUs);  // tampilan normal mulai dari awal setelah pembuka
}

void RunningText::exportState(SavedState &out) const {
  memset(&out, 0, sizeof out);  // padding ikut nol agar CRC & perbandingan isi konsisten
  out.magic = STATE_MAGIC;
  out.version = STATE_VERSION;
  out.baud = baud_;
  out.crc = crc32((const uint8_t *)&out, offsetof(SavedState, crc));
}

bool RunningText::importState(const SavedState &in) {
  if (in.magic != STATE_MAGIC || in.version != STATE_VERSION) return false;
  if (in.crc != crc32((const uint8_t *)&in, offsetof(SavedState, crc))) return false;
  if (!validBaud(in.baud)) return false;
  baud_ = in.baud;
  return true;
}

void RunningText::restart(Zone &z, uint32_t nowUs) {
  layoutText(z, vars_);  // isi sudah divalidasi di handleLine
  const FontInfo &f = fontFor(z.s.size);
  int16_t rowH = z.isLines ? ROW_H : z.h;
  z.baseline = (rowH - f.cap) / 2 + f.cap;  // huruf kapital tepat di tengah baris; ekor g/j/p/y boleh terpotong
  z.off = 0;
  z.cur = 0;
  z.rowCount = 0;
  z.lastStepUs = nowUs;

  if (isEmpty(z)) {
    z.phase = Phase::Idle;
  } else if (z.isLines) {
    linesSetRows(z);
    z.phase = Phase::LinesPause;
    z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
  } else if (isSide(z)) {
    // Credit ke samping: kelompok pertama masuk dari tepi kanan.
    layoutSide(z);
    z.x = PANEL_W;
    z.phase = Phase::SideEnter;
  } else if (isSide2(z) && z.s.moveUp) {
    // SIDE2 naik: baris 16 px bergulir naik seperti credit, berhenti tiap layar (2 baris).
    layoutSide2Up(z);
    z.groupStopped = 0;
    z.offStart = z.h;
    z.offEnd = -z.blockH;
    z.off = z.offStart;
    z.phase = Phase::Roll;
  } else if (isSide2(z)) {
    // SIDE2: layar pertama (2 baris) masuk dari tepi kanan.
    layoutSide2(z);
    z.x = PANEL_W;
    z.rowX[0] = z.rowX[1] = 0;
    z.phase = Phase::SideEnter;
  } else if (isCredit(z)) {
    // Credit: seluruh blok baris masuk dari satu sisi dan keluar di sisi lain tanpa berhenti.
    // off = posisi atas blok; DIR=DOWN: blok turun (baris terakhir masuk lebih dulu dari atas).
    layoutCredit(z);
    z.groupStopped = 0;
    bool down = z.s.dir == Dir::Down;
    z.offStart = down ? -z.blockH : z.h;
    z.offEnd = down ? z.h : -z.blockH;
    z.off = z.offStart;
    z.phase = Phase::Roll;
  } else if (z.s.dir == Dir::Left) {
    if (z.autoStatic && z.textW <= PANEL_W) {
      z.phase = Phase::Static;
      z.x = alignX(z.s.align, z.textW);
    } else {
      z.phase = Phase::Scroll;  // seperti banner WINS: mulai tepat di luar tepi kanan
      z.x = PANEL_W;
    }
  } else {
    int16_t asc, desc;
    inkExtent(f.font, z.shown, asc, desc);
    z.x = alignX(z.s.align, z.textW);
    int16_t below = z.h - (z.baseline - asc);  // geser segini = teks tepat di bawah zona
    int16_t above = -(z.baseline + desc);      // geser segini = teks tepat di atas zona
    z.offStart = z.s.dir == Dir::Up ? below : above;
    z.offEnd = z.s.dir == Dir::Up ? above : below;
    z.off = z.offStart;
    z.phase = Phase::Enter;
  }
  dirty_ = true;
}

// VAR berubah: teks tampil dihitung ulang tanpa memulai animasi dari awal; batas geser ikut lebar baru.
void RunningText::refreshShown(Zone &z, uint32_t nowUs) {
  Phase p = z.phase;
  layoutText(z, vars_);
  if (p == Phase::Idle || isEmpty(z)) {
    restart(z, nowUs);
    return;
  }
  switch (p) {
    case Phase::Static:
      if (z.textW <= PANEL_W) z.x = alignX(z.s.align, z.textW);
      else restart(z, nowUs);  // tidak muat lagi: harus berjalan
      break;
    case Phase::Enter:
    case Phase::HoldWait:
    case Phase::Exit:
      if (z.textW <= PANEL_W) z.x = alignX(z.s.align, z.textW);
      break;
    case Phase::Roll:
    case Phase::RollPause: {
      if (isSide2(z)) layoutSide2Up(z);
      else layoutCredit(z);
      bool down = z.offEnd > z.offStart;
      z.offStart = down ? -z.blockH : z.h;
      z.offEnd = down ? z.h : -z.blockH;
      break;
    }
    case Phase::LinesPause:
      linesSetRows(z);
      break;
    case Phase::SideEnter:
    case Phase::SidePan:
    case Phase::SidePause:
    case Phase::SideMove:
      if (isSide2(z)) {
        layoutSide2(z);
      } else {
        layoutSide(z);
        if (p == Phase::SidePause && z.groupW[z.cur] <= PANEL_W) z.x = sideHoldX(z, z.cur);
      }
      break;
    default:
      break;
  }
  dirty_ = true;
}

void RunningText::linesSetRows(Zone &z) {
  for (uint8_t r = 0; r < 2; r++) z.rowX[r] = alignX(z.s.align, z.lineW[(z.cur + r) % z.lineCount]);
}

void RunningText::linesAfterPause(Zone &z, uint32_t nowUs) {
  uint8_t rows = z.lineCount < 2 ? z.lineCount : 2;
  bool wide = false;
  for (uint8_t r = 0; r < rows; r++) wide |= z.lineW[(z.cur + r) % z.lineCount] > PANEL_W;
  z.lastStepUs = nowUs;
  if (wide) z.phase = Phase::LinesShift;
  else linesAfterView(z, nowUs);
}

// Satu tampilan 2 baris selesai: naik satu baris, ulang, atau selesai (ONCE).
void RunningText::linesAfterView(Zone &z, uint32_t nowUs) {
  uint8_t n = z.lineCount;
  z.lastStepUs = nowUs;
  if (linesLastView(z)) {
    z.phase = Phase::Done;
  } else if (n <= 2) {
    z.phase = Phase::LinesPause;
    z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
  } else {
    z.phase = Phase::LinesScroll;
    z.off = 0;
  }
}

void RunningText::step(Zone &z, uint32_t nowUs) {
  switch (z.phase) {
    case Phase::Scroll:
      if (--z.x <= -z.textW) {
        if (z.s.loop) z.x = PANEL_W;
        else z.phase = Phase::Done;
      }
      break;
    case Phase::Enter:
      z.off += z.offStart > 0 ? -1 : 1;
      if (z.off == 0) {
        if (z.textW > PANEL_W) {
          z.phase = Phase::HoldScroll;
        } else {
          z.phase = Phase::HoldWait;
          z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
        }
      }
      break;
    case Phase::HoldScroll:  // teks lebih lebar dari panel: jalan ke kiri sampai ujungnya terlihat
      if (--z.x <= PANEL_W - z.textW) {
        z.phase = Phase::HoldWait;
        z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
      }
      break;
    case Phase::Exit:
      z.off += z.offEnd > 0 ? 1 : -1;
      if (z.off == z.offEnd) {
        if (z.s.loop) restart(z, nowUs);
        else z.phase = Phase::Done;
      }
      break;
    case Phase::Roll: {
      bool down = z.offEnd > z.offStart;
      z.off += down ? 1 : -1;
      if (down ? z.off >= z.offEnd : z.off <= z.offEnd) {  // >=/<=: tinggi blok bisa berubah karena VAR
        if (z.s.loop) restart(z, nowUs);
        else z.phase = Phase::Done;
        break;
      }
      if (isSide2(z)) {  // SIDE2 naik: berhenti tiap layar (2 baris) memenuhi panel
        if (z.s.pauseMs && z.off == -2 * ROW_H * (int16_t)z.groupStopped && -z.off < z.blockH) {
          z.groupStopped++;
          z.phase = Phase::RollPause;
          z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
        }
        break;
      }
      for (uint8_t g = 0; g < z.lineCount && z.s.pauseMs; g++) {  // kelompok menempel di atas panel: berhenti
        if (z.groupStopped & (1u << g) || z.off + z.groupTop[g] != 0) continue;
        z.groupStopped |= 1u << g;
        z.phase = Phase::RollPause;
        z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
        break;
      }
      break;
    }
    case Phase::LinesShift: {  // baris lebar bergeser bersamaan; yang lebih pendek menunggu di ujungnya
      bool more = false;
      for (uint8_t r = 0; r < 2 && r < z.lineCount; r++) {
        int16_t end = PANEL_W - z.lineW[(z.cur + r) % z.lineCount];
        if (z.rowX[r] > end && --z.rowX[r] > end) more = true;
      }
      if (!more) {  // ujung baris lebar terlihat: diam PAUSE dulu sebelum balik / hilang
        z.phase = Phase::LinesShiftPause;
        z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
      }
      break;
    }
    case Phase::LinesReturn: {  // balik cepat ke posisi awal, terlihat bergerak (bukan lompat)
      bool more = false;
      for (uint8_t r = 0; r < 2 && r < z.lineCount; r++) {
        int16_t start = alignX(z.s.align, z.lineW[(z.cur + r) % z.lineCount]);
        if (z.rowX[r] < start && ++z.rowX[r] < start) more = true;
      }
      if (!more) linesAfterView(z, nowUs);
      break;
    }
    case Phase::SideEnter:  // kelompok/layar pertama masuk dari kanan sampai posisinya
      if (isSide2(z)) {
        if (--z.x <= 0) {
          z.x = 0;
          side2Hold(z, nowUs);
        }
        break;
      }
      if (--z.x <= sideHoldX(z, z.cur)) {
        z.x = sideHoldX(z, z.cur);
        sideHold(z, nowUs);
      }
      break;
    case Phase::SidePan:  // lebih lebar dari panel: geser sampai ujung kanannya terlihat
      if (isSide2(z)) {  // baris lebar bergeser bersamaan; yang pendek menunggu di ujungnya
        bool more = false;
        for (uint8_t r = 0; r < 2; r++) {
          int k = side2Line(z, z.cur, r);
          if (k < 0) continue;
          int16_t end = PANEL_W - z.groupW[k];
          if (z.rowX[r] > end && --z.rowX[r] > end) more = true;
        }
        if (!more) {
          z.phase = Phase::SidePause;
          z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
        }
        break;
      }
      if (--z.x + z.groupW[z.cur] <= PANEL_W) {
        z.phase = Phase::SidePause;
        z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
      }
      break;
    case Phase::SideMove: {  // kelompok/layar keluar ke kiri, berikutnya menyusul dari kanan
      --z.x;
      if (isSide2(z)) {
        int next = side2Next(z);
        int16_t right = side2Right(z);
        if (next >= 0 && right + SIDE_GAP <= 0) {
          z.cur = next;
          z.x = 0;
          side2Hold(z, nowUs);
        } else if (next < 0 && right <= 0) {
          z.phase = Phase::Done;
        }
        break;
      }
      int next = sideNext(z);
      if (next >= 0) {
        int16_t nx = z.x + z.groupW[z.cur] + SIDE_GAP;
        if (nx <= sideHoldX(z, next)) {
          z.cur = next;
          z.x = nx;
          sideHold(z, nowUs);
        }
      } else if (z.x + z.groupW[z.cur] <= 0) {
        z.phase = Phase::Done;
      }
      break;
    }
    case Phase::LinesScroll:  // naik satu baris: bawah jadi atas, baris berikutnya masuk dari bawah
      if (++z.off >= ROW_H) {
        z.cur = (z.cur + 1) % z.lineCount;
        z.off = 0;
        linesSetRows(z);
        z.phase = Phase::LinesPause;
        z.holdUntilUs = nowUs + z.s.pauseMs * 1000;
      }
      break;
    default:
      break;
  }
}

bool RunningText::update(uint32_t nowUs) {
  if (splash_ && (int32_t)(nowUs - splashUntilUs_) >= 0) endSplash(nowUs);
  bool changed = dirty_;
  for (uint8_t i = 0; i < zoneCount(); i++) {
    Zone &z = const_cast<Zone &>(zone(i));
    if (z.phase == Phase::HoldWait || z.phase == Phase::LinesPause || z.phase == Phase::LinesShiftPause ||
        z.phase == Phase::RollPause || z.phase == Phase::SidePause) {
      if ((int32_t)(nowUs - z.holdUntilUs) >= 0) {
        if (z.phase == Phase::LinesPause) {
          linesAfterPause(z, nowUs);
        } else if (z.phase == Phase::LinesShiftPause) {
          // Tampilan terakhir ONCE langsung hilang dari ujung geser; selain itu balik dulu ke posisi awal.
          if (linesLastView(z)) z.phase = Phase::Done;
          else z.phase = Phase::LinesReturn;
          z.lastStepUs = nowUs;
        } else {
          z.phase = z.phase == Phase::HoldWait ? Phase::Exit
                    : z.phase == Phase::RollPause ? Phase::Roll
                                                  : Phase::SideMove;
          z.lastStepUs = nowUs;
        }
        changed = true;
      }
      continue;
    }
    if (!isMoving(z.phase)) continue;

    uint32_t speed = (uint32_t)z.s.speed * (z.phase == Phase::LinesReturn ? RETURN_SPEEDUP : 1);
    uint32_t interval = 1000000UL / speed;
    uint32_t elapsed = nowUs - z.lastStepUs;
    if (elapsed < interval) continue;
    uint32_t steps = elapsed / interval;
    if (steps > MAX_CATCHUP_STEPS) {
      steps = 1;
      z.lastStepUs = nowUs;
    } else {
      z.lastStepUs += steps * interval;
    }
    Phase before = z.phase;
    while (steps-- && z.phase == before) step(z, nowUs);  // kecepatan bisa beda per fase: berhenti saat ganti
    changed = true;
  }

  if (changed) {
    for (uint8_t i = 0; i < zoneCount(); i++) render(const_cast<Zone &>(zone(i)));
    dirty_ = false;
  }
  return changed;
}

void RunningText::render(Zone &z) {
  memset(z.buf, 0, sizeof z.buf);
  memset(z.bufAlt, 0, sizeof z.bufAlt);
  if (z.colorCodes) memset(z.cbuf, 0, sizeof z.cbuf);
  if (z.phase == Phase::Idle || z.phase == Phase::Done) return;

  const GFXfont *f = fontFor(z.s.size).font;
  if (z.isLines) {
    uint8_t n = z.lineCount;
    uint8_t rows = z.phase == Phase::LinesScroll ? 3 : (n < 2 ? n : 2);
    for (uint8_t r = 0; r < rows; r++) {
      if (!z.s.loop && z.cur + r >= n) break;  // ONCE: tidak melingkar ke baris pertama
      uint8_t idx = (z.cur + r) % n;
      const char *t = z.shown + z.lineStart[idx];
      int16_t x = r < 2 ? z.rowX[r] : alignX(z.s.align, z.lineW[idx]);
      drawText(z, z.buf, f, t, strlen(t), x, r * ROW_H + z.baseline - z.off);
    }
  } else if (isSidePhase(z.phase) && isSide2(z)) {
    drawSide2Screen(z, z.cur, z.x, z.rowX);
    int next = side2Next(z);
    static const int16_t noShift[2] = {0, 0};
    if (z.phase == Phase::SideMove && next >= 0) drawSide2Screen(z, next, side2Right(z) + SIDE_GAP, noShift);
  } else if (isSidePhase(z.phase)) {
    drawSideGroup(z, z.cur, z.x);
    int next = sideNext(z);
    if (z.phase == Phase::SideMove && next >= 0) drawSideGroup(z, next, z.x + z.groupW[z.cur] + SIDE_GAP);
  } else if (z.phase == Phase::Roll || z.phase == Phase::RollPause) {
    for (uint8_t k = 0; k < z.rowCount; k++) {
      const Row &r = z.rows[k];
      const FontInfo &rf = fontFor(r.size);
      int16_t base = z.off + r.y;
      if (base + rf.desc < 0 || base - rf.cap - 2 >= z.h) continue;  // baris di luar zona
      int16_t x = isSide2(z) ? r.x : alignX(z.s.align, r.w);  // SIDE2 naik: x sudah dihitung di layout
      drawText(z, r.label ? z.bufAlt : z.buf, rf.font, z.shown + r.start, r.len, x, base);
    }
  } else {
    drawText(z, z.buf, f, z.shown, strlen(z.shown), z.x, z.baseline + z.off);
  }
}

void RunningText::handleLine(char *line, char *reply, size_t replySize, uint32_t nowUs) {
  reply[0] = 0;
  while (*line == '\r' || *line == '\n') line++;  // LF sisa "\r\n" dari perintah sebelumnya
  size_t len = strlen(line);
  while (len && (line[len - 1] == '\r' || line[len - 1] == '\n')) line[--len] = 0;
  if (!len) return;

  // Semua perubahan disiapkan dulu; kalau ada yang salah, tidak ada yang diterapkan.
  static Pending pd;
  memset(&pd, 0, sizeof pd);
  for (uint8_t i = 0; i < ZONES; i++) pd.s[i] = zones_[i].s;
  memcpy(pd.vars, vars_, sizeof vars_);
  pd.layout = layout_;
  pd.baud = baud_;

  bool ok = true;
  // Baris tanpa '@' = teks FULL, kecuali diawali LINEn= (mis. dari terminal yang mengirim CR tiap baris).
  bool lineCmd = line[0] != '@' && firstKeyIsLine(line);
  if (line[0] != '@' && !lineCmd) {
    ok = applyKey(pd, Z_FULL, "TEXT", line);
  } else {
    char *p = lineCmd ? line : line + 1;
    uint8_t zi = Z_FULL;
    if ((p[0] == '1' || p[0] == '2') && p[1] == ':') {
      zi = p[0] - '0';
      p += 2;
    } else if ((p[0] == 'L' || p[0] == 'l') && p[1] == ':') {
      zi = Z_LINES;
      p += 2;
    } else if (hasLinesKey(p)) {
      zi = Z_LINES;
    }
    while (*p && ok) {
      char *key = p;
      while (*p && *p != '=' && !isSep(*p)) p++;
      char delim = *p;
      if (delim) *p++ = 0;
      key = trim(key);
      upper(key);

      char *val = nullptr;
      if (delim == '=') {
        val = p;
        if (isRestKey(key)) {
          p += strlen(p);  // TEXT/LINES mengambil sisa baris, termasuk ';'
        } else {
          while (*p && !isSep(*p)) p++;
          if (*p) *p++ = 0;
          val = trim(val);
        }
      }
      if (*key) ok = applyKey(pd, zi, key, val);
    }
  }

  // LINEn=: susun isi LINES baru. LINE1 = blok baru (MODE: lihat di bawah); LINE2.. menyambung tanpa mengubah MODE.
  if (ok && pd.lineEdit) {
    if (pd.linesSet) {
      snprintf(pd.err, sizeof pd.err, "LINES= dan LINEn= tidak bisa dalam satu perintah");
      ok = false;
    } else {
      static char joined[LINES_JOIN_MAX];
      joined[0] = 0;
      for (uint8_t k = 0; k < pd.lineCount; k++) append(joined, sizeof joined, k ? "\n%s" : "%s", pd.lineVal[k]);
      Settings &s = pd.s[Z_LINES];
      if (strlen(joined) + 1 >= sizeof joined || !setText(s.text, MAX_TEXT, joined, true)) {
        snprintf(pd.err, sizeof pd.err, "LINES terlalu panjang (maks 255 karakter)");
        ok = false;
      } else {
        pd.layout = Layout::Lines;
        pd.restart[Z_LINES] = true;
        pd.linesSet = true;
      }
    }
  }

  // Teks baru tampil sekali, kecuali MODE=LOOP ada di perintah yang sama.
  for (uint8_t i = 0; i < ZONES; i++)
    if (pd.textGiven[i] && !pd.modeGiven[i]) pd.s[i].loop = false;
  // LINE1 juga, tapi MODE boleh datang di perintah setelan @L: sebelumnya (terminal yang mengirim CR tiap baris:
  // "@L:MODE=LOOP;SIZE=1;" lalu "LINE1=..." = satu blok).
  if (pd.lineNew && !pd.modeGiven[Z_LINES] && !linesModeArmed_) pd.s[Z_LINES].loop = false;

  // Teks tampil (setelah VAR diisi) harus muat di semua zona, dan credit maks MAX_ROWS baris tampil.
  if (ok) {
    static Zone tmp;
    for (uint8_t i = 0; i < ZONES && ok; i++) {
      tmp.s = pd.s[i];
      tmp.h = zones_[i].h;
      tmp.isLines = zones_[i].isLines;
      ExpandResult r = layoutText(tmp, pd.vars);
      if (r == EXPAND_TOO_MANY_LINES) snprintf(pd.err, sizeof pd.err, "maks %u baris", MAX_LINES);
      if (r == EXPAND_TOO_LONG)
        snprintf(pd.err, sizeof pd.err, "teks terlalu panjang setelah VAR diisi (maks %u)", (unsigned)MAX_SHOWN);
      ok = r == EXPAND_OK;
      if (ok && isSide2(tmp) && tmp.s.moveUp && !layoutSide2Up(tmp)) {
        snprintf(pd.err, sizeof pd.err, "SIDE2 naik terlalu panjang (maks %u potongan baris)", MAX_ROWS);
        ok = false;
      }
      if (ok && isCredit(tmp) && !layoutCredit(tmp)) {
        snprintf(pd.err, sizeof pd.err, "credit terlalu panjang (maks %u baris tampil)", MAX_ROWS);
        ok = false;
      }
    }
  }

  if (!ok) {
    snprintf(reply, replySize, "ERR %s", pd.err);
    return;
  }

  bool layoutChanged = pd.layout != layout_;
  if (pd.baud != baud_) revision_++;
  for (uint8_t i = 0; i < ZONES; i++) zones_[i].s = pd.s[i];
  memcpy(vars_, pd.vars, sizeof vars_);
  layout_ = pd.layout;
  baud_ = pd.baud;
  if (pd.lineNew || pd.textGiven[Z_LINES]) linesModeArmed_ = false;  // MODE sudah dipakai blok ini
  else if (pd.modeGiven[Z_LINES]) linesModeArmed_ = true;            // MODE tanpa teks: untuk LINE1 berikutnya
  if (splash_ && pd.display) endSplash(nowUs);
  for (uint8_t i = 0; i < ZONES; i++) {
    bool inLayout = zoneInLayout(i, layout_);
    if (pd.restart[i] || (inLayout && (layoutChanged || pd.layoutGiven))) restart(zones_[i], nowUs);
    else if (pd.varsChanged) refreshShown(zones_[i], nowUs);
  }
  dirty_ = true;

  if (pd.help) append(reply, replySize, "%s", HELP_TEXT);
  if (pd.status) {
    append(reply, replySize, "LAYOUT=%s BAUD=%lu SAVES=%lu LAST_SAVE_MS=%lu REFRESH_HZ=%lu\n", layoutName(layout_),
           (unsigned long)baud_, (unsigned long)saveCount_, (unsigned long)lastSaveMs_, (unsigned long)refreshHz_);
    static const char *const label[ZONES] = {"FULL", "1", "2", "L"};
    for (uint8_t i = 0; i < ZONES; i++) {
      const Settings &s = zones_[i].s;
      append(reply, replySize, "%s: %s=\"", label[i], i == Z_LINES ? "LINES" : "TEXT");
      appendEscaped(reply, replySize, s.text);
      append(reply, replySize, "\" COLOR=%06lX", (unsigned long)s.rgb);
      if (i != Z_LINES) append(reply, replySize, " LABELCOLOR=%06lX", (unsigned long)s.labelRgb);
      append(reply, replySize, " SIZE=%u(%s)", s.size, fontFor(s.size).name);
      if (i != Z_LINES) append(reply, replySize, " DIR=%s MOVE=%s", dirName(s.dir), s.moveUp ? "UP" : "SIDE");
      append(reply, replySize, " ALIGN=%s MODE=%s SPEED=%u PAUSE=%lu.%03lu\n", alignName(s.align),
             s.loop ? "LOOP" : "ONCE", s.speed, (unsigned long)(s.pauseMs / 1000), (unsigned long)(s.pauseMs % 1000));
    }
    append(reply, replySize, "VAR:");
    bool any = false;
    for (uint8_t v = 0; v < VAR_COUNT; v++) {
      if (!vars_[v][0]) continue;
      append(reply, replySize, " %u=\"%s\"", v + 1, vars_[v]);
      any = true;
    }
    append(reply, replySize, any ? "\n" : " -\n");
  }

  if (pd.linesSet || (pd.varsChanged && layout_ == Layout::Lines)) {
    // Balasan untuk master: jumlah baris dan baris yang lebih lebar dari panel (akan digeser).
    const Zone &z = zones_[Z_LINES];
    append(reply, replySize, "OK LINES=%u WIDE=", isEmpty(z) ? 0 : z.lineCount);
    bool any = false;
    for (uint8_t k = 0; k < z.lineCount; k++) {
      if (z.lineW[k] <= PANEL_W) continue;
      append(reply, replySize, any ? ",%u" : "%u", k + 1);
      any = true;
    }
    if (!any) append(reply, replySize, "-");
  } else {
    append(reply, replySize, "OK");
  }
}

}  // namespace rt
