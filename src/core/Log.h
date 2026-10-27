#pragma once

#include <QFile>
#include <QObject>
#include <QString>

// Log of the application: shown in the main window and written to
// logs/connect-<date>.log next to the profile. Modules log with their own tag.
class Log : public QObject {
    Q_OBJECT
public:
    static Log& instance();
    static void write(const QString& tag, const QString& text);

    void setFolder(const QString& folder); // empty: no file

Q_SIGNALS:
    void line(const QString& text); // "HH:mm:ss.zzz [tag] text"

private:
    Log() = default;
    QString m_folder;
    QFile m_file;
};
