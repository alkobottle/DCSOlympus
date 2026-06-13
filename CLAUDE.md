# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What is DCS Olympus

DCS Olympus is a mod for DCS World that provides real-time RTS-style map control over a DCS server. It has three distinct subsystems:

- **Backend (C++ DLL)** — loaded by DCS via Lua, exposes a REST API that DCS talks to
- **Frontend server (Node.js/Express)** — serves the web UI and proxies REST requests to the backend DLL
- **Frontend client (React/TypeScript)** — the browser-based map UI built with React, Leaflet, and Tailwind CSS
- **Manager (Electron)** — Windows desktop app for installation and instance management

The backend and frontend never talk to each other directly; the client fetches DCS data from the DLL's REST API and sends commands to it.

## Repository layout

```
backend/          C++ Visual Studio solution (olympus.sln)
  core/           Main DLL: REST server, unit/weapon managers, commands, scheduler
  olympus/        DLL entry point
  dcstools/       DCS Lua bridge utilities
  luatools/       Lua state helpers
  logger/         Logging library
  shared/         Shared headers
frontend/
  react/          Vite+React UI (TypeScript, Tailwind CSS, Leaflet)
  server/         Node.js/Express server (TypeScript, compiled to build/)
manager/          Electron app for Windows installation management
mod/              DCS mod entry point (entry.lua, Options, Theme)
databases/units/  JSON unit databases (aircraft, ground units, helicopters, navy, mods)
scripts/          Build and utility scripts (batch, lua, node, python)
olympus.json      Master configuration file (ports, passwords, map layers, audio)
version.json      Current version string
```

## Build commands

### Frontend React client
```bash
cd frontend/react
npm install
npm run dev          # Dev server on port 8080 (proxies to backend)
npm run build-release  # Outputs to frontend/server/public/
```

### Frontend server
```bash
cd frontend/server
npm install
npx tsc              # Compile TypeScript → build/
npm run server       # Run: node ./build/www.js -c <path-to-olympus.json>
```

### Manager (Electron — Windows only)
```bash
cd manager
npm install
npm start            # Launch Electron app
```

### Backend (C++ — Windows only, Visual Studio)
Open `backend/olympus.sln` in Visual Studio 2022, build the `Release|x64` configuration. This produces the DCS-loadable DLL.

## Development workflow

The frontend dev server (`npm run dev` in `frontend/react`) runs on port 8080 and expects the backend DLL to be running (inside DCS) on the port configured in `olympus.json` (default 4512). The Node.js server can proxy to Vite with the `--vite` flag.

Version strings use the `{{OLYMPUS_VERSION_NUMBER}}` placeholder throughout source files — the build scripts substitute the real value from `version.json`.

## Architecture: data flow

1. The DCS Lua environment loads the C++ DLL (`backend/core`).
2. The DLL's `Server` class starts an HTTP listener that accepts REST requests on the backend port (default 4512).
3. The Node.js frontend server (`frontend/server`) proxies authenticated client requests to the DLL and serves the built React app from `frontend/server/public/`.
4. The React client (`frontend/react`) polls several REST endpoints to refresh state:
   - `units`, `weapons`, `logs`, `airbases`, `bullseyes`, `mission`, `drawings`
   - Commands are sent via PUT to the `commands` endpoint.
5. All state changes flow server → client only. User interactions produce REST commands; the updated state arrives on the next poll. This prevents client/server and client/client inconsistencies.

## Architecture: frontend React app

The app is a singleton `OlympusApp` (see `frontend/react/src/olympusapp.ts`) that owns named managers:

| Manager | Responsibility |
|---|---|
| `UnitsManager` | Tracks all live units, selection, grouping, hot groups |
| `WeaponsManager` | Tracks active weapons/missiles |
| `ServerManager` | HTTP polling loop, authentication, session hash |
| `MissionManager` | Airbases, bullseyes, carrier tracking |
| `Map` (Leaflet subclass) | Map rendering, context menus, sub-states |
| `AudioManager` | SRS audio via WebSocket |
| `ControllerManager` | AWACS/automation controllers |
| `CoalitionAreasManager` | Editable coalition zone polygons/circles |
| `DrawingsManager` | Free-draw annotations |
| `ShortcutManager` | Keyboard shortcut registry |
| `SessionDataManager` | Persisted user session/profile |

App state is managed as an `OlympusState` enum (NOT_INITIALIZED → IDLE → UNIT_CONTROL, etc.) with an `OlympusSubState`. State-dependent UI rendering is driven by these enums.

Inter-component communication uses a custom DOM `CustomEvent` system defined in `frontend/react/src/events.ts`. All events are strongly-typed static classes inheriting from `BaseOlympusEvent` / `BaseUnitEvent` etc. Listen with `SomeEvent.on(callback)`, dispatch with `SomeEvent.dispatch(payload)`.

## Architecture: backend C++

- `Server` — HTTP listener (REST API handler); routes GET/PUT to the right managers
- `UnitsManager` / `WeaponsManager` — maintain in-memory unit/weapon state, serialise to JSON for GET responses
- `Scheduler` — queues Lua commands to run in the DCS mission scripting environment
- `Commands` — value types (`Move`, `Smoke`, `SpawnGroundUnits`, …) that serialise to Lua function call strings for the scheduler
- `ScriptLoader` — injects Olympus Lua scripts into the DCS environment

## Configuration

`olympus.json` (in the instance install directory, not the repo root) is the runtime config. The repo's `olympus.json` is a template. Key sections:
- `backend.port` — DLL REST port (default 4512)
- `frontend.port` — web UI port (default 3000)
- `authentication` — sha256-hashed passwords for game master / commanders / admin
- `frontend.mapLayers` — tile server definitions
- `audio.SRSPort` / `audio.WSPort` — SRS and WebSocket audio ports

## Unit databases

JSON files in `databases/units/` (`aircraftdatabase.json`, `groundunitdatabase.json`, `helicopterdatabase.json`, `navyunitdatabase.json`, `mods.json`) define spawnable unit types, loadouts, and display metadata. The React client loads these via the `/databases` API route.

## Linting

```bash
cd frontend/react
npx eslint .         # TypeScript + readable-tailwind rules
```

ESLint enforces Tailwind class ordering and multiline formatting for `.tsx` files (`eslint-plugin-readable-tailwind`).

## Key roles: command modes

Users connect as one of three roles (controlled by `olympus.json` passwords):
- **Game Master** — full control, all units visible
- **Blue Commander** / **Red Commander** — restricted to own coalition, separate spawn budget
