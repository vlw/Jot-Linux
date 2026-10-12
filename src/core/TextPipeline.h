// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
// ReplacementEngine, ValidationGate, PromptV1, TranscriptDiff, TimeoutPolicy,
// AudioLevelCurve and NoiseFloorEstimator — the pure, load-bearing text and
// signal logic, ported line for line.
#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cmath>
#include <optional>

namespace jot {

// ---------------------------------------------------------------- Replacement
namespace ReplacementEngine {
struct Rule {
    QString wrong;
    QString right;
};
QString apply(const QList<Rule> &rules, const QString &text);
QString propagateCase(const QString &original, const QString &replacement, const QString &wrong = {});
}

// ---------------------------------------------------------------- Validation
namespace ValidationGate {
struct Verdict {
    bool accepted = true;
    QString reason;
};
QString stripArtifacts(const QString &text);
Verdict validate(const QString &raw, const QString &cleaned);
}

// ---------------------------------------------------------------- Prompt
namespace PromptV1 {
enum class Tone { Email, WorkChat, PersonalChat, Code, Neutral };
Tone toneCategory(const QString &appClass);
QString cleanupPrompt(const QString &raw, Tone tone, const QStringList &vocabulary,
                      const QList<QPair<QString, QString>> &spellings);
bool isTerminal(const QString &appClass);
}

// ---------------------------------------------------------------- Diff
namespace TranscriptDiff {
struct Segment {
    QString text;
    bool isCut = false;
};
QList<Segment> segments(const QString &verbatim, const QString &cleaned);
}

// ---------------------------------------------------------------- Timeouts
namespace TimeoutPolicy {
constexpr double liveFinal = 6;
constexpr double connect = 5;
constexpr double slowStateUI = 3;
inline double overallDeadline(double audioDuration) { return 30 + audioDuration / 4; }
inline double cleanupDeadline(int textLength) { return std::min(8.0, 3 + double(std::max(0, textLength)) / 1000.0); }
}

// ---------------------------------------------------------------- Levels
namespace AudioLevelCurve {
constexpr float gain = 11;
constexpr float exponent = 0.65f;
constexpr double floorDBFS = -120;
inline float level(float rms)
{
    if (rms <= 0) return 0;
    return std::min(1.0f, std::pow(std::min(rms * gain, 1.0f), exponent));
}
inline float rmsFromLevel(float level)
{
    if (level <= 0) return 0;
    return std::pow(std::min(level, 1.0f), 1 / exponent) / gain;
}
inline double dBFS(float level)
{
    const float linear = rmsFromLevel(level);
    if (linear <= 0) return floorDBFS;
    return std::max(floorDBFS, 20 * std::log10(double(linear)));
}
}

class NoiseFloorEstimator {
public:
    static constexpr int minimumSamples = 8;
    static constexpr double percentile = 0.10;
    explicit NoiseFloorEstimator(int capacity = 600) : m_capacity(std::max(minimumSamples, capacity)) {}
    void ingest(float level);
    std::optional<double> floorDB() const;
    std::optional<double> measuredSNR() const;
    double peakDB() const { return m_peakDB; }

private:
    int m_capacity;
    QVector<double> m_samples;
    double m_peakDB = AudioLevelCurve::floorDBFS;
};

} // namespace jot
