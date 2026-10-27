#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

class ApiClient;

// One shared, always current picture of the race (docs/design.md 2), built from
// the messages of the API: status, live lists, competitor events, subscribed
// resources (event, competitors, rankings ...) and the clock of the timing device.
// Modules read it and react to its signals; they never parse API messages.
class RaceModel : public QObject {
    Q_OBJECT
public:
    // client may be null (tests feed handle() directly)
    explicit RaceModel(ApiClient* client, QObject* parent = nullptr);

    ApiClient* client() const { return m_client; }
    // Processes one API message (type + data), as received on the WebSocket.
    void handle(const QString& type, const QJsonObject& data);
    void setConnected(bool connected);

    bool isConnected() const { return m_connected; }
    QString instance() const { return m_instance; }
    QJsonObject status() const { return m_status; }
    bool timingActive() const; // connected and the timing window of a run is open
    int timedRun() const;      // run of the timing window, 0 none
    QJsonObject event() const { return m_resources.value(QStringLiteral("event")); }
    QJsonObject live() const { return m_live; }
    QJsonObject resource(const QString& name) const { return m_resources.value(name); }
    bool hasResource(const QString& name) const { return m_resources.contains(name); }
    // Last competitor event of a bib in a run (empty when none was received).
    QJsonObject competitorState(int run, int bib) const { return m_competitors.value(qint64(run) * 10000 + bib); }

    // Competitor object of a bib (from the "competitors" resource), empty if unknown.
    QJsonObject competitor(int bib) const;
    // Public competitor fields of the event (OpenSkiPro2 0.7: event.fields) as (key, label),
    // most important first; the outputs for the spectators show these. Empty when
    // OpenSkiPro2 does not send them (older version, no event).
    QList<QPair<QString, QString>> publicFields() const;
    // Output groups of the event a bib belongs to.
    QList<int> groupsOf(int bib) const;
    // Rank of a bib in a ranking resource ("run/2/results", "results?upto=2"), or in the
    // same ranking inside the first output group of the bib (group = true: groupRanks of
    // the ranking, OpenSkiPro2 0.7.1; older versions need the grouped resource required).
    // Empty when not ranked / not known.
    QString rankIn(const QString& ranking, int bib, bool group) const;
    static QString withGroup(const QString& ranking, int group);

    // Clock of the timing device, extrapolated locally between the clock messages.
    qint64 deviceNowMs() const;
    qint64 clockOffsetMs() const { return m_clockOffsetMs; } // device time of day - local time of day
    bool clockSynced() const { return m_clockSynced; }
    int utcOffsetMin() const { return m_utcOffsetMin; }

    // Resources a module needs pushed after every change (subscribed on the API).
    void require(const QStringList& names);

Q_SIGNALS:
    void connectedChanged(bool connected);
    void restarted();                    // OpenSkiPro2 was restarted: everything reloaded
    void statusChanged();
    void timingChanged(bool active, int run);
    void eventChanged();
    void liveChanged();
    void competitorEvent(const QJsonObject& data);
    void resourceChanged(const QString& name);
    void clockChanged();

private:
    void setClock(const QJsonObject& clock);
    void updateTiming();

    ApiClient* m_client;
    bool m_connected = false;
    QString m_instance;
    QJsonObject m_status;
    QJsonObject m_live;
    QHash<QString, QJsonObject> m_resources;
    QHash<qint64, QJsonObject> m_competitors; // run * 10000 + bib
    bool m_timingActive = false;
    int m_timedRun = 0;

    qint64 m_clockOffsetMs = 0;
    bool m_clockSynced = false;
    int m_utcOffsetMin = 0;
};
