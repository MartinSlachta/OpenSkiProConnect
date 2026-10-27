#pragma once

#include "ScreenConfig.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;
class ScreenModule;

// Editor of the screen configurations (docs/screen-design.md 9): the files of the
// folder "screens" (load, save, save as, delete), the one list of columns, the zones
// of the TV, general settings and colours, the paging of the ranking and an own CSS.
// Every change is shown at once by the previews in the browser (/board/preview-edit,
// /speaker?board=preview-edit).
class ScreenEditor : public QDialog {
    Q_OBJECT
public:
    ScreenEditor(ScreenModule* module, const QString& file, QWidget* parent = nullptr);

protected:
    void reject() override;

private:
    QWidget* buildGeneral();
    QWidget* buildColours();
    QWidget* buildZones();
    QWidget* buildColumns();
    QWidget* buildRanking();
    void fillFiles(const QString& select);
    void loadFile(const QString& file);
    bool saveTo(const QString& file);
    bool maybeSave(); // false: the user cancelled
    void saveAs();
    void deleteFile();
    QString addImage(); // copies an image into screens/images, returns its name
    void toUi();
    void changed();
    void updatePreview();
    // zones
    void fillZones();
    void selectZone(int row);
    void zoneFromUi();
    void fillColumns();
    void columnsFromTable();
    void addColumnRow(int row, const ScreenColumn& c);
    void fillFormats(QComboBox* combo, const QString& field, const QString& value);
    void fillGroups();

    ScreenModule* m_module;
    ScreenConfig m_cfg;
    QString m_file;
    bool m_dirty = false;
    bool m_loading = false;
    int m_zone = -1;
    QTimer* m_preview;

    QComboBox* m_files = nullptr;
    // general
    QLineEdit* m_name = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QSpinBox* m_lineHeight = nullptr;
    QFontComboBox* m_font = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QComboBox* m_clock = nullptr;
    QLineEdit* m_title1 = nullptr;
    QLineEdit* m_title2 = nullptr;
    QLineEdit* m_foot = nullptr;
    QListWidget* m_logos = nullptr;
    QSpinBox* m_logoSeconds = nullptr;
    QComboBox* m_bgImage = nullptr;
    QPlainTextEdit* m_css = nullptr;
    QHash<QString, QPushButton*> m_colours;
    // zones
    QListWidget* m_zones = nullptr;
    QComboBox* m_zoneType = nullptr;
    QLineEdit* m_zoneTitle = nullptr;
    QSpinBox* m_zoneLines = nullptr;
    QComboBox* m_zoneVisible = nullptr;
    QCheckBox* m_zoneCounters = nullptr;
    QTableWidget* m_columns = nullptr;
    // ranking
    QComboBox* m_which = nullptr;
    QComboBox* m_group = nullptr;
    QSpinBox* m_page1 = nullptr;
    QSpinBox* m_page = nullptr;
    QSpinBox* m_maxPages = nullptr;
    QCheckBox* m_freeze = nullptr;
    QSpinBox* m_jump = nullptr;
    QSpinBox* m_finish = nullptr;
};
