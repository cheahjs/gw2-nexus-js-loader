#include "nexus_bridge.h"

namespace NexusBridge {

// The bridge JS is injected on every page load. It provides the window.nexus.*
// API surface identical to the V8 extension that js_bindings.cpp previously
// provided in the renderer subprocess.
//
// JS->Native: console.log("__NEXUS__:" + JSON.stringify({action, ...}))
//   Intercepted by InProcessBrowser::OnConsoleMessage, stripped of prefix,
//   parsed as JSON by IpcHandler::HandleBridgeMessage.
//
// Native->JS: window.__nexus_dispatch({type, ...})
//   Called by IpcHandler via InProcessBrowser::ExecuteJavaScript for async
//   responses, event callbacks, and keybind invocations.
//
// Each browser gets addon/window identity injected as a preamble before this
// script (window.__nexus_addon_id, window.__nexus_window_id). The _send()
// function includes these in every message for routing.

static const std::string s_bridgeScript = R"JS(
(function() {
    'use strict';

    // Avoid double-injection
    if (window.__nexus_bridge_loaded) return;
    window.__nexus_bridge_loaded = true;

    var _addonId = window.__nexus_addon_id || '';
    var _windowId = window.__nexus_window_id || '';

    var _nextRequestId = 1;
    var _pendingRequests = {};    // requestId -> { resolve, reject }
    var _eventCallbacks = {};    // eventName -> [callback, ...]
    var _keybindCallbacks = {};  // keybindId -> callback

    // ---- Internal: send message to native ----
    function _send(msg) {
        msg.__addonId = _addonId;
        msg.__windowId = _windowId;
        console.log('__NEXUS__:' + JSON.stringify(msg));
    }

    // ---- Internal: send async request, returns Promise ----
    function _sendAsync(action, params) {
        return new Promise(function(resolve, reject) {
            var id = _nextRequestId++;
            _pendingRequests[id] = { resolve: resolve, reject: reject };
            var msg = { action: action, requestId: id };
            if (params) {
                for (var k in params) {
                    if (params.hasOwnProperty(k)) msg[k] = params[k];
                }
            }
            _send(msg);
        });
    }

    // ---- Native->JS dispatch handler ----
    window.__nexus_dispatch = function(data) {
        if (!data || !data.type) return;

        if (data.type === 'response') {
            var req = _pendingRequests[data.requestId];
            if (req) {
                delete _pendingRequests[data.requestId];
                if (data.success) {
                    req.resolve(data.value);
                } else {
                    req.reject(data.value);
                }
            }
            return;
        }

        if (data.type === 'event') {
            var cbs = _eventCallbacks[data.name];
            if (cbs) {
                var eventData = data.data;
                for (var i = 0; i < cbs.length; i++) {
                    try { cbs[i](eventData); } catch(e) { console.error('Event callback error:', e); }
                }
            }
            return;
        }

        if (data.type === 'keybind') {
            var cb = _keybindCallbacks[data.id];
            if (cb) {
                try { cb(data.id, data.isRelease); } catch(e) { console.error('Keybind callback error:', e); }
            }
            return;
        }
    };

    // ---- Public API: window.nexus ----
    window.nexus = {
        log: {
            info:     function(channel, message) { _send({ action: 'log', level: 3, channel: channel, message: message }); },
            warning:  function(channel, message) { _send({ action: 'log', level: 2, channel: channel, message: message }); },
            critical: function(channel, message) { _send({ action: 'log', level: 1, channel: channel, message: message }); },
            debug:    function(channel, message) { _send({ action: 'log', level: 4, channel: channel, message: message }); },
            trace:    function(channel, message) { _send({ action: 'log', level: 5, channel: channel, message: message }); }
        },

        events: {
            subscribe: function(name, callback) {
                if (!_eventCallbacks[name]) {
                    _eventCallbacks[name] = [];
                    _send({ action: 'events_subscribe', name: name });
                }
                _eventCallbacks[name].push(callback);
            },
            unsubscribe: function(name, callback) {
                var cbs = _eventCallbacks[name];
                if (!cbs) return;
                var idx = cbs.indexOf(callback);
                if (idx !== -1) cbs.splice(idx, 1);
                if (cbs.length === 0) {
                    delete _eventCallbacks[name];
                    _send({ action: 'events_unsubscribe', name: name });
                }
            },
            raise: function(name, data) {
                _send({ action: 'events_raise', name: name });
            }
        },

        keybinds: {
            register: function(id, defaultBind, callback) {
                _keybindCallbacks[id] = callback;
                _send({ action: 'keybinds_register', id: id, defaultBind: defaultBind });
            },
            deregister: function(id) {
                delete _keybindCallbacks[id];
                _send({ action: 'keybinds_deregister', id: id });
            }
        },

        gamebinds: {
            press:   function(bind) { _send({ action: 'gamebinds_press', bind: bind }); },
            release: function(bind) { _send({ action: 'gamebinds_release', bind: bind }); },
            invoke:  function(bind, durationMs) { _send({ action: 'gamebinds_invoke', bind: bind, durationMs: durationMs }); },
            isBound: function(bind) { return _sendAsync('gamebinds_isBound', { bind: bind }); }
        },

        datalink: {
            getMumbleLink: function() { return _sendAsync('datalink_getMumbleLink'); },
            getNexusLink:  function() { return _sendAsync('datalink_getNexusLink'); }
        },

        paths: {
            getGameDirectory:   function()     { return _sendAsync('paths_getGameDirectory'); },
            getAddonDirectory:  function(name) { return _sendAsync('paths_getAddonDirectory', { name: name || '' }); },
            getCommonDirectory: function()     { return _sendAsync('paths_getCommonDirectory'); }
        },

        quickaccess: {
            add: function(id, texture, textureHover, keybind, tooltip) {
                _send({ action: 'quickaccess_add', id: id, texture: texture,
                         textureHover: textureHover, keybind: keybind, tooltip: tooltip });
            },
            remove: function(id) { _send({ action: 'quickaccess_remove', id: id }); },
            notify: function(id) { _send({ action: 'quickaccess_notify', id: id }); }
        },

        localization: {
            translate: function(id) { return _sendAsync('localization_translate', { id: id }); },
            set: function(id, lang, text) {
                _send({ action: 'localization_set', id: id, lang: lang, text: text });
            }
        },

        windows: {
            create: function(windowId, options) {
                options = options || {};
                return _sendAsync('windows_create', {
                    windowId: windowId,
                    url: options.url || '',
                    width: options.width || 800,
                    height: options.height || 600,
                    title: options.title || ''
                });
            },
            close: function(windowId) {
                _send({ action: 'windows_close', windowId: windowId });
            },
            update: function(windowId, options) {
                options = options || {};
                _send({
                    action: 'windows_update',
                    windowId: windowId,
                    title: options.title,
                    width: options.width,
                    height: options.height,
                    visible: options.visible
                });
            },
            setInputPassthrough: function(windowId, enabled) {
                _send({ action: 'windows_setInputPassthrough', windowId: windowId, enabled: enabled });
            },
            list: function() {
                return _sendAsync('windows_list');
            }
        },

        alert: function(message) { _send({ action: 'alert', message: message }); }
    };
})();
)JS";

const std::string& GetBridgeScript() {
    return s_bridgeScript;
}

} // namespace NexusBridge
