# Dokumentasi Running Text Pico — Perintah Serial (firmware v8)

Panduan perintah serial untuk panel running text **P5 64×32 (HUB75)** dengan **Raspberry Pi Pico (RP2040)**:
semua perintah, contoh, dan cara firmware menjalankannya.

| | |
|---|---|
| Firmware | v8 — `build/running-text.ino.uf2`, SHA-256 `1de6288a…208e` (dibuat 2026-10-05). Beda dengan v7: LINES diam PAUSE juga di ujung geser, `LINE1=`…`LINE16=`, kode warna `%w%` dst. di teks, dan LF sebagai pemisah perintah ([6.8](#68-lines--banyak-baris-2-tampil-bergantian-naik), [7a](#7a-kode-warna-di-teks)). `C:\Users\Public\running-text.uf2` masih v7 (`ce8d5072…2579`) |
| Sumber | `running-text.ino`, `src/RunningText.h`, `src/RunningText.cpp` |
| Disusun | 2026-10-05, dari membaca kode sumber v7 (bagian LINES: v8) lalu menjalankan **semua contoh** di simulasi PC (kode `RunningText.cpp` yang sama, dengan AddressSanitizer + UBSan) |

> **Status verifikasi.** Balasan (`OK` / `ERR …`) dan durasi di dokumen ini adalah **hasil simulasi PC**, bukan
> diukur di panel. Posisi SPLIT/FULL sudah dikonfirmasi user di panel (v4). Credit, SIDE, SIDE2, MOVE=UP, dan
> LINES di v7/v8 belum dilihat di panel asli. Riwayat keputusan ada di [log.md](log.md).

---

## 1. Koneksi

| Jalur | Keterangan |
|---|---|
| **USB** (port COM Pico) | Jalur utama (program di PC). Baud di aplikasi PC **tidak berpengaruh**. Dari uji lapangan: balasan hanya keluar bila **DTR aktif** (SSCOM: centang DTR) |
| **UART0** | **GP0 = TX**, **GP1 = RX**, GND bersama, 8N1, default **115200** (bisa diganti `@BAUD`) |

Kedua jalur aktif bersamaan; balasan dikirim ke jalur yang mengirim perintah.

Pin panel HUB75 (sama dengan program CircuitPython lama):

| Sinyal | R1 | G1 | B1 | R2 | G2 | B2 | A | B | C | D | CLK | LAT | OE |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| GPIO | 2 | 3 | 6 | 7 | 8 | 9 | 10 | 16 | 18 | 20 | 11 | 12 | 13 |

---

## 2. Aturan dasar perintah

1. **Setiap perintah diakhiri CR** (`\r` atau `\r\n`). Di SSCOM: centang *AddCrLf*.
   Perintah yang hanya diakhiri LF **tidak dijalankan** (firmware menunggu CR).
2. **LF di tengah perintah = baris baru di teks.** Tulisan `\n` (backslash + n, dua karakter) juga = baris baru.
   `\\` = tulisan backslash biasa.
3. Baris **tanpa `@`** = seluruh baris jadi teks FULL, termasuk `;` dan `|`:
   `Harga 10.000; diskon` → tampil apa adanya.
4. Baris **diawali `@`** = perintah `KUNCI=nilai`. Beberapa perintah digabung dengan `;`.
   **`TEXT=` / `LINES=` harus paling akhir**: isinya mengambil seluruh sisa baris (boleh berisi `;`).
   Selain di isi `TEXT=` / `LINES=`, **LF (baris baru) juga memisahkan perintah** seperti `;` (v8), jadi perintah
   boleh ditulis beberapa baris dalam satu kiriman (contoh di 6.8).
5. **Awalan zona** setelah `@` berlaku untuk seluruh perintah:

   | Awalan | Zona |
   |---|---|
   | (tanpa) | FULL (seluruh panel). Pengecualian: perintah yang memuat `LINES=` / `LINEn=` otomatis ke zona LINES |
   | (tanpa `@`, diawali `LINEn=`) | Zona LINES (v8) |
   | `@1:` | Baris atas SPLIT (16 px) |
   | `@2:` | Baris bawah SPLIT (16 px) |
   | `@L:` | Zona LINES |

6. Nama kunci dan nilai **tidak peka huruf besar/kecil**, kecuali isi `TEXT`, `LINES`, `LINEn`, dan `VARn` (dipakai apa adanya).
   Spasi di sekitar kunci/nilai diabaikan.
7. **Atomik:** kalau satu bagian salah, **tidak ada** perubahan yang diterapkan, dan balasannya `ERR <alasan>`.
8. Karakter di luar ASCII (mis. `é`) tampil `?`.
9. Batas: satu perintah maks **320 karakter**; teks/template maks **255**; teks setelah VAR diisi maks **511**;
   credit/LINES maks **16 baris sumber**.

**Balasan**

| Situasi | Balasan |
|---|---|
| Berhasil | `OK` |
| Berhasil, menyangkut LINES (`LINES=` dikirim, atau VAR diganti saat layout LINES) | `OK LINES=<jumlah> WIDE=<nomor baris lebar>` / `WIDE=-` |
| Gagal | `ERR <alasan>` (lihat [bagian 12](#12-daftar-error-hasil-simulasi)) |
| Perintah lebih dari 320 karakter | `ERR baris terlalu panjang (maks 320 karakter)` |
| `@STATUS` / `@HELP` | Beberapa baris, diakhiri baris `OK` |

---

## 3. Saat menyala dan nilai bawaan

1. `READY` putih (helvB12) diam di tengah ±2 detik. Perintah tampilan yang datang saat itu langsung berlaku
   (`@STATUS`, `@HELP`, `@BAUD` tidak menghentikan READY).
2. Lalu **panel kosong** sampai ada perintah. **Semua setelan kembali bawaan setiap kali menyala**; hanya `BAUD`
   yang disimpan ke flash. Program PC sebaiknya selalu mengirim setelan lengkap bersama teksnya.

| Zona | COLOR | SIZE | DIR | ALIGN | MODE | SPEED | PAUSE | LABELCOLOR |
|---|---|---|---|---|---|---|---|---|
| FULL | putih | 3 (helvB22) | CREDIT | CENTER | ONCE | 33 px/s | 2 s | abu 808080 |
| `@1:` atas | merah | 2 (helvB12) | LEFT | CENTER | ONCE | 33 | 2 s | abu |
| `@2:` bawah | biru | 2 (helvB12) | LEFT | CENTER | ONCE | 33 | 2 s | abu |
| `@L:` LINES | putih | 2 (helvB12) | — | LEFT | ONCE | 33 | 2 s | — |

`MOVE` bawaan `SIDE`. VAR kosong. Layout FULL.

---

## 4. Layout

| Layout | Isi panel | Cara masuk |
|---|---|---|
| **FULL** | Satu teks satu panel (32 px) | `TEXT=` tanpa awalan, atau teks tanpa `@` |
| **SPLIT** | Dua baris 16 px, masing-masing teks/warna/gerak sendiri | `@1:TEXT=` / `@2:TEXT=` |
| **LINES** | Banyak baris, 2 tampil, bergantian naik | `@LINES=` / `@L:LINES=` |

`@LAYOUT=FULL|SPLIT|LINES` (alias `PENUH|BAGI2|BARIS`) pindah layout tanpa mengganti teks; tampilan zona tujuan
mulai dari awal. Teks setiap zona tetap tersimpan selama tidak di-`CLEAR`.

---

## 5. Daftar perintah

| Perintah | Alias | Nilai | Berlaku | Keterangan |
|---|---|---|---|---|
| `<teks>` | | teks | FULL | Sama dengan `@TEXT=<teks>` |
| `@TEXT=` | | teks ≤ 255 | FULL, `@1:`, `@2:` | Ganti teks, mulai dari awal. Untuk teks yang diawali `@` |
| `@LINES=a\|b\|c` | | teks ≤ 255, ≤ 16 baris | LINES | Pemisah `\|`, LF, atau `\n` |
| `@VAR1=` … `@VAR9=` | | ≤ 40 karakter, tanpa `;` | semua | Mengisi `{1}`…`{9}` di teks |
| `@COLOR=` | `WARNA` | nama atau `RRGGBB` / `#RRGGBB` | semua | Warna teks / nilai |
| `LINE1=` … `LINE16=` | `BARIS1..16` | teks, tanpa `;` | LINES | Isi baris ke-n, baris sesudahnya dibuang; boleh tanpa `@` (lihat 6.8) |
| `@LABELCOLOR=` | `WARNALABEL` | nama atau `RRGGBB` | FULL, `@1:`, `@2:` | Warna label `Label : Nilai` |
| `@SIZE=` | | `1..4` / `S M L XL` | semua | SPLIT/LINES maks 2 |
| `@DIR=` | `ARAH` | `CREDIT SIDE SIDE2 LEFT UP DOWN` | FULL, `@1:`, `@2:` | `SIDE2` hanya FULL |
| `@MOVE=` | `PINDAH` | `SIDE` / `UP` | FULL | Untuk SIDE2. Menempel sampai diganti / mati listrik |
| `@ALIGN=` | `RATA` | `LEFT CENTER RIGHT` | semua | Posisi teks yang muat panel |
| `@MODE=` | | `LOOP` / `ONCE` | semua | Lihat [bagian 8](#8-mode-once--loop) |
| `@SPEED=` | | `1..200` px/detik | semua | 1 px = 5 mm di P5 |
| `@PAUSE=` | | `0..60` detik, boleh desimal | semua | Lama berhenti |
| `@LAYOUT=` | | `FULL SPLIT LINES` | — | Pindah layout |
| `@BAUD=` | | `9600 19200 38400 57600 115200` | — | Baud UART0, disimpan ke flash |
| `@CLEAR` | | — | — | Tanpa awalan: kosongkan **semua** zona. `@1:CLEAR` / `@2:CLEAR` / `@L:CLEAR`: zona itu saja |
| `@STATUS` | | — | — | Tampilkan setelan semua zona + VAR |
| `@HELP` | | — | — | Daftar perintah singkat |

Alias nilai (bahasa Indonesia): `KREDIT SAMPING SAMPING2 KIRI NAIK TURUN`, `TENGAH KANAN`, `ULANG SEKALI`.

**Warna:**

| Nama | Alias | RGB |
|---|---|---|
| RED | MERAH | FF0000 |
| GREEN | HIJAU | 00FF00 |
| BLUE | BIRU | 0000FF |
| WHITE | PUTIH | FFFFFF |
| YELLOW | KUNING | FFFF00 |
| CYAN | TOSKA | 00FFFF |
| MAGENTA | UNGU | FF00FF |
| ORANGE | ORANYE | FF5C00 |
| GRAY / GREY | ABU | 808080 |

**Ukuran huruf** (panel P5, 5 mm per piksel, 64×32 = 32×16 cm):

| SIZE | Font | Tinggi huruf kapital | Muat di |
|---|---|---|---|
| 1 / S | helvB08 | 8 px ≈ 4 cm | FULL, SPLIT, LINES |
| 2 / M | helvB12 | 12 px ≈ 6 cm | FULL, SPLIT, LINES |
| 3 / L | helvB22 | 19 px ≈ 9,5 cm | FULL |
| 4 / XL | helvB24 | 25 px ≈ 12,5 cm | FULL |

Panel hanya **64 px** lebar. Sebagai patokan: `KELUAR PARKIR` = 83 px di helvB08 dan 136 px di helvB12; `Rp 25.000`
= 49 / 76 px. Di mode credit/SIDE/SIDE2, huruf otomatis dikecilkan per baris sampai muat.

---

## 6. Mode tampilan: contoh dan cara kerjanya

Durasi = hasil simulasi PC dari saat perintah diterima sampai panel kosong (ONCE), pada SPEED/PAUSE yang tertulis.

### 6.1 Credit (bawaan FULL)

Dipakai bila `DIR=CREDIT` (bawaan FULL), **atau** teks berisi baris baru dengan DIR `LEFT/UP/DOWN`.

```
HALO
```
→ `OK`. 1 jeda, selesai ±3,5 s.

```
Terima kasih\nPanitia\nSponsor\nSampai jumpa
```
→ `OK`. 4 jeda (satu per baris), selesai ±11,6 s.

Cara kerja:
- Seluruh blok baris **masuk dari bawah dan naik**. Setiap **kelompok** (satu baris sumber) **berhenti PAUSE detik
  saat atasnya menempel di tepi atas panel**, lalu naik lagi. Setelah kelompok terakhir, blok keluar di atas.
  `PAUSE=0` = naik terus tanpa henti. `DIR=DOWN` dengan teks banyak baris = kebalikannya (turun).
- Huruf **dikecilkan per baris** sampai muat 64 px (paling kecil helvB08). Kalau helvB08 pun tidak muat, baris
  **dipecah di spasi**; kata yang tetap terlalu panjang dipotong per huruf. Contoh: `HALO` di helvB22 lebih lebar
  dari 64 px, jadi tampil di helvB12, di bagian atas panel saat berhenti.
- **Gaya `Label : Nilai`** (titik dua **diikuti spasi**, label tidak kosong): label tampil kecil (helvB08, warna
  `LABELCOLOR`, bawaan abu), nilai di bawahnya (ukuran SIZE, warna COLOR). `09:23` dan `http://` **tidak** dipecah.
- Jarak: 2 px antar baris tampil, +4 px antar baris sumber. ALIGN berlaku per baris. Maks 64 baris tampil
  (`ERR credit terlalu panjang …`).

Contoh layar parkir (credit naik, rata kiri):
```
@COLOR=ORANGE;ALIGN=LEFT;SIZE=2;PAUSE=0.5;TEXT=KELUAR PARKIR\nTarif : Rp 0\nStatus : MEMBER\nNomor Kartu : **** **** **** 968A
```
→ `OK`. 4 jeda, selesai ±6,7 s.

### 6.2 LEFT — berjalan ke kiri (gaya banner "RED WINS" lama)

```
@COLOR=RED;SIZE=3;DIR=LEFT;TEXT=RED WINS!!!
```
→ `OK`. Masuk dari tepi kanan, jalan 1 px per langkah sampai habis di kiri. ONCE: selesai ±5,1 s lalu kosong.
Supaya berulang seperti dulu: `@MODE=LOOP;COLOR=RED;SIZE=3;DIR=LEFT;TEXT=RED WINS!!!`.

Cara kerja: di FULL, LEFT **selalu berjalan** walau teksnya pendek. Di baris SPLIT, teks yang muat **diam**
(lihat 6.4).

### 6.3 UP / DOWN — masuk, berhenti, keluar (teks satu baris)

```
@2:COLOR=BLUE;DIR=UP;PAUSE=1.5;TEXT=BLUE WINS
```
→ `OK`. Masuk dari bawah, berhenti 1,5 s di tengah, keluar ke atas (`DOWN` = dari atas ke bawah). Selesai ±3,1 s.

Teks yang lebih lebar dari panel mulai rata kiri. Saat berhenti, teks berjalan ke kiri sampai ujungnya terlihat,
diam PAUSE, lalu keluar. Teks **banyak baris** dengan UP/DOWN dijalankan sebagai credit (6.1).

### 6.4 SPLIT — dua baris terpisah

```
@1:TEXT=RED 3
@2:TEXT=BLUE 2
```
→ masing-masing `OK`. Atas merah, bawah biru (warna bawaan), helvB12, **diam di tengah** karena muat 64 px.

```
@2:TEXT=BIRU MENANG BABAK KEDUA
```
→ teks bawah tidak muat, jadi **berjalan ke kiri** (sekali, karena ONCE; tambahkan `MODE=LOOP;` agar berulang).
Baris atas tetap diam.

Simulasi frame (`#` = LED menyala):
```
|.........########....########..######............####...........|  ← "RED 3"  (baris 2–13)
|....#######....##........##......##..########........####.......|  ← "BLUE 2" (baris 18–29)
```
Posisi: huruf kapital tepat di tengah baris 16 px (baris panel 2–13 dan 18–29). Ekor g/j/p/y terpotong 2 px.
`DIR=UP/DOWN/SIDE` juga bisa dipakai per baris SPLIT. `SIDE2` tidak bisa (`ERR DIR=SIDE2 hanya untuk FULL …`).

### 6.5 SIDE — ke samping, satu kelompok per layar

```
@DIR=SIDE;COLOR=ORANGE;ALIGN=LEFT;SIZE=2;PAUSE=0.5;TEXT=KELUAR PARKIR\nTarif : Rp 0\nStatus : MEMBER\nNomor Kartu : **** **** **** 968A
```
→ `OK`. 4 layar, 4 jeda, selesai **±12,9 s** (SPEED 33). Dengan `SPEED=100;` → **±5,6 s**.

Cara kerja:
- Setiap baris sumber = satu kelompok = satu layar. `Label : Nilai` = label kecil **di atas** nilai, keduanya
  di tengah tinggi panel. Baris biasa = satu baris di tengah.
- Kelompok **masuk dari kanan → diam PAUSE → keluar ke kiri**, kelompok berikutnya menyusul 16 px di belakangnya.
- Kelompok yang di helvB08 pun lebih lebar dari panel **digeser** sampai ujung kanannya terlihat, lalu diam
  (tidak dipecah).
- Di baris SPLIT (16 px), `Label : Nilai` tampil utuh satu baris.

Sebagian besar waktu habis untuk **gerakan geser**, bukan jeda. Untuk mempercepat, naikkan `SPEED`, atau
padatkan isi supaya lebih sedikit layar dan tidak ada yang perlu digeser, misalnya
`TEXT=Tarif Keluar : Rp 0\nMember : **** 968A` (2 layar, semua muat 64 px).

### 6.6 SIDE2 — ke samping, dua baris per layar (hanya FULL)

```
@DIR=SIDE2;COLOR=ORANGE;ALIGN=LEFT;SIZE=2;PAUSE=0.5;TEXT=KELUAR PARKIR\nTarif : Rp 0\nStatus : MEMBER\nNomor Kartu : **** **** **** 968A
```
→ `OK`. **2 layar**, 2 jeda, selesai **±10,5 s** (SPEED 33).

Cara kerja:
- **Setiap 2 baris sumber = satu layar** dengan dua baris 16 px. Layar terakhir boleh berisi 1 baris.
- `Label : Nilai` = label abu (helvB08) lalu **nilai di kanannya** (jarak 4 px, warna COLOR). Nilai dikecilkan,
  paling besar helvB12, sampai satu baris muat 64 px.
- Layar **masuk dari kanan → baris yang lebar digeser bersamaan** (yang pendek menunggu) **→ diam PAUSE → keluar
  ke kiri**, layar berikutnya menyusul 16 px.

Simulasi frame layar 1 saat diam (`#` = COLOR, `+` = LABELCOLOR):
```
|##..##...###...#####.....#####....###...#####..##..##.##.#####..|  ← "…KELUAR PARKIR" (sudah digeser ke ujung)
|++++++...........++..+++.....##....###...#######.......##....##.|  ← "Tarif" (abu) + "Rp 0" (oranye)
```

Batasan: VAR yang membuat baris lebih lebar **saat layar sedang diam** tidak memicu geser ulang; baris terpotong
sampai layar berikutnya.

### 6.7 SIDE2 + `MOVE=UP` — dua baris per layar, naik seperti credit

```
@DIR=SIDE2;MOVE=UP;COLOR=ORANGE;ALIGN=LEFT;SIZE=2;PAUSE=1;TEXT=KELUAR PARKIR\nTarif : Rp 0\nStatus : MEMBER\nNomor Kartu : **** **** **** 968A
```
→ `OK`. 4 jeda (setiap kali 2 baris memenuhi panel), selesai ±8,9 s.
Dengan `PAUSE=0` → naik terus tanpa henti, ±4,9 s.

Cara kerja: baris 16 px gaya SIDE2 (label + nilai berdampingan) bergulir naik. Baris yang di helvB08 pun tidak
muat dipecah: label di barisnya sendiri, nilai/teks per kata ke baris berikutnya (contoh di atas jadi 8 baris).
`MOVE` **menempel**: SIDE2 berikutnya tetap naik sampai dikirim `@MOVE=SIDE`.

### 6.8 LINES — banyak baris, 2 tampil, bergantian naik

```
@LINES=Kartu : Emoney / Tapcash|Tarif : Rp 5.000|Saldo : Rp 20.000
```
→ `OK LINES=3 WIDE=1,2,3` (ketiga baris lebih lebar dari panel di helvB12). Tiap tampilan (1-2 dan 2-3) diam
di awal dan di ujung geser, selesai ±15,9 s.

Urutan tiap tampilan 2 baris:
1. Diam PAUSE.
2. Baris yang lebih lebar dari 64 px **bergeser ke kiri bersamaan** sampai ujungnya terlihat; yang lebih pendek
   berhenti di ujungnya dan menunggu.
3. **Diam PAUSE lagi di ujung geser** (v8), supaya angka di ujung baris sempat terbaca.
4. **Balik cepat** (4× SPEED, terlihat bergerak) ke posisi awal.
5. **Naik satu baris**: 1-2 → 2-3 → 3-4 …

Langkah 2–4 hanya terjadi bila ada baris yang lebih lebar dari panel.

- ONCE: tampilan terakhir (mis. 2-3) kosong **langsung dari ujung geser** setelah langkah 3, tanpa balik.
  LOOP: melingkar (… 3-1 → 1-2 …).
- 1–2 baris: tidak ada naik; langkah 1–4 tetap berjalan.

Contoh transaksi (2 baris, keduanya lebih lebar dari panel di helvB08):
```
@L:SIZE=1;COLOR=YELLOW;PAUSE=2;LINES=Tarif: Rp.5000|Saldo: Rp. 145000
```
→ `OK LINES=2 WIDE=1,2`. Diam 2 s rata kiri → geser bersamaan (Tarif 6 px, Saldo 25 px; Tarif selesai
lebih dulu lalu menunggu) → diam 2 s di ujung → kosong. Selesai ±4,8 s. Dengan `SPEED=15;` gesernya lebih
pelan (±1,7 s), selesai ±5,7 s.
- Setelan LINES memakai awalan `@L:` (COLOR, SIZE maks 2, SPEED, PAUSE, MODE, ALIGN; bawaan ALIGN LEFT).
  Perintah tanpa awalan yang memuat `LINES=` otomatis ke zona LINES: `@COLOR=YELLOW;SIZE=1;LINES=a|b`.
- LINES **tidak** memakai gaya label. `DIR`, `MOVE`, `LABELCOLOR` ditolak.

**Satu kunci per baris + kode warna (v8)** — cara yang disarankan untuk layar transaksi, cukup **satu kali kirim**:
```
@L:SIZE=1;PAUSE=2;
LINE1=%w%Tarif: %g%Rp.5000
LINE2=%w%Saldo: %y%Rp. 145000
```
→ `Tarif:` / `Saldo:` putih, `Rp.5000` hijau, `Rp. 145000` kuning; warna ikut hurufnya saat diam, geser, dan naik.

Ketiga baris boleh sampai sebagai **tiga perintah terpisah** (terminal yang mengirim CR / CR LF tiap baris, mis.
SSCOM) — balasannya `OK`, `OK LINES=1 WIDE=1`, `OK LINES=2 WIDE=1,2` — atau sebagai **satu perintah** dengan LF
di antaranya (balasan satu `OK LINES=2 WIDE=1,2`). Hasil tampilannya sama. Bisa juga ditulis satu baris:
`@L:SIZE=1;PAUSE=2;LINE1=%w%Tarif: %g%Rp.5000;LINE2=%w%Saldo: %y%Rp. 145000`.

- `LINE1=` … `LINE16=` (alias `BARIS1..16`) = isi baris ke-n. Boleh **tanpa `@`** bila baris diawali `LINEn=`
  (tanpa `@` dan bukan `LINEn=` = teks FULL seperti biasa).
- `LINEn=` mengisi baris n dan **membuang baris sesudahnya**; baris sebelumnya tetap. Jadi `LINE1=` memulai isi
  baru, `LINE2=`, `LINE3=` … menyambung — urut dari `LINE1` agar isi lama tidak terbawa. Baris yang dilewati
  (mis. langsung `LINE3=`) jadi kosong.
- **MODE per blok:** `LINE1=` = blok baru, tampil **sekali (ONCE)**, kecuali `MODE=` ditulis di perintah yang sama
  **atau** di baris setelan `@L:` sebelumnya (`@L:SIZE=1;PAUSE=2;MODE=LOOP;` lalu `LINE1=`…). MODE di baris
  setelan hanya berlaku untuk satu `LINE1=` berikutnya. `LINE2=` dst. tidak mengubah MODE. Contoh:
  ```
  @L:SIZE=1;PAUSE=2;MODE=LOOP;
  LINE1=SILAHKAN
  LINE2=TAP KARTU ANDA
  ```
  → berulang terus. Lalu blok transaksi tanpa MODE (contoh di atas) → tampil sekali lalu kosong; panel tidak
  kembali ke SILAHKAN sendiri — program PC mengirim blok SILAHKAN lagi.
- Nilai `LINEn=` berakhir di `;` atau baris baru, jadi isinya tidak bisa memuat `;` (pakai `LINES=` untuk itu).
  `|` di dalam nilai = pemisah baris.
- `LINEn=` dan `LINES=` tidak bisa dalam satu perintah. Tanpa awalan juga ke zona LINES: `@SIZE=1;LINE1=a`.
- Kode warna dijelaskan di [bagian 7a](#7a-kode-warna-di-teks); berlaku juga untuk `LINES=`, `TEXT=`, dan VAR.

```
@L:MODE=LOOP;PAUSE=1;LINES=SATU|DUA|TIGA
```
→ `OK LINES=3 WIDE=-`. Berputar terus (9 jeda dalam 12 s simulasi).

---

## 7. VAR / template — mengganti nilai tanpa mengirim ulang teks

Teks (`TEXT`, `@1:`/`@2:` TEXT, `LINES`) boleh berisi `{1}` … `{9}`. Isinya dikirim dengan `@VARn=`.

```
@L:COLOR=YELLOW;SIZE=1;PAUSE=1;LINES=Tarif : {1}|Saldo : {2}
```
→ `OK LINES=2 WIDE=-` (VAR masih kosong).
```
@VAR1=Rp 5.000;VAR2=Rp 20.000
```
→ `OK LINES=2 WIDE=1,2` (setelah terisi, kedua baris jadi lebih lebar dari 64 px di helvB08).
```
@VAR2=Rp 15.000
```
→ `OK LINES=2 WIDE=1,2`. Hanya saldo yang berubah.

Cara kerja:
- Mengganti VAR **tidak memulai animasi dari awal**. Batas geser memakai lebar baru. Teks SPLIT yang tadinya diam
  berubah jadi berjalan bila kini lebih lebar dari panel.
- Isi VAR maks 40 karakter, tidak boleh `;` (titik koma mengakhiri nilai), spasi di ujung dibuang, baris baru
  tidak bisa. VAR kosong = teks kosong. `{` yang tidak diikuti angka 1–9 dan `}` tampil apa adanya.
- VAR berlaku untuk semua zona dan **hilang saat listrik mati**.

---

## 7a. Kode warna di teks

Tulis `%huruf%` di dalam teks untuk mengganti warna huruf-huruf sesudahnya (mirip kode warna Minecraft):

| Kode | Warna | | Kode | Warna |
|---|---|---|---|---|
| `%w%` | putih FFFFFF | | `%c%` | toska 00FFFF |
| `%r%` | merah FF0000 | | `%m%` | ungu FF00FF |
| `%g%` | hijau 00FF00 | | `%o%` | oranye FF5C00 |
| `%b%` | biru 0000FF | | `%a%` | abu 808080 |
| `%y%` | kuning FFFF00 | | | |

- Huruf besar/kecil sama (`%G%` = `%g%`). Kode tidak tampil dan tidak ikut lebar teks.
- Warna berlaku sampai kode berikutnya atau **akhir baris**; baris baru mulai lagi dengan `COLOR` (label credit:
  `LABELCOLOR`). Teks sebelum kode pertama juga memakai `COLOR`.
- Berlaku di semua tempat teks: `LINEn=`, `LINES=`, `TEXT=` (FULL/SPLIT, credit, SIDE, SIDE2) dan isi VAR
  (`@VAR1=%r%Rp 5.000` = nilai merah).
- `%` yang bukan kode tampil apa adanya: `Diskon 10%` aman. Hindari menulis `%` + salah satu huruf kode + `%`
  bila maksudnya teks biasa.
- Kode ikut dihitung dalam batas 255 karakter template (3 karakter per kode).

---

## 8. MODE: ONCE / LOOP

- **Setiap `TEXT=` / `LINES=` tampil sekali (ONCE)**, kecuali `MODE=LOOP` ada **di perintah yang sama**:
  `@MODE=LOOP;TEXT=PROMO`.
- `@MODE=LOOP` saja (tanpa teks) = teks yang sedang tampil diulang dari awal secara terus-menerus.
- ONCE selesai → zona kosong. Pengecualian: teks SPLIT yang muat dan **diam** tetap tampil.

---

## 9. ALIGN, SPEED, PAUSE

- `ALIGN` hanya berlaku untuk teks/baris yang **muat** 64 px. Teks yang lebih lebar selalu mulai rata kiri
  lalu digeser.
- `SPEED` = piksel per detik untuk semua gerakan zona itu. 33 = 1 px tiap ±30 ms (sama dengan banner lama).
  Balik cepat LINES = 4× SPEED.
- `PAUSE` = lama diam: credit per kelompok, UP/DOWN di tengah, SIDE/SIDE2 per layar, LINES per tampilan
  (di awal dan, bila ada baris lebar, lagi di ujung geser).
  `PAUSE=0` = tanpa henti.
- Mengganti `TEXT/LINES/SIZE/DIR/MOVE/ALIGN/MODE/LAYOUT` memulai animasi dari awal. `COLOR`, `LABELCOLOR`,
  `SPEED`, `PAUSE` langsung berlaku tanpa mengulang.

Mempercepat tampilan:
```
@L:PAUSE=0.5;SPEED=60;LINES=Kartu : Emoney / Tapcash|Tarif : Rp 5.000|Saldo : Rp 20.000
```

---

## 10. BAUD dan penyimpanan

```
@BAUD=9600
```
→ `OK` (dikirim dengan baud lama), lalu UART0 pindah ke 9600. USB tidak terpengaruh, jadi kalau baud UART lupa,
kembalikan lewat USB: `@BAUD=115200`.

- Disimpan ke flash (LittleFS, file `/state.bin`) **5 detik** setelah perubahan terakhir, hanya bila berbeda dari
  yang tersimpan. Penulisan atomik (file sementara lalu rename) dan dicek CRC32; isi rusak diabaikan
  (kembali 115200).
- Setelan tampilan **tidak** disimpan (lihat bagian 3).

---

## 11. `@STATUS` dan `@HELP`

Contoh `@STATUS` setelah contoh VAR di bagian 7 (hasil simulasi):
```
LAYOUT=LINES BAUD=115200 SAVES=0 LAST_SAVE_MS=0 REFRESH_HZ=0
FULL: TEXT="" COLOR=FFFFFF LABELCOLOR=808080 SIZE=3(helvB22) DIR=CREDIT MOVE=SIDE ALIGN=CENTER MODE=ONCE SPEED=33 PAUSE=2.000
1: TEXT="" COLOR=FF0000 LABELCOLOR=808080 SIZE=2(helvB12) DIR=LEFT MOVE=SIDE ALIGN=CENTER MODE=ONCE SPEED=33 PAUSE=2.000
2: TEXT="" COLOR=0000FF LABELCOLOR=808080 SIZE=2(helvB12) DIR=LEFT MOVE=SIDE ALIGN=CENTER MODE=ONCE SPEED=33 PAUSE=2.000
L: LINES="Tarif : {1}|Saldo : {2}" COLOR=FFFF00 SIZE=1(helvB08) ALIGN=LEFT MODE=ONCE SPEED=33 PAUSE=1.000
VAR: 1="Rp 5.000" 2="Rp 15.000"
OK
```
- Baris pertama: layout, baud UART, jumlah simpan ke flash, lama simpan terakhir (ms), dan `REFRESH_HZ`
  (frame panel per detik; di perangkat terukur 179–180, simulasi PC 0).
- Satu baris per zona; baris baru di teks ditulis `\n` (isi dari `LINEn=` juga tampil sebagai `\n`). `VAR: -` bila
  semua kosong. Kode warna ditampilkan apa adanya.

`@HELP` mengirim ringkasan perintah (±25 baris) diakhiri `OK`.

---

## 12. Daftar error (hasil simulasi)

| Perintah | Balasan |
|---|---|
| `@1:SIZE=3` | `ERR SIZE 3 terlalu besar untuk baris 16 px (SPLIT/LINES maks 2)` |
| `@BAUD=119200` | `ERR BAUD harus 9600, 19200, 38400, 57600, atau 115200: 119200` |
| `@SPEED=999` | `ERR SPEED harus 1..200 (px/detik): 999` |
| `@PAUSE=61` | `ERR PAUSE harus 0..60 detik: 61` |
| `@FOO=1` | `ERR perintah tidak dikenal: FOO (ketik @HELP)` |
| `@COLOR=12345` | `ERR COLOR tidak dikenal: 12345 (nama warna atau RRGGBB)` |
| `@1:DIR=SIDE2` | `ERR DIR=SIDE2 hanya untuk FULL (2 baris tidak muat di baris SPLIT)` |
| `@L:TEXT=x` | `ERR TEXT tidak berlaku untuk @L:, pakai LINES=` |
| `@1:LINES=a\|b` | `ERR LINES tidak bisa dengan awalan @1:/@2:` |
| `@L:DIR=UP` | `ERR DIR tidak berlaku untuk LINES` |
| `@1:LINE1=x` | `ERR LINE1 tidak bisa dengan awalan @1:/@2:` |
| `@L:LINE1=a;LINES=b` | `ERR LINES= dan LINEn= tidak bisa dalam satu perintah` |
| `@L:LINE17=x` | `ERR perintah tidak dikenal: LINE17 (ketik @HELP)` |
| `LINEn=` digabung > 255 karakter | `ERR LINES terlalu panjang (maks 255 karakter)` |
| `@VAR1=` (41 karakter) | `ERR VAR1 maks 40 karakter` |
| `@MODE=KADANG` | `ERR MODE tidak dikenal: KADANG (LOOP atau ONCE)` |
| `@ALIGN=ATAS` | `ERR ALIGN tidak dikenal: ATAS (LEFT, CENTER, RIGHT)` |
| `@LAYOUT=X` | `ERR LAYOUT tidak dikenal: X (FULL, SPLIT, LINES)` |
| `@MOVE=DOWN` | `ERR MOVE tidak dikenal: DOWN (SIDE atau UP)` |
| `@SIZE` (tanpa nilai) | `ERR SIZE butuh nilai (tulis KUNCI=nilai)` |
| `@LINES=1\|2\|…\|17` | `ERR maks 16 baris` |
| Teks > 255 karakter | `ERR TEXT terlalu panjang (maks 255 karakter)` / `ERR LINES terlalu panjang …` |
| Teks setelah VAR > 511 | `ERR teks terlalu panjang setelah VAR diisi (maks 511)` |
| Credit > 64 baris tampil | `ERR credit terlalu panjang (maks 64 baris tampil)` |
| SIDE2 naik > 64 potongan | `ERR SIDE2 naik terlalu panjang (maks 64 potongan baris)` |

---

## 13. Contoh pengiriman dari program PC

> Contoh ini **belum diuji** ke perangkat; hanya menunjukkan pola: tulis perintah + CR, lalu baca balasan sampai
> baris `OK…` atau `ERR…`.

```python
import serial  # pip install pyserial

pico = serial.Serial("COM21", 115200, timeout=1)  # sesuaikan port; baud USB tidak berpengaruh
pico.dtr = True                                   # balasan USB hanya keluar bila DTR aktif

def kirim(perintah):
    pico.write((perintah + "\r").encode("ascii", "replace"))
    balasan = []
    while True:
        baris = pico.readline().decode("ascii", "replace").strip()
        if not baris:
            raise TimeoutError("tidak ada balasan")
        balasan.append(baris)
        if baris.startswith(("OK", "ERR")):
            return balasan

# Template sekali di awal, lalu hanya nilai tiap transaksi:
kirim("@L:COLOR=YELLOW;SIZE=1;PAUSE=1;LINES=Tarif : {1}|Saldo : {2}")
kirim("@VAR1=Rp 5.000;VAR2=Rp 20.000")
```

Karena semua setelan kembali bawaan setiap kali Pico menyala, kirim ulang template dan setelan setelah port
dibuka.

---

## 14. Implementasi firmware (ringkas)

| Bagian | Isi |
|---|---|
| `running-text.ino` | **Core 1**: hanya refresh panel (Adafruit Protomatter, interrupt `PWM_IRQ_WRAP` prioritas tertinggi, supaya tidak berkedip oleh interrupt USB). **Core 0**: baca serial USB + UART0 per baris (diakhiri CR, maks 320), panggil `handleLine`, majukan animasi `update(micros())`, gambar ulang bila berubah, simpan BAUD, ukur `REFRESH_HZ` |
| `src/RunningText.*` | Logika tanpa ketergantungan hardware (bisa diuji di PC): parsing perintah, 4 zona (FULL, atas, bawah, LINES), animasi berbasis fase, render ke bitmap 1-bit per zona |
| `src/fonts/*.h` | Font GFX hasil `tools/bdf2gfx.py` dari `fonts-src/*.bdf` — jangan diedit manual |

- **Parsing atomik:** setelan semua zona + VAR disalin ke struktur sementara, setiap `KUNCI=nilai` diterapkan di
  salinan, lalu teks (setelah VAR diisi) dicoba di-layout untuk semua zona. Hanya kalau semuanya lolos, salinan
  dipakai.
- **Animasi:** tiap zona punya fase (mis. `Roll`/`RollPause` untuk credit, `SideEnter/SidePan/SidePause/SideMove`
  untuk SIDE/SIDE2, `LinesPause/LinesShift/LinesReturn/LinesScroll` untuk LINES). Gerak = 1 px per langkah,
  interval `1 / SPEED` detik. Kalau loop sempat tertunda lebih dari 8 langkah, animasi lanjut tanpa melompat.
- **Dua warna per zona:** `buf` (COLOR) dan `bufAlt` (LABELCOLOR, untuk label) digambar berlapis.
  Kode warna (v8): `expandText` membuang `%x%` dari teks tampil dan mencatat kode tiap huruf di `shownColor`;
  saat menggambar, kode itu ditulis ke `cbuf` (satu byte per pixel). Bila zona memuat kode (`colorCodes`),
  `draw()` di .ino menggambar per pixel: kode ≠ 0 → `COLOR_CODE_RGB`, kode 0 → COLOR / LABELCOLOR.
- **Posisi vertikal:** huruf kapital tepat di tengah zona/baris (`baseline = (tinggi − kapital)/2 + kapital`).

Build ulang:
```bash
cd running-text
arduino-cli compile --fqbn rp2040:rp2040:rpipico:flash=2097152_1048576 --output-dir build .
```
Flash: tahan BOOTSEL saat colok USB, lalu salin `running-text.ino.uf2` ke drive `RPI-RP2`.
Kembali ke program CircuitPython lama: flash UF2 CircuitPython lalu salin isi `../circuitpy-backup/` ke CIRCUITPY.

---

## 15. Belum terverifikasi di panel

- Tampilan v7/v8 di panel asli: credit, SIDE, SIDE2 (ke samping & `MOVE=UP`), LINES (termasuk diam di ujung
  geser v8), kecepatan nyata.
- UART GP0/GP1 dan pergantian `@BAUD` dengan perangkat pengirim sungguhan.
- BAUD terbaca lagi setelah listrik mati.
- Contoh program PC di bagian 13.
