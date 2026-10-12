#!/usr/bin/env bash
# Re-scan sources for UI strings and refill the Russian translation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_PREFIX="${QT_PREFIX:-$HOME/Qt/6.12.0/gcc_64}"
cd "$ROOT"
"$QT_PREFIX/bin/lupdate" -locations none -no-obsolete src resources/qml -ts i18n/jot_ru.ts
python3 scripts/fill_ts.py i18n/jot_ru.ts i18n/ru.json
