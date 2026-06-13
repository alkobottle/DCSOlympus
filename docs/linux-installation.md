# DCS Olympus — Linux Installation Guide

This guide covers running DCS Olympus on a Linux server. The DCS World dedicated
server runs under Wine; the Olympus C++ backend DLLs are cross-compiled for
Windows (PE/x86-64) using MinGW-w64 so they load natively into the Wine DCS
process. The Node.js frontend server and React client run as native Linux
processes.

## Architecture overview

```
Browser
  │  HTTPS (nginx reverse proxy, Basic Auth)
  ▼
nginx ──► Node.js frontend server (port 3000)
              │  HTTP proxy
              ▼
         DCS Olympus core.dll REST API (port 3001)
              │  Lua C API
              ▼
         DCS World (wine DCS_server.exe)
              │
              └─► OlympusCommand.lua (mission scripting)
```

nginx handles authentication, strips the browser's `Authorization` header, and
injects `X-Olympus-User` / `X-Olympus-Group` headers that the frontend server
reads to determine the user's role without a second login prompt.

---

## Prerequisites

```bash
# Wine Staging (for DCS World dedicated server)
sudo apt install wine-staging

# MinGW-w64 cross-compiler + build tools
sudo apt install mingw-w64 cmake ninja-build

# Node.js 20+ (frontend server)
sudo apt install nodejs npm

# nginx (reverse proxy)
sudo apt install nginx
```

Assumed paths (adjust to your setup):

| Path | Purpose |
|---|---|
| `/home/dcs/.wine/drive_c/DCS_server/` | DCS World server install |
| `/home/dcs/.wine/drive_c/users/dcs/Saved Games/<InstanceName>/` | DCS saved games / instance |
| `/home/dcs/DCSOlympus/` | This repository |

---

## 1 — Build the backend DLLs

### 1a. Fetch source dependencies

```bash
cd /home/dcs/DCSOlympus

# cpp-httplib (replaces cpprestsdk's HTTP listener — pure WinSock2, works in Wine)
mkdir -p third-party/httplib
wget -O third-party/httplib/httplib.h \
    https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.5/httplib.h

# cpprestsdk (JSON subset only — no HTTP.sys dependency)
git clone --depth=1 -b v2.10.19 \
    https://github.com/microsoft/cpprestsdk.git \
    third-party/cpprestsdk

# GeographicLib 2.1.1
wget -P /tmp \
    https://github.com/geographiclib/geographiclib/releases/download/v2.1.1/GeographicLib-2.1.1.tar.gz
tar -C third-party -xf /tmp/GeographicLib-2.1.1.tar.gz
```

### 1b. Build GeographicLib for MinGW

```bash
cmake -S third-party/GeographicLib-2.1.1 \
      -B /tmp/geog-build \
      -DCMAKE_TOOLCHAIN_FILE=backend/cmake/mingw-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DGEOGRAPHICLIB_LIB_TYPE=STATIC \
      -DCMAKE_INSTALL_PREFIX=mingw-deps \
      -G Ninja
cmake --build /tmp/geog-build --target install
```

### 1c. Create the Lua import library

Copy `lua.dll` out of the DCS installation and generate an import library:

```bash
cp "/home/dcs/.wine/drive_c/DCS_server/bin/lua.dll" third-party/lua/
cd third-party/lua
gendef lua.dll                                      # produces lua.def
x86_64-w64-mingw32-dlltool -d lua.def -l liblua.a  # produces liblua.a
cd ../..
```

Also copy the Lua headers (from the DCS SDK or an equivalent Lua 5.1 source):

```bash
# Lua 5.1 headers must be in third-party/lua/include/
# lua.h  lualib.h  lauxlib.h  luaconf.h
```

### 1d. Compile

```bash
mkdir -p backend/build-mingw
cmake -S backend -B backend/build-mingw \
      -DCMAKE_TOOLCHAIN_FILE=backend/cmake/mingw-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -G Ninja
ninja -C backend/build-mingw -j$(nproc)
```

Verify the output is Windows PE DLLs:

```bash
file backend/build-mingw/*.dll
# expected: PE32+ executable (DLL) (console) x86-64, for MS Windows
```

---

## 2 — Deploy the backend

```bash
OLYMPUS_BIN="/home/dcs/.wine/drive_c/users/dcs/Saved Games/<InstanceName>/Mods/Services/Olympus/bin"

cp backend/build-mingw/{olympus,core,logger,utils,luatools,dcstools}.dll "$OLYMPUS_BIN/"
```

Deploy the Lua scripts (these run inside DCS's mission scripting environment):

```bash
OLYMPUS_SCRIPTS="$OLYMPUS_BIN/../Scripts"
mkdir -p "$OLYMPUS_SCRIPTS"

cp scripts/lua/backend/{OlympusCommand.lua,mist.lua,templates.lua,unitPayloads.lua,mods.lua,OlympusCameraControl.lua} \
   "$OLYMPUS_SCRIPTS/"
```

> **Note:** The `Scripts/` directory must exist and contain all six Lua files.
> If it is missing, the DCS scripting environment will load silently but collect
> no unit data, and the map will appear empty.

---

## 3 — Patch DCS MissionScripting.lua

DCS sanitizes several Lua globals in the mission scripting environment. Olympus
and common mission frameworks need some of them restored.

Edit `<DCS_server>/Scripts/MissionScripting.lua`:

```lua
do
    sanitizeModule('os')
    --sanitizeModule('io')   -- required by DCS Retribution and other mission scripts
    --sanitizeModule('lfs')  -- required by OlympusCommand.lua
    --_G['require'] = nil    -- required by OlympusCommand.lua
    _G['loadlib'] = nil
    _G['package'] = nil
end
```

Back up the original first:

```bash
cp "/home/dcs/.wine/drive_c/DCS_server/Scripts/MissionScripting.lua"{,.bak}
```

---

## 4 — Configure the frontend server

### 4a. olympus.json

The deployed instance config lives in
`Saved Games/<InstanceName>/Config/olympus.json`. Key settings for Linux:

```json
{
    "backend": {
        "address": "localhost",
        "port": 3001
    },
    "frontend": {
        "port": 3000,
        "customAuthHeaders": {
            "enabled": true,
            "username": "X-Olympus-User",
            "group": "X-Olympus-Group"
        }
    },
    "authentication": {
        "gameMasterPassword": "",
        "blueCommanderPassword": "",
        "redCommanderPassword": ""
    }
}
```

With `customAuthHeaders` enabled, the frontend server authenticates users via
HTTP headers injected by nginx rather than a second password prompt. The
passwords in `authentication` can be left blank when nginx is the auth boundary.

### 4b. olympusGroups.json

Create `Saved Games/<InstanceName>/Config/olympusGroups.json` to map nginx
group names to Olympus roles:

```json
{
    "Admins": ["Game master"]
}
```

### 4c. Build and start the frontend server

```bash
cd frontend/server
npm install
npx tsc
node build/www.js -c "/home/dcs/.wine/drive_c/users/dcs/Saved Games/<InstanceName>/Config/olympus.json"
```

Create the elevation cache directory with the correct ownership before starting:

```bash
mkdir -p frontend/server/hgt
chown dcs:dcs frontend/server/hgt
```

### 4d. Build the React client

```bash
cd frontend/react
npm install
npm run build-release   # outputs to frontend/server/public/
```

---

## 5 — nginx reverse proxy

The nginx configuration handles TLS termination, Basic Auth, and the header
translation that lets the frontend server know who the user is without requiring
a second Olympus login.

```nginx
location /map/ {
    auth_basic "Restricted";
    auth_basic_user_file /etc/nginx/.olympus_htpasswd;

    # Strip the browser's Authorization header (nginx credentials ≠ Olympus
    # credentials). Replace with custom headers that carry the authenticated
    # username and group into the Olympus frontend server.
    proxy_set_header Authorization   "";
    proxy_set_header X-Olympus-User  $remote_user;
    proxy_set_header X-Olympus-Group "Admins";

    proxy_pass         http://127.0.0.1:3000/;
    proxy_http_version 1.1;
    proxy_set_header   Host              $host;
    proxy_set_header   X-Real-IP         $remote_addr;
    proxy_set_header   X-Forwarded-For   $proxy_add_x_forwarded_for;
    proxy_set_header   X-Forwarded-Proto $scheme;

    # WebSocket support (SRS audio)
    proxy_set_header   Upgrade    $http_upgrade;
    proxy_set_header   Connection "upgrade";
}
```

Create the htpasswd file:

```bash
sudo htpasswd -c /etc/nginx/.olympus_htpasswd <username>
```

> **Why `Authorization ""` is required:** Without clearing this header, the
> browser's Base64-encoded nginx credentials reach the Olympus frontend server,
> which tries to validate them as Olympus passwords and returns 401. Clearing
> the header and using `X-Olympus-User` / `X-Olympus-Group` instead is the
> correct authentication path when `customAuthHeaders` is enabled.

---

## 6 — DCS server startup

Start DCS under Wine as the service user:

```bash
WINEPREFIX=/home/dcs/.wine DISPLAY=:1 \
    wine /home/dcs/.wine/drive_c/DCS_server/bin/DCS_server.exe -w <InstanceName>
```

A typical wrapper script (`/home/dcs/bin/dcs-server-start.sh`):

```bash
#!/usr/bin/env bash
set -euo pipefail
export WINEPREFIX="/home/dcs/.wine"
export DISPLAY="${DISPLAY:-:1}"
cd "/home/dcs/.wine/drive_c/DCS_server/bin"
exec wine DCS_server.exe -w <InstanceName>
```

When DCS loads a mission, watch for these log lines confirming Olympus is up
(`Saved Games/<InstanceName>/Logs/dcs.log`):

```
INFO  Olympus.HOOKS.LUA: Olympus vX.X.X C++ module callbacks registered correctly.
INFO  Olympus.dll: Module loaded and started successfully.
INFO  SCRIPTING: OlympusCommand script ... loaded successfully
INFO  Dispatcher: loadMission Done: Control passed to the player
```

The backend REST API will be available on port 3001 once `loadMission Done`
appears.

---

## Troubleshooting

### Map loads but shows no units

DCS dedicated servers always start in `ssPaused` state. `OlympusHook.lua` calls
`DCS.setPause(false)` in `onSimulationStart` to force-resume. If you are running
an older hook version without this call, mission timers never fire and the units
table stays empty.

Verify the hook has the fix:

```
grep -i "setPause" \
  "<SavedGames>/Scripts/Hooks/OlympusHook.lua"
```

Should print: `DCS.setPause(false)`

Other causes:
- Verify `Scripts/` exists and contains all six Lua files (see step 2).
- Check `dcs.log` for `Mission script error` lines near `OlympusCommand`.
- Confirm `MissionScripting.lua` has `lfs` and `require` unsanitized (step 3).

### 401 on `/map/olympus/…` despite nginx auth passing

- Confirm `customAuthHeaders.enabled: true` in olympus.json.
- Confirm nginx clears `Authorization ""` before proxying (step 5).
- Confirm `olympusGroups.json` maps the nginx group to a valid Olympus role.

### DCS hangs during mission load (no `loadMission Done` after many minutes)

- Check `dcs.log` for `attempt to index global 'io'` — this means
  `sanitizeModule('io')` is still active in `MissionScripting.lua`.
- Kill DCS, apply the patch from step 3, and restart.

### `getElevation failed: EACCES: permission denied, mkdir './hgt'`

```bash
mkdir -p frontend/server/hgt
chown <service-user>:<service-group> frontend/server/hgt
```

### DLLs fail to load (`olympus.dll loaded successfully` missing from log)

- Run `file backend/build-mingw/*.dll` and confirm `PE32+ x86-64`.
- Run `x86_64-w64-mingw32-nm build-mingw/olympus.dll | grep luaopen_olympus`
  — the symbol must be present for DCS to load the module.
- Ensure all six DLLs (`olympus`, `core`, `logger`, `utils`, `luatools`,
  `dcstools`) are in the same `bin/` directory.
