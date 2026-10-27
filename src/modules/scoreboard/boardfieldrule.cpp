#include "boardfieldrule.h"

#include <QJsonObject>
#include <QPair>
#include <QVector>

namespace {

// Stored as-is in JSON (not the translated UI labels below) so saved
// configs survive label wording changes.
const QVector<QPair<BoardFieldRule::ContentType, QString>> kContentTypeKeys = {
    {BoardFieldRule::ContentType::Field, QStringLiteral("field")},
    {BoardFieldRule::ContentType::RunningTime, QStringLiteral("runningTime")},
    {BoardFieldRule::ContentType::Rank, QStringLiteral("rank")},
    {BoardFieldRule::ContentType::StatusText, QStringLiteral("statusText")},
    {BoardFieldRule::ContentType::Date, QStringLiteral("date")},
    {BoardFieldRule::ContentType::DayTime, QStringLiteral("dayTime")},
    {BoardFieldRule::ContentType::Text, QStringLiteral("text")},
};

const QVector<QPair<BoardFieldRule::Scope, QString>> kScopeKeys = {
    {BoardFieldRule::Scope::OnStart, QStringLiteral("onStart")},
    {BoardFieldRule::Scope::OnCourse, QStringLiteral("onCourse")},
    {BoardFieldRule::Scope::OnFinish, QStringLiteral("onFinish")},
    {BoardFieldRule::Scope::LastFinish, QStringLiteral("lastFinish")},
};

const QVector<QPair<BoardFieldRule::FieldName, QString>> kFieldNameKeys = {
    {BoardFieldRule::FieldName::Bib, QStringLiteral("bib")},
    {BoardFieldRule::FieldName::Category, QStringLiteral("category")},
    {BoardFieldRule::FieldName::FirstName, QStringLiteral("firstName")},
    {BoardFieldRule::FieldName::LastName, QStringLiteral("lastName")},
    {BoardFieldRule::FieldName::Nation, QStringLiteral("nation")},
    {BoardFieldRule::FieldName::Sex, QStringLiteral("sex")},
    {BoardFieldRule::FieldName::Year, QStringLiteral("year")},
    {BoardFieldRule::FieldName::Club, QStringLiteral("club")},
};

const QVector<QPair<BoardFieldRule::RunSelector, QString>> kRunSelectorKeys = {
    {BoardFieldRule::RunSelector::Total, QStringLiteral("total")},
    {BoardFieldRule::RunSelector::ThisRun, QStringLiteral("thisRun")},
};

const QVector<QPair<BoardFieldRule::Trigger, QString>> kTriggerKeys = {
    {BoardFieldRule::Trigger::Continuous, QStringLiteral("continuous")},
    {BoardFieldRule::Trigger::AfterFinish, QStringLiteral("afterFinish")},
    {BoardFieldRule::Trigger::AfterDns, QStringLiteral("afterDns")},
    {BoardFieldRule::Trigger::AfterDnf, QStringLiteral("afterDnf")},
};

const QVector<QPair<BoardFieldRule::Alignment, QString>> kAlignmentKeys = {
    {BoardFieldRule::Alignment::Left, QStringLiteral("left")},
    {BoardFieldRule::Alignment::Right, QStringLiteral("right")},
    {BoardFieldRule::Alignment::Scroll, QStringLiteral("scroll")},
};

template<typename Enum>
QString keyFor(const QVector<QPair<Enum, QString>> &table, Enum value)
{
    for (const auto &pair : table) {
        if (pair.first == value)
            return pair.second;
    }
    return table.first().second;
}

template<typename Enum>
Enum enumFor(const QVector<QPair<Enum, QString>> &table, const QString &key, Enum fallback)
{
    for (const auto &pair : table) {
        if (pair.second == key)
            return pair.first;
    }
    return fallback;
}

} // namespace

QJsonObject BoardFieldRule::toJsonObject() const
{
    QJsonObject obj;
    obj[QStringLiteral("line")] = line;
    obj[QStringLiteral("startChar")] = startChar;
    obj[QStringLiteral("endChar")] = endChar;
    obj[QStringLiteral("alignment")] = keyFor(kAlignmentKeys, alignment);
    obj[QStringLiteral("scope")] = keyFor(kScopeKeys, scope);
    obj[QStringLiteral("contentType")] = keyFor(kContentTypeKeys, contentType);
    obj[QStringLiteral("fieldName")] = keyFor(kFieldNameKeys, fieldName);
    obj[QStringLiteral("scopeIndex")] = scopeIndex;
    obj[QStringLiteral("runSelector")] = keyFor(kRunSelectorKeys, runSelector);
    obj[QStringLiteral("prefix")] = prefix;
    obj[QStringLiteral("suffix")] = suffix;
    obj[QStringLiteral("contentStartChar")] = contentStartChar;
    obj[QStringLiteral("dateTimeFormat")] = dateTimeFormat;
    obj[QStringLiteral("staticText")] = staticText;
    obj[QStringLiteral("trigger")] = keyFor(kTriggerKeys, trigger);
    obj[QStringLiteral("startOffsetSec")] = startOffsetSec;
    obj[QStringLiteral("durationSec")] = durationSec;
    return obj;
}

BoardFieldRule BoardFieldRule::fromJsonObject(const QJsonObject &obj)
{
    BoardFieldRule rule;
    rule.line = obj.value(QStringLiteral("line")).toInt(1);
    rule.startChar = obj.value(QStringLiteral("startChar")).toInt(1);
    rule.endChar = obj.value(QStringLiteral("endChar")).toInt(8);
    rule.alignment =
        enumFor(kAlignmentKeys, obj.value(QStringLiteral("alignment")).toString(), Alignment::Right);
    rule.contentType =
        enumFor(kContentTypeKeys, obj.value(QStringLiteral("contentType")).toString(), ContentType::Field);
    rule.fieldName = enumFor(kFieldNameKeys, obj.value(QStringLiteral("fieldName")).toString(), FieldName::Bib);
    rule.scopeIndex = obj.value(QStringLiteral("scopeIndex")).toInt(obj.value(QStringLiteral("onCourseIndex")).toInt(0));
    rule.runSelector =
        enumFor(kRunSelectorKeys, obj.value(QStringLiteral("runSelector")).toString(), RunSelector::Total);
    rule.prefix = obj.value(QStringLiteral("prefix")).toString();
    rule.suffix = obj.value(QStringLiteral("suffix")).toString();
    rule.contentStartChar = obj.value(QStringLiteral("contentStartChar")).toInt(0);
    rule.dateTimeFormat = obj.value(QStringLiteral("dateTimeFormat")).toString(QStringLiteral("dd.MM.yyyy"));
    rule.staticText = obj.value(QStringLiteral("staticText")).toString();
    rule.trigger = enumFor(kTriggerKeys, obj.value(QStringLiteral("trigger")).toString(), Trigger::Continuous);
    rule.startOffsetSec = obj.value(QStringLiteral("startOffsetSec")).toInt(0);
    rule.durationSec = obj.value(QStringLiteral("durationSec")).toInt(5);

    // Rules saved before Scope existed: back then the trigger itself decided
    // which data a rule read - Continuous meant on-course, any event trigger
    // meant the held finish subject. Deriving the scope from it reproduces
    // the old behaviour exactly, so an existing board keeps showing what it
    // showed without anyone having to re-configure it.
    const QJsonValue scopeValue = obj.value(QStringLiteral("scope"));
    rule.scope = scopeValue.isString()
        ? enumFor(kScopeKeys, scopeValue.toString(), Scope::OnCourse)
        : (rule.trigger == Trigger::Continuous ? Scope::OnCourse : Scope::LastFinish);

    return rule;
}

QString scopeLabel(BoardFieldRule::Scope scope)
{
    switch (scope) {
    case BoardFieldRule::Scope::OnStart:
        return QStringLiteral("On start");
    case BoardFieldRule::Scope::OnCourse:
        return QStringLiteral("On course");
    case BoardFieldRule::Scope::OnFinish:
        return QStringLiteral("Finish list");
    case BoardFieldRule::Scope::LastFinish:
        return QStringLiteral("Last finish (held)");
    }
    return QString();
}

BoardFieldRule::Scope scopeFromLabel(const QString &label)
{
    for (const auto &pair : kScopeKeys) {
        if (scopeLabel(pair.first) == label)
            return pair.first;
    }
    return BoardFieldRule::Scope::OnCourse;
}

QStringList scopeLabels()
{
    QStringList labels;
    for (const auto &pair : kScopeKeys)
        labels.append(scopeLabel(pair.first));
    return labels;
}

QString contentTypeLabel(BoardFieldRule::ContentType type)
{
    switch (type) {
    case BoardFieldRule::ContentType::Field:
        return QStringLiteral("Field of competitor");
    case BoardFieldRule::ContentType::RunningTime:
        return QStringLiteral("Running time / time at finish");
    case BoardFieldRule::ContentType::Rank:
        return QStringLiteral("Rank");
    case BoardFieldRule::ContentType::StatusText:
        return QStringLiteral("Status text (DNS/DNF)");
    case BoardFieldRule::ContentType::Date:
        return QStringLiteral("Date");
    case BoardFieldRule::ContentType::DayTime:
        return QStringLiteral("Day time");
    case BoardFieldRule::ContentType::Text:
        return QStringLiteral("Static text");
    }
    return QString();
}

BoardFieldRule::ContentType contentTypeFromLabel(const QString &label)
{
    for (const auto &pair : kContentTypeKeys) {
        if (contentTypeLabel(pair.first) == label)
            return pair.first;
    }
    return BoardFieldRule::ContentType::Field;
}

QStringList contentTypeLabels()
{
    QStringList labels;
    for (const auto &pair : kContentTypeKeys)
        labels.append(contentTypeLabel(pair.first));
    return labels;
}

QString fieldNameLabel(BoardFieldRule::FieldName field)
{
    switch (field) {
    case BoardFieldRule::FieldName::Bib:
        return QStringLiteral("Bib");
    case BoardFieldRule::FieldName::Category:
        return QStringLiteral("Category");
    case BoardFieldRule::FieldName::FirstName:
        return QStringLiteral("First name");
    case BoardFieldRule::FieldName::LastName:
        return QStringLiteral("Last name");
    case BoardFieldRule::FieldName::Nation:
        return QStringLiteral("Nation");
    case BoardFieldRule::FieldName::Sex:
        return QStringLiteral("Sex");
    case BoardFieldRule::FieldName::Year:
        return QStringLiteral("Year");
    case BoardFieldRule::FieldName::Club:
        return QStringLiteral("Club");
    }
    return QString();
}

BoardFieldRule::FieldName fieldNameFromLabel(const QString &label)
{
    for (const auto &pair : kFieldNameKeys) {
        if (fieldNameLabel(pair.first) == label)
            return pair.first;
    }
    return BoardFieldRule::FieldName::Bib;
}

QStringList fieldNameLabels()
{
    QStringList labels;
    for (const auto &pair : kFieldNameKeys)
        labels.append(fieldNameLabel(pair.first));
    return labels;
}

QString runSelectorLabel(BoardFieldRule::RunSelector selector)
{
    switch (selector) {
    case BoardFieldRule::RunSelector::Total:
        return QStringLiteral("Total");
    case BoardFieldRule::RunSelector::ThisRun:
        return QStringLiteral("This run");
    }
    return QString();
}

BoardFieldRule::RunSelector runSelectorFromLabel(const QString &label)
{
    for (const auto &pair : kRunSelectorKeys) {
        if (runSelectorLabel(pair.first) == label)
            return pair.first;
    }
    return BoardFieldRule::RunSelector::Total;
}

QStringList runSelectorLabels()
{
    QStringList labels;
    for (const auto &pair : kRunSelectorKeys)
        labels.append(runSelectorLabel(pair.first));
    return labels;
}

QString triggerLabel(BoardFieldRule::Trigger trigger)
{
    switch (trigger) {
    case BoardFieldRule::Trigger::Continuous:
        return QStringLiteral("Continuous");
    case BoardFieldRule::Trigger::AfterFinish:
        return QStringLiteral("Event: After a finish");
    case BoardFieldRule::Trigger::AfterDns:
        return QStringLiteral("Event: After a DNS");
    case BoardFieldRule::Trigger::AfterDnf:
        return QStringLiteral("Event: After a DNF");
    }
    return QString();
}

BoardFieldRule::Trigger triggerFromLabel(const QString &label)
{
    for (const auto &pair : kTriggerKeys) {
        if (triggerLabel(pair.first) == label)
            return pair.first;
    }
    return BoardFieldRule::Trigger::Continuous;
}

QStringList triggerLabels()
{
    QStringList labels;
    for (const auto &pair : kTriggerKeys)
        labels.append(triggerLabel(pair.first));
    return labels;
}

QString alignmentLabel(BoardFieldRule::Alignment alignment)
{
    switch (alignment) {
    case BoardFieldRule::Alignment::Left:
        return QStringLiteral("Left");
    case BoardFieldRule::Alignment::Right:
        return QStringLiteral("Right");
    case BoardFieldRule::Alignment::Scroll:
        return QStringLiteral("Scrolling");
    }
    return QString();
}

BoardFieldRule::Alignment alignmentFromLabel(const QString &label)
{
    for (const auto &pair : kAlignmentKeys) {
        if (alignmentLabel(pair.first) == label)
            return pair.first;
    }
    return BoardFieldRule::Alignment::Right;
}

QStringList alignmentLabels()
{
    QStringList labels;
    for (const auto &pair : kAlignmentKeys)
        labels.append(alignmentLabel(pair.first));
    return labels;
}
