// Linux capture engine (replaces upstream AVCapture-based AudioCaptureEngine).
#include "AudioCapture.h"
#include "../core/FileLayout.h"
#include "../core/Settings.h"
#include "../core/TextPipeline.h"

#include <QCoreApplication>
#include <QAudioFormat>
#include <QAudioSource>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <atomic>
#include <QtEndian>
#include <cmath>

Q_LOGGING_CATEGORY(lcAudio, "jot.audio")

namespace jot {

static void refreshGain();

// ================================================================ Microphones

MicrophoneManager &MicrophoneManager::instance()
{
    static MicrophoneManager m;
    return m;
}

MicrophoneManager::MicrophoneManager()
    : m_devices(new QMediaDevices(this))
{
    m_lastAvailable = recordingDevice().has_value();
    connect(m_devices, &QMediaDevices::audioInputsChanged, this, [this] {
        const bool available = recordingDevice().has_value();
        if (available != m_lastAvailable) {
            m_lastAvailable = available;
            qCInfo(lcAudio) << "selected microphone" << (available ? "connected" : "disconnected");
        }
        emit availabilityChanged();
    });
    refreshGain();
    connect(&Settings::instance(), &Settings::changed, this, [this](const QString &key) {
        if (key.startsWith("microphone")) emit availabilityChanged();
        if (key == "micGainDb") refreshGain();
    });
}

QList<QAudioDevice> MicrophoneManager::inputs() const
{
    QList<QAudioDevice> out;
    for (const auto &d : QMediaDevices::audioInputs()) {
        // Monitors of output sinks are not microphones.
        if (QString::fromUtf8(d.id()).endsWith(".monitor")) continue;
        out << d;
    }
    return out;
}

bool MicrophoneManager::followsSystemDefault() const
{
    return Settings::instance().microphoneFollowsSystem();
}

std::optional<QAudioDevice> MicrophoneManager::recordingDevice() const
{
    auto &s = Settings::instance();
    if (s.microphoneFollowsSystem()) {
        const QAudioDevice d = QMediaDevices::defaultAudioInput();
        if (d.isNull()) return std::nullopt;
        return d;
    }
    const QString id = s.microphoneId();
    const QString name = s.microphoneName();
    if (id.isEmpty() && name.isEmpty()) return std::nullopt;
    const auto list = inputs();
    for (const auto &d : list)
        if (QString::fromUtf8(d.id()) == id) return d;
    // Port changed: re-resolve by the same UNIQUE description.
    std::optional<QAudioDevice> match;
    int count = 0;
    for (const auto &d : list)
        if (!name.isEmpty() && d.description() == name) { match = d; ++count; }
    if (count == 1) {
        const_cast<Settings &>(s).setMicrophone(QString::fromUtf8(match->id()), match->description());
        return match;
    }
    return std::nullopt;
}

QString MicrophoneManager::selectedDisplayName() const
{
    if (followsSystemDefault()) return QCoreApplication::translate("Jot", "System default input");
    const QString name = Settings::instance().microphoneName();
    return name.isEmpty() ? QCoreApplication::translate("Jot", "No microphone selected") : name;
}

void MicrophoneManager::select(const QAudioDevice &device)
{
    auto &s = Settings::instance();
    s.setMicrophoneFollowsSystem(false);
    s.setMicrophone(QString::fromUtf8(device.id()), device.description());
}

void MicrophoneManager::selectSystemDefault()
{
    Settings::instance().setMicrophoneFollowsSystem(true);
}

void MicrophoneManager::ensureInitialSelection()
{
    auto &s = Settings::instance();
    if (s.microphoneFollowsSystem() || !s.microphoneId().isEmpty()) return;
    const QAudioDevice def = QMediaDevices::defaultAudioInput();
    if (!def.isNull()) select(def);
}

static QString pulseSourceName()
{
    auto &m = MicrophoneManager::instance();
    if (m.followsSystemDefault()) return QStringLiteral("@DEFAULT_SOURCE@");
    const auto dev = m.recordingDevice();
    return dev ? QString::fromUtf8(dev->id()) : QString();
}

static QString runPactl(const QStringList &args)
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    p.setProcessEnvironment(env);
    p.start("pactl", args);
    if (!p.waitForFinished(1500)) { p.kill(); return {}; }
    return p.exitCode() == 0 ? QString::fromUtf8(p.readAllStandardOutput()) : QString();
}

int MicrophoneManager::systemVolume() const
{
    const QString source = pulseSourceName();
    if (source.isEmpty()) return -1;
    static const QRegularExpression re("(\\d+)%");
    const auto m = re.match(runPactl({"get-source-volume", source}));
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

bool MicrophoneManager::setSystemVolume(int percent)
{
    const QString source = pulseSourceName();
    if (source.isEmpty()) return false;
    QProcess::startDetached("pactl", {"set-source-volume", source, QString::number(std::clamp(percent, 0, 150)) + "%"});
    return true;
}

// Software gain, read on the capture thread for every buffer.
static std::atomic<float> g_gain{1.0f};
static void refreshGain() { g_gain.store(float(std::pow(10.0, Settings::instance().micGainDb() / 20.0))); }

/// Linear gain with a soft knee above 0.9 FS, so boosted speech never hard-clips.
static void applyGain(QByteArray &data, float gain)
{
    if (gain <= 1.0001f) return;
    auto *s = reinterpret_cast<qint16 *>(data.data());
    const qsizetype n = data.size() / 2;
    for (qsizetype i = 0; i < n; ++i) {
        float x = s[i] / 32768.0f * gain;
        const float a = std::fabs(x);
        if (a > 0.9f) x = std::copysign(0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f), x);
        s[i] = qint16(std::lround(std::clamp(x, -1.0f, 32767.0f / 32768.0f) * 32768.0f));
    }
}

// ================================================================ Worker

class CaptureWorker : public QObject {
    Q_OBJECT
public:
    QAudioSource *source = nullptr;
    QIODevice *io = nullptr;
    QFile file;
    QAudioDevice device;
    std::function<void(const QByteArray &)> sink;
    QByteArray sinkBuffer;
    qint64 frames = 0;
    double levelAccumulator = 0;
    int levelCount = 0;
    int samplesSinceLevel = 0;
    float peak = 0;
    bool metered = false;
    int writeFailures = 0;
    qint64 lastHeaderPatchFrames = 0;
    bool stopped = false;
    bool deviceLost = false;
    QMetaObject::Connection devicesConn;
    QMediaDevices *watcher = nullptr;

signals:
    void level(float);
    void writeFailure();
    void engineDied(const QString &message);

public slots:
    bool open(const QString &path, const QAudioDevice &dev)
    {
        device = dev;
        file.setFileName(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcAudio) << "cannot open" << path;
            return false;
        }
        file.write(wavHeader(0));
        file.flush();

        QAudioFormat fmt;
        fmt.setSampleRate(FileLayout::sampleRate);
        fmt.setChannelCount(1);
        fmt.setSampleFormat(QAudioFormat::Int16);
        source = new QAudioSource(device, fmt, this);
        source->setBufferSize(3200); // ~100 ms
        io = source->start();
        if (!io || source->error() != QAudio::NoError) {
            qCWarning(lcAudio) << "QAudioSource failed to start:" << source->error();
            delete source;
            source = nullptr;
            file.close();
            return false;
        }
        connect(io, &QIODevice::readyRead, this, &CaptureWorker::onReady);
        connect(source, &QAudioSource::stateChanged, this, [this](QAudio::State st) {
            if (stopped || !source) return;
            if (st == QAudio::StoppedState && source->error() != QAudio::NoError) {
                qCWarning(lcAudio) << "capture stopped with error" << source->error();
                emit engineDied(QCoreApplication::translate("Jot", "Desktop mic stopped"));
            }
        });
        watcher = new QMediaDevices(this);
        connect(watcher, &QMediaDevices::audioInputsChanged, this, [this] {
            if (stopped) return;
            for (const auto &d : QMediaDevices::audioInputs())
                if (d.id() == device.id()) return;
            deviceLost = true;
            emit engineDied(QCoreApplication::translate("Jot", "Desktop mic disconnected"));
        });
        return true;
    }

    void onReady()
    {
        if (!io || stopped) return;
        const QByteArray data = io->readAll();
        consume(data);
    }

    void consume(QByteArray data)
    {
        if (data.isEmpty()) return;
        if (data.size() % 2) data.chop(1);
        applyGain(data, g_gain.load());
        if (file.write(data) != data.size()) {
            if (++writeFailures >= 3) emit writeFailure();
        } else {
            writeFailures = 0;
        }
        const qint64 n = data.size() / 2;
        frames += n;
        meter(data);
        if (sink) {
            sinkBuffer += data;
            while (sinkBuffer.size() >= 3200) {
                sink(sinkBuffer.left(3200));
                sinkBuffer.remove(0, 3200);
            }
        }
        // Patch the header about once a second so a crash leaves a valid WAV.
        if (frames - lastHeaderPatchFrames >= FileLayout::sampleRate) {
            patchHeader();
            lastHeaderPatchFrames = frames;
        }
    }

    void meter(const QByteArray &data)
    {
        const auto *s = reinterpret_cast<const qint16 *>(data.constData());
        const qsizetype n = data.size() / 2;
        double sum = 0;
        for (qsizetype i = 0; i < n; ++i) {
            const double v = s[i] / 32768.0;
            sum += v * v;
        }
        if (n == 0) return;
        const float rms = float(std::sqrt(sum / double(n)));
        levelAccumulator += rms;
        ++levelCount;
        samplesSinceLevel += int(n);
        metered = true;
        if (samplesSinceLevel >= 800) { // ~50 ms
            const float lvl = AudioLevelCurve::level(float(levelAccumulator / levelCount));
            levelAccumulator = 0;
            levelCount = 0;
            samplesSinceLevel = 0;
            peak = std::max(peak, lvl);
            emit level(lvl);
        }
    }

    static QByteArray wavHeader(qint64 dataBytes)
    {
        QByteArray h(44, '\0');
        auto put32 = [&](int off, quint32 v) { qToLittleEndian(v, h.data() + off); };
        auto put16 = [&](int off, quint16 v) { qToLittleEndian(v, h.data() + off); };
        memcpy(h.data(), "RIFF", 4);
        put32(4, quint32(36 + dataBytes));
        memcpy(h.data() + 8, "WAVEfmt ", 8);
        put32(16, 16);
        put16(20, 1);
        put16(22, 1);
        put32(24, FileLayout::sampleRate);
        put32(28, FileLayout::bytesPerSecond);
        put16(32, 2);
        put16(34, 16);
        memcpy(h.data() + 36, "data", 4);
        put32(40, quint32(dataBytes));
        return h;
    }

    void patchHeader()
    {
        const qint64 pos = file.pos();
        file.seek(0);
        file.write(wavHeader(frames * 2));
        file.seek(pos);
        file.flush();
    }

public slots:
    AudioCaptureResult finish()
    {
        if (io && !deviceLost) consume(io->readAll());
        stopped = true;
        if (source) {
            source->stop();
            delete source;
            source = nullptr;
            io = nullptr;
        }
        if (sink && !sinkBuffer.isEmpty()) {
            sink(sinkBuffer);
            sinkBuffer.clear();
        }
        if (file.isOpen()) {
            patchHeader();
            file.close();
        }
        AudioCaptureResult r;
        r.framesWritten = frames;
        r.durationSeconds = double(frames) / FileLayout::sampleRate;
        r.peakLevel = metered ? peak : 1.0f;
        r.peakIsTrustworthy = metered;
        qCInfo(lcAudio) << "capture finished:" << frames << "frames, peak" << peak;
        return r;
    }
};

// ================================================================ Capture

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
{
    m_thread.setObjectName("jot-capture");
    m_worker = new CaptureWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &CaptureWorker::level, this, &AudioCapture::level);
    connect(m_worker, &CaptureWorker::writeFailure, this, &AudioCapture::writeFailure);
    connect(m_worker, &CaptureWorker::engineDied, this, &AudioCapture::engineDied);
    m_thread.start(QThread::TimeCriticalPriority);
}

AudioCapture::~AudioCapture()
{
    if (m_running && !m_stopping) {
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->finish(); }, Qt::BlockingQueuedConnection);
        emit captureActivityChanged(false);
    }
    m_thread.quit();
    m_thread.wait(2000);
}

AudioCapture::StartError AudioCapture::start(const QString &wavPath, std::function<void(const QByteArray &)> pcmSink)
{
    const auto device = MicrophoneManager::instance().recordingDevice();
    if (!device) return StartError::NoInputDevice;
    bool ok = false;
    m_worker->sink = std::move(pcmSink);
    QMetaObject::invokeMethod(m_worker, [&] { ok = m_worker->open(wavPath, *device); }, Qt::BlockingQueuedConnection);
    if (!ok) return StartError::Failed;
    m_running = true;
    emit captureActivityChanged(true);
    return StartError::None;
}

void AudioCapture::stop(std::function<void(AudioCaptureResult)> cb)
{
    if (!m_running || m_stopping) {
        cb(AudioCaptureResult{0, 0, {}, 0, false});
        return;
    }
    m_stopping = true;
    // Capture one short tail past the release: the device delivers audio in
    // chunks, and tearing down at once threw away the end of the last word.
    QTimer::singleShot(120, this, [this, cb] {
        AudioCaptureResult result;
        QMetaObject::invokeMethod(m_worker, [&] { result = m_worker->finish(); }, Qt::BlockingQueuedConnection);
        m_running = false;
        emit captureActivityChanged(false);
        cb(result);
    });
}

} // namespace jot

#include "AudioCapture.moc"
