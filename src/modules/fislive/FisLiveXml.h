#pragma once

#include <QByteArray>
#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

// XML of the FIS Live timing (FIS "Live XML documentation for AL ... v53"), alpine
// skiing. Fragments are built from the data of the OpenSkiPro2 API and wrapped in
// one <livetiming> message by envelope(). Pure functions, shared by the module and
// the tests.
namespace FisLiveXml {

// ISO 8601 UTC timestamp ("2026-12-14T11:30:00.000Z") of a time of day of the
// timing device (local time, utcOffsetMin from the clock of the API).
QString isoUtc(const QDate& date, qint64 dayMs, int utcOffsetMin);
QString isoUtcNow();
// "1:03.46" / "21.43" of a time in ms, truncated to the precision (0-3) like the API.
QString timeText(qint64 ms, int precision);

// "M" men, "L" ladies, "A" mixed - from the gender of the competitors (the same
// rule as the FIS XML export of OpenSkiPro2).
QString raceGender(const QJsonObject& competitors);
// SL, GS, SG, DH; the alpine combined as "SC" (code of the specification).
QString discipline(const QJsonObject& event);

// <raceinfo> with every run (course data, start time, intermediates).
QByteArray raceinfo(const QJsonObject& event, const QString& gender);
// <startlist runno> from the resource run/<r>/startlist, forerunners left out.
QByteArray startlist(int run, const QJsonObject& startlistResource, const QString& timestamp);
// <command> with one child: activerun no=, endrun / runcompleted / official_result /
// unofficial_result / scheduled / readytostart / inprogress runno=, delayed /
// interrupted / postponed / cancelled with <info>, clear.
QByteArray command(const QString& name, int run, const QString& info, const QString& timestamp);
QByteArray meteo(const QJsonObject& event, const QString& timestamp); // empty when nothing is known
QByteArray message(const QString& text, const QString& timestamp);

// Children of a <raceevent>.
QByteArray nextstart(int bib);
QByteArray start(int bib, const QString& timestamp, bool correction);
// time / diff in the text form of the API ("1:03.46", "0.00"), precision 0-3
QByteArray inter(int point, int bib, const QString& time, const QString& diff, int rank, int precision,
                 const QString& timestamp, bool correction);
QByteArray finish(int bib, const QString& time, const QString& diff, int rank, int precision,
                  const QString& timestamp, bool correction);
// "dns", "dnf", "dq", "nps"
QByteArray status(const QString& tag, int bib, bool correction);
QByteArray raceevent(const QList<QByteArray>& children, const QString& timestamp);

QByteArray keepalive();
// One message: <?xml?> + <livetiming codex passwd sequence timestamp> + body.
QByteArray envelope(const QString& codex, const QString& password, int sequence, const QString& timestamp,
                    const QByteArray& body);
// The same with passwd="***" (logs).
QByteArray masked(const QByteArray& message);
// Sequence numbers in the replies of the server (<livetiming sequence="00005">).
QList<int> replySequences(const QByteArray& data);

} // namespace FisLiveXml
