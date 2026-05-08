#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "enum/e_names.hpp"

cfsmp create_ILinkHandler(
    cfsmp db,
    en::x _x,
    const std::string &_FirmID,
    const std::string &_SessionID,
    const std::string &_OperatorID,
    uint64_t _KeepAliveInterval,
    int _sock, void *_snd, bool _preregister, bool _primary);

cfsmp set_rec_ILinkHandler(
    cfsmp _handler, cfsmp _rec);
