# Jot для Linux (Qt 6.12, KDE Plasma / Wayland)

Порт [Jot](https://github.com/google-gemini/jot-gemini-transcribe-macOS) — диктовки через Gemini 3.5 Transcribe — на Linux.
**Удерживаешь клавишу, говоришь, отпускаешь — текст появляется там, где стоит курсор.**

Это не официальный продукт Google. Лицензия Apache-2.0 (см. `LICENSE`, `THIRD_PARTY_NOTICES.md`).

## Установка

```bash
./scripts/build.sh                     # собирает layer-shell-qt под Qt 6.12 и сам Jot
./scripts/install.sh                   # ставит в ~/.local/opt/jot-linux, ярлык в меню, jot:// ссылки
sudo ./scripts/setup-permissions.sh    # один раз: доступ к клавиатуре для глобальной клавиши
```

Потом запусти **Jot** из меню приложений (или `jot-linux`). При первом запуске откроется онбординг:
ключ Gemini → проверка микрофона → доступ к клавиатуре → выбор клавиши → жесты → «Try it».

## AppImage и проверка сборки

GitHub Actions собирает AppImage для x86_64 на каждом push в `main`, для тегов `v*`
и pull request. Готовый файл и SHA-256 доступны в артефактах workflow **Verified AppImage**.
При push тега `v*` после успешной проверки сборки AppImage автоматически публикуется
в GitHub Releases вместе с SHA-256 и файлом `.sigstore.json`. Уже опубликованные
релизы при повторном запуске сохраняются. Для публикации создай и отправь тег
на коммит с этим workflow.

Распакуй скачанный ZIP из Actions или скачай файлы релиза, затем:

```bash
sha256sum --check Jot-Linux-*.AppImage.sha256
chmod +x Jot-Linux-*.AppImage
./Jot-Linux-*.AppImage
```

Сборки из `main` и тегов получают подписанное GitHub подтверждение происхождения:

```bash
gh attestation verify Jot-Linux-*.AppImage --repo vlw/Jot-Linux \
  --signer-workflow vlw/Jot-Linux/.github/workflows/appimage.yml
```

CI проверяет зависимости внутри пакета и загрузку интерфейса из AppImage.
Подтверждение связывает файл с коммитом и workflow; полный сценарий диктовки
требует проверки на KDE Plasma / Wayland с микрофоном и API-ключом.
Для глобальной клавиши по-прежнему требуется настройка доступа к клавиатуре:
`sudo ./scripts/setup-permissions.sh` из исходников.

## Язык интерфейса

Settings → General → **Язык интерфейса**: «Как в системе» (по умолчанию; при `LANG=ru_RU` — русский),
English или Русский. Переключается на лету, без перезапуска: окна, пилюля, меню трея, уведомления.
Перевод лежит в `i18n/ru.json`; после изменения строк в коде — `./scripts/update-translations.sh`.

## Жесты (как на macOS)

| Жест | Что происходит |
| --- | --- |
| **Удерживать клавишу** (по умолчанию Right Ctrl) | Запись, пока держишь. Отпустил — текст вставлен. |
| **Клавиша + Space** | Hands-free: запись продолжается после отпускания. Нажми клавишу ещё раз, чтобы закончить. |
| **Esc** | Отмена. Записи длиннее 10 секунд остаются в History. |

Клавиша меняется в Settings → General (любая клавиша, включая одиночный модификатор).

## Чем Linux-версия отличается внутри

| macOS | Linux |
| --- | --- |
| CGEventTap (fn) | evdev: чтение `/dev/input/event*` (uaccess-правило udev). Фолбэк — XDG GlobalShortcuts portal |
| AX API + ⌘V | Буфер обмена через `ext_data_control_v1` + Ctrl+V через KWin `org_kde_kwin_fake_input` (Ctrl+Shift+V в терминалах) |
| NSPanel-пилюля | wlr-layer-shell overlay (layer-shell-qt), без фокуса, клики проходят насквозь |
| AVCaptureDeviceInput | Qt Multimedia (PipeWire); выбранный микрофон запоминается по id, после смены USB-порта находится по названию; без тихого фолбэка на другой микрофон |
| Keychain | Secret Service (KWallet) |
| CAF | WAV 16 кГц mono; заголовок обновляется раз в секунду — после краша запись восстанавливается |
| frontmost app / bundle id | KWin-скрипт сообщает активное окно по D-Bus (тон текста, защита от вставки в другое окно) |
| Media pause (браузеры/Music/media-control) | MPRIS: пауза только того, что играло, возобновление только того, что поставил на паузу Jot |
| Login item | `~/.config/autostart/jot-linux.desktop` |

Всё остальное перенесено один в один: Gemini interactions (smart) / legacy endpoint, Live-стриминг по
WebSocket со сверкой байтов и фолбэком на загрузку, 10 ретраев, опциональная очистка текста с
validation gate и автоотключением, словарь (custom vocabulary + детерминированные замены, CSV),
MAI-Transcribe-2 через OpenRouter, история (SQLite) с Retry, очередь офлайн-диктовок, восстановление
после краша, ретеншн аудио, звуки, тексты ошибок.

## Данные

- Настройки: `~/.config/jot-linux/Jot.ini`
- История, записи, словарь: `~/.local/share/Jot/`
- Ключи API: KWallet (Secret Service), никогда не пишутся в файлы

## Полезное

```bash
xdg-open "jot://settings/advanced"   # general | dictation | privacy | advanced | about
xdg-open "jot://history"             # jot://dictionary, jot://onboarding/2, jot://start-hands-free
jot-linux --list-microphones
QT_LOGGING_RULES="jot.*=true" jot-linux   # подробные логи
```

## Ограничения Wayland

- Space и Esc во время диктовки доходят и до активного окна (evdev только наблюдает, не перехватывает).
- Поля паролей на Wayland не определяются; текст всегда кладётся в буфер обмена, вставка — Ctrl+V.
