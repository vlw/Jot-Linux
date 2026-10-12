// UI language selection.
#include "Language.h"
#include "../core/Settings.h"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QLibraryInfo>
#include <QLocale>

namespace jot {

Language &Language::instance()
{
    static Language l;
    return l;
}

Language::Language()
{
    connect(&Settings::instance(), &Settings::changed, this, [this](const QString &key) {
        if (key == "language") apply();
    });
}

QString Language::uiFamily() const
{
    return brandFonts() ? QStringLiteral("Google Sans Flex") : QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
}

QString Language::codeFamily() const
{
    return brandFonts() ? QStringLiteral("Google Sans Code") : QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

void Language::apply()
{
    const QString pref = Settings::instance().value("language", "system").toString();
    QString code = pref;
    if (code != "en" && code != "ru")
        code = QLocale::system().language() == QLocale::Russian ? "ru" : "en";

    QCoreApplication::removeTranslator(&m_app);
    QCoreApplication::removeTranslator(&m_qt);
    if (code == "ru") {
        if (m_app.load(":/i18n/jot_ru.qm")) QCoreApplication::installTranslator(&m_app);
        if (m_qt.load("qt_ru", QLibraryInfo::path(QLibraryInfo::TranslationsPath))) QCoreApplication::installTranslator(&m_qt);
    }
    QLocale::setDefault(QLocale(code == "ru" ? QLocale::Russian : QLocale::English,
                                code == "ru" ? QLocale::Russia : QLocale::UnitedStates));
    const bool wasDifferent = code != m_code;
    m_code = code;
    if (wasDifferent || pref == "system") emit changed();
}

} // namespace jot
