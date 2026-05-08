/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <iostream>
#include <memory>
#include <csignal>
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

/**
 * PongActor - Receives Ping, sends Pong back
 */
class PongActor : public Actor {
public:
    PongActor() {
        strncpy(name, "pong", sizeof(name));
        MESSAGE_HANDLER(msg::Start, on_start);
        MESSAGE_HANDLER(Ping, on_ping);
    }

private:
    void on_start(const msg::Start*) noexcept {
        cout << "PongActor: Ready to receive pings..." << endl;
    }

    void on_ping(const Ping* msg) noexcept {
        cout << "PongActor: Received ping " << msg->count << " from remote" << endl;
        // Reply with Pong
        reply(new Pong(msg->count));
    }
};

// Global manager pointer for signal handler
static Manager* g_manager = nullptr;

void signal_handler(int) {
    if (g_manager) {
        g_manager->terminate();
    }
}

/**
 * PongManager - Sets up the pong actor and ZMQ receiver
 */
class PongManager : public Manager {
    shared_ptr<ZmqSender> zmq_sender_;

public:
    PongManager() {
        const string endpoint = "tcp://0.0.0.0:5001";

        // Create ZMQ sender for replies (now an Actor - must be managed!)
        zmq_sender_ = make_shared<ZmqSender>("tcp://localhost:5001");
        manage(zmq_sender_.get());

        // Create pong actor
        auto* pong_actor = new PongActor();
        manage(pong_actor);

        // Create ZMQ receiver and register local actors
        auto* zmq_receiver = new ZmqReceiver(endpoint, zmq_sender_);
        zmq_receiver->register_actor("pong", pong_actor);
        manage(zmq_receiver);
    }
};

int main() {
    cout << "=== C++ Pong Process (port 5001) ===" << endl;

    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    PongManager mgr;
    g_manager = &mgr;

    mgr.init();

    cout << "Pong process ready, waiting for pings..." << endl;
    cout << "Press Ctrl+C to stop" << endl;

    mgr.end();

    cout << "=== C++ Pong Process Complete ===" << endl;
    return 0;
}
