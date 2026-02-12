#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// Wire format: [uint32_t type][uint32_t length][payload bytes...]
// ============================================================================

namespace PipeProtocol {

// --- Message types ---

enum MessageType : uint32_t {
    // Plugin -> Host
    MSG_CREATE_BROWSER      = 1,   // [int32 w][int32 h][url bytes]
    MSG_CLOSE_BROWSER       = 2,   // (empty)
    MSG_SHUTDOWN            = 3,   // (empty)
    MSG_RESIZE              = 4,   // [int32 w][int32 h]
    MSG_NAVIGATE            = 5,   // [url bytes]
    MSG_RELOAD              = 6,   // (empty)
    MSG_MOUSE_MOVE          = 10,  // MouseMoveData
    MSG_MOUSE_CLICK         = 11,  // MouseClickData
    MSG_MOUSE_WHEEL         = 12,  // MouseWheelData
    MSG_KEY_EVENT           = 13,  // KeyEventData
    MSG_NEXUS_API_RESPONSE  = 20,  // [nameLen][name][serialized args]
    MSG_NEXUS_EVENT_DISPATCH = 21, // [nameLen][name][serialized args]
    MSG_NEXUS_KEYBIND_INVOKE = 22, // [nameLen][name][serialized args]

    // Host -> Plugin
    MSG_FRAME_READY         = 100, // (empty, informational)
    MSG_NEXUS_API_REQUEST   = 101, // [nameLen][name][serialized args]
    MSG_HOST_READY          = 102, // (empty)
    MSG_BROWSER_CREATED     = 103, // (empty)
    MSG_HOST_ERROR          = 104, // [error string bytes]
};

// --- Wire header ---

struct WireHeader {
    uint32_t type;
    uint32_t length;
};

static constexpr uint32_t WIRE_HEADER_SIZE = sizeof(WireHeader);

// --- Input data structs (packed) ---

#pragma pack(push, 1)

struct MouseMoveData {
    int32_t  x;
    int32_t  y;
    uint32_t modifiers;
};

struct MouseClickData {
    int32_t  x;
    int32_t  y;
    uint32_t modifiers;
    uint32_t button;     // 0=left, 1=middle, 2=right
    uint8_t  mouseUp;    // 0=down, 1=up
    int32_t  clickCount;
};

struct MouseWheelData {
    int32_t  x;
    int32_t  y;
    uint32_t modifiers;
    int32_t  deltaX;
    int32_t  deltaY;
};

struct KeyEventData {
    uint32_t type;            // KEYEVENT_RAWKEYDOWN=0, KEYEVENT_KEYUP=2, KEYEVENT_CHAR=3
    uint32_t modifiers;
    int32_t  windowsKeyCode;
    int32_t  nativeKeyCode;
    uint8_t  isSystemKey;
    uint16_t character;
};

#pragma pack(pop)

// --- Shared memory layout ---

static constexpr uint32_t MAX_FRAME_WIDTH  = 3840;
static constexpr uint32_t MAX_FRAME_HEIGHT = 2160;
static constexpr uint32_t BYTES_PER_PIXEL  = 4; // BGRA
static constexpr uint32_t MAX_BUFFER_SIZE  = MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT * BYTES_PER_PIXEL;
static constexpr uint32_t HEADER_SIZE      = 64; // Padded header
static constexpr uint32_t SHMEM_TOTAL_SIZE = HEADER_SIZE + (MAX_BUFFER_SIZE * 2);

struct SharedFrameHeader {
    volatile uint32_t writerSeqNum;   // Incremented by host after writing a frame
    volatile uint32_t readerSeqNum;   // (unused, reserved for future)
    volatile uint32_t width;          // Current frame width
    volatile uint32_t height;         // Current frame height
    volatile uint32_t activeBuffer;   // 0 or 1 - which buffer has the latest frame
    uint32_t reserved[11];            // Pad to 64 bytes
};

static_assert(sizeof(SharedFrameHeader) == HEADER_SIZE, "Header size mismatch");

// Get pointer to buffer 0 or 1 within the shared memory region
inline uint8_t* GetBufferPtr(void* shmemBase, uint32_t bufferIndex) {
    auto* base = static_cast<uint8_t*>(shmemBase);
    return base + HEADER_SIZE + (bufferIndex * MAX_BUFFER_SIZE);
}

inline const uint8_t* GetBufferPtr(const void* shmemBase, uint32_t bufferIndex) {
    auto* base = static_cast<const uint8_t*>(shmemBase);
    return base + HEADER_SIZE + (bufferIndex * MAX_BUFFER_SIZE);
}

// --- IPC argument serialization ---
// Used for NEXUS_API_REQUEST, NEXUS_API_RESPONSE, EVENT_DISPATCH, KEYBIND_INVOKE

struct PipeArg {
    enum Type : uint8_t { TYPE_INT = 0, TYPE_STRING = 1, TYPE_BOOL = 2 };
    Type type = TYPE_INT;
    int32_t intVal = 0;
    std::string strVal;
    bool boolVal = false;

    static PipeArg Int(int32_t v) {
        PipeArg a; a.type = TYPE_INT; a.intVal = v; return a;
    }
    static PipeArg String(const std::string& v) {
        PipeArg a; a.type = TYPE_STRING; a.strVal = v; return a;
    }
    static PipeArg Bool(bool v) {
        PipeArg a; a.type = TYPE_BOOL; a.boolVal = v; return a;
    }
};

// Serialize a message name + args into a binary payload
// Format: [uint32 nameLen][name bytes][uint16 argCount][per-arg: type + data]
inline std::vector<uint8_t> SerializeIpcMessage(const std::string& name,
                                                  const std::vector<PipeArg>& args) {
    std::vector<uint8_t> buf;
    // Reserve rough estimate
    buf.reserve(4 + name.size() + 2 + args.size() * 8);

    // Name
    uint32_t nameLen = static_cast<uint32_t>(name.size());
    buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&nameLen),
               reinterpret_cast<const uint8_t*>(&nameLen) + 4);
    buf.insert(buf.end(), name.begin(), name.end());

    // Arg count
    uint16_t argCount = static_cast<uint16_t>(args.size());
    buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&argCount),
               reinterpret_cast<const uint8_t*>(&argCount) + 2);

    // Each arg
    for (const auto& arg : args) {
        buf.push_back(static_cast<uint8_t>(arg.type));
        switch (arg.type) {
            case PipeArg::TYPE_INT: {
                int32_t v = arg.intVal;
                buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&v),
                           reinterpret_cast<const uint8_t*>(&v) + 4);
                break;
            }
            case PipeArg::TYPE_STRING: {
                uint32_t len = static_cast<uint32_t>(arg.strVal.size());
                buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(&len),
                           reinterpret_cast<const uint8_t*>(&len) + 4);
                buf.insert(buf.end(), arg.strVal.begin(), arg.strVal.end());
                break;
            }
            case PipeArg::TYPE_BOOL: {
                buf.push_back(arg.boolVal ? 1 : 0);
                break;
            }
        }
    }

    return buf;
}

// Deserialize a binary payload into message name + args
// Returns false on parse error
inline bool DeserializeIpcMessage(const uint8_t* data, size_t dataLen,
                                    std::string& outName,
                                    std::vector<PipeArg>& outArgs) {
    size_t offset = 0;

    // Name
    if (offset + 4 > dataLen) return false;
    uint32_t nameLen;
    memcpy(&nameLen, data + offset, 4);
    offset += 4;
    if (offset + nameLen > dataLen) return false;
    outName.assign(reinterpret_cast<const char*>(data + offset), nameLen);
    offset += nameLen;

    // Arg count
    if (offset + 2 > dataLen) return false;
    uint16_t argCount;
    memcpy(&argCount, data + offset, 2);
    offset += 2;

    outArgs.clear();
    outArgs.reserve(argCount);

    for (uint16_t i = 0; i < argCount; ++i) {
        if (offset + 1 > dataLen) return false;
        PipeArg arg;
        arg.type = static_cast<PipeArg::Type>(data[offset++]);

        switch (arg.type) {
            case PipeArg::TYPE_INT: {
                if (offset + 4 > dataLen) return false;
                memcpy(&arg.intVal, data + offset, 4);
                offset += 4;
                break;
            }
            case PipeArg::TYPE_STRING: {
                if (offset + 4 > dataLen) return false;
                uint32_t len;
                memcpy(&len, data + offset, 4);
                offset += 4;
                if (offset + len > dataLen) return false;
                arg.strVal.assign(reinterpret_cast<const char*>(data + offset), len);
                offset += len;
                break;
            }
            case PipeArg::TYPE_BOOL: {
                if (offset + 1 > dataLen) return false;
                arg.boolVal = (data[offset++] != 0);
                break;
            }
            default:
                return false;
        }

        outArgs.push_back(std::move(arg));
    }

    return true;
}

// --- Pipe message (in-memory representation after reading from wire) ---

struct PipeMessage {
    uint32_t type;
    std::vector<uint8_t> payload;
};

} // namespace PipeProtocol
