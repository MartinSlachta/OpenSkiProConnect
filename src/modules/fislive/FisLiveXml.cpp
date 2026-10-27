#include "FisLiveXml.h"

#include <QBuffer>
#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTime>
#include <QTimeZone>
#include <QXmlStreamWriter>
#include <functional>

namespace FisLiveXml {

namespace {

// A fragment written with QXmlStreamWriter (escaping), without the XML declaration.
QByteArray fragment(const std::function<void(QXmlStreamWriter&)>& write)
{
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    QXmlStreamWriter w(&buf);
    w.setAutoFormatting(false);
    write(w);
    w.writeEndDocument(); // completes a last empty element (<dnf bib="5"/>)
    if (out.endsWith('\n'))
        out.chop(1);
    return out;
}

QString text(const QJsonValue& v)
{
    if (v.isDouble())
        return QString::number(v.toDouble());
    return v.toString().trimmed();
}

void element(QXmlStreamWriter& w, const QString& name, const QString& value)
{
    if (!value.isEmpty())
        w.writeTextElement(name, value);
}

void correctionAttr(QXmlStreamWriter& w, bool correction)
{
    if (correction)
        w.writeAttribute(QStringLiteral("correction"), QStringLiteral("y"));
}

} // namespace

QString isoUtc(const QDate& date, qint64 dayMs, int utcOffsetMin)
{
    const QDateTime t = QDateTime(date.isValid() ? date : QDate::currentDate(), QTime(0, 0), QTimeZone::UTC)
                            .addMSecs(dayMs - qint64(utcOffsetMin) * 60000);
    return t.toString(Qt::ISODateWithMs);
}

QString isoUtcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString timeText(qint64 ms, int precision)
{
    if (ms < 0)
        return QString();
    precision = qBound(0, precision, 3);
    qint64 div = 1;
    for (int i = precision; i < 3; ++i)
        div *= 10;
    const qint64 units = ms / div; // truncated, never rounded (like OpenSkiPro2)
    qint64 scale = 1;
    for (int i = 0; i < precision; ++i)
        scale *= 10;
    const qint64 secs = units / scale;
    QString s = secs >= 60 ? QStringLiteral("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0')) : QString::number(secs);
    if (precision > 0)
        s += QLatin1Char('.') + QStringLiteral("%1").arg(units % scale, precision, 10, QLatin1Char('0'));
    return s;
}

QString raceGender(const QJsonObject& competitors)
{
    bool men = false, ladies = false;
    for (const auto& v : competitors.value(QStringLiteral("competitors")).toArray()) {
        const QJsonObject c = v.toObject();
        if (c.value(QStringLiteral("forerunner")).toBool())
            continue;
        const QString g = c.value(QStringLiteral("gender")).toString();
        men |= g == QLatin1String("M");
        ladies |= g == QLatin1String("F");
    }
    return men && ladies ? QStringLiteral("A") : ladies ? QStringLiteral("L") : QStringLiteral("M");
}

QString discipline(const QJsonObject& event)
{
    const QString d = event.value(QStringLiteral("type")).toObject().value(QStringLiteral("discipline")).toString();
    return d == QLatin1String("AC") ? QStringLiteral("SC") : d;
}

QByteArray raceinfo(const QJsonObject& event, const QString& gender)
{
    return fragment([&](QXmlStreamWriter& w) {
        const QJsonObject general = event.value(QStringLiteral("general")).toObject();
        const QJsonObject type = event.value(QStringLiteral("type")).toObject();
        const QJsonArray courses = event.value(QStringLiteral("courses")).toArray();
        const QDate date = QDate::fromString(general.value(QStringLiteral("date")).toString(), Qt::ISODate);
        w.writeStartElement(QStringLiteral("raceinfo"));
        element(w, QStringLiteral("event"), text(general.value(QStringLiteral("title1"))));
        element(w, QStringLiteral("name"), text(general.value(QStringLiteral("title2"))));
        element(w, QStringLiteral("slope"), courses.isEmpty() ? QString() : text(courses.first().toObject().value(QStringLiteral("course"))));
        element(w, QStringLiteral("discipline"), discipline(event));
        element(w, QStringLiteral("gender"), gender);
        w.writeTextElement(QStringLiteral("category"), text(type.value(QStringLiteral("fisCategory"))));
        element(w, QStringLiteral("place"), text(general.value(QStringLiteral("location"))));
        w.writeTextElement(QStringLiteral("tempunit"), QStringLiteral("C"));
        w.writeTextElement(QStringLiteral("longunit"), QStringLiteral("m"));
        w.writeTextElement(QStringLiteral("speedunit"), QStringLiteral("kmh"));
        const int interCount = type.value(QStringLiteral("interCount")).toInt();
        for (const auto& cv : courses) {
            const QJsonObject c = cv.toObject();
            w.writeStartElement(QStringLiteral("run"));
            w.writeAttribute(QStringLiteral("no"), QString::number(c.value(QStringLiteral("run")).toInt()));
            element(w, QStringLiteral("start"), text(c.value(QStringLiteral("startAltitude"))));
            element(w, QStringLiteral("finish"), text(c.value(QStringLiteral("finishAltitude"))));
            element(w, QStringLiteral("height"), text(c.value(QStringLiteral("drop"))));
            element(w, QStringLiteral("length"), text(c.value(QStringLiteral("length"))));
            element(w, QStringLiteral("gates"), text(c.value(QStringLiteral("gates"))));
            element(w, QStringLiteral("turninggates"), text(c.value(QStringLiteral("turningGates"))));
            if (date.isValid()) {
                w.writeTextElement(QStringLiteral("year"), QString::number(date.year()));
                w.writeTextElement(QStringLiteral("month"), QStringLiteral("%1").arg(date.month(), 2, 10, QLatin1Char('0')));
                w.writeTextElement(QStringLiteral("day"), QStringLiteral("%1").arg(date.day(), 2, 10, QLatin1Char('0')));
            }
            const QTime start = QTime::fromString(text(c.value(QStringLiteral("startTime"))), QStringLiteral("H:mm"));
            if (start.isValid()) {
                w.writeTextElement(QStringLiteral("hour"), QStringLiteral("%1").arg(start.hour(), 2, 10, QLatin1Char('0')));
                w.writeTextElement(QStringLiteral("minute"), QStringLiteral("%1").arg(start.minute(), 2, 10, QLatin1Char('0')));
            }
            w.writeStartElement(QStringLiteral("racedef"));
            for (int i = 1; i <= interCount; ++i) {
                w.writeEmptyElement(QStringLiteral("inter"));
                w.writeAttribute(QStringLiteral("i"), QString::number(i));
            }
            w.writeEndElement(); // racedef
            w.writeEndElement(); // run
        }
        w.writeEndElement(); // raceinfo
    });
}

QByteArray startlist(int run, const QJsonObject& resource, const QString& timestamp)
{
    return fragment([&](QXmlStreamWriter& w) {
        w.writeStartElement(QStringLiteral("startlist"));
        w.writeAttribute(QStringLiteral("runno"), QString::number(run));
        w.writeAttribute(QStringLiteral("timestamp"), timestamp);
        int order = 0;
        for (const auto& v : resource.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject e = v.toObject();
            const QJsonObject c = e.value(QStringLiteral("competitor")).toObject();
            if (c.value(QStringLiteral("forerunner")).toBool())
                continue;
            w.writeStartElement(QStringLiteral("racer"));
            w.writeAttribute(QStringLiteral("order"), QString::number(++order));
            w.writeTextElement(QStringLiteral("bib"), QString::number(e.value(QStringLiteral("bib")).toInt()));
            w.writeTextElement(QStringLiteral("lastname"), text(c.value(QStringLiteral("lastName"))));
            w.writeTextElement(QStringLiteral("firstname"), text(c.value(QStringLiteral("firstName"))));
            w.writeTextElement(QStringLiteral("nat"), text(c.value(QStringLiteral("nation"))));
            w.writeTextElement(QStringLiteral("fiscode"), text(c.value(QStringLiteral("code"))));
            w.writeEndElement();
        }
        w.writeEndElement();
    });
}

QByteArray command(const QString& name, int run, const QString& info, const QString& timestamp)
{
    return fragment([&](QXmlStreamWriter& w) {
        w.writeStartElement(QStringLiteral("command"));
        w.writeAttribute(QStringLiteral("timestamp"), timestamp);
        w.writeStartElement(name);
        if (name == QLatin1String("activerun"))
            w.writeAttribute(QStringLiteral("no"), QString::number(run));
        else if (run > 0)
            w.writeAttribute(QStringLiteral("runno"), QString::number(run));
        w.writeAttribute(QStringLiteral("timestamp"), timestamp);
        element(w, QStringLiteral("info"), info);
        w.writeEndElement();
        w.writeEndElement();
    });
}

QByteArray meteo(const QJsonObject& event, const QString& timestamp)
{
    const QJsonObject c = event.value(QStringLiteral("conditions")).toObject();
    const QString weather = text(c.value(QStringLiteral("weather")));
    const QString temp = text(c.value(QStringLiteral("tempStart"))).replace(QLatin1Char(','), QLatin1Char('.'));
    const QString snow = text(c.value(QStringLiteral("snow")));
    if (weather.isEmpty() && temp.isEmpty() && snow.isEmpty())
        return QByteArray();
    return fragment([&](QXmlStreamWriter& w) {
        w.writeStartElement(QStringLiteral("meteo"));
        w.writeAttribute(QStringLiteral("timestamp"), timestamp);
        element(w, QStringLiteral("weather"), weather);
        element(w, QStringLiteral("temperature"), temp);
        element(w, QStringLiteral("snowcondition"), snow);
        w.writeEndElement();
    });
}

QByteArray message(const QString& textValue, const QString& timestamp)
{
    return fragment([&](QXmlStreamWriter& w) {
        w.writeStartElement(QStringLiteral("message"));
        w.writeAttribute(QStringLiteral("timestamp"), timestamp);
        w.writeTextElement(QStringLiteral("text"), textValue);
        w.writeEndElement();
    });
}

QByteArray nextstart(int bib)
{
    return fragment([&](QXmlStreamWriter& w) {
        w.writeEmptyElement(QStringLiteral("nextstart"));
        w.writeAttribute(QStringLiteral("bib"), QString::number(bib));
    });
}

QByteArray start(int bib, const QString& timestamp, bool correction)
{
    return fragment([&](QXmlStreamWriter& w) {
        w.writeEmptyElement(QStringLiteral("start"));
        w.writeAttribute(QStringLiteral("bib"), QString::number(bib));
        correctionAttr(w, correction);
        if (!timestamp.isEmpty())
            w.writeAttribute(QStringLiteral("timestamp"), timestamp);
    });
}

static QByteArray timed(const QString& tag, int point, int bib, const QString& time, const QString& diff, int rank,
                        int precision, const QString& timestamp, bool correction)
{
    return fragment([&](QXmlStreamWriter& w) {
        w.writeStartElement(tag);
        if (point > 0)
            w.writeAttribute(QStringLiteral("i"), QString::number(point));
        w.writeAttribute(QStringLiteral("bib"), QString::number(bib));
        correctionAttr(w, correction);
        w.writeAttribute(QStringLiteral("precision"), QString::number(precision));
        if (!timestamp.isEmpty())
            w.writeAttribute(QStringLiteral("timestamp"), timestamp);
        w.writeTextElement(QStringLiteral("time"), time);
        if (!diff.isEmpty())
            w.writeTextElement(QStringLiteral("diff"), diff);
        if (rank > 0)
            w.writeTextElement(QStringLiteral("rank"), QString::number(rank));
        w.writeEndElement();
    });
}

QByteArray inter(int point, int bib, const QString& time, const QString& diff, int rank, int precision,
                 const QString& timestamp, bool correction)
{
    return timed(QStringLiteral("inter"), point, bib, time, diff, rank, precision, timestamp, correction);
}

QByteArray finish(int bib, const QString& time, const QString& diff, int rank, int precision, const QString& timestamp,
                  bool correction)
{
    return timed(QStringLiteral("finish"), 0, bib, time, diff, rank, precision, timestamp, correction);
}

QByteArray status(const QString& tag, int bib, bool correction)
{
    return fragment([&](QXmlStreamWriter& w) {
        w.writeEmptyElement(tag);
        w.writeAttribute(QStringLiteral("bib"), QString::number(bib));
        correctionAttr(w, correction);
    });
}

QByteArray raceevent(const QList<QByteArray>& children, const QString& timestamp)
{
    QByteArray out = "<raceevent timestamp=\"" + timestamp.toUtf8() + "\">";
    for (const QByteArray& c : children)
        out += c;
    return out + "</raceevent>";
}

QByteArray keepalive()
{
    return QByteArrayLiteral("<keepalive/>");
}

QByteArray envelope(const QString& codex, const QString& password, int sequence, const QString& timestamp,
                    const QByteArray& body)
{
    const QString head = QStringLiteral("<livetiming codex=\"%1\" passwd=\"%2\" sequence=\"%3\" timestamp=\"%4\">")
                             .arg(codex.toHtmlEscaped(), password.toHtmlEscaped())
                             .arg(sequence, 5, 10, QLatin1Char('0'))
                             .arg(timestamp.toHtmlEscaped());
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") + head.toUtf8() + body + QByteArrayLiteral("</livetiming>");
}

QByteArray masked(const QByteArray& message)
{
    static const QRegularExpression rx(QStringLiteral("passwd=\"[^\"]*\""));
    return QString::fromUtf8(message).replace(rx, QStringLiteral("passwd=\"***\"")).toUtf8();
}

QList<int> replySequences(const QByteArray& data)
{
    static const QRegularExpression rx(QStringLiteral("<livetiming[^>]*sequence=\"(\\d+)\""));
    QList<int> l;
    auto it = rx.globalMatch(QString::fromUtf8(data));
    while (it.hasNext())
        l << it.next().captured(1).toInt();
    return l;
}

} // namespace FisLiveXml
