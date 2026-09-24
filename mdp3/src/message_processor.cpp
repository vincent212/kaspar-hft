
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "mdp3/act/MessageProcessor.hpp"

actor_ptr create_MessageProcessor(
    const std::string &_chan_nam,
    actors::Actor *_recovery_processor,
    actor_ptr _decoder,
    bool _dorecovery,
    bool _recoveryonstart,
    actor_ptr _decoder_shadow)
{
    return new mdp3::MessageProcessor(
        _chan_nam,
        _recovery_processor,
        _decoder,
        _dorecovery,
        _recoveryonstart,
        _decoder_shadow);
}