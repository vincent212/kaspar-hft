
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "mq0/act/MQ0_server.hpp"

// Factory function
actor_ptr create_MQ0_server(
    const std::string &name,
    int port,
    actor_ptr console)
{
  return new mq0::act::MQ0_server(name, port, console);
}
