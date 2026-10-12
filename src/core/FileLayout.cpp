// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#include "FileLayout.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace jot {

QString FileLayout::appDataRoot()
{
    static const QString root = [] {
        QString base = qEnvironmentVariable("XDG_DATA_HOME");
        if (base.isEmpty()) base = QDir::homePath() + "/.local/share";
        return base + "/Jot";
    }();
    return root;
}

QString FileLayout::recordingsRoot() { return appDataRoot() + "/recordings"; }

bool FileLayout::ensureDir(const QString &path) { return QDir().mkpath(path); }

QString FileLayout::makeSessionFolder(const QUuid &id, const QDateTime &now)
{
    const QString name = now.toLocalTime().toString("yyyyMMdd-HHmmss") + "-"
        + id.toString(QUuid::WithoutBraces).left(8).toUpper();
    const QString path = recordingsRoot() + "/" + name;
    if (!QDir().mkpath(path)) return {};
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}

std::optional<double> FileLayout::estimatedDuration(const QString &wavPath)
{
    const QFileInfo fi(wavPath);
    if (!fi.exists() || fi.size() <= 4096) return std::nullopt;
    return double(fi.size() - wavHeaderBytes) / bytesPerSecond;
}

} // namespace jot
