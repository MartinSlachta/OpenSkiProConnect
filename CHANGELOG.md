# Changelog

OpenSkiPro Connect uses version numbers 0.x while it is being tried on real races
(minor version = a batch of features, patch version = fixes). It needs OpenSkiPro2 with
its API; the version of OpenSkiPro2 needed by a feature is given in brackets.

## 0.6.4 – 2026-10-27

* Publish: a ranking after (or of) a run is sent only once that run was raced (a
  classified competitor or a status of that run). Before, a DNF of run 1 alone made the
  pages switch to an empty ranking after run 2.

## 0.6.3 – 2026-10-25

* The rank inside every output group comes from the API (`groupRanks`, OpenSkiPro2
  0.7.1): Scoreboard, Screen and Publish show the rank in the category; with an older
  OpenSkiPro2 Connect still computes it itself.

## 0.6.2 – 2026-10-25

* Publish: the rank within the group in each run, for the Categories view of the pages.

## 0.6.1 – 2026-10-25

* Publish: the competitors not processed yet (still to start) are not sent as a status
  group.

## 0.6.0 – 2026-10-25

* Public competitor fields (OpenSkiPro2 0.7: column *Public* of the competitor fields):
  Publish sends only them, Screen offers only them with the labels of OpenSkiPro2; the
  web pages arrange the columns themselves (publish protocol 2, no screen configuration
  for Publish any more).

## 0.5.0 – 2026-10-24

* New module **Publish**: the race live on the internet through an OpenSkiPro Publish
  server (start lists, live, results of every run), with the publish token of the event
  (OpenSkiPro2 0.6). Changes only, debounced, retried after network errors.

## 0.4.0 – 2026-10-24

* Screen simplified ("less is more"): one configuration per kind of race chosen by hand,
  one list of columns for every list and for the TV and the speaker page alike; `/board`
  for every TV, `/speaker` with a fixed light look. Configurations of 0.3 are converted
  at the first start (backup in `screens/0.3-backup`).

## 0.3.0 – 0.3.7 – 2026-10-22

* New module **Screen**: a web server in Connect (port 8090) with the TV boards and the
  speaker page, configurations (`screens/*.oscreen`) with zones and columns, editor with
  live previews, message on the screens, fullscreen in Edge / Chrome kiosk mode on a
  chosen monitor, flags from a folder.
* Pages reload themselves after an update of Connect; time and rank of the current run;
  on course and next to start in the order of the finish.
* FIS Live: the finish and the intermediates of run 2 and later are sent as the total
  (time, diff, rank), as FIS expects; the message can be cleared.

## 0.2.0 – 0.2.2 – 2026-10-22

* New module **FIS Live**: the race to the FIS Live server (FIS Live XML v53) with
  sequence numbers, acknowledgements, a queue kept across connection drops and restarts,
  keepalive, corrections and an XML log with the password hidden.
* As automatic as possible: the codex comes from the event (OpenSkiPro2 0.5), the server
  and the timekeeper's password are set once in *Settings...*, the timed run, *in
  progress* and *end of run* follow the timing.

## 0.1.0 – 2026-10-21

* Core: connection to the OpenSkiPro2 API (WebSocket + REST, reconnection), race model,
  profiles (`--profile`, one instance per profile), log.
* Module **Scoreboard**: results boards (VolaScoreBoard-Pro, TH DIS08 over TCP), line
  programs for timing and outside timing, result hold, rank overall or in the output
  group, import of `SkiAlpBridgeDisplay.ini`.
* `apiserver-replay`: a recording of OpenSkiPro2's `osp-apicheck` served as a fake
  OpenSkiPro2, for trying Connect without a race.
