#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>

class RaceModel;
class ScreenData;

// The sections of the ingest protocol 2 (docs/publish-protocol.md 4) built from the
// RaceModel: the rows of the screens (ScreenData) with their values formatted, the runs
// apart and only the public competitor fields - the server and its pages have no race
// logic, the pages only arrange the columns (docs/fields-design.md). Every run is sent,
// not only the current one.
namespace PublishSections {

// The kind of list a row belongs to: which keys it carries (protocol 4.5 - 4.8).
enum class List { StartList, OnStart, OnCourse, Finished, Total, Run };

// name -> data; data == state of the race now. ScreenData must be rebuilt before.
QMap<QString, QJsonObject> build(const RaceModel* model, const ScreenData* data);

// The public fields of the event; without them (OpenSkiPro2 < 0.7) name, nation, club.
QList<QPair<QString, QString>> fields(const RaceModel* model);
// One row of the protocol (4.5) from a row of ScreenData. run = the run of the list.
// The rank in the group of every run (groups[g].runs) comes from "run<k>GroupRanks" of
// the row (groupRanks of the API, OpenSkiPro2 0.7.1).
QJsonObject row(const QJsonObject& screenRow, const QList<QPair<QString, QString>>& fields, List list, int run, bool bestOfRuns);

// Whether a ranking after (or of) a run is worth sending: a classified row, or a status
// (DNF, DSQ...) in that run. Only DNQ and statuses of earlier runs = the run was not
// raced yet, and the pages would switch to an empty ranking (no start list, run 2 just
// started).
bool raced(const QJsonObject& ranking, int run);

} // namespace PublishSections
