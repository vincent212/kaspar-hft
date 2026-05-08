#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "CoordinatorMessages.hpp"
#include <memory>

namespace actors::coordination {

/**
 * MonitorActor - Periodic tasks and timer
 *
 * Responsibilities:
 * - Generate periodic TimerTick messages for CoordinatorActor
 * - Monitor system health (optional future extension)
 *
 * This actor runs on a timer and sends periodic ticks to drive
 * the coordinator's event loop (stale check, queue processing, etc.)
 */
class MonitorActor : public Actor {
public:
    /**
     * Constructor
     * @param coordinator_ref Reference to CoordinatorActor
     * @param tick_interval_ms Interval between timer ticks in milliseconds
     */
    MonitorActor(Actor* coordinator_ref, uint64_t tick_interval_ms = 100);

    ~MonitorActor() override = default;

protected:
    void init() override;
    void end() override;
    void process_message(const Message* m) override;

private:
    Actor* coordinator_ref_;
    uint64_t tick_interval_ms_;
    bool running_ = true;
};

} // namespace actors::coordination
