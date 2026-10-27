// Displays table, "Add/Edit display" and "Edit lines" dialogs: ported from
// SkiAlpBridgeDisplay (MainWindow), with the data of the OpenSkiPro2 API.

#include "ScoreboardModule.h"

#include "boardcontentcontroller.h"
#include "boardfieldrule.h"
#include "boardprogramengine.h"
#include "core/Log.h"
#include "core/Profile.h"
#include "core/RaceModel.h"
#include "displaydriver.h"
#include "ui/UiHelpers.h"
#include "dispthdis08driver.h"
#include "dispvolaboardprodriver.h"

#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

namespace {
constexpr int kNameColumn = 0;
constexpr int kTypeColumn = 1;
constexpr int kHostColumn = 2;
constexpr int kPortColumn = 3;
constexpr int kUpdateRateColumn = 4;
constexpr int kBrightnessColumn = 5;
constexpr int kActiveColumn = 6;
constexpr int kStatusColumn = 7;

QString formatBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QString formatAgo(const QDateTime &when)
{
    if (!when.isValid())
        return QStringLiteral("never");
    const qint64 secs = when.secsTo(QDateTime::currentDateTime());
    if (secs < 1)
        return QStringLiteral("just now");
    if (secs < 60)
        return QStringLiteral("%1s ago").arg(secs);
    return QStringLiteral("%1m %2s ago").arg(secs / 60).arg(secs % 60);
}

// Result hold (BoardContentController::finishHoldMs()/otherHoldMs()) is a
// manual, app-wide setting rather than derived from the lines - but a rule
// whose own [Start, Start+Duration) window reaches past the applicable
// hold would never get a chance to show (the finish subject moves on to
// the next queued racer first), so both the per-rule editor and the lines
// table flag that case instead of silently ignoring or clamping it.
int applicableHoldSec(BoardFieldRule::Trigger trigger, int finishHoldSec, int otherHoldSec)
{
    return trigger == BoardFieldRule::Trigger::AfterFinish ? finishHoldSec : otherHoldSec;
}

bool ruleExceedsHold(const BoardFieldRule &rule, int finishHoldSec, int otherHoldSec)
{
    if (rule.trigger == BoardFieldRule::Trigger::Continuous)
        return false;
    return (rule.startOffsetSec + rule.durationSec) > applicableHoldSec(rule.trigger, finishHoldSec, otherHoldSec);
}

// Starting point for a freshly added display's Timing program - not blank
// out of the box, editable via "Edit lines..." afterwards. Mirrors the real
// example from docs/board-display-system.md: a running/total time line,
// plus a bib + rank-in-parens-after-finish line.
QVector<BoardFieldRule> defaultTimingRules()
{
    QVector<BoardFieldRule> rules;

    BoardFieldRule time;
    time.line = 1;
    time.startChar = 1;
    time.endChar = 8;
    time.alignment = BoardFieldRule::Alignment::Right;
    time.scope = BoardFieldRule::Scope::OnCourse;
    time.contentType = BoardFieldRule::ContentType::RunningTime;
    time.trigger = BoardFieldRule::Trigger::Continuous;
    rules.append(time);

    BoardFieldRule bib;
    bib.line = 2;
    bib.startChar = 1;
    bib.endChar = 4;
    bib.alignment = BoardFieldRule::Alignment::Left;
    bib.scope = BoardFieldRule::Scope::OnCourse;
    bib.contentType = BoardFieldRule::ContentType::Field;
    bib.fieldName = BoardFieldRule::FieldName::Bib;
    bib.trigger = BoardFieldRule::Trigger::Continuous;
    rules.append(bib);

    BoardFieldRule rank;
    rank.line = 2;
    rank.startChar = 5;
    rank.endChar = 8;
    rank.alignment = BoardFieldRule::Alignment::Right;
    rank.scope = BoardFieldRule::Scope::LastFinish;
    rank.contentType = BoardFieldRule::ContentType::Rank;
    rank.prefix = QStringLiteral("(");
    rank.suffix = QStringLiteral(")");
    rank.trigger = BoardFieldRule::Trigger::AfterFinish;
    rank.startOffsetSec = 0;
    rank.durationSec = 10;
    rules.append(rank);

    return rules;
}

// Starting point for a freshly added display's Outside-timing program - a
// plain clock, shown whenever the bridge isn't actively engaged in a run.
QVector<BoardFieldRule> defaultOutsideTimingRules()
{
    QVector<BoardFieldRule> rules;

    BoardFieldRule clock;
    clock.line = 1;
    clock.startChar = 1;
    clock.endChar = 8;
    clock.alignment = BoardFieldRule::Alignment::Right;
    clock.contentType = BoardFieldRule::ContentType::DayTime;
    clock.dateTimeFormat = QStringLiteral("HH:mm:ss");
    clock.trigger = BoardFieldRule::Trigger::Continuous;
    rules.append(clock);

    return rules;
}

// Maps DisplayConfig::driverType (the persisted key) to what's shown in the
// "Add display" Type combo / Displays table.
QString driverTypeLabel(const QString &driverType)
{
    if (driverType == QStringLiteral("dispVolaBoardPro"))
        return QStringLiteral("VolaScoreBoard-Pro");
    if (driverType == QStringLiteral("thdis08"))
        return QStringLiteral("THDIS08");
    return driverType;
}

// Shared Type/Name/Host/Port/Update rate/Brightness/Active form for both
// "Add display..." and "Edit display..." - `config` supplies the initial
// values and receives the edited ones if accepted. Returns false (config
// left untouched) if the dialog was cancelled or required fields are empty.
bool editDisplayConfigDialog(QWidget *parent, const QString &title, DisplayConfig &config)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = new QFormLayout(&dialog);

    auto *typeCombo = new QComboBox(&dialog);
    typeCombo->addItem(driverTypeLabel(QStringLiteral("dispVolaBoardPro")), QStringLiteral("dispVolaBoardPro"));
    typeCombo->addItem(driverTypeLabel(QStringLiteral("thdis08")), QStringLiteral("thdis08"));
    typeCombo->setCurrentIndex(qMax(0, typeCombo->findData(config.driverType)));

    auto *nameEdit = new QLineEdit(config.name, &dialog);
    auto *hostEdit = new QLineEdit(config.host, &dialog);
    auto *portSpin = new QSpinBox(&dialog);
    portSpin->setRange(1, 65535);
    portSpin->setValue(config.port > 0 ? config.port : 5000);

    auto *updateRateCombo = new QComboBox(&dialog);
    updateRateCombo->addItem(QStringLiteral("1 fps"), 1);
    updateRateCombo->addItem(QStringLiteral("10 fps"), 10);
    updateRateCombo->setCurrentIndex(qMax(0, updateRateCombo->findData(config.updateRateFps)));

    auto *brightnessSpin = new QSpinBox(&dialog);
    brightnessSpin->setRange(1, 3);
    brightnessSpin->setValue(config.brightness > 0 ? config.brightness : 3);

    auto *activeCheck = new QCheckBox(&dialog);
    activeCheck->setChecked(config.active);

    form->addRow(QStringLiteral("Type:"), typeCombo);
    form->addRow(QStringLiteral("Name:"), nameEdit);
    form->addRow(QStringLiteral("Host/IP:"), hostEdit);
    form->addRow(QStringLiteral("Port:"), portSpin);
    form->addRow(QStringLiteral("Update rate:"), updateRateCombo);
    form->addRow(QStringLiteral("Brightness:"), brightnessSpin);
    form->addRow(QStringLiteral("Active:"), activeCheck);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (nameEdit->text().trimmed().isEmpty() || hostEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Missing fields"), QStringLiteral("Name and host are required."));
        return false;
    }

    config.driverType = typeCombo->currentData().toString();
    config.name = nameEdit->text().trimmed();
    config.host = hostEdit->text().trimmed();
    config.port = static_cast<quint16>(portSpin->value());
    config.updateRateFps = updateRateCombo->currentData().toInt();
    config.brightness = brightnessSpin->value();
    config.active = activeCheck->isChecked();
    return true;
}

// Short summary of a rule's content-type-specific detail, for the "Edit
// lines" table's Detail column (the full set of controls lives in the
// per-rule editor sub-dialog).
QString ruleDetailSummary(const BoardFieldRule &rule)
{
    // Scope::LastFinish has no list to index into, so the index is left out
    // of the summary there rather than shown as a meaningless "idx 0".
    const QString indexSuffix = rule.scope == BoardFieldRule::Scope::LastFinish
        ? QString()
        : QStringLiteral(", idx %1").arg(rule.scopeIndex);

    switch (rule.contentType) {
    case BoardFieldRule::ContentType::Field:
        return fieldNameLabel(rule.fieldName) + indexSuffix;
    case BoardFieldRule::ContentType::RunningTime:
    case BoardFieldRule::ContentType::Rank:
    case BoardFieldRule::ContentType::StatusText:
        return runSelectorLabel(rule.runSelector) + indexSuffix;
    case BoardFieldRule::ContentType::Date:
    case BoardFieldRule::ContentType::DayTime:
        return rule.dateTimeFormat;
    case BoardFieldRule::ContentType::Text:
        return rule.staticText;
    }
    return QString();
}

// One rule's row in an "Edit lines" tab's table.
void writeLineRow(QTableWidget *table, int row, const BoardFieldRule &rule, int finishHoldSec, int otherHoldSec)
{
    table->setItem(row, 0, new QTableWidgetItem(QString::number(rule.line)));
    table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("%1-%2").arg(rule.startChar).arg(rule.endChar)));
    table->setItem(row, 2, new QTableWidgetItem(alignmentLabel(rule.alignment)));
    table->setItem(row, 3, new QTableWidgetItem(scopeLabel(rule.scope)));
    table->setItem(row, 4, new QTableWidgetItem(contentTypeLabel(rule.contentType)));
    table->setItem(row, 5, new QTableWidgetItem(ruleDetailSummary(rule)));
    const QString affix = (rule.prefix.isEmpty() && rule.suffix.isEmpty())
        ? QString()
        : rule.prefix + QStringLiteral("...") + rule.suffix;
    table->setItem(row, 6, new QTableWidgetItem(affix));
    table->setItem(row, 7, new QTableWidgetItem(triggerLabel(rule.trigger)));
    const bool continuous = rule.trigger == BoardFieldRule::Trigger::Continuous;
    auto *startItem = new QTableWidgetItem(continuous ? QString() : QString::number(rule.startOffsetSec));
    auto *durationItem = new QTableWidgetItem(continuous ? QString() : QString::number(rule.durationSec));
    if (ruleExceedsHold(rule, finishHoldSec, otherHoldSec)) {
        const int holdSec = applicableHoldSec(rule.trigger, finishHoldSec, otherHoldSec);
        const QString tip = QStringLiteral("Start + Duration (%1s) exceeds the configured Result hold (%2s) for "
                                            "this trigger - this rule won't be visible after the hold ends.")
                                 .arg(rule.startOffsetSec + rule.durationSec)
                                 .arg(holdSec);
        for (QTableWidgetItem *item : {startItem, durationItem}) {
            item->setForeground(QBrush(QColor(Qt::red)));
            item->setToolTip(tip);
        }
    }
    table->setItem(row, 8, startItem);
    table->setItem(row, 9, durationItem);
    table->setItem(row, 10, new QTableWidgetItem(QStringLiteral("(inactive)")));
}

// Sub-dialog for one rule - New/Edit/Copy all funnel through this. Returns
// false (rule left untouched) if cancelled. finishHoldSec/otherHoldSec are
// the app-wide Result hold settings, just for the warning below Start/
// Duration - they're not editable from here.
bool editLineRuleDialog(QWidget *parent, BoardFieldRule &rule, int finishHoldSec, int otherHoldSec)
{
    QDialog sub(parent);
    sub.setWindowTitle(QStringLiteral("Line"));
    auto *form = new QFormLayout(&sub);

    auto *lineSpin = new QSpinBox(&sub);
    lineSpin->setRange(1, 64);
    lineSpin->setValue(rule.line);
    form->addRow(QStringLiteral("Line:"), lineSpin);

    auto *startCharSpin = new QSpinBox(&sub);
    startCharSpin->setRange(1, 999);
    startCharSpin->setValue(rule.startChar);
    auto *endCharSpin = new QSpinBox(&sub);
    endCharSpin->setRange(1, 999);
    endCharSpin->setValue(rule.endChar);
    auto *charsRow = new QHBoxLayout();
    charsRow->addWidget(startCharSpin);
    charsRow->addWidget(new QLabel(QStringLiteral("to"), &sub));
    charsRow->addWidget(endCharSpin);
    form->addRow(QStringLiteral("Characters:"), charsRow);

    auto *alignCombo = new QComboBox(&sub);
    alignCombo->addItems(alignmentLabels());
    alignCombo->setCurrentIndex(alignCombo->findText(alignmentLabel(rule.alignment)));
    form->addRow(QStringLiteral("Alignment:"), alignCombo);

    auto *scopeLabelWidget = new QLabel(QStringLiteral("Scope:"), &sub);
    auto *scopeCombo = new QComboBox(&sub);
    scopeCombo->addItems(scopeLabels());
    scopeCombo->setCurrentIndex(scopeCombo->findText(scopeLabel(rule.scope)));
    scopeCombo->setToolTip(QStringLiteral(
        "Which data this line reads from. \"On start\", \"On course\" and \"Finish list\" are live lists picked by "
        "Scope index; \"Last finish (held)\" is the single result currently being held (Result hold), which is what "
        "an event Trigger below is timed from. Scope and Trigger are independent - e.g. Finish list + \"After a "
        "finish\" shows a results row only during the seconds after somebody finishes.\n\n"
        "\"On start\" carries the competitor's details only (bib, name, category, ...) - the start list has no "
        "times, ranks or statuses, so any other Content stays blank there."));
    form->addRow(scopeLabelWidget, scopeCombo);

    auto *contentCombo = new QComboBox(&sub);
    contentCombo->addItems(contentTypeLabels());
    contentCombo->setCurrentIndex(contentCombo->findText(contentTypeLabel(rule.contentType)));
    form->addRow(QStringLiteral("Content:"), contentCombo);

    auto *fieldNameLabelWidget = new QLabel(QStringLiteral("Field:"), &sub);
    auto *fieldNameCombo = new QComboBox(&sub);
    fieldNameCombo->addItems(fieldNameLabels());
    fieldNameCombo->setCurrentIndex(fieldNameCombo->findText(fieldNameLabel(rule.fieldName)));
    form->addRow(fieldNameLabelWidget, fieldNameCombo);

    auto *scopeIndexLabelWidget = new QLabel(QStringLiteral("Scope index:"), &sub);
    auto *scopeIndexSpin = new QSpinBox(&sub);
    scopeIndexSpin->setRange(0, 20);
    scopeIndexSpin->setValue(rule.scopeIndex);
    scopeIndexSpin->setToolTip(
        QStringLiteral("Position within the Scope's list, 0-based. On start: 0 = first competitor ready at the start "
                       "gate. On course: 0 = first/oldest competitor still on course. Finish list: 0 = most recent "
                       "finisher, 1 = the one before, ... An index with nobody there stays blank."));
    form->addRow(scopeIndexLabelWidget, scopeIndexSpin);

    auto *runSelectorLabelWidget = new QLabel(QStringLiteral("Run:"), &sub);
    auto *runSelectorCombo = new QComboBox(&sub);
    runSelectorCombo->addItems(runSelectorLabels());
    runSelectorCombo->setCurrentIndex(runSelectorCombo->findText(runSelectorLabel(rule.runSelector)));
    form->addRow(runSelectorLabelWidget, runSelectorCombo);

    auto *prefixEdit = new QLineEdit(rule.prefix, &sub);
    form->addRow(QStringLiteral("Prefix:"), prefixEdit);
    auto *suffixEdit = new QLineEdit(rule.suffix, &sub);
    form->addRow(QStringLiteral("Suffix:"), suffixEdit);

    auto *contentStartCharSpin = new QSpinBox(&sub);
    contentStartCharSpin->setRange(0, 999);
    contentStartCharSpin->setValue(rule.contentStartChar);
    contentStartCharSpin->setToolTip(
        QStringLiteral("Offset into the resolved content text (not the Characters position above) - lets one long "
                       "piece of content be split across multiple lines/rules. Note this is 0-based (0 = no "
                       "offset, start from the first character), unlike Characters above which is 1-based - they "
                       "count different things (an offset vs. a position) so aren't meant to line up."));
    form->addRow(QStringLiteral("Content start char:"), contentStartCharSpin);

    auto *dateTimeFormatLabelWidget = new QLabel(QStringLiteral("Date/time format:"), &sub);
    auto *dateTimeFormatEdit = new QLineEdit(rule.dateTimeFormat, &sub);
    dateTimeFormatEdit->setToolTip(QStringLiteral("Qt date/time format string, e.g. \"dd.MM.yyyy\" or \"HH:mm:ss\" - "
                                                   "y/M/d = year/month/day, H/m/s = hour/minute/second."));
    form->addRow(dateTimeFormatLabelWidget, dateTimeFormatEdit);

    auto *staticTextLabelWidget = new QLabel(QStringLiteral("Text:"), &sub);
    auto *staticTextEdit = new QLineEdit(rule.staticText, &sub);
    form->addRow(staticTextLabelWidget, staticTextEdit);

    auto *triggerCombo = new QComboBox(&sub);
    triggerCombo->addItems(triggerLabels());
    triggerCombo->setCurrentIndex(triggerCombo->findText(triggerLabel(rule.trigger)));
    form->addRow(QStringLiteral("Trigger:"), triggerCombo);

    auto *startLabel = new QLabel(QStringLiteral("Start (s):"), &sub);
    auto *startSpin = new QSpinBox(&sub);
    startSpin->setRange(0, 600);
    startSpin->setValue(rule.startOffsetSec);
    form->addRow(startLabel, startSpin);

    auto *durationLabel = new QLabel(QStringLiteral("Duration (s):"), &sub);
    auto *durationSpin = new QSpinBox(&sub);
    durationSpin->setRange(1, 600);
    durationSpin->setValue(rule.durationSec > 0 ? rule.durationSec : 5);
    form->addRow(durationLabel, durationSpin);

    auto *holdWarningLabel = new QLabel(&sub);
    holdWarningLabel->setWordWrap(true);
    holdWarningLabel->setStyleSheet(QStringLiteral("color: #b45309;"));
    form->addRow(holdWarningLabel);

    auto updateHoldWarning = [=]() {
        const auto trigger = triggerFromLabel(triggerCombo->currentText());
        if (trigger == BoardFieldRule::Trigger::Continuous) {
            holdWarningLabel->clear();
            return;
        }
        const int holdSec = applicableHoldSec(trigger, finishHoldSec, otherHoldSec);
        const int endSec = startSpin->value() + durationSpin->value();
        holdWarningLabel->setText(endSec > holdSec
                                       ? QStringLiteral("⚠ Start + Duration (%1s) exceeds the configured Result "
                                                         "hold (%2s) for this trigger - this rule won't be visible "
                                                         "after the hold ends.")
                                             .arg(endSec)
                                             .arg(holdSec)
                                       : QString());
    };

    auto updateVisibility = [=]() {
        const auto contentType = contentTypeFromLabel(contentCombo->currentText());
        const bool isField = contentType == BoardFieldRule::ContentType::Field;
        const bool usesRunSelector = contentType == BoardFieldRule::ContentType::RunningTime
            || contentType == BoardFieldRule::ContentType::Rank
            || contentType == BoardFieldRule::ContentType::StatusText;
        const bool isDateTime = contentType == BoardFieldRule::ContentType::Date
            || contentType == BoardFieldRule::ContentType::DayTime;
        const bool isText = contentType == BoardFieldRule::ContentType::Text;
        // Date/DayTime/Text don't read race data at all, so no scope applies
        // to them; Last finish is a single held result rather than a list, so
        // it has nothing to index into.
        const bool usesScope = isField || usesRunSelector;
        const bool usesScopeIndex =
            usesScope && scopeFromLabel(scopeCombo->currentText()) != BoardFieldRule::Scope::LastFinish;

        fieldNameLabelWidget->setVisible(isField);
        fieldNameCombo->setVisible(isField);
        scopeLabelWidget->setVisible(usesScope);
        scopeCombo->setVisible(usesScope);
        scopeIndexLabelWidget->setVisible(usesScopeIndex);
        scopeIndexSpin->setVisible(usesScopeIndex);
        runSelectorLabelWidget->setVisible(usesRunSelector);
        runSelectorCombo->setVisible(usesRunSelector);
        dateTimeFormatLabelWidget->setVisible(isDateTime);
        dateTimeFormatEdit->setVisible(isDateTime);
        staticTextLabelWidget->setVisible(isText);
        staticTextEdit->setVisible(isText);

        const bool continuous = triggerCombo->currentText() == triggerLabel(BoardFieldRule::Trigger::Continuous);
        startLabel->setVisible(!continuous);
        startSpin->setVisible(!continuous);
        durationLabel->setVisible(!continuous);
        durationSpin->setVisible(!continuous);
    };
    QObject::connect(contentCombo, &QComboBox::currentTextChanged, &sub, updateVisibility);
    QObject::connect(scopeCombo, &QComboBox::currentTextChanged, &sub, updateVisibility);
    QObject::connect(triggerCombo, &QComboBox::currentTextChanged, &sub, updateVisibility);
    QObject::connect(triggerCombo, &QComboBox::currentTextChanged, &sub, updateHoldWarning);
    QObject::connect(startSpin, &QSpinBox::valueChanged, &sub, updateHoldWarning);
    QObject::connect(durationSpin, &QSpinBox::valueChanged, &sub, updateHoldWarning);
    updateVisibility();
    updateHoldWarning();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &sub);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &sub, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &sub, &QDialog::reject);
    form->addRow(buttons);

    if (sub.exec() != QDialog::Accepted)
        return false;

    rule.line = lineSpin->value();
    rule.startChar = startCharSpin->value();
    rule.endChar = endCharSpin->value();
    rule.alignment = alignmentFromLabel(alignCombo->currentText());
    rule.scope = scopeFromLabel(scopeCombo->currentText());
    rule.contentType = contentTypeFromLabel(contentCombo->currentText());
    rule.fieldName = fieldNameFromLabel(fieldNameCombo->currentText());
    rule.scopeIndex = scopeIndexSpin->value();
    rule.runSelector = runSelectorFromLabel(runSelectorCombo->currentText());
    rule.prefix = prefixEdit->text();
    rule.suffix = suffixEdit->text();
    rule.contentStartChar = contentStartCharSpin->value();
    rule.dateTimeFormat = dateTimeFormatEdit->text();
    rule.staticText = staticTextEdit->text();
    rule.trigger = triggerFromLabel(triggerCombo->currentText());
    rule.startOffsetSec = startSpin->value();
    rule.durationSec = durationSpin->value();
    return true;
}

// Builds the table + New/Edit/Delete/Move Up/Down UI for one line program
// (Timing or Outside-timing) - shared between "Edit lines..."'s two tabs.
// `rules` must outlive the returned widget (kept alive by the same
// QDialog::exec() call that owns it).
QWidget *buildLineProgramEditor(QDialog *dialogParent, BoardProgramEngine *engine, bool isTimingProgram,
                                 QVector<BoardFieldRule> &rules, int finishHoldSec, int otherHoldSec)
{
    auto *tab = new QWidget(dialogParent);
    auto *layout = new QVBoxLayout(tab);

    auto *table = new QTableWidget(0, 11, tab);
    table->setHorizontalHeaderLabels({QStringLiteral("Line"), QStringLiteral("Chars"), QStringLiteral("Align"),
                                       QStringLiteral("Scope"), QStringLiteral("Content"), QStringLiteral("Detail"),
                                       QStringLiteral("Affix"), QStringLiteral("Trigger"), QStringLiteral("Start (s)"),
                                       QStringLiteral("Duration (s)"), QStringLiteral("Current text")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table);

    auto refreshTable = [table, &rules, finishHoldSec, otherHoldSec]() {
        table->setRowCount(rules.size());
        for (int i = 0; i < rules.size(); ++i)
            writeLineRow(table, i, rules.at(i), finishHoldSec, otherHoldSec);
    };
    refreshTable();
    table->resizeColumnsToContents();

    auto applyRules = [engine, isTimingProgram, &rules]() {
        if (isTimingProgram)
            engine->setTimingRules(rules);
        else
            engine->setOutsideTimingRules(rules);
    };

    // Live preview column - Qt keeps processing timers/signals while this
    // dialog's modal event loop runs. Each row is evaluated from its own
    // rule via previewText(), not from the merged per-slot fieldsChanged
    // payload - that merged view collapses same-slot rules into a single
    // "winner" and only reflects whichever program currently drives the
    // display, so matching rows against it by line/chars leaked one rule's
    // text into unrelated rows (including rows on the other tab).
    QObject::connect(engine, &BoardProgramEngine::fieldsChanged, table,
                      [table, engine, &rules](const QVector<BoardProgramEngine::RenderedField> &) {
                          for (int i = 0; i < table->rowCount() && i < rules.size(); ++i) {
                              bool active = false;
                              const QString text = engine->previewText(rules.at(i), &active);
                              table->item(i, 10)->setText(active ? text : QStringLiteral("(inactive)"));
                          }
                      });

    auto *buttonsRow = new QHBoxLayout();
    auto *newButton = new QPushButton(QStringLiteral("New"), tab);
    auto *editButton = new QPushButton(QStringLiteral("Edit"), tab);
    auto *copyButton = new QPushButton(QStringLiteral("Copy"), tab);
    auto *deleteButton = new QPushButton(QStringLiteral("Delete"), tab);
    auto *upButton = new QPushButton(QStringLiteral("Move Up"), tab);
    auto *downButton = new QPushButton(QStringLiteral("Move Down"), tab);
    buttonsRow->addWidget(newButton);
    buttonsRow->addWidget(editButton);
    buttonsRow->addWidget(copyButton);
    buttonsRow->addWidget(deleteButton);
    buttonsRow->addStretch(1);
    buttonsRow->addWidget(upButton);
    buttonsRow->addWidget(downButton);
    layout->addLayout(buttonsRow);

    QObject::connect(newButton, &QPushButton::clicked, tab,
                      [dialogParent, &rules, refreshTable, applyRules, finishHoldSec, otherHoldSec]() {
                          BoardFieldRule rule;
                          if (editLineRuleDialog(dialogParent, rule, finishHoldSec, otherHoldSec)) {
                              rules.append(rule);
                              refreshTable();
                              applyRules();
                          }
                      });
    auto editRow = [dialogParent, table, &rules, refreshTable, applyRules, finishHoldSec, otherHoldSec](int r) {
        if (r < 0 || r >= rules.size())
            return;
        if (editLineRuleDialog(dialogParent, rules[r], finishHoldSec, otherHoldSec)) {
            refreshTable();
            applyRules();
        }
    };
    QObject::connect(editButton, &QPushButton::clicked, tab,
                      [table, editRow]() { editRow(table->currentRow()); });
    QObject::connect(table, &QTableWidget::cellDoubleClicked, tab,
                      [editRow](int row, int /*column*/) { editRow(row); });
    QObject::connect(copyButton, &QPushButton::clicked, tab,
                      [dialogParent, table, &rules, refreshTable, applyRules, finishHoldSec, otherHoldSec]() {
                          const int r = table->currentRow();
                          if (r < 0 || r >= rules.size())
                              return;
                          BoardFieldRule rule = rules.at(r);
                          if (editLineRuleDialog(dialogParent, rule, finishHoldSec, otherHoldSec)) {
                              rules.append(rule);
                              refreshTable();
                              applyRules();
                          }
                      });
    QObject::connect(deleteButton, &QPushButton::clicked, tab, [table, &rules, refreshTable, applyRules]() {
        const int r = table->currentRow();
        if (r < 0)
            return;
        rules.removeAt(r);
        refreshTable();
        applyRules();
    });
    QObject::connect(upButton, &QPushButton::clicked, tab, [table, &rules, refreshTable, applyRules]() {
        const int r = table->currentRow();
        if (r <= 0)
            return;
        rules.swapItemsAt(r, r - 1);
        refreshTable();
        table->selectRow(r - 1);
        applyRules();
    });
    QObject::connect(downButton, &QPushButton::clicked, tab, [table, &rules, refreshTable, applyRules]() {
        const int r = table->currentRow();
        if (r < 0 || r >= rules.size() - 1)
            return;
        rules.swapItemsAt(r, r + 1);
        refreshTable();
        table->selectRow(r + 1);
        applyRules();
    });

    return tab;
}

} // namespace

namespace {
constexpr int kColumns = 8;

DisplayDriver *makeDriver(const DisplayConfig &config, QObject *parent)
{
    if (config.driverType == QStringLiteral("thdis08"))
        return new DispThdis08Driver(config.name, config.host, config.port, parent);
    return new DispVolaBoardProDriver(config.name, config.host, config.port, parent);
}

void feedEngine(BoardProgramEngine *engine, BoardContentController *content, RaceModel *model)
{
    // current state first, then every change
    engine->onOnStartChanged(content->onStartCompetitors());
    engine->onOnCourseChanged(content->onCourseSubjects());
    engine->onFinishListChanged(content->finishListSubjects());
    engine->onFinishSubjectChanged(content->currentFinishSubject());
    engine->onClockOffsetChanged(model->clockOffsetMs(), model->clockSynced());
    engine->onTimingModeChanged(model->timingActive());
    QObject::connect(content, &BoardContentController::onStartChanged, engine, &BoardProgramEngine::onOnStartChanged);
    QObject::connect(content, &BoardContentController::onCourseChanged, engine, &BoardProgramEngine::onOnCourseChanged);
    QObject::connect(content, &BoardContentController::finishListChanged, engine, &BoardProgramEngine::onFinishListChanged);
    QObject::connect(content, &BoardContentController::finishSubjectChanged, engine,
                     &BoardProgramEngine::onFinishSubjectChanged);
    QObject::connect(model, &RaceModel::clockChanged, engine,
                     [model, engine] { engine->onClockOffsetChanged(model->clockOffsetMs(), model->clockSynced()); });
    // not connected to OpenSkiPro2 or no timing window: Outside timing, never frozen race data
    QObject::connect(model, &RaceModel::timingChanged, engine, [engine](bool active, int) { engine->onTimingModeChanged(active); });
}
} // namespace

ScoreboardModule::ScoreboardModule(RaceModel *model, QObject *parent)
    : Module(model, parent)
    , m_content(new BoardContentController(model, this))
    , m_statsTimer(new QTimer(this))
{
    load();
    // byte counts and the "last sent Ns ago" text keep advancing
    connect(m_statsTimer, &QTimer::timeout, this, &ScoreboardModule::refreshStatus);
    m_statsTimer->start(1000);
}

ScoreboardModule::~ScoreboardModule()
{
    setRunning(false);
}

QString ScoreboardModule::stateText() const
{
    if (!m_running)
        return tr("stopped");
    int connected = 0;
    for (const auto &d : m_drivers)
        if (d && d->isConnected())
            ++connected;
    return tr("%1 of %2 boards connected").arg(connected).arg(m_configs.size());
}

void ScoreboardModule::load()
{
    QSettings &s = Profile::settings();
    s.beginGroup(id());
    m_configs = DisplayConfigStore::load(s);
    m_content->setRankField(s.value(QStringLiteral("rankField"), QStringLiteral("group")).toString());
    m_content->setFinishHoldMs(s.value(QStringLiteral("holdFinishSeconds"), 10).toInt() * 1000);
    m_content->setOtherHoldMs(s.value(QStringLiteral("holdOtherSeconds"), 5).toInt() * 1000);
    s.endGroup();
    m_drivers.resize(m_configs.size());
    m_engines.resize(m_configs.size());
}

void ScoreboardModule::save()
{
    QSettings &s = Profile::settings();
    s.beginGroup(id());
    DisplayConfigStore::save(s, m_configs);
    s.setValue(QStringLiteral("rankField"), m_content->rankField());
    s.setValue(QStringLiteral("holdFinishSeconds"), m_content->finishHoldMs() / 1000);
    s.setValue(QStringLiteral("holdOtherSeconds"), m_content->otherHoldMs() / 1000);
    s.endGroup();
    s.sync();
}

void ScoreboardModule::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    for (int i = 0; i < m_configs.size(); ++i) {
        if (running)
            startDisplay(i);
        else
            stopDisplay(i);
    }
    Log::write(QStringLiteral("scoreboard"), running ? tr("started") : tr("stopped"));
    refreshStatus();
    Q_EMIT stateChanged();
}

void ScoreboardModule::startDisplay(int i)
{
    const DisplayConfig &config = m_configs.at(i);
    DisplayDriver *driver = makeDriver(config, this);
    driver->setBrightness(config.brightness);
    driver->setActive(config.active);
    connect(driver, &DisplayDriver::statusMessage, this,
            [name = config.name](const QString &message) { Log::write(name, message); });
    connect(driver, &DisplayDriver::connectionStateChanged, this, [this](bool) {
        refreshStatus();
        Q_EMIT stateChanged();
    });

    auto *engine = new BoardProgramEngine(this);
    engine->setUpdateRateFps(config.updateRateFps);
    engine->setTimingRules(config.timingFields);
    engine->setOutsideTimingRules(config.outsideTimingFields);
    feedEngine(engine, m_content, model());
    connect(engine, &BoardProgramEngine::fieldsChanged, driver, &DisplayDriver::updateFields);

    m_drivers[i] = driver;
    m_engines[i] = engine;
}

void ScoreboardModule::stopDisplay(int i)
{
    if (m_engines.value(i))
        delete m_engines[i].data();
    if (DisplayDriver *d = m_drivers.value(i)) {
        d->disconnect(this);
        d->setActive(false); // blanks the board, then disconnects
        d->deleteLater();
    }
    m_drivers[i] = nullptr;
    m_engines[i] = nullptr;
}

QWidget *ScoreboardModule::createPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);

    m_table = new QTableWidget(0, kColumns, page);
    m_table->setHorizontalHeaderLabels({ tr("Name"), tr("Type"), tr("Host"), tr("Port"), tr("Update rate"),
                                         tr("Brightness"), tr("Active"), tr("Status") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    UiHelpers::styleGrid(m_table);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        m_table->selectRow(row);
        editDisplay();
    });
    layout->addWidget(m_table, 1);

    auto *buttons = new QHBoxLayout();
    auto button = [&](const char *icon, const QString &text, void (ScoreboardModule::*slot)()) {
        auto *b = UiHelpers::button(QLatin1String(icon), text, page);
        connect(b, &QPushButton::clicked, this, slot);
        buttons->addWidget(b);
    };
    button("add", tr("Add display..."), &ScoreboardModule::addDisplay);
    button("modify", tr("Edit display..."), &ScoreboardModule::editDisplay);
    button("params", tr("Edit lines..."), &ScoreboardModule::editLines);
    button("delete", tr("Remove"), &ScoreboardModule::removeDisplay);
    buttons->addSpacing(16);
    button("import", tr("Import from SkiAlpBridgeDisplay..."), &ScoreboardModule::import);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto *settings = new QHBoxLayout();
    settings->addWidget(new QLabel(tr("Rank shown:"), page));
    m_rankField = new QComboBox(page);
    m_rankField->addItem(tr("Rank in the output group (category)"), QStringLiteral("group"));
    m_rankField->addItem(tr("Overall rank"), QStringLiteral("overall"));
    m_rankField->setToolTip(tr("Applies to all displays. The output group is the first one the competitor "
                               "belongs to (Event > Output > Output groups in OpenSkiPro2)."));
    m_rankField->setCurrentIndex(qMax(0, m_rankField->findData(m_content->rankField())));
    connect(m_rankField, &QComboBox::currentIndexChanged, this, [this] {
        m_content->setRankField(m_rankField->currentData().toString());
        Log::write(QStringLiteral("scoreboard"), tr("rank shown: %1").arg(m_rankField->currentText()));
        save();
    });
    settings->addWidget(m_rankField);
    settings->addSpacing(16);
    settings->addWidget(new QLabel(tr("Result hold QLF (s):"), page));
    m_finishHold = new QSpinBox(page);
    m_finishHold->setRange(1, 60);
    m_finishHold->setValue(m_content->finishHoldMs() / 1000);
    m_finishHold->setToolTip(tr("How long a regular finish (status QLF) stays the current finish subject "
                                "before the queue advances to the next racer."));
    connect(m_finishHold, &QSpinBox::valueChanged, this, [this](int s) {
        m_content->setFinishHoldMs(s * 1000);
        save();
    });
    settings->addWidget(m_finishHold);
    settings->addWidget(new QLabel(tr("other (s):"), page));
    m_otherHold = new QSpinBox(page);
    m_otherHold->setRange(1, 60);
    m_otherHold->setValue(m_content->otherHoldMs() / 1000);
    m_otherHold->setToolTip(tr("How long any other outcome (DNS/DNF/DSQ) stays the current finish subject."));
    connect(m_otherHold, &QSpinBox::valueChanged, this, [this](int s) {
        m_content->setOtherHoldMs(s * 1000);
        save();
    });
    settings->addWidget(m_otherHold);
    settings->addStretch(1);
    layout->addLayout(settings);

    refreshTable();
    return page;
}

int ScoreboardModule::selectedRow() const
{
    return m_table ? m_table->currentRow() : -1;
}

void ScoreboardModule::refreshTable()
{
    if (!m_table)
        return;
    m_table->setRowCount(int(m_configs.size()));
    for (int row = 0; row < m_configs.size(); ++row) {
        const DisplayConfig &config = m_configs.at(row);
        m_table->setItem(row, 0, new QTableWidgetItem(config.name));
        m_table->setItem(row, 1, new QTableWidgetItem(driverTypeLabel(config.driverType)));
        m_table->setItem(row, 2, new QTableWidgetItem(config.host));
        m_table->setItem(row, 3, new QTableWidgetItem(QString::number(config.port)));
        m_table->setItem(row, 4, new QTableWidgetItem(tr("%1 fps").arg(config.updateRateFps)));
        m_table->setItem(row, 5, new QTableWidgetItem(QString::number(config.brightness)));
        auto *active = new QCheckBox(m_table);
        active->setChecked(config.active);
        connect(active, &QCheckBox::toggled, this, [this, active](bool checked) {
            for (int r = 0; r < m_table->rowCount(); ++r) {
                if (m_table->cellWidget(r, 6) != active)
                    continue;
                m_configs[r].active = checked;
                if (DisplayDriver *d = m_drivers.value(r))
                    d->setActive(checked);
                save();
                break;
            }
        });
        m_table->setCellWidget(row, 6, active);
        m_table->setItem(row, 7, new QTableWidgetItem());
    }
    m_table->resizeColumnsToContents();
    refreshStatus();
}

void ScoreboardModule::refreshStatus()
{
    if (!m_table)
        return;
    for (int i = 0; i < m_configs.size() && i < m_table->rowCount(); ++i) {
        QTableWidgetItem *item = m_table->item(i, 7);
        if (!item)
            continue;
        const DisplayDriver *driver = m_drivers.value(i);
        if (!driver) {
            item->setText(tr("module stopped"));
            item->setForeground(QBrush(QColor(Qt::gray)));
            continue;
        }
        const bool connected = driver->isConnected();
        item->setText(tr("%1 | sent %2 / recv %3 | last sent %4")
                          .arg(connected ? tr("Connected") : tr("Disconnected - retrying"),
                               formatBytes(driver->bytesSent()), formatBytes(driver->bytesReceived()),
                               formatAgo(driver->lastSentAt())));
        item->setForeground(QBrush(connected ? QColor(Qt::darkGreen) : QColor(Qt::red)));
    }
}

void ScoreboardModule::addDisplay()
{
    DisplayConfig config;
    config.port = 5000;
    if (!editDisplayConfigDialog(m_table, tr("Add display"), config))
        return;
    config.timingFields = defaultTimingRules();
    config.outsideTimingFields = defaultOutsideTimingRules();
    m_configs.append(config);
    m_drivers.append(nullptr);
    m_engines.append(nullptr);
    if (m_running)
        startDisplay(int(m_configs.size()) - 1);
    save();
    refreshTable();
}

void ScoreboardModule::removeDisplay()
{
    const int row = selectedRow();
    if (row < 0)
        return;
    stopDisplay(row);
    m_configs.removeAt(row);
    m_drivers.removeAt(row);
    m_engines.removeAt(row);
    save();
    refreshTable();
}

void ScoreboardModule::editDisplay()
{
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(m_table, tr("No display selected"), tr("Select a display in the list first."));
        return;
    }
    DisplayConfig config = m_configs.at(row);
    if (!editDisplayConfigDialog(m_table, tr("Edit display - %1").arg(config.name), config))
        return;
    m_configs[row] = config;
    // a new driver for any change of the target (simplest and always right)
    if (m_running) {
        stopDisplay(row);
        startDisplay(row);
    }
    save();
    refreshTable();
}

void ScoreboardModule::editLines()
{
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(m_table, tr("No display selected"), tr("Select a display in the list first."));
        return;
    }
    // the preview needs an engine: the running one, or a temporary one fed with the live state
    BoardProgramEngine *engine = m_engines.value(row);
    std::unique_ptr<BoardProgramEngine> temporary;
    if (!engine) {
        temporary = std::make_unique<BoardProgramEngine>();
        engine = temporary.get();
        feedEngine(engine, m_content, model());
    }
    const QVector<BoardFieldRule> originalTiming = m_configs.at(row).timingFields;
    const QVector<BoardFieldRule> originalOutside = m_configs.at(row).outsideTimingFields;
    QVector<BoardFieldRule> timingRules = originalTiming;
    QVector<BoardFieldRule> outsideRules = originalOutside;
    engine->setTimingRules(timingRules);
    engine->setOutsideTimingRules(outsideRules);

    QDialog dialog(m_table);
    dialog.setWindowTitle(tr("Edit lines - %1").arg(m_configs.at(row).name));
    dialog.resize(900, 480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(tr("Current text always previews what each rule would show right now, on both tabs, "
                               "regardless of which program (Timing or Outside-timing) is actually driving the "
                               "display - only the output shown on the physical display follows the active program."),
                            &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    const int finishHoldSec = m_content->finishHoldMs() / 1000;
    const int otherHoldSec = m_content->otherHoldMs() / 1000;
    auto *tabs = new QTabWidget(&dialog);
    tabs->addTab(buildLineProgramEditor(&dialog, engine, true, timingRules, finishHoldSec, otherHoldSec), tr("Timing"));
    tabs->addTab(buildLineProgramEditor(&dialog, engine, false, outsideRules, finishHoldSec, otherHoldSec),
                 tr("Outside timing"));
    layout->addWidget(tabs, 1);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(box);

    if (dialog.exec() == QDialog::Accepted) {
        m_configs[row].timingFields = timingRules;
        m_configs[row].outsideTimingFields = outsideRules;
        save();
    } else if (!temporary) {
        engine->setTimingRules(originalTiming);
        engine->setOutsideTimingRules(originalOutside);
    }
}

bool ScoreboardModule::importSkiAlpBridgeDisplay(const QString &iniPath, QString *error)
{
    QSettings ini(iniPath, QSettings::IniFormat);
    const QVector<DisplayConfig> imported = DisplayConfigStore::load(ini);
    if (imported.isEmpty()) {
        if (error)
            *error = tr("No displays in %1 - is it a SkiAlpBridgeDisplay.ini?").arg(iniPath);
        return false;
    }
    for (const DisplayConfig &c : imported) {
        m_configs.append(c);
        m_drivers.append(nullptr);
        m_engines.append(nullptr);
        if (m_running)
            startDisplay(int(m_configs.size()) - 1);
    }
    // rank_cat (rank in the category) = rank in the output group here
    m_content->setRankField(ini.value(QStringLiteral("rankField"), QStringLiteral("rank_cat")).toString() == QLatin1String("rank")
                                ? QStringLiteral("overall")
                                : QStringLiteral("group"));
    m_content->setFinishHoldMs(ini.value(QStringLiteral("holdFinishSeconds"), 10).toInt() * 1000);
    m_content->setOtherHoldMs(ini.value(QStringLiteral("holdOtherSeconds"), 5).toInt() * 1000);
    save();
    Log::write(QStringLiteral("scoreboard"), tr("%1 display(s) imported from %2").arg(imported.size()).arg(iniPath));
    return true;
}

void ScoreboardModule::import()
{
    const QString file = QFileDialog::getOpenFileName(m_table, tr("Import from SkiAlpBridgeDisplay"), QString(),
                                                      tr("SkiAlpBridgeDisplay configuration (*.ini)"));
    if (file.isEmpty())
        return;
    QString err;
    if (!importSkiAlpBridgeDisplay(file, &err)) {
        QMessageBox::warning(m_table, tr("Import"), err);
        return;
    }
    if (m_rankField)
        m_rankField->setCurrentIndex(qMax(0, m_rankField->findData(m_content->rankField())));
    if (m_finishHold)
        m_finishHold->setValue(m_content->finishHoldMs() / 1000);
    if (m_otherHold)
        m_otherHold->setValue(m_content->otherHoldMs() / 1000);
    refreshTable();
}
