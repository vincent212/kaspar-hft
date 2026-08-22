/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Unit tests for the hardware-timestamp trailer parser (mcast_recv/trailer.hpp).
 * The "real capture" cases use bytes taken verbatim from the CME channel-310
 * (ES) Metamako captures, so they pin the exact wire layout and endianness.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include "mcast_recv/trailer.hpp"

using mcast_recv::TrailerSpec;
using mcast_recv::TrailerType;
using mcast_recv::trailer_looks_valid;
using mcast_recv::trailer_timestamp_ns;

// Metamako trailer bytes from the first packet of the side-A (port 14310)
// capture: secs (BE u32) @ offset 8 = 0x5d6806dc = 1567098588,
//          nanos (BE u32) @ offset 12 = 0x13401888 = 322967688.
static const uint8_t kRealTrailer[20] = {0xc4, 0x8b, 0xae, 0xf4, 0x00, 0xdf, 0x1e, 0x20, 0x5d, 0x68,
                                         0x06, 0xdc, 0x13, 0x40, 0x18, 0x88, 0x03, 0x00, 0x28, 0x11};

TEST(TrailerSpec, MetamakoDefaults)
{
    const TrailerSpec spec = TrailerSpec::metamako();
    EXPECT_EQ(spec.type, TrailerType::Metamako);
    EXPECT_TRUE(spec.present());
    EXPECT_EQ(spec.size, 20u);
    EXPECT_EQ(spec.seconds_offset, 8u);
    EXPECT_EQ(spec.nanos_offset, 12u);
}

TEST(TrailerSpec, DefaultIsNone)
{
    const TrailerSpec spec;
    EXPECT_EQ(spec.type, TrailerType::None);
    EXPECT_FALSE(spec.present());
    EXPECT_EQ(spec.size, 0u);
}

TEST(TrailerTimestamp, ParsesRealMetamakoCapture)
{
    const uint64_t ts = trailer_timestamp_ns(kRealTrailer, TrailerSpec::metamako());
    EXPECT_EQ(ts, 1567098588ULL * 1000000000ULL + 322967688ULL);
    EXPECT_EQ(ts, 1567098588322967688ULL);
}

TEST(TrailerTimestamp, BigEndianFieldsCraftedValue)
{
    // secs = 0x00000002 = 2, nanos = 0x00000003 = 3  (both big-endian)
    uint8_t t[20] = {};
    t[8] = 0x00; t[9] = 0x00; t[10] = 0x00; t[11] = 0x02;  // seconds
    t[12] = 0x00; t[13] = 0x00; t[14] = 0x00; t[15] = 0x03; // nanos
    EXPECT_EQ(trailer_timestamp_ns(t, TrailerSpec::metamako()), 2ULL * 1000000000ULL + 3ULL);
}

TEST(TrailerTimestamp, NanosNearOneSecondBoundary)
{
    // nanos = 999999999 = 0x3B9AC9FF, secs = 1
    uint8_t t[20] = {};
    t[11] = 0x01;                                                   // secs = 1
    t[12] = 0x3B; t[13] = 0x9A; t[14] = 0xC9; t[15] = 0xFF;         // nanos = 999999999
    EXPECT_EQ(trailer_timestamp_ns(t, TrailerSpec::metamako()), 1999999999ULL);
}

TEST(TrailerValidate, AcceptsWellFormedFrame)
{
    const TrailerSpec spec = TrailerSpec::metamako();
    // caplen == eth+ip_total (142) + trailer (20) == 162; trailer secs == pcap secs.
    EXPECT_TRUE(trailer_looks_valid(162, 142, 1567098588ULL, 1567098588ULL, spec));
    // within tolerance
    EXPECT_TRUE(trailer_looks_valid(162, 142, 1567098588ULL, 1567098590ULL, spec));
}

TEST(TrailerValidate, RejectsWrongFrameLength)
{
    const TrailerSpec spec = TrailerSpec::metamako();
    // no 20 trailing bytes present -> caplen would be 142, not 162
    EXPECT_FALSE(trailer_looks_valid(142, 142, 1567098588ULL, 1567098588ULL, spec));
}

TEST(TrailerValidate, RejectsImplausibleClock)
{
    const TrailerSpec spec = TrailerSpec::metamako();
    // trailer seconds far from the reference (software) clock -> misconfigured/garbage
    EXPECT_FALSE(trailer_looks_valid(162, 142, 1000000000ULL, 1567098588ULL, spec));
}

TEST(TrailerValidate, NoneSpecNeverValid)
{
    const TrailerSpec spec; // None
    EXPECT_FALSE(trailer_looks_valid(162, 142, 1567098588ULL, 1567098588ULL, spec));
}
