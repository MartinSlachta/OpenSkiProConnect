# OpenSkiPro Connect

Outputs for the alpine timing software [OpenSkiPro2](https://github.com/MartinSlachta/OpenSkiPro2):
results boards, FIS Live timing, screens for TVs and the speaker, and the race live on the
internet - one application with modules, reading the read-only REST + WebSocket API of
OpenSkiPro2. Written in C++17 / Qt 6.

## Modules

| Module | What it does |
|---|---|
| Scoreboard | Results boards at the finish: VolaScoreBoard-Pro and TAG Heuer TH DIS08 over TCP. Line programs for timing and outside timing (bib, name, running time, intermediate, finish time, rank, …), result hold, rank overall or in the category. Import of `SkiAlpBridgeDisplay.ini`. |
| FIS Live | The race to the FIS Live server (FIS Live XML v53): race info, meteo, start lists, start / intermediate / finish, DNS / DNF / DSQ / NPS, corrections. Sequence numbers, acknowledgements, a queue kept across connection drops and restarts, keepalive, XML log (password hidden). The codex comes from the event; everything else follows the timing. |
| Screen | A web server in Connect (port 8090): `/board` for every TV (zones top to bottom, the ranking paged by itself, the last finisher highlighted) and `/speaker` for the commentator (everything at once, nothing redraws under them). One configuration per kind of race (`screens/*.oscreen`: columns, zones, colours), editor with live previews, message on the screens, fullscreen in Edge / Chrome kiosk mode on a chosen monitor, flags from a folder. |
| Publish | The race live on the internet through an OpenSkiPro Publish server: start lists, live (on start, on course, finished), results of every run, ranking in the categories. Only the competitor fields marked *Public* in OpenSkiPro2 are sent. Needs a publish token of the race (entered in OpenSkiPro2) from the operator of the server. |

Several instances can run on different PCs with their own profiles, e.g. FIS Live in
the timing hut and the Scoreboard at the finish.

## Download

Windows builds (64-bit) are on the [Releases](https://github.com/MartinSlachta/OpenSkiProConnect/releases) page:
unzip `OpenSkiProConnect-<version>-win64.zip` anywhere and start `OpenSkiProConnect.exe`, no installation needed.
The program is not signed, so Windows SmartScreen may warn the first time (*More info › Run anyway*).

> **Test versions.** OpenSkiPro Connect is being tried on real races. Run it alongside your
> usual outputs first. Please report problems as [issues](https://github.com/MartinSlachta/OpenSkiProConnect/issues).

## Quick start

1. In OpenSkiPro2: *Local configuration › API Server* - enable it.
2. Start `OpenSkiProConnect.exe`, enter the address of the PC with OpenSkiPro2
   (`localhost` on the same PC) and the port, *Connect*.
3. *FIS Live*: *Settings...* once - the timekeeper's FIS Live password (the same for every
   race) and the server (default `livedata.fis-ski.com:1550`). Tick *FIS Live*.
4. *Scoreboard*: *Import from SkiAlpBridgeDisplay...* or *Add display...*, then tick
   *Scoreboard*.
5. *Screen*: choose the configuration (kind of race) and start it; open
   `http://<PC>:8090/board` on every TV or `/speaker` in any browser of the network, or
   *Fullscreen on* a monitor of this PC.
6. *Publish*: enter the publish token in OpenSkiPro2 (*General Parameters*), tick
   *Publish*.

The settings are in `OpenSkiProConnect.ini` next to the exe. Another profile:
`OpenSkiProConnect.exe --profile finish.ini` (one instance per profile).

## Without OpenSkiPro2: apiserver-replay

`apiserver-replay` serves a recording made with OpenSkiPro2's `osp-apicheck record` as a
fake OpenSkiPro2, in real time or faster:

```sh
apiserver-replay recording.jsonl --port 8099 --speed 5 --wait
```

Connect to `localhost:8099`. Only the resources subscribed during the recording are
available.

## Build

Requirements: Qt 6.8 or newer (tested with 6.8.3, MinGW 64-bit kit) with the modules Core,
Gui, Widgets, Network, WebSockets and HttpServer; CMake ≥ 3.21; Ninja.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
cmake --build build
windeployqt --release --no-translations build/OpenSkiProConnect.exe
```

## Authors

| Role | Who |
|---|---|
| Coder | [Claude](https://claude.com/claude-code) (Anthropic) |
| Tester | Martin Slachta |

The board code (rules, program engine, drivers) comes from Martin Slachta's
SkiAlpBridgeDisplay.

## License

OpenSkiPro Connect is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software Foundation,
version 3 of the License (GPL-3.0). See [LICENSE](LICENSE). It uses Qt under the terms of
the GNU LGPL v3. The OpenSkiPro Publish server is a separate service and not part of this
repository.
