#pragma once

#include <QString>
#include <memory>

class QLockFile;
class QSettings;

// A profile is one INI file with the connection, the enabled modules and their
// settings (docs/design.md 2.1). Default OpenSkiProConnect.ini next to the exe,
// another one with --profile <file>. Several instances may run with different
// profiles; a profile is locked by the instance using it.
namespace Profile {

// Opens the profile; false when another instance uses it (error set).
bool open(const QString& path, QString* error);
QString path();
QString folder();
QSettings& settings();
// Default profile next to the executable.
QString defaultPath();

} // namespace Profile
