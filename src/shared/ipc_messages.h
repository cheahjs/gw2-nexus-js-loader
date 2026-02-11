#pragma once

// IPC message names for communication between browser and renderer processes.
// Convention: "category:action" format.

namespace IPC {

// ---- Logging ----
constexpr const char* LOG_MESSAGE         = "nexus:log";

// ---- Events ----
constexpr const char* EVENTS_SUBSCRIBE    = "nexus:events:subscribe";
constexpr const char* EVENTS_UNSUBSCRIBE  = "nexus:events:unsubscribe";
constexpr const char* EVENTS_RAISE        = "nexus:events:raise";
// Browser → Renderer: push event to JS callback
constexpr const char* EVENTS_DISPATCH     = "nexus:events:dispatch";

// ---- Keybinds ----
constexpr const char* KEYBINDS_REGISTER   = "nexus:keybinds:register";
constexpr const char* KEYBINDS_DEREGISTER = "nexus:keybinds:deregister";
// Browser → Renderer: keybind was pressed/released
constexpr const char* KEYBINDS_INVOKE     = "nexus:keybinds:invoke";

// ---- Game Binds ----
constexpr const char* GAMEBINDS_PRESS     = "nexus:gamebinds:press";
constexpr const char* GAMEBINDS_RELEASE   = "nexus:gamebinds:release";
constexpr const char* GAMEBINDS_INVOKE    = "nexus:gamebinds:invoke";
constexpr const char* GAMEBINDS_ISBOUND   = "nexus:gamebinds:isBound";

// ---- DataLink ----
constexpr const char* DATALINK_GET_MUMBLE = "nexus:datalink:getMumbleLink";
constexpr const char* DATALINK_GET_NEXUS  = "nexus:datalink:getNexusLink";

// ---- Paths ----
constexpr const char* PATHS_GAME_DIR      = "nexus:paths:gameDir";
constexpr const char* PATHS_ADDON_DIR     = "nexus:paths:addonDir";
constexpr const char* PATHS_COMMON_DIR    = "nexus:paths:commonDir";

// ---- Quick Access ----
constexpr const char* QA_ADD              = "nexus:quickaccess:add";
constexpr const char* QA_REMOVE           = "nexus:quickaccess:remove";
constexpr const char* QA_NOTIFY           = "nexus:quickaccess:notify";

// ---- Localization ----
constexpr const char* LOC_TRANSLATE       = "nexus:localization:translate";
constexpr const char* LOC_SET             = "nexus:localization:set";

// ---- Alerts ----
constexpr const char* ALERT               = "nexus:alert";

// ---- Generic async response ----
// Browser → Renderer: carries result of an async call back to JS Promise
// Arguments: [request_id: int, success: bool, value: string/json]
constexpr const char* ASYNC_RESPONSE      = "nexus:async:response";

} // namespace IPC
