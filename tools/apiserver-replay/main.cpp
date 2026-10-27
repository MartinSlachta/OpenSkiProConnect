// apiserver-replay: serves a recording of osp-apicheck as a fake OpenSkiPro2.
//   apiserver-replay <recording.jsonl> [--port 8080] [--speed 1] [--wait]
// --wait: starts replaying when the first client is connected.

#include "ReplayServer.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("Serves a recording of osp-apicheck as a fake OpenSkiPro2 API."));
    p.addHelpOption();
    p.addPositionalArgument(QStringLiteral("recording"), QStringLiteral("osp-apicheck recording (.jsonl)"));
    p.addOption({ QStringLiteral("port"), QStringLiteral("port (default 8080)"), QStringLiteral("port"), QStringLiteral("8080") });
    p.addOption({ QStringLiteral("speed"), QStringLiteral("replay speed (default 1 = real time)"), QStringLiteral("x"), QStringLiteral("1") });
    p.addOption({ QStringLiteral("wait"), QStringLiteral("start replaying when the first client connects") });
    p.process(app);

    QTextStream out(stdout);
    ReplayServer server;
    QString err;
    if (p.positionalArguments().isEmpty() || !server.load(p.positionalArguments().first(), &err)) {
        out << "cannot load the recording: " << err << "\n";
        return 2;
    }
    if (!server.start(quint16(p.value(QStringLiteral("port")).toUInt()), &err)) {
        out << "cannot listen: " << err << "\n";
        return 2;
    }
    out << "replaying " << server.count() << " messages on port " << server.port() << "\n";
    out.flush();
    const double speed = p.value(QStringLiteral("speed")).toDouble();
    QObject::connect(&server, &ReplayServer::finished, &app, [&out] {
        out << "end of the recording (the server keeps running)\n";
        out.flush();
    });
    if (p.isSet(QStringLiteral("wait"))) {
        auto* poll = new QTimer(&app);
        QObject::connect(poll, &QTimer::timeout, &app, [&server, poll, speed] {
            if (server.clientCount() > 0) {
                poll->stop();
                server.play(speed);
            }
        });
        poll->start(200);
    } else {
        server.play(speed);
    }
    return QCoreApplication::exec();
}
