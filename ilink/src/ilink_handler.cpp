/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "ilink/act/ILinkHandler.hpp"

actor_ptr create_ILinkHandler(
    actor_ptr db,
    en::x _exch,
    const std::string &_FirmID,
    const std::string &_SessionID,
    const std::string &_OperatorID,
    uint64_t _KeepAliveInterval,
    int _sock,
    void *_snd,
    bool _preregister, 
    bool _primary)
{
    return new ilink::ILinkHandler(
        db,
        _exch,
        _FirmID,
        _SessionID,
        _OperatorID,
        _KeepAliveInterval,
        _sock, (m2::ilink::ILinkSnd *)_snd, _preregister, _primary);
}

actor_ptr set_rec_ILinkHandler(
    actor_ptr _handler, 
    actor_ptr _rec)
{
    ((ilink::ILinkHandler *)_handler)->set_rec(_rec);
    return _handler;
}
