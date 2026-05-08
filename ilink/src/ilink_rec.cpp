/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "ilink/act/ILinkRec.hpp"

actor_ptr create_ILinkRec(
    en::x _exch,
    int _sock,  
    void*_cbif)
{
    return new ilink::ILinkReceiver(
        _exch, _sock, (m2::ilink::CBIF*)_cbif);
}