// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>
#include <optional>

namespace jot {

/// ~/.local/share/Jot/{recordings/<yyyyMMdd-HHmmss-xxxxxxxx>/{audio.wav,meta.json}, history.sqlite, dictionary.json}
///
/// Audio is 16 kHz mono s16le. The Linux port stores it as WAV (the macOS build
/// used CAF). The header is written up front and patched as frames land, so a
/// crash leaves a file whose PCM is still recoverable from byte 44 to EOF.
namespace FileLayout {
QString appDataRoot();
QString recordingsRoot();
bool ensureDir(const QString &path);
QString makeSessionFolder(const QUuid &id, const QDateTime &now);
inline QString audioFile(const QString &folder) { return folder + "/audio.wav"; }
inline QString audioFLAC(const QString &folder) { return folder + "/audio.flac"; }
inline QString metaJSON(const QString &folder) { return folder + "/meta.json"; }
std::optional<double> estimatedDuration(const QString &wavPath);
constexpr int sampleRate = 16000;
constexpr int bytesPerSecond = 32000;
constexpr int wavHeaderBytes = 44;
}

} // namespace jot
