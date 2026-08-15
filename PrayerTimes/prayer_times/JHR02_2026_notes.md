# Source: Waktu Solat Johor Bharu — 2026

## Current source of truth: JHR02_2026_full.csv (full year, 365 rows)

- **Zone**: JHR02 — Johor Bahru, Kota Tinggi, Mersing, Kulai, and areas sharing the same prayer times.
- **Source**: [JAKIM e-Solat portal](https://www.e-solat.gov.my/index.php?siteId=24&pageId=24) (official Malaysian government source, Jabatan Kemajuan Islam Malaysia). Site is dynamic/JS-driven (zone selector, date-range picker, PDF/CSV/XML export) — the user selected JHR02, "Tahunan" (yearly) search, and printed the results page to PDF (`Portal e-Solat.pdf`, 37 pages, saved 2026-08-07) since the site "doesn't save well" via direct fetch/download.
- **Pipeline**: `Portal e-Solat.pdf` (source, kept in the sketch root one level up) → manually transcribed to `JHR02_2026_full_raw.txt` (raw `DD-Mon-YYYY,hijri,day_name,H:MM am/pm × 7` format, matching the PDF's on-screen 12h format) → `convert_full_year.py` → `JHR02_2026_full.csv` (24h HH:MM, ISO dates, sanity-checked: exactly 365 rows, Jan 1 – Dec 31, no gaps/duplicates) → `csv_to_c_array.py` → `prayer_times_data.h` (embedded in firmware).
- Columns: Imsak, Subuh, Syuruk, Zuhur, Asar, Maghrib, Isyak (24h HH:MM, local Malaysia time / GMT+8).
- Hijri date and Malay day name included per row (Ahad=Sun, Isnin=Mon, Selasa=Tue, Rabu=Wed, Khamis=Thu, Jumaat=Fri, Sabtu=Sat).
- Cross-checked: 2026-08-01 values match exactly between `JHR02_2026_full.csv` and the earlier `JHR02_2026-08.csv` (myrakan.com source) — `05:44,05:54,07:06,13:14,16:35,19:18,20:31`. One transcription slip caught and fixed during manual entry: Dec 31's Hijri date was mis-copied as `21-Dis-2026` instead of the correct `22-Rej-1448` (continuing the Hijri sequence from Dec 30's `21-Rej-1448`).
- `csv_to_c_array.py` takes `JHR02_2026_full.csv` as its only input, and errors out if it's missing.

## Earlier partial source (superseded)

Before the full-year JAKIM export was obtained, two months (Aug + Sep 2026) were captured from myrakan.com/waktusolat as PDF exports, since the site was unreachable from the dev sandbox directly. They served only to cross-check the transcription — 2026-08-01 matched the JAKIM data exactly (see above) — and were never firmware input.

Superseded by the full-year export and removed (2026-08-15); the per-month fallback path in `csv_to_c_array.py` was dropped with them. Public holidays that fell in those months, for reference: 25 Aug (Maulidur Rasul), 31 Aug (Hari Kebangsaan), 16 Sep (Hari Malaysia).

## Coverage status

**Full year now loaded** — `prayer_times_data.h` has all 365 days of 2026 (`PRAYER_DOY[i] == i+1` for every row, i.e. a direct day-of-year index, no gaps). The firmware's day-of-year lookup was simplified accordingly (`find_prayer_row_for_doy()` is now a direct array index with bounds checking, not a linear search) while keeping the "no data" fallback path as a defensive guard for out-of-range days (e.g. day 366 in a future leap year, since this table is 365 rows).

**Known limitation**: prayer times are computed from solar position and do shift slightly year to year for the same calendar date (leap-year drift, orbital mechanics) — this table is specifically JAKIM's 2026 calculation, reused indefinitely by day-of-year. That's a reasonable approximation for a clock/display like this, not a claim of exact accuracy for years other than 2026. If genuine precision matters later, the table should be regenerated per-year from a fresh JAKIM export.
