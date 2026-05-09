<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Registry Query Commands

This document describes the new query functionality added to `RegistryClient` for programmatic access to registry information.

## Overview

Previously, the only way to query all managers and actors was through the `RegistryQueryActor` accessible via the MQ0 monitoring interface. Now, `RegistryClient` provides direct query methods that can be used programmatically from any actor or manager.

## New Message Types

Added to `actors/coordination/messages.hpp`:

```cpp
enum class MsgType : uint8_t {
    // ... existing types ...

    // Registry query messages (58-61)
    LIST_MANAGERS = 58,         // Request list of all managers
    MANAGERS_RESPONSE = 59,     // Response with manager list
    LIST_ALL_ACTORS = 60,       // Request list of all actors
    ALL_ACTORS_RESPONSE = 61    // Response with all actors
};
```

## New Message Structs

### Request Messages

```cpp
/**
 * Request list of all registered managers.
 */
struct ListManagers {
    ListManagers() = default;
};

/**
 * Request list of all registered actors.
 */
struct ListAllActors {
    ListAllActors() = default;
};
```

### Response Messages

```cpp
/**
 * Response with list of all managers.
 */
struct ManagersResponse {
    std::vector<std::string> manager_ids;  // All manager IDs
    std::vector<std::string> endpoints;     // Corresponding endpoints

    ManagersResponse() = default;
};

/**
 * Response with list of all actors.
 */
struct AllActorsResponse {
    std::vector<std::string> actor_names;   // Actor names (not qualified)
    std::vector<std::string> manager_ids;   // Manager owning each actor

    AllActorsResponse() = default;
};
```

## RegistryClient API

### list_managers()

Query all registered managers.

```cpp
ManagersResponse list_managers();
```

**Returns:** `ManagersResponse` containing:
- `manager_ids` - Vector of all registered manager IDs
- `endpoints` - Vector of corresponding ZMQ endpoints (same order)

**Throws:**
- `RegistryError` - If not connected to registry
- `TimeoutError` - If no response received within timeout

**Example:**
```cpp
RegistryClient client("tcp://localhost:11100");
client.connect();

auto response = client.list_managers();
for (size_t i = 0; i < response.manager_ids.size(); ++i) {
    std::cout << "Manager: " << response.manager_ids[i]
              << " @ " << response.endpoints[i] << std::endl;
}
```

### list_all_actors()

Query all registered actors across all managers.

```cpp
AllActorsResponse list_all_actors();
```

**Returns:** `AllActorsResponse` containing:
- `actor_names` - Vector of all actor names (not qualified with manager)
- `manager_ids` - Vector of manager IDs (same order, shows which manager owns each actor)

**Throws:**
- `RegistryError` - If not connected to registry
- `TimeoutError` - If no response received within timeout

**Example:**
```cpp
RegistryClient client("tcp://localhost:11100");
client.connect();

auto response = client.list_all_actors();
for (size_t i = 0; i < response.actor_names.size(); ++i) {
    std::cout << "Actor: " << response.actor_names[i]
              << " (owned by " << response.manager_ids[i] << ")" << std::endl;
}
```

## Implementation Details

### Protocol Flow

1. **Client → Registry:** Send `LIST_MANAGERS` or `LIST_ALL_ACTORS` message
2. **Registry → Client:** Send `MANAGERS_RESPONSE` or `ALL_ACTORS_RESPONSE` with data
3. **Client:** Parse response and return structured data

### RegistryActor Handlers

Two new handlers were added to `RegistryActor`:

```cpp
void handle_list_managers(const std::string& identity, const ListManagers& msg);
void handle_list_all_actors(const std::string& identity, const ListAllActors& msg);
```

These handlers:
- Iterate through the `managers_` map
- Build response with all manager/actor information
- Send response back to requesting client

## Testing

A test program is provided at `actors/cpp/test_registry_queries.cpp`.

### Running the Test

```bash
# Start the registry
cd /home/vm/m2_kaspr/actors/cpp
./global_registry --port 11100 --monitor-port 11102 --debug &

# Run the test
./test_registry_queries tcp://localhost:11100
```

### Expected Output

```
Testing RegistryClient Query Methods
================================================================================
Registry address: tcp://localhost:11100
================================================================================
Connected to registry

1. Registering test manager 'TestManager'...
   ✓ Registered TestManager with 3 actors

2. Testing list_managers()...
================================================================================
   Found 1 manager(s):

   Manager ID                    Endpoint
   ----------------------------------------------------------------------
   TestManager                   tcp://localhost:9999

3. Testing list_all_actors()...
================================================================================
   Found 3 actor(s):

   Actor Name                    Manager
   ----------------------------------------------------------------------
   ActorA                        TestManager
   ActorB                        TestManager
   ActorC                        TestManager

4. Unregistering test manager...
   ✓ Unregistered TestManager

5. Verifying cleanup (should show fewer/no results)...
   Managers after cleanup: 0
   Actors after cleanup: 0
================================================================================
✓ All tests completed successfully!
================================================================================
```

## Comparison with MQ0 Query Interface

| Feature | RegistryClient Queries | RegistryQueryActor (MQ0) |
|---------|------------------------|---------------------------|
| **Access Method** | Programmatic (C++ API) | Text-based (via ZMQ REQ socket) |
| **Return Format** | Structured C++ objects | JSON strings |
| **Use Case** | Internal actor communication | Human inspection, debugging |
| **Connection** | Main registry endpoint (11100) | Monitoring endpoint (11102) |
| **Manager List** | `list_managers()` → `ManagersResponse` | Text: "managers" → JSON |
| **Actor List** | `list_all_actors()` → `AllActorsResponse` | Text: "actors" → JSON |

Both interfaces query the same underlying `RegistryActor` data, but serve different purposes:
- **RegistryClient queries** - For actors/managers that need programmatic access
- **MQ0 interface** - For humans debugging via command line

## Files Modified

### Protocol Definition
- `actors/cpp/include/actors/coordination/messages.hpp`
  - Added message types: `LIST_MANAGERS`, `MANAGERS_RESPONSE`, `LIST_ALL_ACTORS`, `ALL_ACTORS_RESPONSE`
  - Added message structs: `ListManagers`, `ManagersResponse`, `ListAllActors`, `AllActorsResponse`
  - Added serialization/deserialization implementations

### RegistryClient
- `actors/cpp/include/actors/registry/RegistryClient.hpp`
  - Added `list_managers()` method
  - Added `list_all_actors()` method

- `actors/cpp/src/registry/RegistryClient.cpp`
  - Implemented `list_managers()`
  - Implemented `list_all_actors()`

### RegistryActor
- `actors/cpp/include/actors/registry/RegistryActor.hpp`
  - Added `handle_list_managers()` declaration
  - Added `handle_list_all_actors()` declaration

- `actors/cpp/src/registry/RegistryActor.cpp`
  - Added case handlers in `on_incoming_zmq()` for new message types
  - Implemented `handle_list_managers()`
  - Implemented `handle_list_all_actors()`

### Test Program
- `actors/cpp/test_registry_queries.cpp` (NEW)
  - Comprehensive test of new query functionality

## Usage Examples

### Example 1: Print All Registered Managers

```cpp
#include "actors/registry/RegistryClient.hpp"
#include <iostream>

using namespace actors::registry;

int main() {
    RegistryClient client("tcp://localhost:11100");
    client.connect();

    try {
        auto managers = client.list_managers();
        std::cout << "Registered Managers:" << std::endl;
        for (size_t i = 0; i < managers.manager_ids.size(); ++i) {
            std::cout << "  " << managers.manager_ids[i]
                      << " @ " << managers.endpoints[i] << std::endl;
        }
    } catch (const RegistryError& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    client.disconnect();
    return 0;
}
```

### Example 2: Count Actors Per Manager

```cpp
#include "actors/registry/RegistryClient.hpp"
#include <iostream>
#include <map>

using namespace actors::registry;

int main() {
    RegistryClient client("tcp://localhost:11100");
    client.connect();

    try {
        auto actors = client.list_all_actors();

        // Count actors per manager
        std::map<std::string, int> counts;
        for (const auto& mgr : actors.manager_ids) {
            counts[mgr]++;
        }

        std::cout << "Actors per Manager:" << std::endl;
        for (const auto& [mgr, count] : counts) {
            std::cout << "  " << mgr << ": " << count << " actors" << std::endl;
        }
    } catch (const RegistryError& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    client.disconnect();
    return 0;
}
```

### Example 3: Monitor Registry Growth

```cpp
#include "actors/registry/RegistryClient.hpp"
#include <iostream>
#include <chrono>
#include <thread>

using namespace actors::registry;
using namespace std::chrono_literals;

int main() {
    RegistryClient client("tcp://localhost:11100");
    client.connect();

    for (int i = 0; i < 10; ++i) {
        auto managers = client.list_managers();
        auto actors = client.list_all_actors();

        std::cout << "[" << i << "] "
                  << managers.manager_ids.size() << " managers, "
                  << actors.actor_names.size() << " actors" << std::endl;

        std::this_thread::sleep_for(5s);
    }

    client.disconnect();
    return 0;
}
```

## Notes

- Both query methods use the existing `send_and_receive()` template with a default 5-second timeout
- Queries are synchronous - they block until the response is received
- The registry must be running and accessible at the specified address
- Empty responses (0 managers/actors) are valid and indicate no registrations exist
