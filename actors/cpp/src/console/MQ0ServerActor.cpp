/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/console/MQ0ServerActor.hpp"
#include <unistd.h>
#include <ctime>
#include <iostream>
#include <sstream>

namespace frame {
namespace cons {

MQ0ServerActor::MQ0ServerActor(actors::ActorRef console_ref, int port)
    : console_ref_(console_ref)
    , port_(port)
{
    MESSAGE_HANDLER(PollTimeout, on_poll_timeout);
}

void MQ0ServerActor::init() {
    std::cout << "MQ0ServerActor: Initializing on port " << port_ << std::endl;

    // Create ZMQ context and socket
    context_ = std::make_unique<zmq::context_t>(1);
    socket_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::rep);

    // Set socket options
    int rcvtimeo = 100;  // 100ms receive timeout (non-blocking)
    socket_->set(zmq::sockopt::rcvtimeo, rcvtimeo);

    int sndtimeo = 10000;  // 10s send timeout
    socket_->set(zmq::sockopt::sndtimeo, sndtimeo);

    int linger = 0;  // Don't wait on close
    socket_->set(zmq::sockopt::linger, linger);

    // Bind to port
    std::string bind_addr = "tcp://*:" + std::to_string(port_);
    socket_->bind(bind_addr);

    running_ = true;
    std::cout << "MQ0_server started on " << bind_addr << std::endl;

    // Start polling loop
    schedule_continue();
}

void MQ0ServerActor::end() {
    std::cout << "MQ0ServerActor: Shutting down" << std::endl;
    running_ = false;

    if (socket_) {
        socket_->close();
        socket_.reset();
    }

    if (context_) {
        context_->close();
        context_.reset();
    }

    std::cout << "MQ0_server shutdown complete" << std::endl;
}

void MQ0ServerActor::on_poll_timeout(const PollTimeout* m) noexcept {
    if (!running_ || !socket_) {
        return;
    }

    try {
        // Blocking receive with 100ms timeout (set in init())
        zmq::message_t request;
        auto recv_result = socket_->recv(request, zmq::recv_flags::none);

        if (recv_result) {
            std::string command(static_cast<char*>(request.data()), request.size());
            handle_command(command);
        }
    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN) {  // Ignore timeout errors
            std::cerr << "MQ0_server error receiving: " << e.what() << std::endl;
        }
    }

    // Schedule next poll (the 100ms timeout prevents spinning)
    schedule_continue();
}

void MQ0ServerActor::handle_command(const std::string& command) {
    // Trim whitespace
    std::string cmd = command;
    cmd.erase(0, cmd.find_first_not_of(" \t\n\r"));
    cmd.erase(cmd.find_last_not_of(" \t\n\r") + 1);

    std::string response;

    // Built-in commands
    if (cmd == "pid") {
        response = std::to_string(getpid());
    } else if (cmd == "time") {
        std::time_t t = std::time(nullptr);
        char buf[100];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&t));
        response = buf;
    } else if (cmd == "quit") {
        response = "Shutting down...";
        running_ = false;

        // Send shutdown to manager (if we have access)
        // Note: In full implementation, this would trigger Manager shutdown
        // For now, just stop the MQ0 server

    } else {
        // Forward to Console using fast_send for synchronous response
        try {
            msg::Cmd cmd_msg(cmd);
            auto reply_msg = console_ref_.fast_send(new msg::Cmd(cmd), this);

            if (reply_msg) {
                auto* cmdr = dynamic_cast<const msg::CmdR*>(reply_msg.get());
                if (cmdr) {
                    response = cmdr->res;
                } else {
                    response = "ERROR: Unexpected response type";
                }
            } else {
                response = "ERROR: No response from Console";
            }
        } catch (const std::exception& e) {
            response = std::string("ERROR: ") + e.what();
        }
    }

    // Send response
    zmq::message_t reply(response.size());
    memcpy(reply.data(), response.data(), response.size());
    socket_->send(reply, zmq::send_flags::none);
}

void MQ0ServerActor::schedule_continue() {
    if (running_) {
        // Send self-message to continue polling
        send(new PollTimeout(), this);
    }
}

} // namespace cons
} // namespace frame
