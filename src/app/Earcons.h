// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — EarconPlayer.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>

class QMediaDevices;
class QSoundEffect;

namespace jot {

class Earcons : public QObject {
    Q_OBJECT
public:
    enum class Sound { Start, Stop, Lock, Success, Cancel, Error, Celebration };
    static Earcons &instance();
    void preload();
    void play(Sound s);

private:
    Earcons() = default;
    void load();
    QHash<int, QSoundEffect *> m_effects;
    QMediaDevices *m_devices = nullptr;
    QByteArray m_outputId;
};

} // namespace jot
