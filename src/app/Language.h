// UI language: "system" (default), "en" or "ru". Applied live — QML engines
// retranslate, the tray menu and status line rebuild.
#pragma once

#include <QObject>
#include <QTranslator>

namespace jot {

class Language : public QObject {
    Q_OBJECT
    /// Google Sans Flex/Code carry no Cyrillic, so Russian uses the desktop's own
    /// UI and fixed fonts rather than mixing two families in one line.
    Q_PROPERTY(QString uiFamily READ uiFamily NOTIFY changed)
    Q_PROPERTY(QString codeFamily READ codeFamily NOTIFY changed)
    Q_PROPERTY(bool brandFonts READ brandFonts NOTIFY changed)
public:
    QString uiFamily() const;
    QString codeFamily() const;
    bool brandFonts() const { return m_code != "ru"; }
    static Language &instance();
    /// Effective language code: "en" or "ru".
    QString code() const { return m_code; }
    void apply();

signals:
    void changed();

private:
    Language();
    QTranslator m_app;
    QTranslator m_qt;
    QString m_code = "en";
};

} // namespace jot
