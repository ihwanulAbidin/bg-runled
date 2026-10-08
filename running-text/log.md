# Log — Running Text Pico (P5 64×32)

Catatan keputusan dan pekerjaan untuk folder ini.

---

## 2026-10-02 — Pindah dari CircuitPython (padel scoring) ke firmware C++ running text

**Status:** in progress

### Asal-usul
- File lama dari `W:\AWG.Corp\Project\counter` disalin ke `../cpy/` (27 file, checksum cocok).
- Program yang jalan di Pico (`D:\code.py`) = `../cpy/padelv1.3.py` (SHA-256 identik:
  `F1CC94E2…F827`). Papan: Raspberry Pi Pico (RP2040), CircuitPython 10.0.3.
- Seluruh isi drive CIRCUITPY dicadangkan ke `../circuitpy-backup/` (58 file, checksum cocok).
  Untuk kembali ke program lama: flash UF2 CircuitPython lalu salin isi folder itu ke CIRCUITPY.

### Keputusan (dikonfirmasi user)
| Topik | Keputusan |
|---|---|
| Fungsi | Bukan lagi penghitung skor padel → running text yang diatur lewat serial |
| Firmware | Native C++: Arduino core arduino-pico + Adafruit Protomatter (HUB75) + font format GFX |
| Folder | `running-text/` (folder kosong `runnning-text` di-rename) |
| Serial | 115200 baud (119200 = typo). Masuk lewat USB dan UART0 (GP0 = TX, GP1 = RX) |
| Layout | FULL = satu teks satu panel (default helvB22, sama dengan "RED/BLUE WINS" lama); SPLIT = 2 baris, teks/warna/gerak sendiri-sendiri |
| Arah gerak | Kiri (gaya WINS lama), naik (bawah→atas), turun (atas→bawah) |
| Gerak vertikal | Masuk → berhenti N detik → keluar. Teks lebih lebar dari panel: saat berhenti berjalan ke kiri dulu sampai ujung, baru keluar |
| SPLIT | Teks pendek diam di tengah, teks panjang otomatis berjalan |
| Diatur lewat serial | Teks, warna, kecepatan, ukuran font, arah, ulang/sekali, lama berhenti. Kecerahan tidak |
| Font | helvB08/12/22 dari CIRCUITPY + helvB24 (X11 100dpi, keluarga sama) sebagai opsi XL |

### Tambahan dari sesi dokumentasi (bg-running-text-30), dikonfirmasi user di sesi itu
Jawaban user asli diperiksa langsung dari transcript sesi tersebut, bukan hanya dari ringkasan agent-nya.
| Topik | Keputusan |
|---|---|
| Pengirim | Program di PC lewat USB (serial write, bukan diketik manual). UART0 tetap ada sebagai cadangan |
| Baud UART | Bisa diubah lewat perintah `@BAUD=`, disimpan di flash. USB tidak terpengaruh baud |
| Setelah listrik mati | Tampilkan teks & setelan terakhir (disimpan ke flash), bukan selalu `READY` |
| Dokumentasi | PDF lokal untuk operator, dibuat sesi dokumentasi setelah firmware teruji di panel |
| Format perintah | User memilih "Setuju, ada perubahan" — perubahannya kemudian disebutkan: layout LINES + adaptasi dari riset protokol. Lihat bagian **Spesifikasi berikutnya** di bawah |

Keputusan teknis turunan (sesi ini):
- Penyimpanan memakai LittleFS dengan partisi FS 1 MB (`flash=2097152_1048576`), bukan EEPROM satu sektor.
  Alasan: pengirim adalah program PC yang bisa mengganti teks sering; LittleFS menyebar tulisan ke ±256 blok
  sehingga umur flash (±100 ribu hapus per blok) tidak habis dalam hitungan bulan.
- Tulis hanya bila isi berubah, setelah 5 detik tanpa perubahan baru; tulis ke file sementara lalu rename
  (atomik) supaya listrik mati di tengah penulisan tidak merusak setelan; isi divalidasi versi + CRC32.
- Flash RP2040 menghentikan interrupt selama hapus/tulis → panel bisa tersendat sesaat. Lama penulisan
  ditampilkan di `@STATUS` untuk diukur di perangkat asli.

### Revisi user 2026-10-02 sore (sesi firmware) — menggantikan keputusan sebelumnya bila bertentangan
| Topik | Keputusan baru |
|---|---|
| Saat menyala | `READY` sekali, lalu panel **kosong** sampai ada input serial. Menggantikan "tampilkan teks & setelan terakhir" |
| Setelan saat menyala | **Semua kembali default**; hanya `BAUD` yang disimpan ke flash. Usulan "template LINES disimpan ke flash" (B1) ikut gugur |
| Posisi vertikal | Huruf kapital tepat di tengah zona (SPLIT helvB12 turun 2 px: 2 LED kosong atas & bawah). FULL ikut aturan sama. Ekor g/j/p/y boleh terpotong |
| `\n` di teks | Ditulis `\n` (backslash-n seperti C++) = baris baru. DIR=UP/DOWN: baris bertumpuk dan bergulir terus seperti credits |
| `\n` vs `@LINES` | Dua fitur terpisah: `@LINES=a\|b\|c` sesuai spesifikasi di bawah; TEXT berisi `\n` = credits |
| Akhir perintah | **CR** (`\r` atau `\r\n`). LF di dalam teks = baris baru (seperti `\n` C++). Menggantikan "Enter = CR atau LF" |
| Credits | Teks berisi baris baru langsung bergulir seperti credits, tanpa perlu `@DIR` |
| Credit: baris lebar *(2026-10-03, v6)* | Huruf dikecilkan **per baris** sampai muat (paling kecil helvB08); kalau helvB08 pun tidak muat → dipecah di spasi (kata yang tetap terlalu panjang dipotong per huruf). Ukuran huruf antar baris boleh beda |
| Credit jadi default *(2026-10-03, v6)* | Saat menyala, FULL `DIR=CREDIT` (juga untuk teks satu baris); LEFT/UP/DOWN tetap lewat `@DIR`. Baris SPLIT tetap LEFT (diam/berjalan; posisi sudah dikonfirmasi) |
| Gaya layar parkir *(2026-10-03, v6)* | Dari foto user (layar KELUAR PARKIR): di credit, baris `Label : Nilai` otomatis jadi label kecil (helvB08, `@LABELCOLOR`, default abu 808080) + nilai (SIZE, COLOR). Pemisah = titik dua **diikuti spasi** (`09:23`, `http://` tidak terpecah). Contoh `Kartu : … / Tarif : … / Saldo : …` bisa lewat credit (`@TEXT=`) maupun LINES (`@LINES=`), dipilih program PC |
| Urutan | v4 = posisi tengah + boot READY→kosong + default saat nyala. v5 = LINES + `\n` credits + VAR + ALIGN |

### Spesifikasi berikutnya — dari sesi dokumentasi (bg-running-text-30), dikerjakan di v5
**Status:** dicatat 2026-10-02 ±11:20 atas permintaan user ("catat dulu, biar agent lain eksekusi").
Dikonfirmasi user kecuali yang ditandai *(usulan)*. **Dikerjakan di v5** (lihat Pekerjaan), dengan
keputusan agent firmware yang diserahkan spesifikasi:
- Bentuk perintah LINES: awalan zona `@L:` untuk setelan (`@L:COLOR=YELLOW`). Perintah **tanpa awalan**
  yang memuat `LINES=` otomatis ditujukan ke zona LINES (`@COLOR=YELLOW;SIZE=1;LINES=a|b`). `@1:`/`@2:`
  dengan `LINES=`, `@L:TEXT=`, dan `@L:DIR=` ditolak.
- Batas: maks 16 baris (LINES dan credits), template 255 karakter, hasil setelah VAR diisi 511,
  isi VAR maks 40 karakter (berhenti di `;`). Pemisah baris LINES: `|` dan juga LF / `\n`.
- 1 baris: tampil di baris atas. *(usulan)* ONCE dan *(usulan)* `WIDE=` terbaru saat VAR berubah:
  dikerjakan seperti usulan. *(usulan)* template disimpan ke flash: gugur (Revisi user: semua default saat nyala).
- Balik cepat = 4× SPEED. Tanpa jeda di ujung kanan sebelum balik (sesuai urutan spesifikasi).

#### A. Layout LINES — banyak baris, panel menampilkan 2 baris (masing-masing 16 px)
Contoh dari user (layar info kartu):
```
@LINES=Kartu : Emoney / Tapcash|Tarif : Rp 5.000|Saldo : Rp 20.000
```
- Pemisah baris `|`. Satu perintah = satu set data, diterapkan utuh (atomik seperti perintah lain).
- Lebar contoh (diukur dari BDF): helvB08 127 / 76 / 86 px; helvB12 202 / 117 / 135 px. Panel 64 px →
  baris lebih lebar dari panel adalah kasus utama, bukan kasus pinggir.
- SIZE maks 2. COLOR, SIZE, SPEED, PAUSE, MODE berlaku untuk LINES; bentuk perintahnya (awalan sendiri
  atau ikut zona yang ada) ditentukan agent firmware, lalu didokumentasikan di tabel perintah + `@HELP`.
- Batas jumlah baris/karakter ditentukan agent firmware dan ditulis di `@HELP`.

Urutan animasi tiap tampilan 2 baris (mis. baris 1-2):
1. Diam PAUSE detik.
2. Baris yang lebih lebar dari 64 px bergeser ke kiri pada SPEED sampai ujung kanan teksnya terlihat.
   Kalau atas DAN bawah sama-sama lebar → keduanya bergeser **bersamaan**; yang lebih pendek berhenti di
   ujungnya dan menunggu yang lebih panjang.
3. Bergeser **balik cepat** ke posisi awal, animasinya terlihat (bukan lompat; mis. 4× SPEED).
4. Scroll up **satu baris** dengan animasi piksel demi piksel: 1-2 → 2-3 → 3-4 … (baris bawah naik jadi
   baris atas, baris berikutnya masuk dari bawah).
- Setelah baris terakhir: ikut `@MODE`. LOOP = melingkar (… → 3-1 → 1-2 …).
  ONCE = *(usulan)* berhenti setelah tampilan terakhir selesai lalu kosong, sama dengan ONCE yang ada.
- Hanya 1–2 baris: tidak ada scroll up, tapi langkah 2–3 tetap berlaku.

Feedback ke master — balasan langsung saat data diterima:
```
OK LINES=3 WIDE=1,2,3
```
`LINES` = jumlah baris diterima; `WIDE` = nomor baris yang lebih lebar dari panel (akan digeser);
`WIDE=-` kalau tidak ada. Error tetap `ERR <alasan>`.

#### B. Adaptasi dari riset protokol running text umum
Sumber: Moving Sign Communication Protocol V1.2 (perintah `C` = variabel, atribut align),
Onbon BX-6X (label tetap + nilai yang diperbarui), Alpha Sign. User: "adaptasi yang layak diadaptasi".

**B1. Variabel / template** — paling cocok untuk layar kartu (Tarif/Saldo berganti tiap tap).
- Teks (TEXT, `@1:`/`@2:` TEXT, dan LINES) boleh berisi tempat isi `{1}` … `{9}`.
- `@VAR<n>=<nilai>` mengisi variabel n, mis. `@VAR1=Rp 5.000;VAR2=Rp 20.000` (boleh digabung dengan `;`).
- Mengganti VAR **tidak** memulai animasi dari awal kalau lebar teks tidak berubah; kalau lebar berubah,
  batas geser dihitung ulang. *(usulan)* Balasan untuk LINES ikut melaporkan `WIDE=` terbaru.
- `{` yang bukan diikuti angka 1–9 dan `}` ditampilkan apa adanya. Variabel kosong = string kosong.
- *(usulan)* Template disimpan ke flash seperti setelan lain, **nilai VAR tidak** — nilai berubah tiap
  transaksi dan akan memicu tulis flash terus-menerus. Setelah menyala, VAR kosong sampai master mengirim.
- `@STATUS` menampilkan nilai VAR saat ini.

**B2. Rata teks** — `@ALIGN=LEFT|CENTER|RIGHT` (juga `KIRI TENGAH KANAN`).
- Hanya berlaku untuk teks yang muat di panel dan sedang diam; teks yang lebih lebar tetap bergeser.
- Default mempertahankan perilaku sekarang: SPLIT dan UP/DOWN = CENTER; LINES = LEFT.

**Tidak diadaptasi sekarang** (dicatat supaya tidak dibahas ulang):
- Alamat display + checksum (Alpha/Moving Sign/Modbus) — hanya perlu untuk RS485 banyak display; USB
  sudah punya pemeriksaan error sendiri.
- Balasan tahap 2 "selesai tampil" (`SOH` di Moving Sign) — user hanya memilih balasan saat data diterima.
- Warna berbeda dalam satu baris (kode sisipan warna) dan kecepatan dalam level 1–5 — opsional, belum diminta.
- Jadwal tampil per hari/jam, password, kecerahan — tidak relevan / kecerahan sengaja tidak dibuat.

#### C. Setelah dikerjakan
- Perbarui tabel **Perintah serial**, **Perilaku**, dan `@HELP`.
- Tambahkan skenario LINES (lebar/pendek, atas-bawah bersamaan, LOOP/ONCE, 1–2 baris) dan VAR/ALIGN ke
  uji simulasi di PC.
- Sesi dokumentasi membuat PDF panduan operator dari format final setelah teruji di panel.

### Permintaan berikutnya — SIDE "2 baris per layar" (sesi dokumentasi, 2026-10-03), dikerjakan (SIDE2)
**Status:** dicatat atas permintaan user ("catat aja dulu"), untuk dieksekusi agent firmware.
Arah besarnya dari user; detail di bawah yang ditandai **perlu keputusan user** belum dijawab — tanyakan
dulu sebelum implementasi.

Perintah contoh dari user:
```
@DIR=SIDE;COLOR=ORANGE;ALIGN=LEFT;SIZE=2;PAUSE=0.5;TEXT=KELUAR PARKIR\nTarif : Rp 0\nStatus : MEMBER\nNomor Kartu : **** **** **** 968A
```
Permintaan user: "kalau contoh ini saya mau jadiin 2 baris biar cepet" — dua informasi tampil bersamaan
per layar (mis. layar 1 = KELUAR PARKIR + Tarif, layar 2 = Status + Nomor Kartu), bukan satu per layar.

Perilaku v6 sekarang (dari kode `layoutSide`/`step`): tiap baris sumber = 1 kelompok = 1 layar →
4 layar. Lebar setelah dikecilkan: `KELUAR PARKIR` 83 px (digeser), `Tarif`/`Rp 0` 36, `Status`/`MEMBER` 46,
`Nomor Kartu`/`**** **** **** 968A` 83 (digeser). Hitungan dari kode (belum diukur di panel): 360 langkah
geser + 4 × 0,5 s ≈ **13 s** pada SPEED 33; ±5,6 s pada SPEED 100. Waktu habis terutama di geser, bukan PAUSE.

Sudah disarankan ke user sebagai solusi sementara tanpa ubah firmware: `SPEED=100`, atau padatkan isi jadi
2 kelompok label+nilai yang muat 64 px: `TEXT=Tarif Keluar : Rp 0\nMember : **** 968A`.
`LINES` tidak disarankan untuk contoh ini (hampir semua baris > 64 px → banyak geser).

Lebar untuk acuan (helvB08 / helvB12, px): `KELUAR PARKIR` 83/136, `Tarif : Rp 0` 55/86,
`Status : MEMBER` 88/135, `Nomor Kartu : **** **** **** 968A` 155/241, `Kartu : ****968A` 78/121,
`MEMBER` 46/69, `**** 968A` 45/68, `Rp 25.000` 49/76.

**Perlu keputusan user** (tanyakan sebelum mengerjakan):
1. Cara mengelompokkan: otomatis tiap 2 baris sumber jadi satu layar, atau pemisah eksplisit dari
   program PC (mis. baris kosong / `\n\n` = ganti layar)?
2. Tampilan `Label : Nilai` di baris 16 px: label kecil + nilai tidak muat bertumpuk dalam 16 px. Pilihan:
   satu baris utuh `Tarif : Rp 0` (helvB08), hanya nilai, atau label dan nilai berdampingan dengan warna beda.
3. Baris yang tetap lebih lebar dari 64 px (`KELUAR PARKIR`, `Status : MEMBER`, `Nomor Kartu : …` di helvB08):
   digeser bersamaan seperti LINES (yang pendek menunggu), atau dipecah/dipotong?
4. Perpindahan antar layar: tetap ke samping (gaya SIDE sekarang) atau naik?
5. Diaktifkan lewat perintah baru (mis. `@DIR=SIDE2` / `@GROUP=2`) atau mengganti perilaku SIDE yang ada?

**Jawaban user (2026-10-03, sesi firmware):** 1 = otomatis tiap 2 baris sumber jadi satu layar;
2 = label dan nilai berdampingan dengan warna beda (label abu helvB08, lalu nilai warna COLOR);
3 = baris lebar digeser bersamaan seperti LINES (yang pendek menunggu); 4 + 5 = perintah baru
`@DIR=SIDE2`, pindah layar ke samping (SIDE lama tetap ada). Dikerjakan agent firmware (lihat Pekerjaan).

### Ukuran di panel P5 (5 mm per pixel, panel 64×32 = 32×16 cm)
| SIZE | Font | Huruf kapital | Descender (g/j/p/y) | Total | Muat di |
|---|---|---|---|---|---|
| 1 | helvB08 | 8 px ≈ 4 cm | 2 px | 10 px | FULL, SPLIT |
| 2 | helvB12 | 12 px ≈ 6 cm | 4 px | 16 px | FULL, SPLIT (pas 1 baris) |
| 3 | helvB22 | 19 px ≈ 9,5 cm | 6 px | 25 px | FULL (default, = WINS lama) |
| 4 | helvB24 | 25 px ≈ 12,5 cm | 7 px | 32 px | FULL (pas setinggi panel) |

Catatan: file `helvB22.bdf` sebenarnya Helvetica Bold 19 pt / 27 px; namanya dipertahankan sesuai file lama.

### Temuan dari program lama
- Komentar pin salah: `adc0 = AnalogIn(board.A1)  # GP26` — di Pico A1 = GP27. Sensor IR tidak dipakai lagi.
- Banner WINS lama: masuk dari tepi kanan (x = 64), geser 1 px tiap 30 ms (≈ 33 px/s, nominal),
  `label.y = 12` dengan helvB22 → baseline ≈ baris 21. Loop berhenti di `x > -(lebar_teks + 64 + 4)`,
  jadi ada sisa layar kosong ±68 px setelah teks habis.

### Pekerjaan
- [x] Pasang alat build di WSL: arduino-cli 1.5.1, core rp2040 6.2.0, Protomatter 1.7.1, GFX 1.12.6
- [x] `tools/bdf2gfx.py`: konversi BDF → header GFXfont (ASCII 0x20–0x7E, teks lisensi Adobe/DEC ikut disalin)
- [x] Hasil konversi diverifikasi: 4 font × 95 glyph, bit dan metrik cocok dengan BDF
- [x] Sketch firmware (`running-text.ino`) + logika (`src/RunningText.*`, tanpa ketergantungan hardware)
- [x] Compile → `build/running-text.ino.uf2` (86.796 byte program = 4% flash, RAM global 14.552 byte = 5%),
      tanpa warning dari kode sendiri (warning yang ada berasal dari library Protomatter)
- [x] Uji logika di PC: simulasi frame ASCII, 42 skenario lulus dengan AddressSanitizer + UBSan (file uji dihapus)
- [x] Input sesi dokumentasi: `@BAUD=`, simpan setelan ke flash (LittleFS, partisi FS 1 MB), STATUS
      menampilkan `BAUD`, `SAVES`, `LAST_SAVE_MS`. Uji PC: 33 skenario lulus (regresi inti, BAUD, simpan
      hanya bila berubah, export→import identik, 12 jenis isi flash rusak ditolak); file uji dihapus.
- [x] Compile ulang dengan `flash=2097152_1048576`: 115.368 byte program (11% dari 1 MB), RAM global
      15.724 byte; `build/running-text.ino.uf2` SHA-256 `1fa275f0…e32f`. Salinan untuk flash dari Windows:
      `C:\Users\Public\running-text.uf2` (checksum sama).
- [x] Flash v1 — cara 1 (REPL COM20) ditolak pengaman otomatis Claude Code; user flash manual
      (BOOTSEL + salin UF2). Pico muncul sebagai COM21 (USB `2E8A:000A`, serial `E6611C08CB289E24`).
      `@STATUS` dijawab dalam 192 ms dengan setelan default → firmware jalan.
- [x] Temuan user di panel: **`READY` berkedip, tidak sestabil RED/BLUE WINS di CircuitPython.**
      Analisis: arduino-pico menjalankan `tud_task()` (USB) di interrupt tiap 1 ms dengan prioritas sama
      dengan interrupt PWM Protomatter. Interrupt panel tertunda → baris LED menyala lebih lama dari jadwal
      bitplane secara acak → kecerahan berubah tiap frame. CPU juga beda (Arduino 200 MHz, CircuitPython 125 MHz).
- [x] v2: `matrix.begin()` dipindah ke core 1 (`setup1`, `loop1` = `__wfi()`), prioritas `PWM_IRQ_WRAP`
      tertinggi → refresh panel tidak lagi berbagi core dengan USB/serial. `@STATUS` menampilkan
      `REFRESH_HZ` (penghitung frame Protomatter). Compile bersih: 115.624 byte.
      Cadangan v1: scratchpad sesi (`running-text-v1.uf2`). Flash otomatis (sentuhan 1200 baud COM21)
      juga ditolak pengaman otomatis → v2 disiapkan di `C:\Users\Public\running-text.uf2` untuk flash manual.
- [x] Flash v2 (manual oleh user). `@STATUS` dijawab dalam 38 ms (v1: 192 ms); `REFRESH_HZ` 179–180
      stabil di 5 sampel (frame panel penuh per detik, di atas ambang kedip ±100 Hz).
- [x] Uji serial di Pico (v2): `@HELP` memuat `@BAUD`; perintah salah ditolak dan setelan tidak berubah
      (`@1:SIZE=3`, `@BAUD=119200`, `@SPEED=999`, `@FOO=1`, `@COLOR=12345`).
      Simpan ke flash terbukti: `@SPEED=34` → `SAVES=1` (32 ms), `@SPEED=33` → `SAVES=2` (38 ms),
      kirim ulang nilai sama → tetap `SAVES=2`. Setelan akhir = default.
- [x] Permintaan user: **READY hanya saat booting**, bukan teks default yang berjalan terus.
      Dikonfirmasi: setelah READY → lanjut teks terakhir tersimpan (kosong bila belum ada); READY diam di
      tengah ±2 detik. READY di helvB22 = 89 px (tidak muat diam di 64 px) → dipakai helvB12 (57 px, ±6 cm).
      Implementasi v3: `beginSplash()`; teks default FULL = kosong; perintah yang mengubah tampilan
      menghentikan pembuka lebih awal, `@STATUS`/`@HELP`/perintah salah tidak. Uji PC 14 skenario lulus
      (ASan + UBSan, termasuk micros() wrap); compile bersih 116.024 byte; UF2 SHA-256 `8ddf2ba3…f509`
      disiapkan di `C:\Users\Public\running-text.uf2`.
      Teks `READY` yang tersimpan di Pico (dari uji SPEED) dihapus lewat `@CLEAR` di v2 → `SAVES=3`
      (72 ms), `TEXT=""`.
- [x] Flash v3 oleh user. Temuan user lewat SSCOM: SPLIT `RED 3`/`BLUE 2` punya 4 LED kosong di bawah
      (ruang ekor helvB12) → minta turun 2 px; `|` di TEXT tidak jadi baris baru (memang belum ada fiturnya);
      saat boot teks terakhir langsung tampil → diganti "READY sekali lalu ikut serial" (lihat Revisi user).
- [x] v4: posisi = huruf kapital tepat di tengah zona (`baseline = (h − cap)/2 + cap`) untuk semua zona/font;
      setelan tampilan tidak disimpan lagi (SavedState v2 = BAUD saja, 16 byte; file v1 832 byte diabaikan);
      pembuka READY berakhir oleh perintah tampilan apa pun (bukan STATUS/HELP/BAUD).
      Uji PC 23 skenario lulus (ASan + UBSan): SPLIT kapital baris 2–13 dan 18–29, FULL helvB22 6–24,
      helvB24 3–27, helvB08 12–19, READY 10–21, UP berhenti di tengah, setelan tampilan tidak memicu simpan,
      BAUD tersimpan & versi lama ditolak. Compile bersih 115.632 byte; UF2 SHA-256 `34f1fae4…d79d` di
      `C:\Users\Public\running-text.uf2`.
- [x] Flash v4 oleh user: **posisi pas** (dikonfirmasi user). Temuan: `TEXT=GOAL!\n123!\n321!` dari SSCOM
      hanya menampilkan `321!` — LF dulu mengakhiri perintah, jadi terpotong jadi 3 perintah; dan teks
      dengan `\n` belum jadi credits (fitur v5).
- [x] Keputusan user: **perintah diakhiri CR** (`\r` atau `\r\n`); LF di dalam teks = baris baru seperti
      `\n` di C++; tulisan `\n` (backslash-n) juga diterima. Teks berisi baris baru = langsung credits,
      tanpa perlu `@DIR` (contoh user: `Terima kasih\nPanitia\nSponsor\nSampai jumpa`).
- [x] v5: LINES (`@LINES=`/`@L:`), credits `\n`, VAR `{1}`–`{9}`, `@ALIGN`, akhir perintah CR, balasan
      `OK LINES=n WIDE=…`, `@STATUS` 4 zona + VAR. Ditemukan & diperbaiki bug lama: pesan
      `ERR <kunci> butuh nilai` mencetak isi memori acak (format `%s` ganda, argumen satu).
      Uji PC 63 skenario lulus (ASan + UBSan): regresi, credits naik/turun/LOOP/ONCE/ALIGN, LF asli & `\n`,
      LINES 3 baris (diam → geser bersamaan → balik 4× → naik 1-2→2-3→3-1→1-2), ONCE berhenti di 2-3,
      2 baris tanpa naik, 1 baris, awalan/penolakan, 17 baris ditolak; VAR isi/kosong/tidak mulai ulang/
      SPLIT jadi berjalan/>40 & >511 ditolak; ALIGN; STATUS terbesar 1.858 byte (buffer 2.600).
      Compile bersih 119.808 byte, RAM global 21.704 byte; UF2 SHA-256 `d1263f42…4585` di
      `C:\Users\Public\running-text.uf2`.
- [x] v6 (2026-10-03): credit default FULL (`@DIR=CREDIT`/`KREDIT`), gaya "Label : Nilai" (lapisan bitmap
      kedua `bufAlt` untuk warna label, `@LABELCOLOR`, warna `GRAY`/`ABU`/`GREY`), baris credit dikecilkan
      per baris lalu dipecah; jarak 2 px antar baris tampil, +4 px antar baris sumber; maks 64 baris tampil
      (`ERR credit terlalu panjang`). Uji PC 38 skenario lulus (ASan + UBSan): isi foto parkir (26 baris
      tampil, semua ≤ 64 px, 09:23 utuh), contoh 2 credit & LINES, aturan pemisah label, kecilkan/pecah/potong
      huruf, VAR tidak mulai ulang & pecah ulang, regresi WINS/UP/SPLIT/LINES/VAR/ALIGN.
      Compile bersih 121.656 byte, RAM global 27.752 byte; UF2 SHA-256 `87bb0955…1080` di
      `C:\Users\Public\running-text.uf2`. v5 tidak sempat di-flash; v6 sudah berisi semua isi v5.
      Catatan: di 64 px hampir semua baris foto turun ke helvB08 dan dipecah per kata (mis. "KELUAR PARKIR"
      → "KELUAR" / "PARKIR" kecil); alternatif "pecah dulu di ukuran SIZE" ditawarkan ke user.
- [x] Flash v6 oleh user. Temuan: credit terlalu cepat — harus ada jeda tiap teks; dan harus tampil sekali.
      Keputusan user: berhenti **tiap kelompok** (satu baris sumber: judul / pesan / pasangan Label+Nilai)
      saat atasnya menempel di atas panel, diam PAUSE (default 2 dtk), gerak tetap 33 px/s;
      **ONCE per perintah**: setiap TEXT/LINES tampil sekali kecuali `MODE=LOOP` ada di perintah yang sama
      (`@MODE=LOOP;TEXT=...`); MODE default saat menyala = ONCE.
- [x] v7: fase `RollPause` + `groupTop[]`; `PAUSE=0` = credit tanpa jeda; DIR=DOWN ikut berhenti;
      ONCE per perintah (`textGiven`/`modeGiven`). Uji PC 20 skenario lulus (ASan + UBSan): contoh user
      berhenti 6× @2,000 s lalu kosong (total ±20 s), LOOP per perintah, `@MODE=LOOP` saja, LINES ONCE,
      SPLIT diam tetap tampil, PAUSE=0, LOOP berhenti lagi, DOWN, VAR saat jeda, regresi WINS.
      Compile bersih 121.968 byte; UF2 di `C:\Users\Public\running-text.uf2`.
- [x] Permintaan user: credit ke samping. Dipilih: **per kelompok masuk dari kanan → diam PAUSE → keluar
      ke kiri**, kelompok berikutnya menyusul (jarak 16 px). `@DIR=SIDE` (alias `SAMPING`). Kelompok
      = label kecil + nilai bertumpuk di tengah tinggi panel (atau satu baris teks). Huruf dikecilkan per baris
      (juga agar label + nilai muat 32 px); kalau di helvB08 masih lebih lebar dari panel, kelompok digeser
      ke kiri sampai ujungnya terlihat lalu diam (tidak dipecah). Baris SPLIT 16 px: "Label : Nilai" utuh.
      Deteksi label dijadikan satu fungsi `labelSep()` untuk credit naik & ke samping.
      Uji PC 19 skenario lulus (ASan + UBSan): 4 kelompok diam 4× @2 s, 2 kelompok lebar digeser, ONCE
      kosong (total ±18 s), LOOP bergantian, satu kelompok LOOP, CENTER, SPLIT, VAR saat diam, SIZE=4
      dikecilkan, regresi credit/label/WINS/LINES. Compile bersih 123.384 byte; UF2 di
      `C:\Users\Public\running-text.uf2`.
- [x] SIDE2 (permintaan sesi dokumentasi, jawaban user di atas): `@DIR=SIDE2` (alias `SAMPING2`), hanya FULL.
      Tiap 2 baris sumber = 1 layar (baris 16 px; layar terakhir boleh 1 baris). "Label : Nilai" = label abu
      helvB08 + jarak 4 px + nilai warna COLOR (dikecilkan, maks helvB12, sampai satu baris muat 64 px).
      Layar masuk dari kanan → baris lebar digeser bersamaan (yang pendek menunggu) → diam PAUSE → keluar ke
      kiri, layar berikutnya menyusul 16 px. Uji PC 16 skenario lulus (ASan + UBSan): contoh dari log = 2 layar,
      keduanya digeser, total 10,5 s @SPEED 33 & PAUSE 0,5 (SIDE: ±13 s), 3 baris = 2 layar, LOOP, CENTER,
      SIZE 3 dibatasi helvB12, SPLIT ditolak, VAR saat diam, regresi SIDE/CREDIT/WINS/LINES.
      Batasan: VAR yang membuat baris jadi lebih lebar saat layar sedang diam tidak memicu geser ulang (baris
      terpotong sampai layar berikutnya). Compile bersih 125.112 byte; UF2 di `C:\Users\Public\running-text.uf2`.
- [x] Permintaan user: SIDE2 juga bisa naik seperti credit (mis. PAUSE=0, speed wajar). Keputusan user:
      setelan baru `@MOVE=SIDE|UP` (alias `SAMPING|NAIK`, default SIDE, menempel seperti setelan lain) untuk
      SIDE2; baris yang di helvB08 pun tidak muat dipecah ke baris berikut (label di barisnya sendiri, nilai
      dipecah per kata); PAUSE > 0 = berhenti tiap layar (2 baris 16 px memenuhi panel).
      Pemecah kata dijadikan satu fungsi `wrapWords()` untuk credit & SIDE2 naik.
      Uji PC 19 skenario lulus (ASan + UBSan): contoh `KELUAR PARKIR / Tarif / Status / Nomor Kartu` =
      8 baris 16 px, semua muat 64 px, PAUSE=0 naik tanpa henti 4,8 s @SPEED 33 lalu kosong; PAUSE=1
      berhenti 4× (tiap 2 baris); LOOP; MOVE menempel; MOVE=SIDE kembali ke samping; ALIGN; VAR; regresi
      credit (isi foto tetap 26 baris/348 px), SIDE2 ke samping, SIDE. Compile bersih 126.872 byte;
      UF2 di `C:\Users\Public\running-text.uf2`.
- [x] v8 (2026-10-05) — permintaan user untuk `@L:SIZE=1;COLOR=YELLOW;PAUSE=2;LINES=Tarif: Rp.5000|Saldo: Rp. 145000`:
      "ketika geser akhir itu nahan bentar sebelum hilang". LINES kini **diam PAUSE juga di ujung geser**
      (fase baru `LinesShiftPause`): diam awal → geser → diam di ujung → balik → naik. Tampilan terakhir ONCE
      langsung kosong dari ujung geser (tidak balik dulu). Tampilan tanpa baris lebar tidak berubah.
      Uji PC (ASan + UBSan): contoh di atas diam 0–2 s, geser sampai 2,76 s (Tarif −6 px, Saldo −25 px), diam
      di ujung sampai 4,76 s lalu kosong; LOOP balik lalu ulang; 3 baris ONCE diam di ujung tiap tampilan,
      kosong 11,87 s; baris pendek tanpa geser tetap sama; SPEED=15 memperlambat geser. Compile bersih
      126.952 byte (UF2 ini digantikan lanjutan di bawah).
- [x] v8 (2026-10-05, lanjutan) — keluhan user: warna LINES harus bisa diatur sendiri dan tetap 1x kirim. Sempat
      dibuat `@L:COLOR1..16=` (satu warna per baris), lalu **diganti** atas permintaan user ("ambil contoh punya
      minecraft"; contoh user `LINE1=%w%Tarif: %g%Rp.5000` / `LINW2=%w%Saldo: #yRp. 145000` — `LINW2` dan `#y`
      dibaca sebagai salah ketik `LINE2` / `%y%`, user sendiri menyebut contohnya typo). COLORn dihapus.
      Hasil akhir:
      - **Kode warna di teks** `%w% %r% %g% %b% %y% %c% %m% %o% %a%` (putih merah hijau biru kuning toska ungu
        oranye abu), berlaku sampai kode berikut / akhir baris, di semua mode dan di isi VAR. Dibuang dari teks
        tampil (`shownColor` per huruf), digambar ke `cbuf` per pixel; `draw()` .ino per pixel bila ada kode.
      - **`LINE1=`…`LINE16=`** (alias `BARIS1..16`): satu kunci per baris, mengganti seluruh isi LINES (baris yang
        tidak disebut kosong); nilai berakhir di `;`/LF; tidak bisa bersama `LINES=`.
      - **LF memisahkan perintah** seperti `;` (di luar isi TEXT/LINES), sehingga contoh user yang ditulis
        beberapa baris (`@L:SIZE=1;PAUSE=2;` LF `LINE1=…` LF `LINE2=…` CR) = satu perintah.
      Uji PC (ASan + UBSan): contoh user persis (putih/hijau/kuning per huruf saat diam, geser, diam di ujung),
      versi satu baris, tanpa awalan + huruf kecil + alias BARIS, hanya LINE3 (2 baris kosong), `10%` dan kode
      tak dikenal tampil apa adanya, kode di credit + label, kode di VAR, MODE=LOOP satu perintah, TEXT dengan
      LF tetap baris baru, error (@1:LINE1, LINE+LINES, LINE17, LINE0, tanpa nilai, > 255), regresi
      SIDE2/SPLIT/credit/LINES. `@HELP` 1.963 byte (batas balasan 2.600). Compile bersih 128.400 byte, RAM
      global 44.980 byte (+14 KB untuk `cbuf`); UF2 `5357c2ff…d088` (digantikan perbaikan di bawah).
      Belum diukur di perangkat: lama `draw()` per pixel saat ada kode warna.
- [x] v8 (2026-10-05, perbaikan setelah uji user di panel) — contoh `@L:SIZE=1;PAUSE=2;` / `LINE1=…` / `LINE2=…`
      dari SSCOM: hanya Saldo tampil, naik seperti credit. Penyebab: SSCOM mengirim CR tiap baris → 3 perintah;
      `LINE1=…` tanpa `@` jadi teks FULL (credit), dan perintah terakhir menimpa. User: "harusnya dibaca sampe
      data abis". Perbaikan:
      - Baris tanpa `@` yang diawali `LINEn=` / `BARISn=` = perintah zona LINES.
      - `LINEn=` kini menyambung: mulai dari isi LINES sekarang, isi baris n, buang baris sesudahnya (LINE1 =
        isi baru, LINE2.. menyambung). Satu perintah atau beberapa perintah terpisah hasilnya sama.
      - `LINEn=` tidak memaksa MODE ke ONCE (MODE ikut setelan terakhir), supaya `MODE=` di baris setelan tetap
        berlaku walau LINE datang sebagai perintah terpisah.
      Uji PC (ASan + UBSan): 3 perintah terpisah dengan LF sisa CRLF di awal = 2 baris putih/hijau/kuning,
      diam-geser-diam di ujung-kosong; satu perintah dengan LF; isi lama 3 baris lalu LINE1+LINE2 = 2 baris;
      LINE2 saja menjaga baris 1; LINE3 dari kosong; `LINE1 rusak` / `LINEA=1` tetap teks FULL; MODE=LOOP dari
      baris setelan bertahan; error @1:LINE1 dan LINE+LINES; regresi SIDE2/SPLIT/credit/LINES. Compile bersih
      128.608 byte (UF2 digantikan perbaikan di bawah).
- [x] v8 (2026-10-05, perbaikan MODE) — user: setelah blok `@L:SIZE=1;PAUSE=2;MODE=LOOP;` / `LINE1=SILAHKAN` /
      `LINE2=TAP KARTU ANDA`, blok transaksi tanpa MODE ikut LOOP ("harusnya gausah ikut loop"). Aturan baru:
      `LINE1=` = blok baru → ONCE, kecuali MODE ada di perintah yang sama atau di perintah zona LINES tanpa teks
      sebelumnya (`linesModeArmed_`, dipakai sekali oleh LINE1 / LINES= berikutnya). LINE2.. tidak mengubah MODE.
      Uji PC (ASan + UBSan): urutan user per baris (CRLF): SILAHKAN LOOP → transaksi ONCE lalu kosong → SILAHKAN
      LOOP lagi; versi satu perintah; MODE=LOOP lepas setelah dipakai satu LINE1; LINES= tetap aturan lama;
      regresi SIDE2/SPLIT/credit. Compile bersih 128.776 byte; `build/running-text.ino.uf2` SHA-256
      `1de6288a…208e`.
- [ ] Flash v8 (manual oleh user) + cek credit naik, SIDE, SIDE2 (ke samping & MOVE=UP), LINES diam di ujung geser, LINEn + kode warna di panel.
- [ ] Konfirmasi visual user: kedip hilang / stabil seperti RED/BLUE WINS?
- ~~Amati sendatan saat setelan disimpan / muat setelan setelah listrik mati~~ — gugur sejak v4 (hanya BAUD
  yang disimpan, jarang ditulis).

### Status implementasi terhadap log (dicek ulang 2026-10-02, v5)
Belum diimplementasikan:
- **SIDE "2 baris per layar"** (2026-10-03) — lihat bagian **Permintaan berikutnya**; ada 5 detail yang
  perlu keputusan user dulu.
- **PDF panduan operator** — tugas sesi dokumentasi; menunggu firmware teruji di panel.
  Sumbernya sudah ada: [documentation.md](documentation.md) (2026-10-05, sesi dokumentasi) — semua perintah,
  contoh, dan cara kerja v7 (UF2 SHA-256 `ce8d5072…2579`, sama dengan yang di-flash user). Disusun dari kode
  sumber v7; semua contoh dijalankan di simulasi PC (ASan + UBSan, file uji dihapus) untuk balasan & durasi.
- Animasi kembang api sebelum banner WINS (ada di program lama `game_won()`) — tidak pernah diminta,
  belum dibuat. Kecerahan (`@BRIGHT`) sengaja tidak dibuat sesuai keputusan user.
- Bagian **"Tidak diadaptasi sekarang"** dari spesifikasi (alamat+checksum, balasan "selesai tampil",
  warna per kata, jadwal) — sengaja tidak dibuat.

Sudah diimplementasikan semua keputusan lain, termasuk Revisi user sore dan spesifikasi LINES/VAR/ALIGN.

### Struktur folder
| Path | Isi |
|---|---|
| `running-text.ino` | Core 1: refresh panel (Protomatter, pin sama dengan code.py lama). Core 0: serial USB + UART0, animasi, simpan setelan |
| `src/RunningText.h/.cpp` | Perintah serial, animasi, render bitmap 1-bit per zona |
| `src/fonts/*.h` | Font GFX hasil generate — jangan diedit manual |
| `fonts-src/*.bdf` | Sumber font (helvB08/12/22 dari CIRCUITPY, helvB24 dari paket X11 `xfonts-100dpi`) |
| `tools/bdf2gfx.py` | Konverter BDF → GFX |
| `build/running-text.ino.uf2` | Firmware siap flash |

Build ulang:
```bash
cd running-text
arduino-cli compile --fqbn rp2040:rp2040:rpipico:flash=2097152_1048576 --output-dir build .
# font (kalau BDF diganti):
python3 tools/bdf2gfx.py fonts-src/helvB22.bdf src/fonts/helvB22.h helvB22
```

### Perintah serial
USB (port COM Pico) atau UART0: **GP0 = TX, GP1 = RX**, 115200 8N1, GND bersama.
**Setiap perintah diakhiri CR** (`\r` atau `\r\n`; SSCOM: centang AddCrLf). LF di dalam perintah = baris
baru di teks; perintah yang hanya diakhiri LF **tidak dijalankan** (menunggu CR). Balasan: `OK`
(LINES: `OK LINES=n WIDE=…`), atau `ERR <alasan>` (kalau ada error, tidak ada perubahan yang diterapkan).
USB: balasan hanya terkirim bila DTR aktif (SSCOM: centang DTR).

| Perintah | Fungsi |
|---|---|
| `<teks>` | Teks FULL panel (layout otomatis FULL) |
| `@TEXT=<teks>` | Sama; untuk teks yang diawali `@` |
| `@1:TEXT=<teks>` / `@2:TEXT=<teks>` | Teks baris atas / bawah (layout otomatis SPLIT) |
| teks berisi `\n` atau LF | Baris baru → bergulir terus seperti credits (DIR=DOWN: ke bawah). `\\n` = tulisan `\n` biasa |
| `@LINES=a\|b\|c` | Banyak baris (maks 16), panel menampilkan 2 baris, bergantian naik (layout otomatis LINES) |
| `@L:<setelan>` | Setelan zona LINES: COLOR, SIZE (maks 2), SPEED, PAUSE, MODE, ALIGN |
| `@VAR1=<isi>` … `@VAR9=<isi>` | Mengisi `{1}`…`{9}` di TEXT / LINES (maks 40 karakter, tidak boleh `;`) |
| `@LAYOUT=FULL\|SPLIT\|LINES` | Pindah layout tanpa mengganti teks |
| `@COLOR=<warna>` | `RED GREEN BLUE WHITE YELLOW CYAN MAGENTA ORANGE GRAY` (juga `MERAH BIRU … ABU`) atau `RRGGBB` |
| `@LABELCOLOR=<warna>` | Warna label `Label : Nilai` di credit (default `GRAY` 808080). Tidak untuk LINES |
| `@SIZE=1..4` | 1 = helvB08, 2 = helvB12, 3 = helvB22, 4 = helvB24 (juga `S M L XL`). SPLIT/LINES maks 2 |
| `@DIR=CREDIT\|SIDE\|SIDE2\|LEFT\|UP\|DOWN` | Credit naik (default FULL) / ke samping per kelompok / ke samping 2 baris per layar (hanya FULL) / kiri (gaya WINS) / naik-berhenti / turun-berhenti (juga `KREDIT SAMPING SAMPING2 KIRI NAIK TURUN`). Tidak untuk LINES |
| `@MOVE=SIDE\|UP` | SIDE2: pindah layar ke samping (default) / naik seperti credit, baris 16 px (juga `SAMPING NAIK`). Menempel |
| `@ALIGN=LEFT\|CENTER\|RIGHT` | Posisi teks yang muat panel (juga `KIRI TENGAH KANAN`). Default CENTER, LINES LEFT |
| `@MODE=LOOP\|ONCE` | Ulang terus / sekali lalu kosong. **Setiap TEXT/LINES = ONCE**, kecuali `MODE=LOOP` di perintah yang sama (`@MODE=LOOP;TEXT=...`) |
| `@SPEED=1..200` | Kecepatan, pixel per detik (1 px = 5 mm di P5) |
| `@PAUSE=<detik>` | Lama berhenti: credit tiap kelompok, UP/DOWN di tengah, LINES per tampilan (0–60, boleh desimal: `1.5`; 0 = credit tanpa jeda) |
| `@BAUD=9600\|19200\|38400\|57600\|115200` | Baud UART0 (GP0/GP1), disimpan. Balasan `OK` dikirim dengan baud lama, lalu pindah. USB tidak terpengaruh |
| `@CLEAR` / `@1:CLEAR` / `@L:CLEAR` | Kosongkan semua / zona tertentu |
| `@STATUS` / `@HELP` | Tampilkan pengaturan (4 zona + VAR) / daftar perintah |

- Awalan `@1:`, `@2:`, `@L:` berlaku untuk seluruh perintah; tanpa awalan = FULL, kecuali perintah yang
  memuat `LINES=` (otomatis ke zona LINES).
- Beberapa perintah bisa digabung dengan `;`; `TEXT`/`LINES` harus paling akhir (boleh berisi `;`):
  `@2:COLOR=BLUE;DIR=UP;PAUSE=1.5;TEXT=BLUE WINS`, `@L:COLOR=YELLOW;LINES=Tarif : {1}|Saldo : {2}`
- Contoh gaya lama: `@COLOR=RED;SIZE=3;DIR=LEFT;TEXT=RED WINS!!!`
- `{` yang tidak diikuti angka 1–9 dan `}` tampil apa adanya; VAR kosong = teks kosong.
- Karakter di luar ASCII (mis. `é`) tampil `?`. Teks maks 255 karakter (setelah VAR 511), satu perintah
  maks 320 karakter.

### Perilaku
- Saat menyala (v6): `READY` putih diam di tengah ±2 detik (helvB12), lalu panel kosong dengan setelan
  default: FULL, putih, helvB22, **DIR=CREDIT**, ALIGN CENTER, LABELCOLOR abu, **ONCE**, 33 px/s, PAUSE 2 s. Baris SPLIT default: atas merah,
  bawah biru (warna tim lama), helvB12. Perintah tampilan saat READY masih tampil → langsung berlaku.
- Penyimpanan (v4): hanya BAUD (`/state.bin`), 5 detik setelah berubah, hanya bila beda dari yang tersimpan.
- LEFT (FULL): teks masuk dari tepi kanan, jalan 1 px per langkah; setelah habis di kiri langsung mulai lagi
  dari kanan. Beda dengan WINS lama: tidak ada ekor kosong ±68 px setelah teks habis.
- LEFT (SPLIT): teks yang muat 64 px diam di tengah; yang lebih panjang berjalan.
- UP / DOWN: masuk → berhenti PAUSE detik di tengah → keluar. Teks lebih lebar dari panel mulai rata kiri,
  berjalan ke kiri sampai ujungnya terlihat, lalu berhenti PAUSE detik, lalu keluar.
- Credit (v7; DIR=CREDIT, atau teks berisi baris baru dengan DIR apa pun): blok baris masuk dari bawah;
  setiap kelompok (satu baris sumber) yang atasnya menempel di atas panel berhenti PAUSE detik, lalu naik
  lagi; setelah kelompok terakhir, blok keluar di atas (DIR=DOWN: kebalikannya). ONCE = lalu kosong;
  LOOP = mulai lagi dari bawah.
  Tiap baris sumber: `Label : Nilai` → label helvB08 warna LABELCOLOR + nilai ukuran SIZE warna COLOR;
  baris lain → ukuran SIZE warna COLOR. Baris yang tidak muat dikecilkan sampai helvB08, lalu dipecah di
  spasi, kata yang tetap terlalu panjang dipotong per huruf. Jarak: 2 px antar baris tampil, +4 px antar
  baris sumber. ALIGN berlaku per baris tampil (`@ALIGN=LEFT` = mirip foto layar parkir).
- Credit ke samping (v7, DIR=SIDE): tiap kelompok (judul / pesan / Label+Nilai bertumpuk di tengah tinggi
  panel) masuk dari kanan → (bila lebih lebar dari panel: geser sampai ujung kanannya terlihat) → diam PAUSE
  → keluar ke kiri sementara kelompok berikutnya menyusul 16 px di belakangnya. ONCE: setelah kelompok
  terakhir keluar, kosong; LOOP: kembali ke kelompok pertama. Posisi diam ikut ALIGN.
- SIDE2 (v7, hanya FULL): tiap 2 baris sumber = satu layar 2 baris (16 px). "Label : Nilai" = label abu
  helvB08 lalu nilai di kanannya (warna COLOR, dikecilkan maks helvB12 sampai muat 64 px). Layar masuk dari
  kanan → baris lebar digeser bersamaan (yang pendek menunggu) → diam PAUSE → keluar ke kiri, layar
  berikutnya menyusul. Contoh `KELUAR PARKIR\nTarif : Rp 0\nStatus : MEMBER\nNomor Kartu : …` = 2 layar.
- SIDE2 + `MOVE=UP`: baris 16 px yang sama (label + nilai berdampingan) bergulir naik seperti credit.
  Baris yang di helvB08 pun lebih lebar dari panel dipecah: label di barisnya sendiri, nilai/teks per kata.
  PAUSE > 0: berhenti tiap 2 baris memenuhi panel; PAUSE=0: naik terus. ONCE/LOOP sama dengan credit.
- LINES (v5, v8): tampilan 2 baris (16 px, helvB12/08) → diam PAUSE → baris lebar bergeser kiri bersamaan
  sampai ujungnya terlihat (yang pendek menunggu) → diam PAUSE lagi di ujung (v8) → balik 4× SPEED → naik
  satu baris (1-2 → 2-3 → …). LOOP melingkar (… 3-1 → 1-2); ONCE: tampilan terakhir kosong langsung dari
  ujung geser (v8). 1–2 baris: tanpa naik.
- VAR (v5): mengganti VAR tidak memulai animasi dari awal; batas geser memakai lebar baru. Teks SPLIT yang
  jadi lebih lebar dari panel berganti dari diam ke berjalan.
- Mengganti TEXT/LINES/SIZE/DIR/ALIGN/MODE/LAYOUT memulai animasi dari awal; COLOR/SPEED/PAUSE langsung berlaku.
- SPEED=33 = 30,3 ms per langkah (lama nominal 30 ms; CircuitPython aslinya lebih lambat karena beban render).
- Posisi vertikal (v4): huruf kapital tepat di tengah zona. SPLIT helvB12 → baris 2–13 / 18–29;
  FULL helvB22 → 6–24 (WINS lama ±2–20). Ekor g/j/p/y terpotong 2 px di SPLIT helvB12 dan 3 px di
  FULL helvB24; glyph lebih tinggi dari kapital (mis. `(`, `|`) bisa terpotong 1 px di SPLIT.

### Belum terverifikasi
- Tampilan di panel asli (warna, kedip setelah v2, kecepatan nyata, UP/DOWN, credits, LINES) — perlu
  dilihat user. Posisi SPLIT/FULL v4 sudah dikonfirmasi pas.
- Perilaku SSCOM terhadap `\n`: dari hasil v4 kemungkinan SSCOM mengirim `\n` sebagai LF asli — v5 menerima
  keduanya (LF asli dan backslash-n), jadi hasilnya sama.
- BAUD terbaca lagi setelah listrik mati (v4 hanya menyimpan BAUD).
- UART GP0/GP1 dan pergantian `@BAUD` — belum ada perangkat pengirim sungguhan.
