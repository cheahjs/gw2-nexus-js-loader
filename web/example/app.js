// GW2 Nexus JS Loader — Example Addon
// Demonstrates all nexus.* API bindings including multi-window support.
//
// To install this example, copy the web/example/ directory into:
//   <GW2>/addons/jsloader/example/
// The directory name ("example") becomes the addon ID.

function setStatus(msg) {
    document.getElementById('statusBar').textContent = msg;
}

function appendLog(elementId, msg) {
    var el = document.getElementById(elementId);
    var line = document.createElement('div');
    line.textContent = '[' + new Date().toLocaleTimeString() + '] ' + msg;
    el.appendChild(line);
    el.scrollTop = el.scrollHeight;
}

// ---- Logging ----

function testLog() {
    var channel = document.getElementById('logChannel').value;
    var message = document.getElementById('logMessage').value;
    nexus.log.info(channel, message);
    setStatus('Logged: ' + message);
}

// ---- Alert ----

function testAlert() {
    var message = document.getElementById('alertMessage').value;
    nexus.alert(message);
    setStatus('Alert sent: ' + message);
}

// ---- Events ----

function subscribeEvent() {
    var name = document.getElementById('eventName').value;
    nexus.events.subscribe(name, function(data) {
        appendLog('eventLog', 'Event "' + name + '" received: ' + JSON.stringify(data));
    });
    setStatus('Subscribed to: ' + name);
    appendLog('eventLog', 'Subscribed to "' + name + '"');
}

function raiseEvent() {
    var name = document.getElementById('eventName').value;
    nexus.events.raise(name);
    setStatus('Raised event: ' + name);
}

// ---- Data Links ----

async function getMumbleLink() {
    try {
        var result = await nexus.datalink.getMumbleLink();
        document.getElementById('dataLinkOutput').textContent =
            'Mumble Link:\n' + JSON.stringify(result, null, 2);
        setStatus('Got Mumble Link data');
    } catch (e) {
        document.getElementById('dataLinkOutput').textContent = 'Error: ' + e;
        setStatus('Failed to get Mumble Link');
    }
}

async function getNexusLink() {
    try {
        var result = await nexus.datalink.getNexusLink();
        document.getElementById('dataLinkOutput').textContent =
            'Nexus Link:\n' + JSON.stringify(result, null, 2);
        setStatus('Got Nexus Link data');
    } catch (e) {
        document.getElementById('dataLinkOutput').textContent = 'Error: ' + e;
        setStatus('Failed to get Nexus Link');
    }
}

// ---- Paths ----

async function getGameDir() {
    try {
        var result = await nexus.paths.getGameDirectory();
        document.getElementById('pathsOutput').textContent = 'Game Dir: ' + result;
        setStatus('Got game directory');
    } catch (e) {
        document.getElementById('pathsOutput').textContent = 'Error: ' + e;
    }
}

async function getAddonDir() {
    try {
        var result = await nexus.paths.getAddonDirectory('jsloader');
        document.getElementById('pathsOutput').textContent = 'Addon Dir: ' + result;
        setStatus('Got addon directory');
    } catch (e) {
        document.getElementById('pathsOutput').textContent = 'Error: ' + e;
    }
}

async function getCommonDir() {
    try {
        var result = await nexus.paths.getCommonDirectory();
        document.getElementById('pathsOutput').textContent = 'Common Dir: ' + result;
        setStatus('Got common directory');
    } catch (e) {
        document.getElementById('pathsOutput').textContent = 'Error: ' + e;
    }
}

// ---- Keybinds ----

function registerKeybind() {
    var id = document.getElementById('keybindId').value;
    var defaultBind = document.getElementById('keybindDefault').value;

    nexus.keybinds.register(id, defaultBind, function(identifier, isRelease) {
        var action = isRelease ? 'released' : 'pressed';
        appendLog('keybindLog', 'Keybind "' + identifier + '" ' + action);
    });

    setStatus('Registered keybind: ' + id);
    appendLog('keybindLog', 'Registered "' + id + '" (' + defaultBind + ')');
}

function deregisterKeybind() {
    var id = document.getElementById('keybindId').value;
    nexus.keybinds.deregister(id);
    setStatus('Deregistered keybind: ' + id);
    appendLog('keybindLog', 'Deregistered "' + id + '"');
}

// ---- Game Binds ----

function pressGameBind() {
    var bind = parseInt(document.getElementById('gameBind').value);
    nexus.gamebinds.press(bind);
    setStatus('Pressed game bind: ' + bind);
}

function releaseGameBind() {
    var bind = parseInt(document.getElementById('gameBind').value);
    nexus.gamebinds.release(bind);
    setStatus('Released game bind: ' + bind);
}

async function checkIsBound() {
    var bind = parseInt(document.getElementById('gameBind').value);
    try {
        var result = await nexus.gamebinds.isBound(bind);
        document.getElementById('gameBindOutput').textContent =
            'Game bind ' + bind + ' is bound: ' + result;
        setStatus('Checked game bind: ' + bind);
    } catch (e) {
        document.getElementById('gameBindOutput').textContent = 'Error: ' + e;
    }
}

// ---- Quick Access ----

function addQuickAccess() {
    nexus.quickaccess.add(
        'QA_JSLOADER_EXAMPLE',
        'ICON_JSLOADER',
        'ICON_JSLOADER_HOVER',
        'KB_JSLOADER_TOGGLE',
        'JS Loader Example'
    );
    setStatus('Added Quick Access shortcut');
}

function removeQuickAccess() {
    nexus.quickaccess.remove('QA_JSLOADER_EXAMPLE');
    setStatus('Removed Quick Access shortcut');
}

function notifyQuickAccess() {
    nexus.quickaccess.notify('QA_JSLOADER_EXAMPLE');
    setStatus('Sent Quick Access notification');
}

// ---- Localization ----

function setLocalization() {
    var id = document.getElementById('locId').value;
    var lang = document.getElementById('locLang').value;
    var text = document.getElementById('locText').value;
    nexus.localization.set(id, lang, text);
    setStatus('Set localization: ' + id + ' = ' + text + ' (' + lang + ')');
}

async function translateLocalization() {
    var id = document.getElementById('locId').value;
    try {
        var result = await nexus.localization.translate(id);
        document.getElementById('locOutput').textContent = 'Translation: ' + result;
        setStatus('Translated: ' + id);
    } catch (e) {
        document.getElementById('locOutput').textContent = 'Error: ' + e;
    }
}

// ---- Windows ----

async function createWindow() {
    var windowId = document.getElementById('newWindowId').value;
    var title = document.getElementById('newWindowTitle').value;
    try {
        var result = await nexus.windows.create(windowId, {
            url: 'popup.html',
            width: 400,
            height: 300,
            title: title
        });
        document.getElementById('windowsOutput').textContent =
            'Window "' + windowId + '": ' + result;
        setStatus('Created window: ' + windowId);
    } catch (e) {
        document.getElementById('windowsOutput').textContent = 'Error: ' + e;
        setStatus('Failed to create window');
    }
}

function closeWindow() {
    var windowId = document.getElementById('newWindowId').value;
    nexus.windows.close(windowId);
    document.getElementById('windowsOutput').textContent =
        'Closed window: ' + windowId;
    setStatus('Closed window: ' + windowId);
}

async function listWindows() {
    try {
        var result = await nexus.windows.list();
        document.getElementById('windowsOutput').textContent =
            'Windows:\n' + JSON.stringify(result, null, 2);
        setStatus('Listed ' + result.length + ' window(s)');
    } catch (e) {
        document.getElementById('windowsOutput').textContent = 'Error: ' + e;
        setStatus('Failed to list windows');
    }
}

// Main window input passthrough toggle (boolean for full passthrough)
document.getElementById('mainPassthrough').addEventListener('change', function() {
    nexus.windows.setInputPassthrough('main', this.checked);
    setStatus('Main window input passthrough: ' + (this.checked ? 'ON' : 'OFF'));
});

// Alpha-based passthrough (threshold=10: transparent areas pass through)
document.getElementById('mainAlphaPassthrough').addEventListener('change', function() {
    if (this.checked) {
        document.getElementById('mainPassthrough').checked = false;
        nexus.windows.setInputPassthrough('main', 10);
        setStatus('Main window alpha passthrough: ON (threshold=10)');
    } else {
        nexus.windows.setInputPassthrough('main', false);
        setStatus('Main window alpha passthrough: OFF');
    }
});

// ---- Initialization ----

nexus.log.info('ExampleAddon', 'Example addon loaded successfully!');
setStatus('Example addon loaded — all nexus.* APIs available');
