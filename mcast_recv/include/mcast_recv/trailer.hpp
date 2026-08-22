#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <cstring>
#include <arpa/inet.h> // ntohl

namespace mcast_recv
{
    // Vendor/layout of a hardware-timestamp trailer that some capture devices
    // append to the Ethernet frame *after* the UDP payload. This is NOT part of
    // the IP/UDP datagram (it lives beyond ip_total_len), so it only survives an
    // L2 capture (pcap) -- a normal UDP socket never sees it.
    enum class TrailerType : uint8_t
    {
        None = 0, // no trailer present
        Metamako  // Metamako / Arista MetaWatch 20-byte trailer
    };

    // Describes where the hardware timestamp lives inside a trailer. Kept as data
    // (not hard-coded in the reader) so other vendors/layouts can be added and so
    // a reader can be configured per capture source.
    struct TrailerSpec
    {
        TrailerType type = TrailerType::None;
        uint16_t size = 0;           // total trailer length in bytes (Metamako: 20)
        uint16_t seconds_offset = 0; // offset of the big-endian uint32 seconds field
        uint16_t nanos_offset = 0;   // offset of the big-endian uint32 nanoseconds field

        bool present() const noexcept { return type != TrailerType::None; }

        // Canonical Metamako layout: 20-byte trailer, big-endian seconds at
        // offset 8 and big-endian nanoseconds at offset 12.
        static constexpr TrailerSpec metamako() noexcept
        {
            return TrailerSpec{TrailerType::Metamako, 20, 8, 12};
        }
    };

    // Parse the hardware timestamp (ns since the Unix epoch) from a trailer whose
    // first byte is at `trailer`. Caller guarantees >= spec.size readable bytes.
    // Fields are big-endian (network order) per the Metamako format.
    inline uint64_t trailer_timestamp_ns(const uint8_t *trailer, const TrailerSpec &spec) noexcept
    {
        uint32_t secs_be = 0;
        uint32_t nanos_be = 0;
        std::memcpy(&secs_be, trailer + spec.seconds_offset, sizeof(secs_be));
        std::memcpy(&nanos_be, trailer + spec.nanos_offset, sizeof(nanos_be));
        const uint64_t secs = ntohl(secs_be);
        const uint64_t nanos = ntohl(nanos_be);
        return secs * 1000000000ULL + nanos;
    }

    // Cheap sanity check that a *configured* trailer actually matches the wire,
    // so a mis-configuration is caught loudly instead of silently corrupting
    // data. Two conditions:
    //   1. the captured frame is exactly `frame_len_without_trailer + spec.size`
    //      bytes (i.e. there really are spec.size trailing bytes past the IP
    //      payload), and
    //   2. the trailer seconds are within `tolerance_s` of a reference clock
    //      (e.g. the pcap record-header seconds).
    inline bool trailer_looks_valid(
        uint32_t caplen,
        uint32_t frame_len_without_trailer,
        uint64_t trailer_secs,
        uint64_t reference_secs,
        const TrailerSpec &spec,
        uint64_t tolerance_s = 5) noexcept
    {
        if (!spec.present())
            return false;
        if (caplen != frame_len_without_trailer + spec.size)
            return false;
        const uint64_t diff =
            trailer_secs > reference_secs ? trailer_secs - reference_secs : reference_secs - trailer_secs;
        return diff <= tolerance_s;
    }
}
