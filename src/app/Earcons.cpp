// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — EarconPlayer.
#include "Earcons.h"
#include "../core/Settings.h"

#include <QAudioDevice>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QSoundEffect>
#include <QUrl>

namespace jot {

Q_LOGGING_CATEGORY(lcEarcons, "jot.earcons")

Earcons &Earcons::instance()
{
    static Earcons e;
    return e;
}

void Earcons::preload()
{
    // A QSoundEffect stays bound to the output it was created for. At login Jot starts while
    // PipeWire still only offers its dummy sink, so reload whenever the default output changes.
    m_devices = new QMediaDevices(this);
    connect(m_devices, &QMediaDevices::audioOutputsChanged, this, [this] {
        if (QMediaDevices::defaultAudioOutput().id() != m_outputId) load();
    });
    load();
}

void Earcons::load()
{
    qDeleteAll(m_effects);
    m_effects.clear();
    const QAudioDevice output = QMediaDevices::defaultAudioOutput();
    m_outputId = output.id();
    qCInfo(lcEarcons) << "sounds use output" << (output.isNull() ? QStringLiteral("(none)") : output.description());
    const QList<QPair<Sound, QString>> files = {
        {Sound::Start, "start"}, {Sound::Stop, "stop"}, {Sound::Lock, "lock"}, {Sound::Success, "success"},
        {Sound::Cancel, "cancel"}, {Sound::Error, "error"}, {Sound::Celebration, "celebration"},
    };
    for (const auto &[sound, name] : files) {
        auto *fx = new QSoundEffect(output, this);
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
