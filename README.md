<img align="left" width="30" src="https://github.com/Pax1601/DCSOlympus/assets/103559271/0ecff279-a87c-4e2d-a4c7-da98c74adf38">

[**Join the Discord**](https://discord.gg/kNAQkhUHnQ)

<img align="left" width="30" src="https://github.com/Pax1601/DCSOlympus/assets/103559271/1c0dd3fd-339c-4b03-94da-3e5215b0358a">

[**YouTube**](https://www.youtube.com/@DCSOlympus)


# DCS Olympus

Real-time RTS-style map control for DCS World multiplayer servers. Spawn units, issue waypoints and tasks, trigger effects — all from a browser-based map interface, with no client mods required.

> **This fork adds Linux dedicated server support.** See [docs/linux-installation.md](docs/linux-installation.md) for the full setup guide.

---

## What is it?

DCS Olympus is a free, open-source mod for DCS World that turns a server into something closer to an RTS game. A game master (or coalition commanders) can:

- Spawn and command AI units in real-time from a Leaflet map UI
- Issue waypoints, tasks, and engagement orders to any AI group
- Deploy effects: smoke, flares, napalm, explosions, and more
- Run coalition-restricted modes — Blue and Red commanders each see only their own side, with a spawn budget, allowing human-vs-human RTS play alongside or against pilots
- Configure AWACS/automation controllers
- Draw annotations on the shared map

No players need to install anything — Olympus runs entirely server-side.

## How it works

Olympus has three parts:

| Component | What it does |
|---|---|
| **Backend DLL** (`backend/core`) | Loaded by DCS via Lua; runs a REST API that DCS talks to |
| **Frontend server** (`frontend/server`) | Node.js/Express; serves the React UI and proxies REST requests to the DLL |
| **React client** (`frontend/react`) | Browser map UI built with Leaflet, React, and Tailwind CSS |

The backend and frontend never talk to each other directly. The client polls the DLL's REST API for state and sends commands via PUT. All state flows server → client only.

## Linux support (this fork)

The upstream project targets Windows. This fork cross-compiles the backend DLLs for Windows (PE/x86-64) using **MinGW-w64** on Linux, so they load natively into a Wine-hosted DCS dedicated server. The Node.js frontend and React client run as native Linux processes.

Key changes over upstream:
- **CMake build system** (`backend/CMakeLists.txt`) replaces the Visual Studio solution for MinGW cross-compilation
- **cpp-httplib** replaces cpprestsdk's HTTP listener (which requires HTTP.sys, unsupported in Wine)
- **nginx reverse proxy** handles TLS and Basic Auth; `customAuthHeaders` mode passes identity to the frontend without a second login prompt
- `OlympusHook.lua` calls `DCS.setPause(false)` on simulation start — dedicated servers always launch paused, which prevents mission timers from firing
- `OlympusCommand.lua` scans `coalition.getGroups()` at init to catch dynamically spawned units (e.g. from DCS Retribution campaigns)

See **[docs/linux-installation.md](docs/linux-installation.md)** for the full build and deployment walkthrough.

## Installing (Windows)

Check the [Wiki](https://github.com/Pax1601/DCSOlympus/wiki) for Windows installation instructions.

## Repository layout

```
backend/          C++ DLL source (CMakeLists.txt for MinGW, olympus.sln for MSVC)
  core/           REST server, unit/weapon managers, commands, scheduler
  olympus/        DLL entry point and hook registration
frontend/
  react/          Vite + React UI (TypeScript, Tailwind CSS, Leaflet)
  server/         Node.js/Express server
scripts/lua/
  backend/        OlympusCommand.lua — mission scripting environment
  hooks/          OlympusHook.lua — DCS hook callbacks
docs/
  linux-installation.md   Linux build and deployment guide
databases/units/  JSON unit databases (aircraft, ground, helicopters, navy, mods)
olympus.json      Master config template (ports, passwords, map layers, audio)
```

## FAQ

**Does it affect server performance?**
Olympus itself has minimal overhead. Spawning hundreds of units through it will impact performance the same as placing them in the mission editor — that's a DCS limit, not an Olympus one.

**Does it work with mission scripts?**
Generally yes. Olympus avoids interfering with other scripts. Some scripts may need `io` or `lfs` unsanitized in `MissionScripting.lua` — see the Linux guide's patch section for details.

**Does it support modded units?**
Yes, with a unit database entry. See `databases/units/mods.json`. Players who lack the mod will see a default model; some mods can cause client crashes if players don't have them installed.

**Can I contribute?**
Join the Discord and ping a developer to get started.
