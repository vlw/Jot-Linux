// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — EarconPlayer.
#include "Earcons.h"
#include "../core/Settings.h"

#include <QSoundEffect>
#include <QUrl>

namespace jot {

Earcons &Earcons::instance()
{
    static Earcons e;
    return e;
}

void Earcons::preload()
{
    const QList<QPair<Sound, QString>> files = {
        {Sound::Start, "start"}, {Sound::Stop, "stop"}, {Sound::Lock, "lock"}, {Sound::Success, "success"},
        {Sound::Cancel, "cancel"}, {Sound::Error, "error"}, {Sound::Celebration, "celebration"},
    };
    for (const auto &[sound, name] : files) {
        auto *fx = new QSoundEffect(this);
        fx->setSource(QUrl("qrc:/sounds/" + name + ".wav"));
        fx->setVolume(0.9f);
        m_effects.insert(int(sound), fx);
    }
}

void Earcons::play(Sound s)
{
    if (!Settings::instance().soundsEnabled()) return;
    if (auto *fx = m_effects.value(int(s))) {
        fx->stop();
        fx->play();
    }
}

} // namespace jot
