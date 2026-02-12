# GW2 Nexus JS Loader

> [!WARNING]
> This is not production-ready code. Do not use, security issues abound.

A [Nexus](https://raidcore.gg/Nexus) addon for Guild Wars 2 that uses Chromium Embedded Framework (CEF) to render local web applications as in-game overlays with JavaScript bindings to the Nexus API.

Build in-game overlays using HTML, CSS, and JavaScript while accessing game data and Nexus functionality through the `nexus.*` API.

## Features

- **Multi-addon framework** — Discover and load multiple independent addons from local directories
- **Local resource serving** — Each addon's HTML/CSS/JS/images are served from the filesystem via CEF scheme handlers
- **Multi-window support** — Addons can create multiple ImGui-hosted overlay windows at runtime
- **GPU-accelerated** — Off-screen pixel buffers are uploaded to D3D11 textures and composited via ImGui
- **Full Nexus API in JavaScript** — Logging, events, data links, keybinds, game binds, quick access, localization, window management, and more
- **Per-window input control** — Each window can programmatically capture or pass through keyboard/mouse input
- **In-process architecture** — CEF runs in-process by reusing GW2's already-loaded `libcef.dll` via delay-load

## Creating an Addon

Each addon is a directory under `<GW2>/addons/jsloader/` containing a `manifest.json` and your web assets. The directory name becomes the addon ID.

### Directory structure

```
<GW2>/addons/jsloader/
└── my-addon/              # Addon ID = "my-addon"
    ├── manifest.json      # Required: addon metadata
    ├── index.html         # Entry point (referenced in manifest)
    ├── style.css
    ├── app.js
    └── ...                # Any other assets (images, fonts, etc.)
```

### manifest.json

```json
{
    "name": "My Addon",
    "version": "1.0.0",
    "author": "Your Name",
    "description": "Short description of the addon",
    "entry": "index.html"
}
```

All fields are required. `entry` is the HTML file loaded when the addon starts, relative to the addon directory.

## JavaScript API

Addons have access to the full Nexus API through the global `nexus` object:

```js
// Logging
nexus.log.info(channel, message)
nexus.log.warning(channel, message)
nexus.log.critical(channel, message)
nexus.log.debug(channel, message)

// Alerts
nexus.alert(message)

// Events
nexus.events.subscribe(name, callback)
nexus.events.unsubscribe(name, callback)
nexus.events.raise(name)

// Data links (async)
await nexus.datalink.getMumbleLink()
await nexus.datalink.getNexusLink()

// Paths (async)
await nexus.paths.getGameDirectory()
await nexus.paths.getAddonDirectory(name)
await nexus.paths.getCommonDirectory()

// Keybinds
nexus.keybinds.register(id, defaultBind, callback)
nexus.keybinds.deregister(id)

// Game binds
nexus.gamebinds.press(bind)
nexus.gamebinds.release(bind)
nexus.gamebinds.invoke(bind, durationMs)
await nexus.gamebinds.isBound(bind)

// Quick Access
nexus.quickaccess.add(id, icon, iconHover, keybindId, tooltip)
nexus.quickaccess.remove(id)
nexus.quickaccess.notify(id)

// Localization
nexus.localization.set(id, lang, text)
await nexus.localization.translate(id)

// Window management
await nexus.windows.create(windowId, { url, width, height, title })
nexus.windows.close(windowId)
nexus.windows.update(windowId, { title, width, height, visible })
nexus.windows.setInputPassthrough(windowId, enabled)
await nexus.windows.list()
```

See [`web/example/`](web/example/) for a working example addon that demonstrates all API functions.

## Building

### Requirements

- CMake 3.20+
- Visual Studio 2022 (MSVC toolchain)
- Ninja build system

CEF is downloaded automatically during the CMake configure step.

### Build steps

```bash
git clone --recurse-submodules https://github.com/cheahjs/gw2-nexus-js-loader.git
cd gw2-nexus-js-loader

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Or use the Docker-based build on non-Windows systems:

```bash
docker/build.sh
```

The build produces `nexus_js_loader.dll`.

## Installation

1. Install [Nexus](https://raidcore.gg/Nexus) if you haven't already
2. Copy `nexus_js_loader.dll` to your Nexus addons directory
3. Create your addon directories under `<GW2>/addons/jsloader/` (each with a `manifest.json`)
4. Launch Guild Wars 2 — addons are discovered and loaded automatically

### Installing the example addon

Copy the `web/example/` directory into your jsloader addons folder:

```
<GW2>/addons/jsloader/example/
├── manifest.json
├── index.html
├── popup.html
├── style.css
└── app.js
```

## Usage

- Press **Alt+Shift+J** to toggle the overlay
- Each addon's windows appear as draggable/resizable ImGui panels
- Open the JS Loader options panel in Nexus to see addon status, reload addons, or open DevTools
- Use `nexus.windows.setInputPassthrough(windowId, true)` to let mouse/keyboard events pass through to the game

## Architecture

The plugin uses CEF's in-process model, reusing GW2's already-loaded `libcef.dll` via delay-load:

```
GW2 Process
┌─────────────────────────────────────────────────┐
│ nexus_js_loader.dll                             │
│                                                 │
│  AddonManager        (discover + lifecycle)     │
│  ├── AddonInstance   (per-addon runtime state)  │
│  │   ├── WindowInfo "main"   (ImGui + browser)  │
│  │   ├── WindowInfo "popup"  (ImGui + browser)  │
│  │   └── IPC state   (events, keybinds)         │
│  ├── AddonInstance   (another addon)            │
│  │   └── WindowInfo "main"                      │
│  └── ...                                        │
│                                                 │
│  SchemeHandler        (local file serving)       │
│  InProcessBrowser     (CEF OSR + D3D11 texture) │
│  Overlay              (ImGui multi-window)      │
│  InputHandler         (per-window routing)      │
│  NexusBridge          (JS API injection)        │
└─────────────────────────────────────────────────┘
```

Each addon's local files are served via HTTPS scheme handlers at `https://<addon-id>.jsloader.local/`.

## Project Structure

```
src/
├── plugin/              # Main addon DLL
│   ├── main.cpp               Entry point and Nexus addon callbacks
│   ├── addon_manager.*        Addon discovery, lifecycle, orchestration
│   ├── addon_instance.*       Per-addon runtime state and window management
│   ├── addon_scheme_handler.* Local file serving via CEF scheme handlers
│   ├── in_process_browser.*   CEF in-process browser (OSR client)
│   ├── nexus_bridge.*         JavaScript API injection
│   ├── ipc_handler.*          Bridge message dispatch
│   ├── overlay.*              ImGui multi-window rendering
│   ├── input_handler.*        Per-window input routing
│   ├── d3d11_texture.*        D3D11 texture upload
│   ├── cef_loader.*           CEF availability detection
│   └── globals.*              Shared state
├── shared/
│   └── version.h              Addon metadata
web/
└── example/             # Example addon demonstrating all APIs
    ├── manifest.json
    ├── index.html
    ├── popup.html
    ├── style.css
    └── app.js
third_party/
├── nexus-api/           # Nexus API headers (submodule)
├── imgui/               # ImGui (submodule)
├── nlohmann/            # JSON library
└── cef/                 # CEF binary distribution (downloaded at build time)
```

## License

TBD
