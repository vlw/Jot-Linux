// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — EarconPlayer.
#pragma once

#include <QHash>
#include <QObject>

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
    QHash<int, QSoundEffect *> m_effects;
};

} // namespace jot
