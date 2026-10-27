#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// A screen configuration (docs/screen-design.md 9): one per kind of race (FIS race,
// regional cup, company event), chosen by hand in the module and used by every screen.
// ONE list of columns for every list (ranking, finish, on course, next to start, start
// list): each list fills the same columns with what belongs to it, so they line up by
// themselves. The zones are those of the TV board; the speaker page shows everything
// in the same columns. Saved in a *.oscreen file (JSON) in the folder "screens"; the
// same JSON is sent to the browsers, which draw it.

struct ScreenColumn {
    QString field;  // key of a row (ScreenConfig::fields())
    QString header; // written by the user ("Bib", "Š.č.")
    int width = 10; // relative
    QString align = QStringLiteral("left"); // left / center / right
    QString format; // name: "last first" / "first last" / "last"; nation: "flagcode" / "flag" / "code"

    QJsonObject toJson() const;
    static ScreenColumn fromJson(const QJsonObject& o);
};

// A zone of the TV board, top to bottom.
struct ScreenZone {
    QString type = QStringLiteral("ranking"); // ScreenConfig::zoneTypes()
    QString title;                            // title bar of the zone, empty = none
    int lines = 10;
    QString visible = QStringLiteral("always"); // ScreenConfig::visibilities()
    bool counters = true;                     // ranking: Classified / List / DNS / DNF / DSQ in the title bar

    QJsonObject toJson() const;
    static ScreenZone fromJson(const QJsonObject& o);
};

struct ScreenConfig {
    QString name;
    QList<ScreenColumn> columns; // every list, the TV and the speaker
    // TV board
    int width = 1920;
    int height = 1080;
    int lineHeight = 44; // pixels at the design size
    QString font = QStringLiteral("Arial");
    int fontSize = 30;   // pixels at the design size
    QString background = QStringLiteral("#0b1f3a");
    QString text = QStringLiteral("#ffffff");
    QString titleBackground = QStringLiteral("#c8dcf5");
    QString titleText = QStringLiteral("#0b1f3a");
    QString headerText = QStringLiteral("#9fc3ec");
    QString row1 = QStringLiteral("#000000");
    QString row2 = QStringLiteral("#1a1a1a");
    QString highlight = QStringLiteral("#f5c400");
    QString highlightText = QStringLiteral("#000000");
    QString accent = QStringLiteral("#7fb2ff"); // rank / bib boxes
    QString backgroundImage;                    // file in screens/images
    QString title1 = QStringLiteral("{title1}"); // {title1} {title2} {discipline} {location} {date} {run} or text
    QString title2 = QStringLiteral("{title2}");
    QString clock = QStringLiteral("device"); // device / pc / none
    QString footText;
    QStringList logos; // files in screens/images
    int logoSeconds = 10;
    // ranking
    QString ranking = QStringLiteral("total"); // total / run
    QString group = QStringLiteral("all");     // all / rotate / <index of the output group>
    int page1Seconds = 8;
    int pageSeconds = 5;
    int maxPages = 0; // 0 = all
    bool freeze = false;
    int jumpSeconds = 8;  // after a finish: page of the finisher (0 = no)
    int finishSeconds = 8; // the newest finisher highlighted
    QString css; // own CSS for special wishes
    QList<ScreenZone> zones;

    QJsonObject toJson() const;
    // Also a file of 0.3 (columns per zone): the columns of its ranking zone are taken.
    static ScreenConfig fromJson(const QJsonObject& o);
    // A file of 0.3 (columns per zone, "kind" board / speaker).
    static bool isVersion1(const QJsonObject& o);

    bool load(const QString& path, QString* error);
    bool save(const QString& path, QString* error) const;

    // Catalogue: id -> label shown in the editor.
    static QList<QPair<QString, QString>> zoneTypes();
    static QList<QPair<QString, QString>> visibilities();
    // runs: the runs of the event (1-9) - "Time of run n" only for those
    static QList<QPair<QString, QString>> fields(int runs = 9);
    static QList<QPair<QString, QString>> formats(const QString& field);
    // Fields of the competitor (OpenSkiPro2 decides which of them are public).
    static bool isCompetitorField(const QString& field);
    // The configuration for an event with these public fields (RaceModel::publicFields):
    // columns of competitor fields that are not public are left out, an empty header of a
    // field gets the label from OpenSkiPro2. No fields (older OpenSkiPro2) = all of them.
    ScreenConfig forPublicFields(const QList<QPair<QString, QString>>& fields) const;

    // Delivered examples: file name -> configuration.
    static QList<QPair<QString, ScreenConfig>> examples();
};
