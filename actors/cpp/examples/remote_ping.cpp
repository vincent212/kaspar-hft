/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <iostream>
#include <memory>
#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"
#include "actors/act/Manager.hpp"
#include "actors/msg/Start.hpp"
#include "actors/remote/Serialization.hpp"
#include "actors/remote/ZmqSender.hpp"
#include "actors/remote/ZmqReceiver.hpp"

using namespace actors;
using namespace std;

// Define Ping message (ID=100)
class Ping : public Message_N<100> {
public:
    int count;
    Ping(int c = 0) : count(c) {}
};

// Define Pong message (ID=101)
class Pong : public Message_N<101> {
public:
    int count;
    Pong(int c = 0) : count(c) {}
};

// Register messages for remote serialization - just one line each!
REGISTER_REMOTE_MESSAGE_1(Ping, count, int)
REGISTER_REMOTE_MESSAGE_1(Pong, count, int)

// Forward declaration
class PingManager;

/**
 * PingActor - Sends Ping to remote pong, receives Pong back
 */
class PingActor : public Actor {
    ActorRef pong_ref_;
    Manager* manager_;

public:
    PingActor(ActorRef pong_ref, Manager* manager)
        : pong_ref_(std::move(pong_ref))
        , manager_(manager) {
        strncpy(name, "ping", sizeof(name));
        MESSAGE_HANDLER(msg::Start, on_start);
        MESSAGE_HANDLER(Pong, on_pong);
    }

private:
    void on_start(const msg::Start*) noexcept {
        cout << "PingActor: Starting ping-pong with remote pong" << endl;
        pong_ref_.send(new Ping(1), this);
    }

    void on_pong(const Pong* msg) noexcept {
        cout << "PingActor: Received pong " << msg->count << " from remote" << endl;

        if (msg->count >= 5) {
            cout << "PingActor: Done!" << endl;
            manager_->terminate();
        } else {
            pong_ref_.send(new Ping(msg->count + 1), this);
        }
    }
};

/**
 * PingManager - Sets up the ping actor and ZMQ components
 */
class PingManager : public Manager {
    shared_ptr<ZmqSender> zmq_sender_;

public:
    PingManager() {
        const string local_endpoint = "tcp://0.0.0.0:5002";
        const string remote_pong_endpoint = "tcp://localhost:5001";

        // Create ZMQ sender (now an Actor - must be managed!)
        zmq_sender_ = make_shared<ZmqSender>("tcp://localhost:5002");
        manage(zmq_sender_.get());

        // Create remote ref to pong on other process
        ActorRef remote_pong = zmq_sender_->remote_ref("pong", remote_pong_endpoint);

        // Create ping actor
        auto* ping_actor = new PingActor(std::move(remote_pong), this);
        manage(ping_actor);

        // Create ZMQ receiver for incoming Pong messages
        auto* zmq_receiver = new ZmqReceiver(local_endpoint, zmq_sender_);
        zmq_receiver->register_actor("ping", ping_actor);
        manage(zmq_receiver);
    }
};

int main() {
    cout << "=== C++ Ping Process (port 5002) ===" << endl;

    PingManager mgr;
    mgr.init();

    cout << "Ping process starting..." << endl;

    mgr.end();

    cout << "=== C++ Ping Process Complete ===" << endl;
    return 0;
}
