#include "Log.h"

#include <QDate>
#include <QDateTime>
#include <QDir>

Log& Log::instance()
{
    static Log log;
    return log;
}

void Log::setFolder(const QString& folder)
{
    m_folder = folder;
    m_file.close();
}

void Log::write(const QString& tag, const QString& text)
{
    Log& l = instance();
    const QString s = QStringLiteral("%1 [%2] %3").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), tag, text);
    if (!l.m_folder.isEmpty()) {
        const QString path = QDir(l.m_folder).filePath(QStringLiteral("connect-%1.log").arg(QDate::currentDate().toString(Qt::ISODate)));
        if (l.m_file.fileName() != path) {
            l.m_file.close();
            QDir().mkpath(l.m_folder);
            l.m_file.setFileName(path);
            l.m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        }
        if (l.m_file.isOpen()) {
            l.m_file.write(s.toUtf8() + '\n');
            l.m_file.flush();
        }
    }
    Q_EMIT l.line(s);
}
