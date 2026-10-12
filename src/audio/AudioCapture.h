// Linux capture engine (replaces upstream AVCapture-based AudioCaptureEngine).
#pragma once

#include <QAudioDevice>
#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QThread>
#include <functional>
#include <memory>
#include <optional>

class QAudioSource;
class QIODevice;
class QMediaDevices;

namespace jot {

struct AudioCaptureResult {
    qint64 framesWritten = 0;
    double durationSeconds = 0;
    QList<double> gapMarkers;
    float peakLevel = 1.0f;
    bool peakIsTrustworthy = true;
};

/// Resolves which physical microphone Jot records from.
///
/// The user picks a microphone once (default: the system default input at first
/// run) and Jot sticks to it — a change of the system default input (headset,
/// Bluetooth) never reroutes capture, and there is no silent fallback to another
/// mic. The Pulse/PipeWire source name encodes the USB port, so after a port
/// change it is re-resolved by its unique description.
class MicrophoneManager : public QObject {
    Q_OBJECT
public:
    static MicrophoneManager &instance();
    QList<QAudioDevice> inputs() const;
    /// The device to record from, or nullopt when the chosen mic is absent.
    std::optional<QAudioDevice> recordingDevice() const;
    QString selectedDisplayName() const;
    bool followsSystemDefault() const;
    void select(const QAudioDevice &device);
    void selectSystemDefault();
    /// First run: pin the current system default input.
    void ensureInitialSelection();
    /// System (PipeWire/Pulse) input volume of the recording device, in percent; -1 if unknown.
    int systemVolume() const;
    bool setSystemVolume(int percent);

signals:
    void availabilityChanged();

private:
    MicrophoneManager();
    QMediaDevices *m_devices;
    bool m_lastAvailable = false;
};

class CaptureWorker;

/// One capture session. start() → stop(); never reused.
class AudioCapture : public QObject {
    Q_OBJECT
public:
    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    enum class StartError { None, NoInputDevice, Failed };
    /// Opens the device and starts writing the WAV. Blocks only for the device open.
    StartError start(const QString &wavPath, std::function<void(const QByteArray &)> pcmSink = {});
    /// Captures a short tail, closes the file and reports what was written.
    void stop(std::function<void(AudioCaptureResult)> cb);
    bool isRunning() const { return m_running; }

signals:
    void level(float level);
    void deviceChange(const QString &message);
    void writeFailure();
    void engineDied(const QString &message);
    void captureActivityChanged(bool active);

private:
    QThread m_thread;
    CaptureWorker *m_worker = nullptr;
    bool m_running = false;
    bool m_stopping = false;
};

} // namespace jot
