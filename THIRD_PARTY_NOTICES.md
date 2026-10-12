# Third-party notices

## Jot (upstream)
This project is a Linux port of Jot, Copyright 2026 Google LLC, licensed under the
Apache License 2.0 (see `LICENSE`). Source files derived from it carry the
"Port of Jot" header. This is not an officially supported Google product.

## Fonts
- **Google Sans Flex**, **Google Sans Code** — SIL Open Font License 1.1
  (`resources/fonts/OFL-*.txt`).

## Sounds
- Earcons in `resources/sounds/` are original works from Jot, Apache License 2.0.

## Icons
- Icons in `resources/icons/breeze/` are from KDE Breeze Icons,
  LGPL-3.0-or-later (`resources/icons/breeze/LICENSE.txt`).

## Libraries (linked, not vendored)
- **Qt 6** — LGPL-3.0 (https://www.qt.io/licensing)
- **layer-shell-qt** (KDE) — LGPL-3.0-or-later; fetched and built by `scripts/build.sh`
- **libFLAC** — BSD-3-Clause
- **libevdev** — MIT
- **libwayland-client** — MIT
- Wayland protocol files in `protocols/`: `ext-data-control-v1.xml` (wayland-protocols, MIT),
  `fake-input.xml` (plasma-wayland-protocols, LGPL-2.1-or-later)
