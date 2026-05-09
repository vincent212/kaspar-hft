#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>
#include "actors/Message.hpp"

namespace actors::serialization {

using json = nlohmann::json;

// Function types for serialize/deserialize
using SerializeFn = std::function<json(const Message*)>;
using DeserializeFn = std::function<Message*(const json&)>;

/**
 * Registry entry for a message type
 */
struct RegistryEntry {
    std::string type_name;
    SerializeFn serialize;
    DeserializeFn deserialize;
};

/**
 * Global message registry singleton
 */
class MessageRegistry {
public:
    static MessageRegistry& instance() {
        static MessageRegistry registry;
        return registry;
    }

    /**
     * Register a message type for remote serialization
     *
     * @param msg_id Message ID (from Message::msg_type())
     * @param type_name Wire format name (e.g., "Ping", "Pong")
     * @param serialize Function to serialize message to JSON
     * @param deserialize Function to deserialize JSON to message
     */
    void register_message(int msg_id,
                          const std::string& type_name,
                          SerializeFn serialize,
                          DeserializeFn deserialize) {
        std::lock_guard<std::mutex> lock(mutex_);
        RegistryEntry entry{type_name, std::move(serialize), std::move(deserialize)};
        id_to_entry_[msg_id] = entry;
        name_to_entry_[type_name] = std::move(entry);
    }

    /**
     * Get type name for a message ID
     */
    std::string get_type_name(int msg_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = id_to_entry_.find(msg_id);
        if (it != id_to_entry_.end()) {
            return it->second.type_name;
        }
        return "";
    }

    /**
     * Serialize a message to JSON
     */
    json serialize(const Message* msg) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = id_to_entry_.find(msg->get_message_id());
        if (it != id_to_entry_.end()) {
            return it->second.serialize(msg);
        }
        throw std::runtime_error("Message type not registered: " + std::to_string(msg->get_message_id()));
    }

    /**
     * Deserialize JSON to a message
     */
    Message* deserialize(const std::string& type_name, const json& data) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = name_to_entry_.find(type_name);
        if (it != name_to_entry_.end()) {
            return it->second.deserialize(data);
        }
        return nullptr;  // Unknown message type
    }

    /**
     * Check if a type name is registered
     */
    bool is_registered(const std::string& type_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return name_to_entry_.find(type_name) != name_to_entry_.end();
    }

private:
    MessageRegistry() = default;
    mutable std::mutex mutex_;
    std::unordered_map<int, RegistryEntry> id_to_entry_;
    std::unordered_map<std::string, RegistryEntry> name_to_entry_;
};

// Convenience functions
inline void register_message(int msg_id,
                             const std::string& type_name,
                             SerializeFn serialize,
                             DeserializeFn deserialize) {
    MessageRegistry::instance().register_message(msg_id, type_name,
                                                  std::move(serialize),
                                                  std::move(deserialize));
}

inline std::string get_type_name(int msg_id) {
    return MessageRegistry::instance().get_type_name(msg_id);
}

inline json serialize(const Message* msg) {
    return MessageRegistry::instance().serialize(msg);
}

inline Message* deserialize(const std::string& type_name, const json& data) {
    return MessageRegistry::instance().deserialize(type_name, data);
}

inline bool is_registered(const std::string& type_name) {
    return MessageRegistry::instance().is_registered(type_name);
}

/**
 * REGISTER_REMOTE_MESSAGE_1 - Register a message with 1 field
 *
 * Usage:
 *   class Ping : public Message_N<100> {
 *   public:
 *       int count;
 *       Ping(int c = 0) : count(c) {}
 *   };
 *
 *   REGISTER_REMOTE_MESSAGE_1(Ping, count, int)
 *
 * This expands to the full registration boilerplate automatically.
 */
#define REGISTER_REMOTE_MESSAGE_1(Type, field1, type1)                          \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message* m) -> nlohmann::json {                 \
                    const Type* msg = static_cast<const Type*>(m);               \
                    return nlohmann::json{{#field1, msg->field1}};               \
                },                                                               \
                [](const nlohmann::json& j) -> actors::Message* {                \
                    return new Type(j[#field1].get<type1>());                    \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

/**
 * REGISTER_REMOTE_MESSAGE_2 - Register a message with 2 fields
 */
#define REGISTER_REMOTE_MESSAGE_2(Type, field1, type1, field2, type2)           \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message* m) -> nlohmann::json {                 \
                    const Type* msg = static_cast<const Type*>(m);               \
                    return nlohmann::json{{#field1, msg->field1}, {#field2, msg->field2}}; \
                },                                                               \
                [](const nlohmann::json& j) -> actors::Message* {                \
                    return new Type(j[#field1].get<type1>(), j[#field2].get<type2>()); \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

/**
 * REGISTER_REMOTE_MESSAGE_3 - Register a message with 3 fields
 */
#define REGISTER_REMOTE_MESSAGE_3(Type, f1, t1, f2, t2, f3, t3)                 \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message* m) -> nlohmann::json {                 \
                    const Type* msg = static_cast<const Type*>(m);               \
                    return nlohmann::json{{#f1, msg->f1}, {#f2, msg->f2}, {#f3, msg->f3}}; \
                },                                                               \
                [](const nlohmann::json& j) -> actors::Message* {                \
                    return new Type(j[#f1].get<t1>(), j[#f2].get<t2>(), j[#f3].get<t3>()); \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

/**
 * REGISTER_REMOTE_MESSAGE_0 - Register a message with no fields
 */
#define REGISTER_REMOTE_MESSAGE_0(Type)                                         \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message*) -> nlohmann::json {                   \
                    return nlohmann::json::object();                             \
                },                                                               \
                [](const nlohmann::json&) -> actors::Message* {                  \
                    return new Type();                                           \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

/**
 * REGISTER_REMOTE_MESSAGE_4 through REGISTER_REMOTE_MESSAGE_10 for larger messages
 */
#define REGISTER_REMOTE_MESSAGE_4(Type, f1, t1, f2, t2, f3, t3, f4, t4)         \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message* m) -> nlohmann::json {                 \
                    const Type* msg = static_cast<const Type*>(m);               \
                    return nlohmann::json{{#f1, msg->f1}, {#f2, msg->f2}, {#f3, msg->f3}, {#f4, msg->f4}}; \
                },                                                               \
                [](const nlohmann::json& j) -> actors::Message* {                \
                    return new Type(j[#f1].get<t1>(), j[#f2].get<t2>(), j[#f3].get<t3>(), j[#f4].get<t4>()); \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

#define REGISTER_REMOTE_MESSAGE_5(Type, f1, t1, f2, t2, f3, t3, f4, t4, f5, t5) \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message* m) -> nlohmann::json {                 \
                    const Type* msg = static_cast<const Type*>(m);               \
                    return nlohmann::json{{#f1, msg->f1}, {#f2, msg->f2}, {#f3, msg->f3}, {#f4, msg->f4}, {#f5, msg->f5}}; \
                },                                                               \
                [](const nlohmann::json& j) -> actors::Message* {                \
                    return new Type(j[#f1].get<t1>(), j[#f2].get<t2>(), j[#f3].get<t3>(), j[#f4].get<t4>(), j[#f5].get<t5>()); \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

#define REGISTER_REMOTE_MESSAGE_6(Type, f1, t1, f2, t2, f3, t3, f4, t4, f5, t5, f6, t6) \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message* m) -> nlohmann::json {                 \
                    const Type* msg = static_cast<const Type*>(m);               \
                    return nlohmann::json{{#f1, msg->f1}, {#f2, msg->f2}, {#f3, msg->f3}, {#f4, msg->f4}, {#f5, msg->f5}, {#f6, msg->f6}}; \
                },                                                               \
                [](const nlohmann::json& j) -> actors::Message* {                \
                    return new Type(j[#f1].get<t1>(), j[#f2].get<t2>(), j[#f3].get<t3>(), j[#f4].get<t4>(), j[#f5].get<t5>(), j[#f6].get<t6>()); \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

#define REGISTER_REMOTE_MESSAGE_7(Type, f1, t1, f2, t2, f3, t3, f4, t4, f5, t5, f6, t6, f7, t7) \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message* m) -> nlohmann::json {                 \
                    const Type* msg = static_cast<const Type*>(m);               \
                    return nlohmann::json{{#f1, msg->f1}, {#f2, msg->f2}, {#f3, msg->f3}, {#f4, msg->f4}, {#f5, msg->f5}, {#f6, msg->f6}, {#f7, msg->f7}}; \
                },                                                               \
                [](const nlohmann::json& j) -> actors::Message* {                \
                    return new Type(j[#f1].get<t1>(), j[#f2].get<t2>(), j[#f3].get<t3>(), j[#f4].get<t4>(), j[#f5].get<t5>(), j[#f6].get<t6>(), j[#f7].get<t7>()); \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

#define REGISTER_REMOTE_MESSAGE_8(Type, f1, t1, f2, t2, f3, t3, f4, t4, f5, t5, f6, t6, f7, t7, f8, t8) \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message* m) -> nlohmann::json {                 \
                    const Type* msg = static_cast<const Type*>(m);               \
                    return nlohmann::json{{#f1, msg->f1}, {#f2, msg->f2}, {#f3, msg->f3}, {#f4, msg->f4}, {#f5, msg->f5}, {#f6, msg->f6}, {#f7, msg->f7}, {#f8, msg->f8}}; \
                },                                                               \
                [](const nlohmann::json& j) -> actors::Message* {                \
                    return new Type(j[#f1].get<t1>(), j[#f2].get<t2>(), j[#f3].get<t3>(), j[#f4].get<t4>(), j[#f5].get<t5>(), j[#f6].get<t6>(), j[#f7].get<t7>(), j[#f8].get<t8>()); \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

#define REGISTER_REMOTE_MESSAGE_9(Type, f1, t1, f2, t2, f3, t3, f4, t4, f5, t5, f6, t6, f7, t7, f8, t8, f9, t9) \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message* m) -> nlohmann::json {                 \
                    const Type* msg = static_cast<const Type*>(m);               \
                    return nlohmann::json{{#f1, msg->f1}, {#f2, msg->f2}, {#f3, msg->f3}, {#f4, msg->f4}, {#f5, msg->f5}, {#f6, msg->f6}, {#f7, msg->f7}, {#f8, msg->f8}, {#f9, msg->f9}}; \
                },                                                               \
                [](const nlohmann::json& j) -> actors::Message* {                \
                    return new Type(j[#f1].get<t1>(), j[#f2].get<t2>(), j[#f3].get<t3>(), j[#f4].get<t4>(), j[#f5].get<t5>(), j[#f6].get<t6>(), j[#f7].get<t7>(), j[#f8].get<t8>(), j[#f9].get<t9>()); \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

#define REGISTER_REMOTE_MESSAGE_10(Type, f1, t1, f2, t2, f3, t3, f4, t4, f5, t5, f6, t6, f7, t7, f8, t8, f9, t9, f10, t10) \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message* m) -> nlohmann::json {                 \
                    const Type* msg = static_cast<const Type*>(m);               \
                    return nlohmann::json{{#f1, msg->f1}, {#f2, msg->f2}, {#f3, msg->f3}, {#f4, msg->f4}, {#f5, msg->f5}, {#f6, msg->f6}, {#f7, msg->f7}, {#f8, msg->f8}, {#f9, msg->f9}, {#f10, msg->f10}}; \
                },                                                               \
                [](const nlohmann::json& j) -> actors::Message* {                \
                    return new Type(j[#f1].get<t1>(), j[#f2].get<t2>(), j[#f3].get<t3>(), j[#f4].get<t4>(), j[#f5].get<t5>(), j[#f6].get<t6>(), j[#f7].get<t7>(), j[#f8].get<t8>(), j[#f9].get<t9>(), j[#f10].get<t10>()); \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

/**
 * REGISTER_REMOTE_MESSAGE - Legacy macro for custom serialize/deserialize
 *
 * For complex messages where the simple macros don't work, use this:
 *
 *   REGISTER_REMOTE_MESSAGE(MyMsg,
 *       { return json{{"data", msg->data}}; },
 *       { return new MyMsg(j["data"].get<std::string>()); }
 *   )
 */
#define REGISTER_REMOTE_MESSAGE(Type, serialize_body, deserialize_body)         \
    namespace {                                                                  \
        static bool Type##_registered_ = []() {                                  \
            actors::serialization::register_message(Type().get_message_id(), #Type, \
                [](const actors::Message* m) -> nlohmann::json {                 \
                    const Type* msg = static_cast<const Type*>(m);               \
                    (void)msg;                                                   \
                    serialize_body                                               \
                },                                                               \
                [](const nlohmann::json& j) -> actors::Message* {                \
                    (void)j;                                                     \
                    deserialize_body                                             \
                });                                                              \
            return true;                                                         \
        }();                                                                     \
    }

} // namespace actors::serialization
