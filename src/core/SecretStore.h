// Linux replacement for the macOS Keychain store: freedesktop Secret Service
// (KWallet/ksecretd on Plasma) over D-Bus. Keys never touch plaintext files.
#pragma once

#include <QObject>
#include <QString>

namespace jot {

class SecretStore : public QObject {
    Q_OBJECT
public:
    static SecretStore &instance();

    QString geminiKey();
    QString openRouterKey();
    bool setGeminiKey(const QString &key);       // empty = delete
    bool setOpenRouterKey(const QString &key);   // empty = delete
    bool available();
    QString lastError() const { return m_lastError; }

signals:
    void keyChanged(const QString &account);

private:
    SecretStore() = default;
    QString load(const QString &account);
    bool store(const QString &account, const QString &label, const QString &secret);
    bool remove(const QString &account);
    bool ensureSession();
    QString defaultCollection();
    bool unlock(const QString &objectPath);

    QString m_session;
    QString m_lastError;
    // Cache so the hot path (every dictation) never round-trips D-Bus.
    QString m_cacheGemini, m_cacheOpenRouter;
    bool m_loadedGemini = false, m_loadedOpenRouter = false;
};

} // namespace jot
