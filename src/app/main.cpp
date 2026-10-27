#include "MainWindow.h"
#include "core/Log.h"
#include "core/Profile.h"
#include "ui/UiHelpers.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("OpenSkiProConnect"));
    QApplication::setApplicationVersion(QStringLiteral(OSC_VERSION));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("Outputs for OpenSkiPro2: results boards, FIS Live, ..."));
    p.addHelpOption();
    p.addVersionOption();
    p.addOption({ QStringLiteral("profile"), QStringLiteral("profile (INI file) to use, default OpenSkiProConnect.ini next to the exe"),
                  QStringLiteral("file") });
    p.addOption({ QStringLiteral("screenshot"), QStringLiteral("developer: save an image of the window after 4 s and quit"),
                  QStringLiteral("png") });
    p.addOption({ QStringLiteral("page"), QStringLiteral("developer: page shown for --screenshot (0 connection, 1.. modules)"),
                  QStringLiteral("index"), QStringLiteral("0") });
    p.process(app);

    QString error;
    if (!Profile::open(p.value(QStringLiteral("profile")), &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("OpenSkiPro Connect"), error);
        return 1;
    }
    Log::instance().setFolder(QDir(Profile::folder()).filePath(QStringLiteral("logs")));

    QFont f(QStringLiteral("Segoe UI"));
    f.setPointSize(9);
    QApplication::setFont(f);
    app.setStyleSheet(UiHelpers::appStyleSheet()); // the look of OpenSkiPro2

    MainWindow w;
    w.show();
    // developer aid: an image of the window after a few seconds, then quit
    if (p.isSet(QStringLiteral("screenshot"))) {
        const QString file = p.value(QStringLiteral("screenshot"));
        w.showPage(p.value(QStringLiteral("page")).toInt());
        QTimer::singleShot(4000, &w, [&w, file] {
            w.grab().save(file);
            QApplication::quit();
        });
    }
    return QApplication::exec();
}
