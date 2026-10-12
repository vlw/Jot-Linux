// Integrated media pause over MPRIS (replaces upstream's browser JS / Music /
// media-control coordinators). Same ownership rules:
//  * pause only for actual capture; only confirmed pauses grant ownership;
//  * resume only what Jot paused, while it is still the same item and still paused;
//  * manual playback or a changed item relinquishes ownership;
//  * a new capture within 1 s of the last keeps ownership (no second pause);
//  * quit resumes owned media at once; sleep abandons automatic resume.
// All D-Bus traffic runs on a worker thread so audio never waits on a player.
#pragma once

#include <QObject>
#include <QThread>

namespace jot {

class MediaWorker;

class MediaPause : public QObject {
    Q_OBJECT
public:
    static MediaPause &instance();
    void captureActivityChanged(bool active);
    void finish();          // quit: resume owned media now, wait briefly
    void systemSleeping();  // abandon automatic resume

private:
    MediaPause();
    ~MediaPause() override;
    QThread m_thread;
    MediaWorker *m_worker;
};

} // namespace jot
