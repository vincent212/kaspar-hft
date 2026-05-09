#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <string>
#include <vector>
#include <set>
#include <chrono>
#include <cstring>

namespace actors::coordination {

/**
 * Message types for the coordination protocol.
 *
 * The protocol supports:
 * - Registration: Groups and actors register with GroupManager
 * - Permission: Deterministic execution via permission tokens
 * - Shutdown: Coordinated shutdown across all groups
 */
enum class MsgType : uint8_t {
    // Registration messages (GroupManager)
    REGISTER_GROUP = 1,
    REGISTER_ACTOR = 2,
    REGISTER_ACK = 3,
    REGISTER_NACK = 4,

    // Permission protocol messages
    PERMISSION_TOKEN = 10,
    PERMISSION_REQUEST = 11,
    PERMISSION_GRANT = 12,
    PERMISSION_WAIT = 13,
    PERMISSION_DONE = 14,

    // Shutdown messages
    SHUTDOWN_REQUEST = 20,
    SHUTDOWN = 21,
    SHUTDOWN_ACK = 22,

    // GlobalRegistry messages
    REGISTER_MANAGER = 30,      // Manager registering with endpoint and actors
    UNREGISTER_MANAGER = 31,    // Manager explicitly unregistering
    LOOKUP_ACTOR = 32,          // Query for actor endpoint
    LOOKUP_RESULT = 33,         // Response with endpoint (or not found)
    HEARTBEAT = 34,             // Keep connection alive
    HEARTBEAT_ACK = 35,         // Response to heartbeat

    // Debug messages (40-59)
    DEBUG_STOP = 40,            // Stop granting permissions (pause)
    DEBUG_CONTINUE = 41,        // Resume granting permissions
    DEBUG_ADD_BREAKPOINT = 42,  // Add breakpoint on actor pattern
    DEBUG_REMOVE_BREAKPOINT = 43, // Remove breakpoint by ID
    DEBUG_LIST_BREAKPOINTS = 44,  // Query all breakpoints
    DEBUG_BREAKPOINTS_RESPONSE = 45, // Response with breakpoints
    DEBUG_LIST_GROUPS = 46,     // Query registered groups
    DEBUG_GROUPS_RESPONSE = 47, // Response with groups list
    DEBUG_LIST_ACTORS = 48,     // Query actors in a specific group
    DEBUG_ACTORS_RESPONSE = 49, // Response with actors list
    DEBUG_STATUS = 50,          // Query debug state
    DEBUG_STATUS_RESPONSE = 51, // Response with debug state
    DEBUG_SET_LOGGING = 52,     // Enable/disable permission logging
    DEBUG_LOGGING_RESPONSE = 53, // Confirm logging state
    DEBUG_GET_QUEUE = 54,       // Query permission queue contents
    DEBUG_QUEUE_RESPONSE = 55,  // Response with queue entries
    DEBUG_BREAKPOINT_ADDED = 56,// Response when breakpoint is added
    DEBUG_FLUSH = 57,           // Flush token queue and pending requests

    // Registry query messages (58-61)
    LIST_MANAGERS = 58,         // Request list of all managers
    MANAGERS_RESPONSE = 59,     // Response with manager list
    LIST_ALL_ACTORS = 60,       // Request list of all actors
    ALL_ACTORS_RESPONSE = 61    // Response with all actors
};

// ============================================================================
// Registration Messages
// ============================================================================

/**
 * Group registration request.
 * Sent by a Group when it connects to the GroupManager.
 */
struct RegisterGroup {
    std::string group_id;

    RegisterGroup() = default;
    explicit RegisterGroup(const std::string& id) : group_id(id) {}
};

/**
 * Actor registration request.
 * Sent by a Group for each actor it manages.
 */
struct RegisterActor {
    std::string group_id;
    std::string actor_id;

    RegisterActor() = default;
    RegisterActor(const std::string& gid, const std::string& aid)
        : group_id(gid), actor_id(aid) {}

    // Returns the full actor ID (group.actor)
    std::string full_id() const {
        return group_id + "." + actor_id;
    }
};

/**
 * Registration acknowledgment (success).
 */
struct RegisterAck {
    std::string id;  // group_id or full actor_id (group.actor)

    RegisterAck() = default;
    explicit RegisterAck(const std::string& id) : id(id) {}
};

/**
 * Registration rejection (failure).
 */
struct RegisterNack {
    std::string id;
    std::string reason;

    RegisterNack() = default;
    RegisterNack(const std::string& id, const std::string& reason)
        : id(id), reason(reason) {}
};

// ============================================================================
// Permission Protocol Messages
// ============================================================================

/**
 * Permission token sent with every actor message in coordinated mode.
 *
 * When an actor sends a message, the Group sends this token to the
 * GroupManager BEFORE delivering the message payload. This ensures
 * the GroupManager knows about all pending work.
 */
struct PermissionToken {
    std::string recipient_id;   // Full actor ID (group.actor) - who will process
    std::string sender_id;      // Full actor ID - who sent the message
    uint64_t timestamp;         // Logical timestamp for ordering
    std::string message_type;   // Message type name for tracing (e.g., "NewOrder")

    PermissionToken() : timestamp(0) {}
    PermissionToken(const std::string& recipient, const std::string& sender, uint64_t ts,
                    const std::string& msg_type = "")
        : recipient_id(recipient), sender_id(sender), timestamp(ts), message_type(msg_type) {}
};

/**
 * Permission request from a Group.
 *
 * When an actor has a message to process, its Group sends this request
 * to ask for permission to execute. Only one actor across all Groups
 * may execute at a time.
 */
struct PermissionRequest {
    std::string actor_id;   // Full actor ID requesting permission
    std::string group_id;   // Group making the request

    PermissionRequest() = default;
    PermissionRequest(const std::string& aid, const std::string& gid)
        : actor_id(aid), group_id(gid) {}
};

/**
 * Permission granted by GroupManager.
 *
 * Sent when an actor is at the head of the permission queue and
 * may proceed with processing its next message.
 */
struct PermissionGrant {
    std::string actor_id;
    uint64_t sequence;      // Global sequence number for this grant

    PermissionGrant() : sequence(0) {}
    PermissionGrant(const std::string& aid, uint64_t seq)
        : actor_id(aid), sequence(seq) {}
};

/**
 * Permission wait notification (informational).
 *
 * Sent when an actor requests permission but is not at the head
 * of the queue. The Group should wait and retry.
 */
struct PermissionWait {
    std::string actor_id;
    uint32_t position;      // Position in queue (0 = head)

    PermissionWait() : position(0) {}
    PermissionWait(const std::string& aid, uint32_t pos)
        : actor_id(aid), position(pos) {}
};

/**
 * Permission completion notification.
 *
 * Sent by a Group after its actor has finished processing a message.
 * This allows the GroupManager to grant the next permission.
 */
struct PermissionDone {
    std::string actor_id;
    uint64_t sequence;      // Sequence number of the completed grant

    PermissionDone() : sequence(0) {}
    PermissionDone(const std::string& aid, uint64_t seq)
        : actor_id(aid), sequence(seq) {}
};

// ============================================================================
// Shutdown Messages
// ============================================================================

/**
 * Shutdown request from a Group.
 *
 * Sent when an actor triggers a shutdown (e.g., end of simulation).
 * The GroupManager will coordinate shutdown across all Groups.
 */
struct ShutdownRequest {
    std::string group_id;
    std::string reason;

    ShutdownRequest() = default;
    ShutdownRequest(const std::string& gid, const std::string& reason)
        : group_id(gid), reason(reason) {}
};

/**
 * Shutdown command from GroupManager.
 *
 * Sent to all Groups when shutdown is initiated. Groups should
 * stop processing, clean up, and send ShutdownAck.
 */
struct Shutdown {
    std::string reason;

    Shutdown() = default;
    explicit Shutdown(const std::string& reason) : reason(reason) {}
};

/**
 * Shutdown acknowledgment from a Group.
 *
 * Sent after a Group has shut down all its actors.
 * GroupManager exits when all Groups have acknowledged.
 */
struct ShutdownAck {
    std::string group_id;

    ShutdownAck() = default;
    explicit ShutdownAck(const std::string& gid) : group_id(gid) {}
};

// ============================================================================
// GlobalRegistry Messages
// ============================================================================

/**
 * Manager registration with GlobalRegistry.
 *
 * Sent when a Manager connects to register itself and all its actors.
 * The endpoint is how other managers can reach this manager.
 */
struct RegisterManager {
    std::string manager_id;              // Unique manager ID
    std::string endpoint;                // ZMQ endpoint for reaching this manager
    std::vector<std::string> actors;     // List of actor names (not qualified)

    RegisterManager() = default;
    RegisterManager(const std::string& mid, const std::string& ep,
                    const std::vector<std::string>& acts = {})
        : manager_id(mid), endpoint(ep), actors(acts) {}
};

/**
 * Manager unregistration (graceful disconnect).
 *
 * Sent when a Manager is shutting down to cleanly deregister.
 */
struct UnregisterManager {
    std::string manager_id;

    UnregisterManager() = default;
    explicit UnregisterManager(const std::string& mid) : manager_id(mid) {}
};

/**
 * Look up an actor's endpoint by name.
 *
 * Supports both qualified (manager::actor) and unqualified (actor) names.
 * Unqualified lookups succeed only if the actor name is unique across managers.
 */
struct LookupActor {
    std::string name;           // "actor" or "manager::actor"
    std::string requester_id;   // Who's asking (for logging)

    LookupActor() = default;
    LookupActor(const std::string& n, const std::string& req)
        : name(n), requester_id(req) {}
};

/**
 * Result of actor lookup.
 *
 * Lookup scenarios:
 * - found=true, ambiguous=false  → Success, use endpoint
 * - found=false, ambiguous=false → Actor doesn't exist
 * - found=false, ambiguous=true  → Actor exists in multiple managers, use qualified name
 */
struct LookupResult {
    std::string actor_id;       // The actor that was looked up
    bool found;                 // Whether the actor was uniquely found
    bool ambiguous;             // True if actor exists in multiple managers
    std::string endpoint;       // Endpoint if found, empty if not
    std::string manager_id;     // Manager that owns this actor

    LookupResult() : found(false), ambiguous(false) {}
    LookupResult(const std::string& aid, bool f, bool amb,
                 const std::string& ep = "", const std::string& mid = "")
        : actor_id(aid), found(f), ambiguous(amb), endpoint(ep), manager_id(mid) {}

    // Factory methods for common cases
    static LookupResult not_found(const std::string& actor_id) {
        return LookupResult(actor_id, false, false);
    }

    static LookupResult found_at(const std::string& actor_id,
                                  const std::string& endpoint,
                                  const std::string& manager_id) {
        return LookupResult(actor_id, true, false, endpoint, manager_id);
    }

    static LookupResult ambiguous_name(const std::string& actor_id) {
        return LookupResult(actor_id, false, true);
    }
};

/**
 * Heartbeat to keep connection alive.
 *
 * Managers send this periodically to prevent timeout-based deregistration.
 */
struct Heartbeat {
    std::string manager_id;

    Heartbeat() = default;
    explicit Heartbeat(const std::string& mid) : manager_id(mid) {}
};

/**
 * Heartbeat acknowledgment.
 *
 * Sent by GlobalRegistry in response to Heartbeat.
 */
struct HeartbeatAck {
    uint64_t timestamp;         // Server timestamp

    HeartbeatAck() : timestamp(0) {}
    explicit HeartbeatAck(uint64_t ts) : timestamp(ts) {}
};

/**
 * Manager info stored by registry.
 *
 * Tracks manager state including actors and heartbeat timestamp.
 */
struct ManagerInfo {
    std::string manager_id;
    std::string endpoint;
    std::string zmq_identity;
    std::set<std::string> actors;  // Actor names (not qualified)
    std::chrono::steady_clock::time_point last_heartbeat;

    ManagerInfo() = default;
    ManagerInfo(const std::string& id, const std::string& ep, const std::string& identity)
        : manager_id(id), endpoint(ep), zmq_identity(identity),
          last_heartbeat(std::chrono::steady_clock::now()) {}
};

// ============================================================================
// Debug Messages
// ============================================================================

/**
 * Debug stop command - pause permission granting.
 */
struct DebugStop {
    std::string reason;

    DebugStop() = default;
    explicit DebugStop(const std::string& r) : reason(r) {}
};

/**
 * Debug continue command - resume permission granting.
 */
struct DebugContinue {
    DebugContinue() = default;
};

/**
 * Add a breakpoint on actor pattern.
 * Pattern can be exact "group.actor" or wildcard "group.*"
 */
struct DebugAddBreakpoint {
    std::string pattern;

    DebugAddBreakpoint() = default;
    explicit DebugAddBreakpoint(const std::string& p) : pattern(p) {}
};

/**
 * Response when breakpoint is added.
 */
struct DebugBreakpointAdded {
    uint32_t breakpoint_id;
    std::string pattern;

    DebugBreakpointAdded() : breakpoint_id(0) {}
    DebugBreakpointAdded(uint32_t id, const std::string& p)
        : breakpoint_id(id), pattern(p) {}
};

/**
 * Remove a breakpoint by ID.
 */
struct DebugRemoveBreakpoint {
    uint32_t breakpoint_id;

    DebugRemoveBreakpoint() : breakpoint_id(0) {}
    explicit DebugRemoveBreakpoint(uint32_t id) : breakpoint_id(id) {}
};

/**
 * Request to list all breakpoints.
 */
struct DebugListBreakpoints {
    DebugListBreakpoints() = default;
};

/**
 * Response with all breakpoints.
 */
struct DebugBreakpointsResponse {
    std::vector<uint32_t> ids;
    std::vector<std::string> patterns;

    DebugBreakpointsResponse() = default;
};

/**
 * Request to list all registered groups.
 */
struct DebugListGroups {
    DebugListGroups() = default;
};

/**
 * Response with all registered groups.
 */
struct DebugGroupsResponse {
    std::vector<std::string> groups;

    DebugGroupsResponse() = default;
};

/**
 * Request to list actors in a specific group.
 */
struct DebugListActors {
    std::string group_id;

    DebugListActors() = default;
    explicit DebugListActors(const std::string& g) : group_id(g) {}
};

/**
 * Response with actors in a group.
 */
struct DebugActorsResponse {
    std::string group_id;
    std::vector<std::string> actors;

    DebugActorsResponse() = default;
    explicit DebugActorsResponse(const std::string& g) : group_id(g) {}
};

/**
 * Request current debug status.
 */
struct DebugStatus {
    DebugStatus() = default;
};

/**
 * Response with current debug status.
 */
struct DebugStatusResponse {
    bool stopped;
    std::string stopped_reason;
    uint64_t current_sequence;
    uint32_t queue_size;
    std::string active_actor;
    bool logging_enabled;

    DebugStatusResponse()
        : stopped(false), current_sequence(0), queue_size(0), logging_enabled(false) {}
};

/**
 * Enable/disable permission logging.
 */
struct DebugSetLogging {
    bool enabled;

    DebugSetLogging() : enabled(false) {}
    explicit DebugSetLogging(bool e) : enabled(e) {}
};

/**
 * Response confirming logging state.
 */
struct DebugLoggingResponse {
    bool enabled;

    DebugLoggingResponse() : enabled(false) {}
    explicit DebugLoggingResponse(bool e) : enabled(e) {}
};

/**
 * Request permission queue contents (only when stopped).
 */
struct DebugGetQueue {
    DebugGetQueue() = default;
};

/**
 * Response with permission queue contents.
 * Position 0 = head (next to be granted).
 */
struct DebugQueueResponse {
    std::vector<std::string> recipient_ids;
    std::vector<std::string> sender_ids;
    std::vector<uint64_t> timestamps;

    DebugQueueResponse() = default;
};

/**
 * Flush token queue and pending requests (clear all state).
 * Useful for resetting coordinator between tests.
 */
struct DebugFlush {
    DebugFlush() = default;
};

// ============================================================================
// Registry Query Messages
// ============================================================================

/**
 * Request list of all registered managers.
 * Sent by RegistryClient to query all manager IDs.
 */
struct ListManagers {
    ListManagers() = default;
};

/**
 * Response with list of all managers.
 */
struct ManagersResponse {
    std::vector<std::string> manager_ids;
    std::vector<std::string> endpoints;

    ManagersResponse() = default;
};

/**
 * Request list of all registered actors.
 * Sent by RegistryClient to query all actors across all managers.
 */
struct ListAllActors {
    ListAllActors() = default;
};

/**
 * Response with list of all actors.
 * Each actor includes its name and the manager it belongs to.
 */
struct AllActorsResponse {
    std::vector<std::string> actor_names;    // Actor names (not qualified)
    std::vector<std::string> manager_ids;    // Manager owning each actor

    AllActorsResponse() = default;
};

// ============================================================================
// Serialization Helpers
// ============================================================================

/**
 * Simple binary serialization for coordination messages.
 * Format: [1 byte MsgType][payload]
 * Strings: [4 bytes length][string data]
 */
class MessageSerializer {
public:
    // Serialize a message to bytes
    template<typename T>
    static std::vector<uint8_t> serialize(MsgType type, const T& msg);

    // Get message type from bytes
    static MsgType get_type(const uint8_t* data, size_t len) {
        if (len < 1) return static_cast<MsgType>(0);
        return static_cast<MsgType>(data[0]);
    }

    // Deserialize specific message types
    static RegisterGroup deserialize_register_group(const uint8_t* data, size_t len);
    static RegisterActor deserialize_register_actor(const uint8_t* data, size_t len);
    static RegisterAck deserialize_register_ack(const uint8_t* data, size_t len);
    static RegisterNack deserialize_register_nack(const uint8_t* data, size_t len);
    static PermissionToken deserialize_permission_token(const uint8_t* data, size_t len);
    static PermissionRequest deserialize_permission_request(const uint8_t* data, size_t len);
    static PermissionGrant deserialize_permission_grant(const uint8_t* data, size_t len);
    static PermissionWait deserialize_permission_wait(const uint8_t* data, size_t len);
    static PermissionDone deserialize_permission_done(const uint8_t* data, size_t len);
    static ShutdownRequest deserialize_shutdown_request(const uint8_t* data, size_t len);
    static Shutdown deserialize_shutdown(const uint8_t* data, size_t len);
    static ShutdownAck deserialize_shutdown_ack(const uint8_t* data, size_t len);

    // GlobalRegistry message deserialization
    static RegisterManager deserialize_register_manager(const uint8_t* data, size_t len);
    static UnregisterManager deserialize_unregister_manager(const uint8_t* data, size_t len);
    static LookupActor deserialize_lookup_actor(const uint8_t* data, size_t len);
    static LookupResult deserialize_lookup_result(const uint8_t* data, size_t len);
    static Heartbeat deserialize_heartbeat(const uint8_t* data, size_t len);
    static HeartbeatAck deserialize_heartbeat_ack(const uint8_t* data, size_t len);

    // Debug message deserialization (requests)
    static DebugStop deserialize_debug_stop(const uint8_t* data, size_t len);
    static DebugContinue deserialize_debug_continue(const uint8_t* data, size_t len);
    static DebugAddBreakpoint deserialize_debug_add_breakpoint(const uint8_t* data, size_t len);
    static DebugRemoveBreakpoint deserialize_debug_remove_breakpoint(const uint8_t* data, size_t len);
    static DebugListBreakpoints deserialize_debug_list_breakpoints(const uint8_t* data, size_t len);
    static DebugListGroups deserialize_debug_list_groups(const uint8_t* data, size_t len);
    static DebugListActors deserialize_debug_list_actors(const uint8_t* data, size_t len);
    static DebugStatus deserialize_debug_status(const uint8_t* data, size_t len);
    static DebugSetLogging deserialize_debug_set_logging(const uint8_t* data, size_t len);
    static DebugGetQueue deserialize_debug_get_queue(const uint8_t* data, size_t len);
    static DebugFlush deserialize_debug_flush(const uint8_t* data, size_t len);

    // Debug message deserialization (responses)
    static DebugBreakpointAdded deserialize_debug_breakpoint_added(const uint8_t* data, size_t len);
    static DebugBreakpointsResponse deserialize_debug_breakpoints_response(const uint8_t* data, size_t len);
    static DebugGroupsResponse deserialize_debug_groups_response(const uint8_t* data, size_t len);
    static DebugActorsResponse deserialize_debug_actors_response(const uint8_t* data, size_t len);
    static DebugStatusResponse deserialize_debug_status_response(const uint8_t* data, size_t len);
    static DebugLoggingResponse deserialize_debug_logging_response(const uint8_t* data, size_t len);
    static DebugQueueResponse deserialize_debug_queue_response(const uint8_t* data, size_t len);

    // Registry query message deserialization
    static ListManagers deserialize_list_managers(const uint8_t* data, size_t len);
    static ManagersResponse deserialize_managers_response(const uint8_t* data, size_t len);
    static ListAllActors deserialize_list_all_actors(const uint8_t* data, size_t len);
    static AllActorsResponse deserialize_all_actors_response(const uint8_t* data, size_t len);

private:
    // Helper to write a string (length-prefixed)
    static void write_string(std::vector<uint8_t>& buf, const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.size());
        buf.push_back((len >> 0) & 0xFF);
        buf.push_back((len >> 8) & 0xFF);
        buf.push_back((len >> 16) & 0xFF);
        buf.push_back((len >> 24) & 0xFF);
        buf.insert(buf.end(), s.begin(), s.end());
    }

    // Helper to read a string
    static std::string read_string(const uint8_t*& ptr, const uint8_t* end) {
        if (ptr + 4 > end) return "";
        uint32_t len = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
        ptr += 4;
        if (ptr + len > end) return "";
        std::string s(reinterpret_cast<const char*>(ptr), len);
        ptr += len;
        return s;
    }

    // Helper to write uint64
    static void write_uint64(std::vector<uint8_t>& buf, uint64_t v) {
        for (int i = 0; i < 8; i++) {
            buf.push_back((v >> (i * 8)) & 0xFF);
        }
    }

    // Helper to read uint64
    static uint64_t read_uint64(const uint8_t*& ptr, const uint8_t* end) {
        if (ptr + 8 > end) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) {
            v |= static_cast<uint64_t>(ptr[i]) << (i * 8);
        }
        ptr += 8;
        return v;
    }

    // Helper to write uint32
    static void write_uint32(std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back((v >> 0) & 0xFF);
        buf.push_back((v >> 8) & 0xFF);
        buf.push_back((v >> 16) & 0xFF);
        buf.push_back((v >> 24) & 0xFF);
    }

    // Helper to read uint32
    static uint32_t read_uint32(const uint8_t*& ptr, const uint8_t* end) {
        if (ptr + 4 > end) return 0;
        uint32_t v = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
        ptr += 4;
        return v;
    }

    // Helper to write a vector of strings
    static void write_string_vector(std::vector<uint8_t>& buf, const std::vector<std::string>& vec) {
        write_uint32(buf, static_cast<uint32_t>(vec.size()));
        for (const auto& s : vec) {
            write_string(buf, s);
        }
    }

    // Helper to read a vector of strings
    static std::vector<std::string> read_string_vector(const uint8_t*& ptr, const uint8_t* end) {
        std::vector<std::string> vec;
        uint32_t count = read_uint32(ptr, end);
        vec.reserve(count);
        for (uint32_t i = 0; i < count && ptr < end; ++i) {
            vec.push_back(read_string(ptr, end));
        }
        return vec;
    }

    // Helper to write bool
    static void write_bool(std::vector<uint8_t>& buf, bool v) {
        buf.push_back(v ? 1 : 0);
    }

    // Helper to read bool
    static bool read_bool(const uint8_t*& ptr, const uint8_t* end) {
        if (ptr >= end) return false;
        return *ptr++ != 0;
    }

    // Helper to write a vector of uint32
    static void write_uint32_vector(std::vector<uint8_t>& buf, const std::vector<uint32_t>& vec) {
        write_uint32(buf, static_cast<uint32_t>(vec.size()));
        for (const auto& v : vec) {
            write_uint32(buf, v);
        }
    }

    // Helper to read a vector of uint32
    static std::vector<uint32_t> read_uint32_vector(const uint8_t*& ptr, const uint8_t* end) {
        std::vector<uint32_t> vec;
        uint32_t count = read_uint32(ptr, end);
        vec.reserve(count);
        for (uint32_t i = 0; i < count && ptr < end; ++i) {
            vec.push_back(read_uint32(ptr, end));
        }
        return vec;
    }

    // Helper to write a vector of uint64
    static void write_uint64_vector(std::vector<uint8_t>& buf, const std::vector<uint64_t>& vec) {
        write_uint32(buf, static_cast<uint32_t>(vec.size()));
        for (const auto& v : vec) {
            write_uint64(buf, v);
        }
    }

    // Helper to read a vector of uint64
    static std::vector<uint64_t> read_uint64_vector(const uint8_t*& ptr, const uint8_t* end) {
        std::vector<uint64_t> vec;
        uint32_t count = read_uint32(ptr, end);
        vec.reserve(count);
        for (uint32_t i = 0; i < count && ptr < end; ++i) {
            vec.push_back(read_uint64(ptr, end));
        }
        return vec;
    }
};

// ============================================================================
// Serialization Implementations
// ============================================================================

// Specializations for each message type
template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const RegisterGroup& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.group_id);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const RegisterActor& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.group_id);
    write_string(buf, msg.actor_id);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const RegisterAck& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.id);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const RegisterNack& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.id);
    write_string(buf, msg.reason);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const PermissionToken& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.recipient_id);
    write_string(buf, msg.sender_id);
    write_uint64(buf, msg.timestamp);
    write_string(buf, msg.message_type);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const PermissionRequest& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.actor_id);
    write_string(buf, msg.group_id);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const PermissionGrant& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.actor_id);
    write_uint64(buf, msg.sequence);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const PermissionWait& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.actor_id);
    write_uint32(buf, msg.position);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const PermissionDone& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.actor_id);
    write_uint64(buf, msg.sequence);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const ShutdownRequest& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.group_id);
    write_string(buf, msg.reason);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const Shutdown& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.reason);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const ShutdownAck& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.group_id);
    return buf;
}

// Deserialization implementations
inline RegisterGroup MessageSerializer::deserialize_register_group(const uint8_t* data, size_t len) {
    RegisterGroup msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;  // Skip type byte
    const uint8_t* end = data + len;
    msg.group_id = read_string(ptr, end);
    return msg;
}

inline RegisterActor MessageSerializer::deserialize_register_actor(const uint8_t* data, size_t len) {
    RegisterActor msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.group_id = read_string(ptr, end);
    msg.actor_id = read_string(ptr, end);
    return msg;
}

inline RegisterAck MessageSerializer::deserialize_register_ack(const uint8_t* data, size_t len) {
    RegisterAck msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.id = read_string(ptr, end);
    return msg;
}

inline RegisterNack MessageSerializer::deserialize_register_nack(const uint8_t* data, size_t len) {
    RegisterNack msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.id = read_string(ptr, end);
    msg.reason = read_string(ptr, end);
    return msg;
}

inline PermissionToken MessageSerializer::deserialize_permission_token(const uint8_t* data, size_t len) {
    PermissionToken msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.recipient_id = read_string(ptr, end);
    msg.sender_id = read_string(ptr, end);
    msg.timestamp = read_uint64(ptr, end);
    // Read message_type if present (for backward compatibility with old clients)
    if (ptr < end) {
        msg.message_type = read_string(ptr, end);
    }
    return msg;
}

inline PermissionRequest MessageSerializer::deserialize_permission_request(const uint8_t* data, size_t len) {
    PermissionRequest msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.actor_id = read_string(ptr, end);
    msg.group_id = read_string(ptr, end);
    return msg;
}

inline PermissionGrant MessageSerializer::deserialize_permission_grant(const uint8_t* data, size_t len) {
    PermissionGrant msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.actor_id = read_string(ptr, end);
    msg.sequence = read_uint64(ptr, end);
    return msg;
}

inline PermissionWait MessageSerializer::deserialize_permission_wait(const uint8_t* data, size_t len) {
    PermissionWait msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.actor_id = read_string(ptr, end);
    msg.position = read_uint32(ptr, end);
    return msg;
}

inline PermissionDone MessageSerializer::deserialize_permission_done(const uint8_t* data, size_t len) {
    PermissionDone msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.actor_id = read_string(ptr, end);
    msg.sequence = read_uint64(ptr, end);
    return msg;
}

inline ShutdownRequest MessageSerializer::deserialize_shutdown_request(const uint8_t* data, size_t len) {
    ShutdownRequest msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.group_id = read_string(ptr, end);
    msg.reason = read_string(ptr, end);
    return msg;
}

inline Shutdown MessageSerializer::deserialize_shutdown(const uint8_t* data, size_t len) {
    Shutdown msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.reason = read_string(ptr, end);
    return msg;
}

inline ShutdownAck MessageSerializer::deserialize_shutdown_ack(const uint8_t* data, size_t len) {
    ShutdownAck msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.group_id = read_string(ptr, end);
    return msg;
}

// ============================================================================
// GlobalRegistry Serialization Implementations
// ============================================================================

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const RegisterManager& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.manager_id);
    write_string(buf, msg.endpoint);
    write_string_vector(buf, msg.actors);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const UnregisterManager& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.manager_id);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const LookupActor& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.name);
    write_string(buf, msg.requester_id);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const LookupResult& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.actor_id);
    write_bool(buf, msg.found);
    write_bool(buf, msg.ambiguous);
    write_string(buf, msg.endpoint);
    write_string(buf, msg.manager_id);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const Heartbeat& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.manager_id);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const HeartbeatAck& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_uint64(buf, msg.timestamp);
    return buf;
}

// GlobalRegistry deserialization implementations

inline RegisterManager MessageSerializer::deserialize_register_manager(const uint8_t* data, size_t len) {
    RegisterManager msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.manager_id = read_string(ptr, end);
    msg.endpoint = read_string(ptr, end);
    msg.actors = read_string_vector(ptr, end);
    return msg;
}

inline UnregisterManager MessageSerializer::deserialize_unregister_manager(const uint8_t* data, size_t len) {
    UnregisterManager msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.manager_id = read_string(ptr, end);
    return msg;
}

inline LookupActor MessageSerializer::deserialize_lookup_actor(const uint8_t* data, size_t len) {
    LookupActor msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.name = read_string(ptr, end);
    msg.requester_id = read_string(ptr, end);
    return msg;
}

inline LookupResult MessageSerializer::deserialize_lookup_result(const uint8_t* data, size_t len) {
    LookupResult msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.actor_id = read_string(ptr, end);
    msg.found = read_bool(ptr, end);
    msg.ambiguous = read_bool(ptr, end);
    msg.endpoint = read_string(ptr, end);
    msg.manager_id = read_string(ptr, end);
    return msg;
}

inline Heartbeat MessageSerializer::deserialize_heartbeat(const uint8_t* data, size_t len) {
    Heartbeat msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.manager_id = read_string(ptr, end);
    return msg;
}

inline HeartbeatAck MessageSerializer::deserialize_heartbeat_ack(const uint8_t* data, size_t len) {
    HeartbeatAck msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.timestamp = read_uint64(ptr, end);
    return msg;
}

// ============================================================================
// Debug Message Serialization Implementations
// ============================================================================

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugStop& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.reason);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugContinue&) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugAddBreakpoint& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.pattern);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugBreakpointAdded& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_uint32(buf, msg.breakpoint_id);
    write_string(buf, msg.pattern);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugRemoveBreakpoint& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_uint32(buf, msg.breakpoint_id);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugListBreakpoints&) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugBreakpointsResponse& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_uint32_vector(buf, msg.ids);
    write_string_vector(buf, msg.patterns);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugListGroups&) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugGroupsResponse& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string_vector(buf, msg.groups);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugListActors& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.group_id);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugActorsResponse& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string(buf, msg.group_id);
    write_string_vector(buf, msg.actors);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugStatus&) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugStatusResponse& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_bool(buf, msg.stopped);
    write_string(buf, msg.stopped_reason);
    write_uint64(buf, msg.current_sequence);
    write_uint32(buf, msg.queue_size);
    write_string(buf, msg.active_actor);
    write_bool(buf, msg.logging_enabled);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugSetLogging& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_bool(buf, msg.enabled);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugLoggingResponse& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_bool(buf, msg.enabled);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugGetQueue&) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugFlush&) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const DebugQueueResponse& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string_vector(buf, msg.recipient_ids);
    write_string_vector(buf, msg.sender_ids);
    write_uint64_vector(buf, msg.timestamps);
    return buf;
}

// ============================================================================
// Registry Query Message Serialization Implementations
// ============================================================================

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const ListManagers&) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const ManagersResponse& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string_vector(buf, msg.manager_ids);
    write_string_vector(buf, msg.endpoints);
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const ListAllActors&) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    return buf;
}

template<>
inline std::vector<uint8_t> MessageSerializer::serialize(MsgType type, const AllActorsResponse& msg) {
    std::vector<uint8_t> buf;
    buf.push_back(static_cast<uint8_t>(type));
    write_string_vector(buf, msg.actor_names);
    write_string_vector(buf, msg.manager_ids);
    return buf;
}

// Debug message deserialization implementations

inline DebugStop MessageSerializer::deserialize_debug_stop(const uint8_t* data, size_t len) {
    DebugStop msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.reason = read_string(ptr, end);
    return msg;
}

inline DebugContinue MessageSerializer::deserialize_debug_continue(const uint8_t* data, size_t len) {
    (void)data; (void)len;
    return DebugContinue{};
}

inline DebugAddBreakpoint MessageSerializer::deserialize_debug_add_breakpoint(const uint8_t* data, size_t len) {
    DebugAddBreakpoint msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.pattern = read_string(ptr, end);
    return msg;
}

inline DebugRemoveBreakpoint MessageSerializer::deserialize_debug_remove_breakpoint(const uint8_t* data, size_t len) {
    DebugRemoveBreakpoint msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.breakpoint_id = read_uint32(ptr, end);
    return msg;
}

inline DebugListBreakpoints MessageSerializer::deserialize_debug_list_breakpoints(const uint8_t* data, size_t len) {
    (void)data; (void)len;
    return DebugListBreakpoints{};
}

inline DebugListGroups MessageSerializer::deserialize_debug_list_groups(const uint8_t* data, size_t len) {
    (void)data; (void)len;
    return DebugListGroups{};
}

inline DebugListActors MessageSerializer::deserialize_debug_list_actors(const uint8_t* data, size_t len) {
    DebugListActors msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.group_id = read_string(ptr, end);
    return msg;
}

inline DebugStatus MessageSerializer::deserialize_debug_status(const uint8_t* data, size_t len) {
    (void)data; (void)len;
    return DebugStatus{};
}

inline DebugSetLogging MessageSerializer::deserialize_debug_set_logging(const uint8_t* data, size_t len) {
    DebugSetLogging msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.enabled = read_bool(ptr, end);
    return msg;
}

inline DebugGetQueue MessageSerializer::deserialize_debug_get_queue(const uint8_t* data, size_t len) {
    (void)data; (void)len;
    return DebugGetQueue{};
}

inline DebugFlush MessageSerializer::deserialize_debug_flush(const uint8_t* data, size_t len) {
    (void)data; (void)len;
    return DebugFlush{};
}

// Debug response deserialization implementations

inline DebugBreakpointAdded MessageSerializer::deserialize_debug_breakpoint_added(const uint8_t* data, size_t len) {
    DebugBreakpointAdded msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.breakpoint_id = read_uint32(ptr, end);
    msg.pattern = read_string(ptr, end);
    return msg;
}

inline DebugBreakpointsResponse MessageSerializer::deserialize_debug_breakpoints_response(const uint8_t* data, size_t len) {
    DebugBreakpointsResponse msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.ids = read_uint32_vector(ptr, end);
    msg.patterns = read_string_vector(ptr, end);
    return msg;
}

inline DebugGroupsResponse MessageSerializer::deserialize_debug_groups_response(const uint8_t* data, size_t len) {
    DebugGroupsResponse msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.groups = read_string_vector(ptr, end);
    return msg;
}

inline DebugActorsResponse MessageSerializer::deserialize_debug_actors_response(const uint8_t* data, size_t len) {
    DebugActorsResponse msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.group_id = read_string(ptr, end);
    msg.actors = read_string_vector(ptr, end);
    return msg;
}

inline DebugStatusResponse MessageSerializer::deserialize_debug_status_response(const uint8_t* data, size_t len) {
    DebugStatusResponse msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.stopped = read_bool(ptr, end);
    msg.stopped_reason = read_string(ptr, end);
    msg.current_sequence = read_uint64(ptr, end);
    msg.queue_size = read_uint32(ptr, end);
    msg.active_actor = read_string(ptr, end);
    msg.logging_enabled = read_bool(ptr, end);
    return msg;
}

inline DebugLoggingResponse MessageSerializer::deserialize_debug_logging_response(const uint8_t* data, size_t len) {
    DebugLoggingResponse msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.enabled = read_bool(ptr, end);
    return msg;
}

inline DebugQueueResponse MessageSerializer::deserialize_debug_queue_response(const uint8_t* data, size_t len) {
    DebugQueueResponse msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.recipient_ids = read_string_vector(ptr, end);
    msg.sender_ids = read_string_vector(ptr, end);
    msg.timestamps = read_uint64_vector(ptr, end);
    return msg;
}

// ============================================================================
// Registry Query Message Deserialization Implementations
// ============================================================================

inline ListManagers MessageSerializer::deserialize_list_managers(const uint8_t* data, size_t len) {
    (void)data; (void)len;
    return ListManagers{};
}

inline ManagersResponse MessageSerializer::deserialize_managers_response(const uint8_t* data, size_t len) {
    ManagersResponse msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.manager_ids = read_string_vector(ptr, end);
    msg.endpoints = read_string_vector(ptr, end);
    return msg;
}

inline ListAllActors MessageSerializer::deserialize_list_all_actors(const uint8_t* data, size_t len) {
    (void)data; (void)len;
    return ListAllActors{};
}

inline AllActorsResponse MessageSerializer::deserialize_all_actors_response(const uint8_t* data, size_t len) {
    AllActorsResponse msg;
    if (len < 1) return msg;
    const uint8_t* ptr = data + 1;
    const uint8_t* end = data + len;
    msg.actor_names = read_string_vector(ptr, end);
    msg.manager_ids = read_string_vector(ptr, end);
    return msg;
}

} // namespace actors::coordination
