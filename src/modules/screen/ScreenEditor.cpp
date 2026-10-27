#include "ScreenEditor.h"
#include "ScreenData.h"
#include "ScreenModule.h"
#include "core/RaceModel.h"

#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

enum ColumnCol { CField, CHeader, CWidth, CAlign, CFormat };

struct Colour {
    const char* key;
    const char* label;
    QString ScreenConfig::*member;
};

const Colour kColours[] = {
    { "background", QT_TRANSLATE_NOOP("ScreenEditor", "Background"), &ScreenConfig::background },
    { "text", QT_TRANSLATE_NOOP("ScreenEditor", "Text"), &ScreenConfig::text },
    { "titleBackground", QT_TRANSLATE_NOOP("ScreenEditor", "Title bars"), &ScreenConfig::titleBackground },
    { "titleText", QT_TRANSLATE_NOOP("ScreenEditor", "Text of the title bars"), &ScreenConfig::titleText },
    { "headerText", QT_TRANSLATE_NOOP("ScreenEditor", "Column headers"), &ScreenConfig::headerText },
    { "row1", QT_TRANSLATE_NOOP("ScreenEditor", "Rows (odd)"), &ScreenConfig::row1 },
    { "row2", QT_TRANSLATE_NOOP("ScreenEditor", "Rows (even)"), &ScreenConfig::row2 },
    { "highlight", QT_TRANSLATE_NOOP("ScreenEditor", "Highlight (new finisher)"), &ScreenConfig::highlight },
    { "highlightText", QT_TRANSLATE_NOOP("ScreenEditor", "Text of the highlight"), &ScreenConfig::highlightText },
    { "accent", QT_TRANSLATE_NOOP("ScreenEditor", "Rank boxes"), &ScreenConfig::accent },
};

void setCombo(QComboBox* c, const QString& data)
{
    const int i = c->findData(data);
    c->setCurrentIndex(i >= 0 ? i : 0);
}

void colourButton(QPushButton* b, const QString& colour)
{
    b->setText(colour);
    const bool dark = QColor(colour).lightness() < 128;
    b->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;min-width:110px;border:1px solid #888;padding:4px}")
                         .arg(colour, dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000")));
}

} // namespace

ScreenEditor::ScreenEditor(ScreenModule* module, const QString& file, QWidget* parent)
    : QDialog(parent)
    , m_module(module)
    , m_preview(new QTimer(this))
{
    setWindowTitle(tr("Screen configurations"));
    resize(1100, 760);
    m_preview->setSingleShot(true);
    m_preview->setInterval(250);
    connect(m_preview, &QTimer::timeout, this, &ScreenEditor::updatePreview);

    auto* lay = new QVBoxLayout(this);
    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Configuration :"), this));
    m_files = new QComboBox(this);
    m_files->setMinimumWidth(260);
    top->addWidget(m_files);
    auto* save = UiHelpers::button(QStringLiteral("save"), tr("Save"), this);
    connect(save, &QPushButton::clicked, this, [this] { saveTo(m_file); });
    top->addWidget(save);
    auto* saveAs = UiHelpers::button(QStringLiteral("new"), tr("Save as..."), this);
    saveAs->setToolTip(tr("A copy of this configuration under a new name (also to start a new one)."));
    connect(saveAs, &QPushButton::clicked, this, &ScreenEditor::saveAs);
    top->addWidget(saveAs);
    auto* del = UiHelpers::button(QStringLiteral("delete"), tr("Delete"), this);
    connect(del, &QPushButton::clicked, this, &ScreenEditor::deleteFile);
    top->addWidget(del);
    auto* folder = UiHelpers::button(QStringLiteral("folder"), QString(), this);
    folder->setToolTip(tr("Open the folder of the configurations (to copy them to another PC)."));
    connect(folder, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_module->configFolder())); });
    top->addWidget(folder);
    top->addStretch();
    // this configuration with the current race; every change appears at once, also before saving
    for (const auto& p : { qMakePair(tr("Preview TV"), QStringLiteral("board/preview-edit")),
                           qMakePair(tr("Preview speaker"), QStringLiteral("speaker?board=preview-edit")) }) {
        auto* preview = UiHelpers::button(QStringLiteral("preview"), p.first, this);
        preview->setToolTip(tr("Shows this configuration in the browser with the current race; every change appears at once, also before saving."));
        connect(preview, &QPushButton::clicked, this, [this, url = p.second] {
            if (!m_module->isRunning()) {
                QMessageBox::information(this, windowTitle(), tr("Start the module Screen first (the web server)."));
                return;
            }
            updatePreview();
            QDesktopServices::openUrl(QUrl(m_module->pageUrl(url)));
        });
        top->addWidget(preview);
    }
    lay->addLayout(top);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildColumns(), tr("Columns"));
    tabs->addTab(buildZones(), tr("TV zones"));
    tabs->addTab(buildGeneral(), tr("General"));
    tabs->addTab(buildColours(), tr("TV colours"));
    tabs->addTab(buildRanking(), tr("Ranking and pages"));
    m_css = new QPlainTextEdit(this);
    m_css->setPlaceholderText(tr("Own CSS for special wishes, e.g.\n.z-ranking .row { font-weight: bold; }\n.title { font-size: 1.4em; }"));
    connect(m_css, &QPlainTextEdit::textChanged, this, [this] {
        m_cfg.css = m_css->toPlainText();
        changed();
    });
    tabs->addTab(m_css, tr("Own CSS"));
    lay->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &ScreenEditor::reject);
    lay->addWidget(buttons);

    fillFiles(file);
    loadFile(m_files->currentData().toString());
    connect(m_files, &QComboBox::activated, this, [this] {
        const QString f = m_files->currentData().toString();
        if (f == m_file)
            return;
        if (!maybeSave()) {
            setCombo(m_files, m_file);
            return;
        }
        loadFile(f);
    });
}

void ScreenEditor::reject()
{
    if (maybeSave())
        QDialog::reject();
}

QWidget* ScreenEditor::buildGeneral()
{
    auto* w = new QWidget(this);
    auto* form = new QFormLayout(w);
    m_name = new QLineEdit(w);
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString& t) { m_cfg.name = t; changed(); });
    form->addRow(tr("Name :"), m_name);

    auto* size = new QHBoxLayout();
    m_width = new QSpinBox(w);
    m_width->setRange(320, 7680);
    m_height = new QSpinBox(w);
    m_height->setRange(200, 4320);
    auto* presets = new QComboBox(w);
    presets->addItem(tr("Presets..."));
    for (const QSize& s : { QSize(1920, 1080), QSize(1280, 720), QSize(1280, 1024), QSize(1024, 768), QSize(1280, 800), QSize(3840, 2160), QSize(1080, 1920) })
        presets->addItem(QStringLiteral("%1 x %2").arg(s.width()).arg(s.height()), s);
    size->addWidget(m_width);
    size->addWidget(new QLabel(QStringLiteral("x"), w));
    size->addWidget(m_height);
    size->addWidget(presets);
    size->addStretch();
    connect(m_width, &QSpinBox::valueChanged, this, [this](int v) { m_cfg.width = v; changed(); });
    connect(m_height, &QSpinBox::valueChanged, this, [this](int v) { m_cfg.height = v; changed(); });
    connect(presets, &QComboBox::activated, this, [this, presets](int i) {
        if (i > 0) {
            const QSize s = presets->itemData(i).toSize();
            m_width->setValue(s.width());
            m_height->setValue(s.height());
        }
        presets->setCurrentIndex(0);
    });
    form->addRow(tr("TV design size (pixels) :"), size);
    auto* sizeInfo = new QLabel(tr("The board is drawn in this size and scaled to fit the screen."), w);
    sizeInfo->setStyleSheet(QStringLiteral("color:#667"));
    form->addRow(QString(), sizeInfo);

    m_lineHeight = new QSpinBox(w);
    m_lineHeight->setRange(10, 400);
    m_lineHeight->setSuffix(tr(" px"));
    connect(m_lineHeight, &QSpinBox::valueChanged, this, [this](int v) { m_cfg.lineHeight = v; changed(); });
    form->addRow(tr("TV line height :"), m_lineHeight);
    auto* font = new QHBoxLayout();
    m_font = new QFontComboBox(w);
    m_fontSize = new QSpinBox(w);
    m_fontSize->setRange(6, 300);
    m_fontSize->setSuffix(tr(" px"));
    font->addWidget(m_font, 1);
    font->addWidget(m_fontSize);
    connect(m_font, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) { m_cfg.font = f.family(); changed(); });
    connect(m_fontSize, &QSpinBox::valueChanged, this, [this](int v) { m_cfg.fontSize = v; changed(); });
    form->addRow(tr("Font (TV size) :"), font);

    m_title1 = new QLineEdit(w);
    m_title2 = new QLineEdit(w);
    connect(m_title1, &QLineEdit::textEdited, this, [this](const QString& t) { m_cfg.title1 = t; changed(); });
    connect(m_title2, &QLineEdit::textEdited, this, [this](const QString& t) { m_cfg.title2 = t; changed(); });
    form->addRow(tr("Title :"), m_title1);
    form->addRow(tr("Title 2 :"), m_title2);
    auto* titleInfo = new QLabel(tr("Own text and / or the data of the event: {title1} {title2} {discipline} {location} {date} {run}"), w);
    titleInfo->setStyleSheet(QStringLiteral("color:#667"));
    form->addRow(QString(), titleInfo);
    m_clock = new QComboBox(w);
    m_clock->addItem(tr("Time of the timing device"), QStringLiteral("device"));
    m_clock->addItem(tr("Time of the PC (of the browser)"), QStringLiteral("pc"));
    m_clock->addItem(tr("No clock"), QStringLiteral("none"));
    connect(m_clock, &QComboBox::activated, this, [this] { m_cfg.clock = m_clock->currentData().toString(); changed(); });
    form->addRow(tr("Clock :"), m_clock);

    m_foot = new QLineEdit(w);
    connect(m_foot, &QLineEdit::textEdited, this, [this](const QString& t) { m_cfg.footText = t; changed(); });
    form->addRow(tr("Text of the foot :"), m_foot);
    auto* logos = new QHBoxLayout();
    m_logos = new QListWidget(w);
    m_logos->setMaximumHeight(90);
    auto* logoButtons = new QVBoxLayout();
    auto* addLogo = UiHelpers::button(QStringLiteral("add"), tr("Logo..."), w);
    auto* delLogo = UiHelpers::button(QStringLiteral("remove"), QString(), w);
    logoButtons->addWidget(addLogo);
    logoButtons->addWidget(delLogo);
    logoButtons->addStretch();
    logos->addWidget(m_logos, 1);
    logos->addLayout(logoButtons);
    m_logoSeconds = new QSpinBox(w);
    m_logoSeconds->setRange(1, 3600);
    m_logoSeconds->setSuffix(tr(" s"));
    logos->addWidget(new QLabel(tr("each"), w));
    logos->addWidget(m_logoSeconds);
    connect(addLogo, &QPushButton::clicked, this, [this] {
        const QString name = addImage();
        if (!name.isEmpty()) {
            m_cfg.logos << name;
            m_logos->addItem(name);
            changed();
        }
    });
    connect(delLogo, &QPushButton::clicked, this, [this] {
        const int r = m_logos->currentRow();
        if (r >= 0) {
            m_cfg.logos.removeAt(r);
            delete m_logos->takeItem(r);
            changed();
        }
    });
    connect(m_logoSeconds, &QSpinBox::valueChanged, this, [this](int v) { m_cfg.logoSeconds = v; changed(); });
    form->addRow(tr("Logos (foot) :"), logos);

    auto* bg = new QHBoxLayout();
    m_bgImage = new QComboBox(w);
    auto* addBg = UiHelpers::button(QStringLiteral("add"), tr("Image..."), w);
    bg->addWidget(m_bgImage, 1);
    bg->addWidget(addBg);
    connect(m_bgImage, &QComboBox::activated, this, [this] { m_cfg.backgroundImage = m_bgImage->currentData().toString(); changed(); });
    connect(addBg, &QPushButton::clicked, this, [this] {
        const QString name = addImage();
        if (name.isEmpty())
            return;
        m_cfg.backgroundImage = name;
        if (m_bgImage->findData(name) < 0)
            m_bgImage->addItem(name, name);
        setCombo(m_bgImage, name);
        changed();
    });
    form->addRow(tr("Background image :"), bg);
    return w;
}

QWidget* ScreenEditor::buildColours()
{
    auto* w = new QWidget(this);
    auto* outer = new QVBoxLayout(w);
    auto* form = new QFormLayout();
    for (const Colour& c : kColours) {
        auto* b = new QPushButton(w);
        m_colours.insert(QLatin1String(c.key), b);
        connect(b, &QPushButton::clicked, this, [this, b, c] {
            const QColor col = QColorDialog::getColor(QColor(m_cfg.*c.member), this, tr(c.label));
            if (!col.isValid())
                return;
            m_cfg.*c.member = col.name();
            colourButton(b, col.name());
            changed();
        });
        form->addRow(tr(c.label) + QStringLiteral(" :"), b);
    }
    outer->addLayout(form);
    outer->addStretch();
    return w;
}

QWidget* ScreenEditor::buildRanking()
{
    auto* w = new QWidget(this);
    auto* form = new QFormLayout(w);
    m_which = new QComboBox(w);
    m_which->addItem(tr("Total (after the current run)"), QStringLiteral("total"));
    m_which->addItem(tr("The current run"), QStringLiteral("run"));
    connect(m_which, &QComboBox::activated, this, [this] { m_cfg.ranking = m_which->currentData().toString(); changed(); });
    form->addRow(tr("Ranking :"), m_which);
    m_group = new QComboBox(w);
    connect(m_group, &QComboBox::activated, this, [this] { m_cfg.group = m_group->currentData().toString(); changed(); });
    form->addRow(tr("Output group :"), m_group);
    auto spin = [w](int max, const QString& suffix) {
        auto* s = new QSpinBox(w);
        s->setRange(0, max);
        s->setSuffix(suffix);
        return s;
    };
    m_page1 = spin(3600, tr(" s"));
    m_page1->setMinimum(1);
    m_page = spin(3600, tr(" s"));
    m_page->setMinimum(1);
    m_maxPages = spin(100, QString());
    m_maxPages->setSpecialValueText(tr("all"));
    m_freeze = new QCheckBox(tr("Freeze the ranking on the 1st page"), w);
    m_jump = spin(3600, tr(" s"));
    m_jump->setSpecialValueText(tr("no"));
    m_finish = spin(3600, tr(" s"));
    m_finish->setSpecialValueText(tr("no"));
    connect(m_page1, &QSpinBox::valueChanged, this, [this](int v) { m_cfg.page1Seconds = v; changed(); });
    connect(m_page, &QSpinBox::valueChanged, this, [this](int v) { m_cfg.pageSeconds = v; changed(); });
    connect(m_maxPages, &QSpinBox::valueChanged, this, [this](int v) { m_cfg.maxPages = v; changed(); });
    connect(m_freeze, &QCheckBox::toggled, this, [this](bool v) { m_cfg.freeze = v; changed(); });
    connect(m_jump, &QSpinBox::valueChanged, this, [this](int v) { m_cfg.jumpSeconds = v; changed(); });
    connect(m_finish, &QSpinBox::valueChanged, this, [this](int v) { m_cfg.finishSeconds = v; changed(); });
    form->addRow(tr("Time of the 1st page :"), m_page1);
    form->addRow(tr("Time of the following pages :"), m_page);
    form->addRow(tr("Number of pages :"), m_maxPages);
    form->addRow(QString(), m_freeze);
    form->addRow(tr("After a finish, page of the finisher during :"), m_jump);
    form->addRow(tr("New finisher highlighted during :"), m_finish);
    auto* info = new QLabel(tr("The pages are those of the zones Ranking, Start list and DNS / DNF / DSQ of the TV; "
                               "the speaker page is never paged (the speaker scrolls himself)."), w);
    info->setWordWrap(true);
    info->setStyleSheet(QStringLiteral("color:#667"));
    form->addRow(QString(), info);
    return w;
}

QWidget* ScreenEditor::buildZones()
{
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);

    auto* left = new QVBoxLayout();
    left->addWidget(UiHelpers::titleBar(tr("Zones (top to bottom)"), w));
    m_zones = new QListWidget(w);
    left->addWidget(m_zones, 1);
    auto* zb = new QGridLayout();
    auto* add = UiHelpers::button(QStringLiteral("add"), tr("New"), w);
    auto* copy = UiHelpers::button(QStringLiteral("new"), tr("Copy"), w);
    auto* del = UiHelpers::button(QStringLiteral("delete"), tr("Delete"), w);
    auto* up = UiHelpers::button(QStringLiteral("num_up"), tr("Up"), w);
    auto* down = UiHelpers::button(QStringLiteral("num_down"), tr("Down"), w);
    zb->addWidget(add, 0, 0);
    zb->addWidget(copy, 0, 1);
    zb->addWidget(del, 0, 2);
    zb->addWidget(up, 1, 0);
    zb->addWidget(down, 1, 1);
    left->addLayout(zb);
    lay->addLayout(left, 1);

    connect(m_zones, &QListWidget::currentRowChanged, this, &ScreenEditor::selectZone);
    connect(add, &QPushButton::clicked, this, [this] {
        ScreenZone z;
        z.title = tr("Ranking");
        const int at = m_zone >= 0 ? m_zone + 1 : int(m_cfg.zones.size());
        m_cfg.zones.insert(at, z);
        fillZones();
        m_zones->setCurrentRow(at);
        changed();
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        if (m_zone < 0)
            return;
        m_cfg.zones.insert(m_zone + 1, m_cfg.zones.at(m_zone));
        const int at = m_zone + 1;
        fillZones();
        m_zones->setCurrentRow(at);
        changed();
    });
    connect(del, &QPushButton::clicked, this, [this] {
        if (m_zone < 0)
            return;
        const int at = m_zone;
        m_cfg.zones.removeAt(at);
        fillZones();
        m_zones->setCurrentRow(qMin(at, int(m_cfg.zones.size()) - 1));
        changed();
    });
    auto move = [this](int d) {
        const int to = m_zone + d;
        if (m_zone < 0 || to < 0 || to >= m_cfg.zones.size())
            return;
        m_cfg.zones.swapItemsAt(m_zone, to);
        fillZones();
        m_zones->setCurrentRow(to);
        changed();
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });

    auto* right = new QVBoxLayout();
    right->addWidget(UiHelpers::titleBar(tr("Zone"), w));
    auto* form = new QFormLayout();
    m_zoneType = new QComboBox(w);
    for (const auto& t : ScreenConfig::zoneTypes())
        m_zoneType->addItem(t.second, t.first);
    m_zoneTitle = new QLineEdit(w);
    m_zoneTitle->setPlaceholderText(tr("empty: no title bar"));
    m_zoneLines = new QSpinBox(w);
    m_zoneLines->setRange(0, 200);
    m_zoneVisible = new QComboBox(w);
    for (const auto& v : ScreenConfig::visibilities())
        m_zoneVisible->addItem(v.second, v.first);
    m_zoneCounters = new QCheckBox(tr("Classified / List / DNS / DNF / DSQ in the title bar"), w);
    form->addRow(tr("Type :"), m_zoneType);
    form->addRow(tr("Title :"), m_zoneTitle);
    form->addRow(tr("Lines :"), m_zoneLines);
    form->addRow(tr("Visible :"), m_zoneVisible);
    form->addRow(QString(), m_zoneCounters);
    right->addLayout(form);
    connect(m_zoneType, &QComboBox::activated, this, &ScreenEditor::zoneFromUi);
    connect(m_zoneTitle, &QLineEdit::textEdited, this, &ScreenEditor::zoneFromUi);
    connect(m_zoneLines, &QSpinBox::valueChanged, this, &ScreenEditor::zoneFromUi);
    connect(m_zoneVisible, &QComboBox::activated, this, &ScreenEditor::zoneFromUi);
    connect(m_zoneCounters, &QCheckBox::toggled, this, &ScreenEditor::zoneFromUi);

    auto* zoneInfo = new QLabel(tr("Every zone shows its list in the columns of the configuration (tab Columns)."), w);
    zoneInfo->setStyleSheet(QStringLiteral("color:#667"));
    right->addWidget(zoneInfo);
    right->addStretch(1);
    lay->addLayout(right, 3);
    return w;
}

QWidget* ScreenEditor::buildColumns()
{
    auto* w = new QWidget(this);
    auto* lay = new QVBoxLayout(w);
    auto* info = new QLabel(tr("The columns of every list - ranking, finish, on course, next to start, start list - on the TV and "
                               "on the speaker page. Each list fills them with what belongs to it, so they line up: on course "
                               "the time runs under the current run (under the time in the 1st run) and the gap is the one of "
                               "the last intermediate; next to start shows the rank, time and gap after the previous runs; "
                               "the start list its order under the rank; DNS / DNF / DSQ stand under the times."),
                            w);
    info->setWordWrap(true);
    info->setStyleSheet(QStringLiteral("color:#667"));
    lay->addWidget(info);
    m_columns = new QTableWidget(0, 5, w);
    m_columns->setHorizontalHeaderLabels({ tr("Field"), tr("Header"), tr("Width"), tr("Alignment"), tr("Format") });
    m_columns->horizontalHeader()->setSectionResizeMode(CField, QHeaderView::Stretch);
    m_columns->horizontalHeader()->setSectionResizeMode(CHeader, QHeaderView::Stretch);
    m_columns->verticalHeader()->setDefaultSectionSize(30);
    m_columns->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_columns->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_columns, &QTableWidget::itemChanged, this, [this] { columnsFromTable(); });
    lay->addWidget(m_columns, 1);
    auto* cb = new QHBoxLayout();
    auto* cadd = UiHelpers::button(QStringLiteral("add"), tr("Column"), w);
    auto* cdel = UiHelpers::button(QStringLiteral("delete"), tr("Delete"), w);
    auto* cup = UiHelpers::button(QStringLiteral("left"), tr("Left"), w);
    auto* cdown = UiHelpers::button(QStringLiteral("right"), tr("Right"), w);
    cb->addWidget(cadd);
    cb->addWidget(cdel);
    cb->addWidget(cup);
    cb->addWidget(cdown);
    cb->addStretch();
    auto* widthInfo = new QLabel(tr("Widths are relative (shares of the line)."), w);
    widthInfo->setStyleSheet(QStringLiteral("color:#667"));
    cb->addWidget(widthInfo);
    lay->addLayout(cb);
    connect(cadd, &QPushButton::clicked, this, [this] {
        const int at = m_columns->currentRow() >= 0 ? m_columns->currentRow() + 1 : m_columns->rowCount();
        m_cfg.columns.insert(at, ScreenColumn{ QStringLiteral("club"), tr("Club"), 15, QStringLiteral("left"), QString() });
        fillColumns();
        m_columns->setCurrentCell(at, CField);
        changed();
    });
    connect(cdel, &QPushButton::clicked, this, [this] {
        const int r = m_columns->currentRow();
        if (r < 0)
            return;
        m_cfg.columns.removeAt(r);
        fillColumns();
        changed();
    });
    auto cmove = [this](int d) {
        const int r = m_columns->currentRow(), to = r + d;
        if (r < 0 || to < 0 || to >= m_cfg.columns.size())
            return;
        m_cfg.columns.swapItemsAt(r, to);
        fillColumns();
        m_columns->setCurrentCell(to, CField);
        changed();
    };
    connect(cup, &QPushButton::clicked, this, [cmove] { cmove(-1); });
    connect(cdown, &QPushButton::clicked, this, [cmove] { cmove(1); });
    return w;
}

void ScreenEditor::fillFiles(const QString& select)
{
    m_files->clear();
    for (const QString& f : m_module->configFiles())
        m_files->addItem(QFileInfo(f).completeBaseName(), f);
    setCombo(m_files, select);
}

void ScreenEditor::loadFile(const QString& file)
{
    m_file = file;
    ScreenConfig cfg;
    QString err;
    if (!file.isEmpty() && !cfg.load(QDir(m_module->configFolder()).filePath(file), &err))
        QMessageBox::warning(this, windowTitle(), tr("Cannot read %1: %2").arg(file, err));
    m_cfg = cfg;
    toUi();
    m_dirty = false;
    updatePreview();
}

bool ScreenEditor::saveTo(const QString& file)
{
    if (file.isEmpty())
        return false;
    QString err;
    if (!m_cfg.save(QDir(m_module->configFolder()).filePath(file), &err)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot save %1: %2").arg(file, err));
        return false;
    }
    m_dirty = false;
    m_module->server()->reloadConfigs(); // the boards using it show it at once
    return true;
}

bool ScreenEditor::maybeSave()
{
    if (!m_dirty)
        return true;
    const auto b = QMessageBox::question(this, windowTitle(), tr("Save the changes of %1?").arg(QFileInfo(m_file).completeBaseName()),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (b == QMessageBox::Cancel)
        return false;
    if (b == QMessageBox::Save)
        return saveTo(m_file);
    m_dirty = false;
    return true;
}

void ScreenEditor::saveAs()
{
    const QString name = QInputDialog::getText(this, tr("Save as"), tr("Name of the configuration (e.g. Pohar Liptova) :"), QLineEdit::Normal,
                                               m_cfg.name)
                             .trimmed();
    if (name.isEmpty())
        return;
    QString base = name;
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString file = base + QStringLiteral(".oscreen");
    if (QFileInfo::exists(QDir(m_module->configFolder()).filePath(file))
        && QMessageBox::question(this, windowTitle(), tr("%1 exists. Replace it?").arg(base)) != QMessageBox::Yes)
        return;
    m_cfg.name = name;
    if (!saveTo(file))
        return;
    m_file = file;
    fillFiles(file);
    toUi();
}

void ScreenEditor::deleteFile()
{
    if (m_file.isEmpty())
        return;
    if (m_file == m_module->configFile()) {
        QMessageBox::information(this, windowTitle(), tr("The screens use this configuration now - choose another one in the module first."));
        return;
    }
    if (QMessageBox::question(this, windowTitle(), tr("Delete the configuration %1?").arg(QFileInfo(m_file).completeBaseName())) != QMessageBox::Yes)
        return;
    QFile::remove(QDir(m_module->configFolder()).filePath(m_file));
    m_dirty = false;
    fillFiles(QString());
    loadFile(m_files->currentData().toString());
}

QString ScreenEditor::addImage()
{
    const QString src = QFileDialog::getOpenFileName(this, tr("Image"), QString(), tr("Images (*.png *.jpg *.jpeg *.gif *.svg *.bmp *.webp)"));
    if (src.isEmpty())
        return QString();
    const QString name = QFileInfo(src).fileName();
    const QString dst = QDir(m_module->configFolder()).filePath(QStringLiteral("images/") + name);
    if (QFileInfo(src).absoluteFilePath() != QFileInfo(dst).absoluteFilePath()) {
        QFile::remove(dst);
        if (!QFile::copy(src, dst)) {
            QMessageBox::warning(this, windowTitle(), tr("Cannot copy the image to %1.").arg(dst));
            return QString();
        }
    }
    return name;
}

void ScreenEditor::toUi()
{
    m_loading = true;
    m_name->setText(m_cfg.name);
    m_width->setValue(m_cfg.width);
    m_height->setValue(m_cfg.height);
    m_lineHeight->setValue(m_cfg.lineHeight);
    m_font->setCurrentFont(QFont(m_cfg.font));
    m_fontSize->setValue(m_cfg.fontSize);
    m_title1->setText(m_cfg.title1);
    m_title2->setText(m_cfg.title2);
    setCombo(m_clock, m_cfg.clock);
    m_foot->setText(m_cfg.footText);
    m_logos->clear();
    m_logos->addItems(m_cfg.logos);
    m_logoSeconds->setValue(m_cfg.logoSeconds);
    m_bgImage->clear();
    m_bgImage->addItem(tr("(none)"), QString());
    for (const QString& f : QDir(QDir(m_module->configFolder()).filePath(QStringLiteral("images"))).entryList(QDir::Files, QDir::Name))
        m_bgImage->addItem(f, f);
    if (!m_cfg.backgroundImage.isEmpty() && m_bgImage->findData(m_cfg.backgroundImage) < 0)
        m_bgImage->addItem(m_cfg.backgroundImage, m_cfg.backgroundImage);
    setCombo(m_bgImage, m_cfg.backgroundImage);
    m_css->setPlainText(m_cfg.css);
    for (const Colour& c : kColours)
        colourButton(m_colours.value(QLatin1String(c.key)), m_cfg.*c.member);
    setCombo(m_which, m_cfg.ranking);
    fillGroups();
    m_page1->setValue(m_cfg.page1Seconds);
    m_page->setValue(m_cfg.pageSeconds);
    m_maxPages->setValue(m_cfg.maxPages);
    m_freeze->setChecked(m_cfg.freeze);
    m_jump->setValue(m_cfg.jumpSeconds);
    m_finish->setValue(m_cfg.finishSeconds);
    m_zone = -1;
    fillZones();
    fillColumns();
    m_loading = false;
    if (!m_cfg.zones.isEmpty())
        m_zones->setCurrentRow(0);
}

void ScreenEditor::fillGroups()
{
    m_group->clear();
    m_group->addItem(tr("All competitors"), QStringLiteral("all"));
    m_group->addItem(tr("Every output group in turn"), QStringLiteral("rotate"));
    const QJsonObject groups = m_module->data()->state().value(QStringLiteral("event")).toObject().value(QStringLiteral("groups")).toObject();
    for (auto it = groups.begin(); it != groups.end(); ++it)
        m_group->addItem(tr("Only %1 (group %2 of the event)").arg(it.value().toString(), it.key()), it.key());
    if (m_group->findData(m_cfg.group) < 0)
        m_group->addItem(tr("Only the group %1").arg(m_cfg.group), m_cfg.group);
    setCombo(m_group, m_cfg.group);
}

void ScreenEditor::changed()
{
    if (m_loading)
        return;
    m_dirty = true;
    m_preview->start();
}

void ScreenEditor::updatePreview()
{
    m_module->server()->setPreview(QStringLiteral("edit"), m_cfg);
}

void ScreenEditor::fillZones()
{
    const bool loading = m_loading;
    m_loading = true;
    m_zones->blockSignals(true);
    m_zones->clear();
    for (const ScreenZone& z : std::as_const(m_cfg.zones)) {
        QString type = z.type;
        for (const auto& t : ScreenConfig::zoneTypes())
            if (t.first == z.type)
                type = t.second;
        m_zones->addItem(z.title.isEmpty() ? type : QStringLiteral("%1  -  %2").arg(type, z.title));
    }
    m_zones->blockSignals(false);
    m_zone = -1;
    m_loading = loading;
}

void ScreenEditor::selectZone(int row)
{
    m_zone = row >= 0 && row < m_cfg.zones.size() ? row : -1;
    const bool loading = m_loading;
    m_loading = true;
    const ScreenZone z = m_zone >= 0 ? m_cfg.zones.at(m_zone) : ScreenZone();
    setCombo(m_zoneType, z.type);
    m_zoneTitle->setText(z.title);
    m_zoneLines->setValue(z.lines);
    setCombo(m_zoneVisible, z.visible);
    m_zoneCounters->setChecked(z.counters);
    m_zoneCounters->setEnabled(z.type == QLatin1String("ranking"));
    for (QWidget* w : { static_cast<QWidget*>(m_zoneType), static_cast<QWidget*>(m_zoneTitle), static_cast<QWidget*>(m_zoneLines),
                        static_cast<QWidget*>(m_zoneVisible) })
        w->setEnabled(m_zone >= 0);
    m_loading = loading;
}

void ScreenEditor::zoneFromUi()
{
    if (m_loading || m_zone < 0)
        return;
    ScreenZone& z = m_cfg.zones[m_zone];
    z.type = m_zoneType->currentData().toString();
    z.title = m_zoneTitle->text();
    z.lines = m_zoneLines->value();
    z.visible = m_zoneVisible->currentData().toString();
    z.counters = m_zoneCounters->isChecked();
    m_zoneCounters->setEnabled(z.type == QLatin1String("ranking"));
    QString type = z.type;
    for (const auto& t : ScreenConfig::zoneTypes())
        if (t.first == z.type)
            type = t.second;
    if (QListWidgetItem* it = m_zones->item(m_zone))
        it->setText(z.title.isEmpty() ? type : QStringLiteral("%1  -  %2").arg(type, z.title));
    changed();
}

void ScreenEditor::fillFormats(QComboBox* combo, const QString& field, const QString& value)
{
    combo->blockSignals(true);
    combo->clear();
    const auto formats = ScreenConfig::formats(field);
    for (const auto& f : formats)
        combo->addItem(f.second, f.first);
    combo->setEnabled(!formats.isEmpty());
    setCombo(combo, value);
    combo->blockSignals(false);
}

void ScreenEditor::addColumnRow(int row, const ScreenColumn& c)
{
    auto* field = new QComboBox(m_columns);
    // the runs of the open event (all 9 without one)
    const int runs = m_module->model()->event().value(QStringLiteral("type")).toObject().value(QStringLiteral("runCount")).toInt(9);
    for (const auto& f : ScreenConfig::fields(runs))
        field->addItem(f.second, f.first);
    if (field->findData(c.field) < 0) // e.g. run 3 in a configuration made for a race of 3 runs
        for (const auto& f : ScreenConfig::fields())
            if (f.first == c.field)
                field->addItem(f.second, f.first);
    // competitor fields: the label from OpenSkiPro2, greyed when they are not public there
    const auto pub = m_module->model()->publicFields();
    if (!pub.isEmpty()) {
        auto* items = qobject_cast<QStandardItemModel*>(field->model());
        for (int i = 0; i < field->count(); ++i) {
            const QString key = field->itemData(i).toString();
            if (!ScreenConfig::isCompetitorField(key))
                continue;
            const auto it = std::find_if(pub.cbegin(), pub.cend(), [&](const auto& f) { return f.first == key; });
            if (it != pub.cend()) {
                if (!it->second.isEmpty() && it->second != field->itemText(i))
                    field->setItemText(i, QStringLiteral("%1 (%2)").arg(field->itemText(i), it->second));
            } else if (key != QLatin1String("name")) {
                field->setItemText(i, tr("%1 - not public in OpenSkiPro2").arg(field->itemText(i)));
                if (items && items->item(i))
                    items->item(i)->setEnabled(false);
            }
        }
    }
    setCombo(field, c.field);
    m_columns->setCellWidget(row, CField, field);
    m_columns->setItem(row, CHeader, new QTableWidgetItem(c.header));
    auto* width = new QSpinBox(m_columns);
    width->setRange(1, 100);
    width->setValue(c.width);
    m_columns->setCellWidget(row, CWidth, width);
    auto* align = new QComboBox(m_columns);
    align->addItem(tr("Left"), QStringLiteral("left"));
    align->addItem(tr("Center"), QStringLiteral("center"));
    align->addItem(tr("Right"), QStringLiteral("right"));
    setCombo(align, c.align);
    m_columns->setCellWidget(row, CAlign, align);
    auto* format = new QComboBox(m_columns);
    fillFormats(format, c.field, c.format);
    m_columns->setCellWidget(row, CFormat, format);

    connect(field, &QComboBox::activated, this, [this, field, format] {
        // a new field: its header and its formats
        for (int r = 0; r < m_columns->rowCount(); ++r) {
            if (m_columns->cellWidget(r, CField) != field)
                continue;
            fillFormats(format, field->currentData().toString(), QString());
            QTableWidgetItem* h = m_columns->item(r, CHeader);
            // a public field of the competitor keeps an empty header: the label of OpenSkiPro2
            const bool fromOsp = ScreenConfig::isCompetitorField(field->currentData().toString())
                && !m_module->model()->publicFields().isEmpty();
            if (h && h->text().isEmpty() && !fromOsp)
                h->setText(field->currentText());
        }
        columnsFromTable();
    });
    connect(width, &QSpinBox::valueChanged, this, [this] { columnsFromTable(); });
    connect(align, &QComboBox::activated, this, [this] { columnsFromTable(); });
    connect(format, &QComboBox::activated, this, [this] { columnsFromTable(); });
}

void ScreenEditor::fillColumns()
{
    const bool loading = m_loading;
    m_loading = true;
    m_columns->blockSignals(true);
    m_columns->setRowCount(int(m_cfg.columns.size()));
    for (int r = 0; r < m_cfg.columns.size(); ++r)
        addColumnRow(r, m_cfg.columns.at(r));
    m_columns->blockSignals(false);
    m_loading = loading;
}

void ScreenEditor::columnsFromTable()
{
    if (m_loading)
        return;
    QList<ScreenColumn> cols;
    for (int r = 0; r < m_columns->rowCount(); ++r) {
        ScreenColumn c;
        if (auto* f = qobject_cast<QComboBox*>(m_columns->cellWidget(r, CField)))
            c.field = f->currentData().toString();
        if (QTableWidgetItem* h = m_columns->item(r, CHeader))
            c.header = h->text();
        if (auto* w = qobject_cast<QSpinBox*>(m_columns->cellWidget(r, CWidth)))
            c.width = w->value();
        if (auto* a = qobject_cast<QComboBox*>(m_columns->cellWidget(r, CAlign)))
            c.align = a->currentData().toString();
        if (auto* f = qobject_cast<QComboBox*>(m_columns->cellWidget(r, CFormat)))
            c.format = f->currentData().toString();
        cols << c;
    }
    m_cfg.columns = cols;
    changed();
}
