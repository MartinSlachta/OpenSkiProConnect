#pragma once

#include <QObject>
#include <QString>

class QWidget;
class RaceModel;

// A module of OpenSkiPro Connect (Scoreboard, FIS Live, ...): reads the RaceModel,
// has its own page in the main window and its own settings in the profile.
class Module : public QObject {
    Q_OBJECT
public:
    explicit Module(RaceModel* model, QObject* parent = nullptr)
        : QObject(parent)
        , m_model(model)
    {
    }

    virtual QString id() const = 0;    // key in the profile ("scoreboard")
    virtual QString title() const = 0; // shown in the main window
    virtual QWidget* createPage(QWidget* parent) = 0;
    // On / off from the main window (stored in the profile by the shell).
    virtual void setRunning(bool running) = 0;
    virtual bool isRunning() const = 0;
    virtual QString stateText() const = 0; // one line for the list of modules

    RaceModel* model() const { return m_model; }

Q_SIGNALS:
    void stateChanged();

private:
    RaceModel* m_model;
};
