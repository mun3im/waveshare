#!/usr/bin/env python3
"""
Convert the raw JAKIM e-Solat transcription (JHR02_2026_full_raw.txt) into
JHR02_2026_full.csv, matching the schema of the existing per-month CSVs:
date,hijri,day_name,imsak,subuh,syuruk,zuhur,asar,maghrib,isyak (24h HH:MM).

Also sanity-checks: exactly 365 rows, dates strictly sequential with no gaps
or duplicates, covering 2026-01-01 through 2026-12-31.
"""
import csv
import datetime
import os
import re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
IN_PATH = os.path.join(SCRIPT_DIR, "JHR02_2026_full_raw.txt")
OUT_PATH = os.path.join(SCRIPT_DIR, "JHR02_2026_full.csv")

MONTH_MAP = {
    "Jan": 1, "Feb": 2, "Mac": 3, "Apr": 4, "Mei": 5, "Jun": 6,
    "Jul": 7, "Aug": 8, "Sep": 9, "Oct": 10, "Nov": 11, "Dis": 12,
}


def to24(t: str) -> str:
    t = t.strip()
    m = re.match(r"(\d+):(\d+)\s*(am|pm)", t)
    h, mi, ap = int(m.group(1)), int(m.group(2)), m.group(3)
    if ap == "pm" and h != 12:
        h += 12
    if ap == "am" and h == 12:
        h = 0
    return f"{h:02d}:{mi:02d}"


def parse_date(s: str) -> datetime.date:
    dd, mon, yyyy = s.split("-")
    return datetime.date(int(yyyy), MONTH_MAP[mon], int(dd))


def main():
    rows = []
    with open(IN_PATH, newline="") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = [p.strip() for p in line.split(",")]
            date_str, hijri, day_name = parts[0], parts[1], parts[2]
            times = parts[3:10]  # imsak,subuh,syuruk,zuhur,asar,maghrib,isyak
            date = parse_date(date_str)
            times24 = [to24(t) for t in times]
            rows.append((date, hijri, day_name, times24))

    rows.sort(key=lambda r: r[0])

    # Sanity checks
    n = len(rows)
    assert n == 365, f"expected 365 rows, got {n}"
    expected = datetime.date(2026, 1, 1)
    for date, *_ in rows:
        assert date == expected, f"gap/duplicate: expected {expected}, got {date}"
        expected += datetime.timedelta(days=1)
    assert expected == datetime.date(2027, 1, 1), "did not reach end of year"

    with open(OUT_PATH, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["date", "hijri", "day_name", "imsak", "subuh", "syuruk",
                          "zuhur", "asar", "maghrib", "isyak"])
        for date, hijri, day_name, times24 in rows:
            writer.writerow([date.isoformat(), hijri, day_name] + times24)

    print(f"Wrote {OUT_PATH}: {n} rows, {date.isoformat()} last date, checks passed.")


if __name__ == "__main__":
    main()
