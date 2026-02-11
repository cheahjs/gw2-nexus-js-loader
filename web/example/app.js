// GW2 Nexus JS Loader — Example Web App
// Demonstrates all nexus.* API bindings

function setStatus(msg) {
    document.getElementById('statusBar').textContent = msg;
}

function appendLog(elementId, msg) {
    const el = document.getElementById(elementId);
    const line = document.createElement('div');
    line.textContent = `[${new Date().toLocaleTimeString()}] ${msg}`;
    el.appendChild(line);
    el.scrollTop = el.scrollHeight;
}

// ---- Logging ----

function testLog() {
    const channel = document.getElementById('logChannel').value;
    const message = document.getElementById('logMessage').value;
    nexus.log.info(channel, message);
    setStatus(`Logged: ${message}`);
}

// ---- Alert ----

function testAlert() {
    const message = document.getElementById('alertMessage').value;
    nexus.alert(message);
    setStatus(`Alert sent: ${message}`);
}

// ---- Events ----

function subscribeEvent() {
    const name = document.getElementById('eventName').value;
    nexus.events.subscribe(name, function(data) {
        appendLog('eventLog', `Event "${name}" received: ${JSON.stringify(data)}`);
    });
    setStatus(`Subscribed to: ${name}`);
    appendLog('eventLog', `Subscribed to "${name}"`);
}

function raiseEvent() {
    const name = document.getElementById('eventName').value;
    nexus.events.raise(name);
    setStatus(`Raised event: ${name}`);
}

// ---- Data Links ----

async function getMumbleLink() {
    try {
        const result = await nexus.datalink.getMumbleLink();
        document.getElementById('dataLinkOutput').textContent =
            `Mumble Link:\n${JSON.stringify(JSON.parse(result), null, 2)}`;
        setStatus('Got Mumble Link data');
    } catch (e) {
        document.getElementById('dataLinkOutput').textContent = `Error: ${e}`;
        setStatus('Failed to get Mumble Link');
    }
}

async function getNexusLink() {
    try {
        const result = await nexus.datalink.getNexusLink();
        document.getElementById('dataLinkOutput').textContent =
            `Nexus Link:\n${JSON.stringify(JSON.parse(result), null, 2)}`;
        setStatus('Got Nexus Link data');
    } catch (e) {
        document.getElementById('dataLinkOutput').textContent = `Error: ${e}`;
        setStatus('Failed to get Nexus Link');
    }
}

// ---- Paths ----

async function getGameDir() {
    try {
        const result = await nexus.paths.getGameDirectory();
        document.getElementById('pathsOutput').textContent = `Game Dir: ${result}`;
        setStatus('Got game directory');
    } catch (e) {
        document.getElementById('pathsOutput').textContent = `Error: ${e}`;
    }
}

async function getAddonDir() {
    try {
        const result = await nexus.paths.getAddonDirectory('JSLoader');
        document.getElementById('pathsOutput').textContent = `Addon Dir: ${result}`;
        setStatus('Got addon directory');
    } catch (e) {
        document.getElementById('pathsOutput').textContent = `Error: ${e}`;
    }
}

async function getCommonDir() {
    try {
        const result = await nexus.paths.getCommonDirectory();
        document.getElementById('pathsOutput').textContent = `Common Dir: ${result}`;
        setStatus('Got common directory');
    } catch (e) {
        document.getElementById('pathsOutput').textContent = `Error: ${e}`;
    }
}

// ---- Keybinds ----

function registerKeybind() {
    const id = document.getElementById('keybindId').value;
    const defaultBind = document.getElementById('keybindDefault').value;

    nexus.keybinds.register(id, defaultBind, function(identifier, isRelease) {
        const action = isRelease ? 'released' : 'pressed';
        appendLog('keybindLog', `Keybind "${identifier}" ${action}`);
    });

    setStatus(`Registered keybind: ${id}`);
    appendLog('keybindLog', `Registered "${id}" (${defaultBind})`);
}

function deregisterKeybind() {
    const id = document.getElementById('keybindId').value;
    nexus.keybinds.deregister(id);
    setStatus(`Deregistered keybind: ${id}`);
    appendLog('keybindLog', `Deregistered "${id}"`);
}

// ---- Game Binds ----

function pressGameBind() {
    const bind = parseInt(document.getElementById('gameBind').value);
    nexus.gamebinds.press(bind);
    setStatus(`Pressed game bind: ${bind}`);
}

function releaseGameBind() {
    const bind = parseInt(document.getElementById('gameBind').value);
    nexus.gamebinds.release(bind);
    setStatus(`Released game bind: ${bind}`);
}

async function checkIsBound() {
    const bind = parseInt(document.getElementById('gameBind').value);
    try {
        const result = await nexus.gamebinds.isBound(bind);
        document.getElementById('gameBindOutput').textContent =
            `Game bind ${bind} is bound: ${result}`;
        setStatus(`Checked game bind: ${bind}`);
    } catch (e) {
        document.getElementById('gameBindOutput').textContent = `Error: ${e}`;
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
    const id = document.getElementById('locId').value;
    const lang = document.getElementById('locLang').value;
    const text = document.getElementById('locText').value;
    nexus.localization.set(id, lang, text);
    setStatus(`Set localization: ${id} = ${text} (${lang})`);
}

async function translateLocalization() {
    const id = document.getElementById('locId').value;
    try {
        const result = await nexus.localization.translate(id);
        document.getElementById('locOutput').textContent = `Translation: ${result}`;
        setStatus(`Translated: ${id}`);
    } catch (e) {
        document.getElementById('locOutput').textContent = `Error: ${e}`;
    }
}

// ---- Initialization ----

nexus.log.info('ExampleApp', 'Example web app loaded successfully!');
setStatus('Example web app loaded — all nexus.* APIs available');
