#include "Profile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>

namespace Profile {

namespace {
QString s_path;
std::unique_ptr<QSettings> s_settings;
std::unique_ptr<QLockFile> s_lock;
} // namespace

QString defaultPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("OpenSkiProConnect.ini"));
}

bool open(const QString& pathIn, QString* error)
{
    const QString p = QFileInfo(pathIn.isEmpty() ? defaultPath() : pathIn).absoluteFilePath();
    // not "<file>.lock": QSettings itself locks that one while writing the file
    auto lock = std::make_unique<QLockFile>(p + QStringLiteral(".instance"));
    lock->setStaleLockTime(0);
    if (!lock->tryLock(100)) {
        if (error)
            *error = QCoreApplication::translate("Profile", "The profile %1 is used by another running OpenSkiPro Connect.")
                         .arg(QDir::toNativeSeparators(p));
        return false;
    }
    s_lock = std::move(lock);
    s_path = p;
    s_settings = std::make_unique<QSettings>(p, QSettings::IniFormat);
    return true;
}

QString path()
{
    return s_path;
}

QString folder()
{
    return QFileInfo(s_path).absolutePath();
}

QSettings& settings()
{
    if (!s_settings) // tests / tools without a profile: an in-memory one next to the exe
        s_settings = std::make_unique<QSettings>(defaultPath(), QSettings::IniFormat);
    return *s_settings;
}

} // namespace Profile
