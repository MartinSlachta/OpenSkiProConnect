#include "ScreenConfig.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

namespace {

const char* kFormat = "OpenSkiPro Connect screen 2";

QString tr(const char* s)
{
    return QCoreApplication::translate("ScreenConfig", s);
}

ScreenColumn col(const QString& field, const QString& header, int width, const QString& align = QStringLiteral("left"),
                 const QString& format = QString())
{
    ScreenColumn c;
    c.field = field;
    c.header = header;
    c.width = width;
    c.align = align;
    c.format = format;
    return c;
}

ScreenZone zone(const QString& type, const QString& title, int lines, const QString& visible)
{
    ScreenZone z;
    z.type = type;
    z.title = title;
    z.lines = lines;
    z.visible = visible;
    return z;
}

// title + clock, on course 2, finish 4, ranking 12 (Display-SkiAlp-Pro ALPIN_1280_1024),
// start list before the race, foot (docs/screen-design.md 8.2)
ScreenConfig board(const QString& name, const QList<ScreenColumn>& columns)
{
    ScreenConfig c;
    c.name = name;
    c.columns = columns;
    c.lineHeight = 40;
    c.fontSize = 28;
    c.zones << zone(QStringLiteral("title"), QString(), 1, QStringLiteral("always"))
            << zone(QStringLiteral("onCourse"), QStringLiteral("On Course"), 2, QStringLiteral("timing"))
            << zone(QStringLiteral("finish"), QStringLiteral("Finish"), 4, QStringLiteral("timing"))
            << zone(QStringLiteral("ranking"), QStringLiteral("Ranking"), 12, QStringLiteral("withResults"))
            << zone(QStringLiteral("startList"), QStringLiteral("Start List"), 20, QStringLiteral("beforeRace"))
            << zone(QStringLiteral("foot"), QString(), 1, QStringLiteral("always"));
    return c;
}

// The columns of a file of 0.3: those of its ranking zone (else finish, else the first
// zone with columns), fields that do not exist any more left out.
QList<ScreenColumn> columnsOfVersion1(const QJsonArray& zones)
{
    QJsonArray best;
    int bestScore = -1;
    for (const auto& v : zones) {
        const QJsonObject z = v.toObject();
        const QJsonArray cols = z.value(QStringLiteral("columns")).toArray();
        const QString type = z.value(QStringLiteral("type")).toString();
        const int score = cols.isEmpty() ? -1 : type == QLatin1String("ranking") ? 2 : type == QLatin1String("finish") ? 1 : 0;
        if (score > bestScore) {
            bestScore = score;
            best = cols;
        }
    }
    QStringList known;
    for (const auto& f : ScreenConfig::fields())
        known << f.first;
    QList<ScreenColumn> out;
    for (const auto& v : best) {
        ScreenColumn c = ScreenColumn::fromJson(v.toObject());
        if (c.field == QLatin1String("running"))
            c.field = QStringLiteral("runTime");
        if (known.contains(c.field))
            out << c;
    }
    return out;
}

} // namespace

QJsonObject ScreenColumn::toJson() const
{
    QJsonObject o{ { QStringLiteral("field"), field }, { QStringLiteral("header"), header },
                   { QStringLiteral("width"), width }, { QStringLiteral("align"), align } };
    if (!format.isEmpty())
        o.insert(QStringLiteral("format"), format);
    return o;
}

ScreenColumn ScreenColumn::fromJson(const QJsonObject& o)
{
    ScreenColumn c;
    c.field = o.value(QStringLiteral("field")).toString();
    c.header = o.value(QStringLiteral("header")).toString();
    c.width = qBound(1, o.value(QStringLiteral("width")).toInt(c.width), 100);
    c.align = o.value(QStringLiteral("align")).toString(c.align);
    c.format = o.value(QStringLiteral("format")).toString();
    return c;
}

QJsonObject ScreenZone::toJson() const
{
    return QJsonObject{ { QStringLiteral("type"), type },       { QStringLiteral("title"), title },
                        { QStringLiteral("lines"), lines },     { QStringLiteral("visible"), visible },
                        { QStringLiteral("counters"), counters } };
}

ScreenZone ScreenZone::fromJson(const QJsonObject& o)
{
    ScreenZone z;
    z.type = o.value(QStringLiteral("type")).toString(z.type);
    z.title = o.value(QStringLiteral("title")).toString();
    z.lines = qBound(0, o.value(QStringLiteral("lines")).toInt(z.lines), 200);
    z.visible = o.value(QStringLiteral("visible")).toString(z.visible);
    z.counters = o.value(QStringLiteral("counters")).toBool(z.counters);
    return z;
}

QJsonObject ScreenConfig::toJson() const
{
    QJsonArray cols;
    for (const ScreenColumn& c : columns)
        cols.append(c.toJson());
    QJsonArray zs;
    for (const ScreenZone& z : zones)
        zs.append(z.toJson());
    return QJsonObject{
        { QStringLiteral("format"), QLatin1String(kFormat) },
        { QStringLiteral("name"), name },
        { QStringLiteral("columns"), cols },
        { QStringLiteral("width"), width },
        { QStringLiteral("height"), height },
        { QStringLiteral("lineHeight"), lineHeight },
        { QStringLiteral("font"), font },
        { QStringLiteral("fontSize"), fontSize },
        { QStringLiteral("theme"), QJsonObject{ { QStringLiteral("background"), background },
                                               { QStringLiteral("text"), text },
                                               { QStringLiteral("titleBackground"), titleBackground },
                                               { QStringLiteral("titleText"), titleText },
                                               { QStringLiteral("headerText"), headerText },
                                               { QStringLiteral("row1"), row1 },
                                               { QStringLiteral("row2"), row2 },
                                               { QStringLiteral("highlight"), highlight },
                                               { QStringLiteral("highlightText"), highlightText },
                                               { QStringLiteral("accent"), accent },
                                               { QStringLiteral("backgroundImage"), backgroundImage } } },
        { QStringLiteral("title1"), title1 },
        { QStringLiteral("title2"), title2 },
        { QStringLiteral("clock"), clock },
        { QStringLiteral("footText"), footText },
        { QStringLiteral("logos"), QJsonArray::fromStringList(logos) },
        { QStringLiteral("logoSeconds"), logoSeconds },
        { QStringLiteral("ranking"), QJsonObject{ { QStringLiteral("which"), ranking },
                                                 { QStringLiteral("group"), group },
                                                 { QStringLiteral("page1Seconds"), page1Seconds },
                                                 { QStringLiteral("pageSeconds"), pageSeconds },
                                                 { QStringLiteral("maxPages"), maxPages },
                                                 { QStringLiteral("freeze"), freeze },
                                                 { QStringLiteral("jumpSeconds"), jumpSeconds },
                                                 { QStringLiteral("finishSeconds"), finishSeconds } } },
        { QStringLiteral("css"), css },
        { QStringLiteral("zones"), zs },
    };
}

bool ScreenConfig::isVersion1(const QJsonObject& o)
{
    return !o.isEmpty() && o.value(QStringLiteral("format")).toString() != QLatin1String(kFormat);
}

ScreenConfig ScreenConfig::fromJson(const QJsonObject& o)
{
    ScreenConfig c;
    c.name = o.value(QStringLiteral("name")).toString();
    if (o.contains(QStringLiteral("columns"))) {
        for (const auto& v : o.value(QStringLiteral("columns")).toArray())
            c.columns << ScreenColumn::fromJson(v.toObject());
    } else {
        c.columns = columnsOfVersion1(o.value(QStringLiteral("zones")).toArray());
    }
    c.width = qBound(320, o.value(QStringLiteral("width")).toInt(c.width), 7680);
    c.height = qBound(200, o.value(QStringLiteral("height")).toInt(c.height), 4320);
    c.lineHeight = qBound(10, o.value(QStringLiteral("lineHeight")).toInt(c.lineHeight), 400);
    c.font = o.value(QStringLiteral("font")).toString(c.font);
    c.fontSize = qBound(6, o.value(QStringLiteral("fontSize")).toInt(c.fontSize), 300);
    const QJsonObject t = o.value(QStringLiteral("theme")).toObject();
    auto colour = [&t](const char* key, QString& v) { v = t.value(QLatin1String(key)).toString(v); };
    colour("background", c.background);
    colour("text", c.text);
    colour("titleBackground", c.titleBackground);
    colour("titleText", c.titleText);
    colour("headerText", c.headerText);
    colour("row1", c.row1);
    colour("row2", c.row2);
    colour("highlight", c.highlight);
    colour("highlightText", c.highlightText);
    colour("accent", c.accent);
    colour("backgroundImage", c.backgroundImage);
    c.title1 = o.value(QStringLiteral("title1")).toString(c.title1);
    c.title2 = o.value(QStringLiteral("title2")).toString(c.title2);
    c.clock = o.value(QStringLiteral("clock")).toString(c.clock);
    c.footText = o.value(QStringLiteral("footText")).toString();
    for (const auto& v : o.value(QStringLiteral("logos")).toArray())
        c.logos << v.toString();
    c.logoSeconds = qBound(1, o.value(QStringLiteral("logoSeconds")).toInt(c.logoSeconds), 3600);
    const QJsonObject r = o.value(QStringLiteral("ranking")).toObject();
    c.ranking = r.value(QStringLiteral("which")).toString(c.ranking);
    c.group = r.value(QStringLiteral("group")).toVariant().toString();
    if (c.group.isEmpty())
        c.group = QStringLiteral("all");
    c.page1Seconds = qBound(1, r.value(QStringLiteral("page1Seconds")).toInt(c.page1Seconds), 3600);
    c.pageSeconds = qBound(1, r.value(QStringLiteral("pageSeconds")).toInt(c.pageSeconds), 3600);
    c.maxPages = qBound(0, r.value(QStringLiteral("maxPages")).toInt(c.maxPages), 100);
    c.freeze = r.value(QStringLiteral("freeze")).toBool(c.freeze);
    c.jumpSeconds = qBound(0, r.value(QStringLiteral("jumpSeconds")).toInt(c.jumpSeconds), 3600);
    c.finishSeconds = qBound(0, r.value(QStringLiteral("finishSeconds")).toInt(c.finishSeconds), 3600);
    c.css = o.value(QStringLiteral("css")).toString();
    for (const auto& v : o.value(QStringLiteral("zones")).toArray())
        c.zones << ScreenZone::fromJson(v.toObject());
    return c;
}

bool ScreenConfig::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        if (error)
            *error = pe.errorString();
        return false;
    }
    *this = fromJson(doc.object());
    return true;
}

bool ScreenConfig::save(const QString& path, QString* error) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

QList<QPair<QString, QString>> ScreenConfig::zoneTypes()
{
    return {
        { QStringLiteral("title"), tr("Title + clock") },     { QStringLiteral("onCourse"), tr("On course") },
        { QStringLiteral("finish"), tr("Finish (last finishers)") }, { QStringLiteral("ranking"), tr("Ranking") },
        { QStringLiteral("startList"), tr("Start list") },    { QStringLiteral("nextStart"), tr("Next to start") },
        { QStringLiteral("unclassified"), tr("DNS / DNF / DSQ") }, { QStringLiteral("foot"), tr("Foot (text, logos)") },
    };
}

QList<QPair<QString, QString>> ScreenConfig::visibilities()
{
    return {
        { QStringLiteral("always"), tr("Always") },
        { QStringLiteral("timing"), tr("During the timing") },
        { QStringLiteral("notTiming"), tr("Outside the timing") },
        { QStringLiteral("beforeRace"), tr("Before the race (no result yet)") },
        { QStringLiteral("withResults"), tr("With results (timing or results known)") },
    };
}

QList<QPair<QString, QString>> ScreenConfig::fields(int runs)
{
    // what every list puts in these columns: docs/screen-design.md 9.2
    QList<QPair<QString, QString>> l = {
        { QStringLiteral("rank"), tr("Rank") },
        { QStringLiteral("groupRank"), tr("Rank in the group (category)") },
        { QStringLiteral("bib"), tr("Bib") },
        { QStringLiteral("name"), tr("Name") },
        { QStringLiteral("nation"), tr("Nation") },
        { QStringLiteral("club"), tr("Club") },
        { QStringLiteral("team"), tr("Team") },
        { QStringLiteral("region"), tr("Region") },
        { QStringLiteral("year"), tr("Year of birth") },
        { QStringLiteral("gender"), tr("Gender") },
        { QStringLiteral("category"), tr("Category") },
        { QStringLiteral("group"), tr("Output group") },
        { QStringLiteral("code"), tr("FIS / federation code") },
        { QStringLiteral("custom"), tr("Custom field") },
        { QStringLiteral("runTime"), tr("Time of the current run (running on course)") },
        { QStringLiteral("runRank"), tr("Rank in the current run") },
        { QStringLiteral("time"), tr("Time (total)") },
        { QStringLiteral("gap"), tr("Gap (on course: of the last intermediate)") },
        { QStringLiteral("groupGap"), tr("Gap in the group") },
    };
    for (int r = 1; r <= qBound(1, runs, 9); ++r) {
        l << qMakePair(QStringLiteral("run%1").arg(r), tr("Time of run %1").arg(r));
        l << qMakePair(QStringLiteral("run%1Rank").arg(r), tr("Rank in run %1").arg(r));
    }
    for (int i = 1; i <= 3; ++i) {
        l << qMakePair(QStringLiteral("inter%1").arg(i), tr("Intermediate %1: time").arg(i));
        l << qMakePair(QStringLiteral("inter%1Gap").arg(i), tr("Intermediate %1: gap").arg(i));
        l << qMakePair(QStringLiteral("inter%1Rank").arg(i), tr("Intermediate %1: rank").arg(i));
    }
    l << qMakePair(QStringLiteral("plannedStart"), tr("Planned start time"));
    return l;
}

bool ScreenConfig::isCompetitorField(const QString& field)
{
    static const QStringList l = { QStringLiteral("name"), QStringLiteral("nation"), QStringLiteral("club"), QStringLiteral("team"),
                                   QStringLiteral("region"), QStringLiteral("year"), QStringLiteral("gender"), QStringLiteral("category"),
                                   QStringLiteral("code"), QStringLiteral("custom") };
    return l.contains(field);
}

ScreenConfig ScreenConfig::forPublicFields(const QList<QPair<QString, QString>>& fields) const
{
    if (fields.isEmpty())
        return *this;
    ScreenConfig c = *this;
    c.columns.clear();
    for (ScreenColumn col : columns) {
        if (isCompetitorField(col.field)) {
            const auto it = std::find_if(fields.cbegin(), fields.cend(), [&](const auto& f) { return f.first == col.field; });
            if (it == fields.cend() && col.field != QLatin1String("name"))
                continue; // not public in OpenSkiPro2
            if (col.header.isEmpty() && it != fields.cend())
                col.header = it->second;
        }
        c.columns << col;
    }
    return c;
}

QList<QPair<QString, QString>> ScreenConfig::formats(const QString& field)
{
    if (field == QLatin1String("name"))
        return { { QString(), tr("LASTNAME Firstname") }, { QStringLiteral("first last"), tr("Firstname LASTNAME") },
                 { QStringLiteral("last"), tr("LASTNAME") } };
    if (field == QLatin1String("nation"))
        return { { QString(), tr("Flag + code") }, { QStringLiteral("flag"), tr("Flag") }, { QStringLiteral("code"), tr("Code") } };
    if (field == QLatin1String("rank") || field == QLatin1String("groupRank") || field == QLatin1String("bib"))
        return { { QString(), tr("Box") }, { QStringLiteral("plain"), tr("Plain") } };
    return {};
}

QList<QPair<QString, ScreenConfig>> ScreenConfig::examples()
{
    const QString c = QStringLiteral("center");
    const QString r = QStringLiteral("right");
    QList<QPair<QString, ScreenConfig>> l;

    // "Run" = the current run (running on course); empty in the 1st run, where it is the total
    l << qMakePair(QStringLiteral("FIS race.oscreen"),
                   board(QStringLiteral("FIS race"),
                         { col(QStringLiteral("rank"), QStringLiteral("Rank"), 7, c), col(QStringLiteral("bib"), QStringLiteral("Bib"), 7, c),
                           col(QStringLiteral("name"), QStringLiteral("Name"), 38), col(QStringLiteral("nation"), QStringLiteral("Nat."), 12),
                           col(QStringLiteral("runTime"), QStringLiteral("Run"), 12, r), col(QStringLiteral("time"), QStringLiteral("Total"), 12, r),
                           col(QStringLiteral("gap"), QStringLiteral("Gap"), 12, r) }));

    l << qMakePair(QStringLiteral("Regional cup.oscreen"),
                   board(QStringLiteral("Regional cup"),
                         { col(QStringLiteral("rank"), QStringLiteral("Rank"), 7, c), col(QStringLiteral("bib"), QStringLiteral("Bib"), 7, c),
                           col(QStringLiteral("name"), QStringLiteral("Name"), 30), col(QStringLiteral("category"), QStringLiteral("Cat."), 10, r),
                           col(QStringLiteral("groupRank"), QStringLiteral("[Rank]"), 8, c), col(QStringLiteral("runTime"), QStringLiteral("Run"), 12, r),
                           col(QStringLiteral("time"), QStringLiteral("Time"), 13, r), col(QStringLiteral("gap"), QStringLiteral("Gap"), 13, r) }));

    ScreenConfig company = board(QStringLiteral("Company event"),
                                 { col(QStringLiteral("rank"), QStringLiteral("Rank"), 8, c),
                                   col(QStringLiteral("name"), QStringLiteral("Name"), 44, QStringLiteral("left"), QStringLiteral("first last")),
                                   col(QStringLiteral("team"), QStringLiteral("Team"), 24), col(QStringLiteral("time"), QStringLiteral("Time"), 24, r) });
    company.background = QStringLiteral("#1b2b1b");
    company.titleBackground = QStringLiteral("#d8efc8");
    company.titleText = QStringLiteral("#1b2b1b");
    company.headerText = QStringLiteral("#b5dca0");
    company.accent = QStringLiteral("#8fd16a");
    l << qMakePair(QStringLiteral("Company event.oscreen"), company);
    return l;
}
