# GW2 Nexus JS Loader

A [Nexus](https://raidcore.gg/Nexus) addon for Guild Wars 2 that embeds the Chromium Embedded Framework (CEF) to render web applications as in-game overlays with JavaScript bindings to the Nexus API.

Build in-game overlays using HTML, CSS, and JavaScript while accessing game data and Nexus functionality through the `nexus.*` API.

## Features

- **Web overlay rendering** — Load any web page as an in-game overlay using CEF's off-screen rendering pipeline
- **GPU-accelerated** — Pixel buffers are uploaded to D3D11 textures and composited via ImGui
- **Full Nexus API in JavaScript** — Logging, events, data links, keybinds, game binds, quick access, localization, and more
- **Multi-process architecture** — Browser and renderer run in separate processes for stability
- **Input forwarding** — Keyboard and mouse events are forwarded to the browser when the overlay is focused

## JavaScript API

Web apps have access to the full Nexus API through the global `nexus` object:

```js
// Logging
nexus.log.info(channel, message)

// Alerts
nexus.alert(message)

// Events
nexus.events.subscribe(name, callback)
nexus.events.raise(name, data)
nexus.events.unsubscribe(name)

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
nexus.gamebinds.press(id)
nexus.gamebinds.release(id)
await nexus.gamebinds.isBound(id)

// Quick Access
nexus.quickaccess.add(id, icon, iconHover, keybindId, description)
nexus.quickaccess.remove(id)
nexus.quickaccess.notify(id)

// Localization
nexus.localization.set(id, lang, text)
await nexus.localization.translate(id)
```

See [`web/example/`](web/example/) for a working example that demonstrates all API functions.

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

The build produces:

```
build/
├── nexus_js_loader.dll              # Main plugin DLL
└── nexus_js_loader/
    ├── nexus_js_subprocess.exe      # CEF subprocess
    ├── libcef.dll                   # CEF runtime
    └── ...                          # Other CEF resources
```

## Installation

1. Install [Nexus](https://raidcore.gg/Nexus) if you haven't already
2. Copy `nexus_js_loader.dll` to your Nexus addons directory
3. Copy the `nexus_js_loader/` folder (containing the subprocess and CEF files) alongside the DLL
4. Launch Guild Wars 2 — the addon will load automatically through Nexus

## Usage

- Press **Alt+Shift+J** to toggle the overlay
- Open the addon settings panel in Nexus to enter a URL and load a web app
- The overlay captures keyboard and mouse input when visible

## Architecture

The plugin uses CEF's multi-process model:

```
GW2 Process (browser)                    Subprocess (renderer)
┌─────────────────────┐                 ┌─────────────────────┐
│ nexus_js_loader.dll │                 │ nexus_js_subprocess │
│                     │   CEF IPC       │                     │
│  CEF Manager        │◄───────────────►│  Renderer App       │
│  Browser Client     │                 │  JS Bindings (V8)   │
│  OSR Render Handler │                 │  IPC Client         │
│  D3D11 Texture      │                 └─────────────────────┘
│  ImGui Overlay      │
│  Input Handler      │
└─────────────────────┘
```

CEF DLLs are placed in a `nexus_js_loader/` subfolder and loaded via a delay-load hook to avoid conflicts with GW2's own bundled `libcef.dll`.

## Project Structure

```
src/
├── plugin/          # Main addon DLL (browser process)
│   ├── main.cpp           Entry point and Nexus addon callbacks
│   ├── cef_manager.*      CEF lifecycle management
│   ├── browser_app.*      CefApp for browser process
│   ├── browser_client.*   CefClient implementation
│   ├── osr_render_handler.*  Off-screen render handler
│   ├── d3d11_texture.*    D3D11 texture upload
│   ├── overlay.*          ImGui overlay rendering
│   ├── input_handler.*    Input forwarding to CEF
│   ├── ipc_handler.*      Browser-side IPC dispatch
│   └── web_app_manager.*  Web app lifecycle
├── subprocess/      # CEF subprocess executable (renderer process)
│   ├── main.cpp           Subprocess entry point
│   ├── renderer_app.*     CefApp for renderer
│   ├── js_bindings.*      V8 JavaScript API extensions
│   └── ipc_client.*       Renderer-side IPC client
├── shared/          # Shared between plugin and subprocess
│   ├── version.h          Addon metadata
│   └── ipc_messages.h     IPC message constants
web/
└── example/         # Example web app demonstrating all APIs
third_party/
├── nexus-api/       # Nexus API headers (submodule)
├── imgui/           # ImGui (submodule)
└── cef/             # CEF binary distribution (downloaded at build time)
```

## License

TBD
