#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <set>
#include <netinet/in.h>
#include "actors/Actor.hpp"
#include "mdp3/mbo_if.hpp"

#include "mdp3/if/MessageProcessor.hpp"
#include "mcast_recv/if/MsgBuf.hpp"
#include "mdp3/if/RecoveryProcessor.hpp"
#include "mcast_recv/if/SocketReader.hpp"
#include "mcast_recv/if/PCAPReader.hpp"

#include "mdp3/if/DataRecoveryRecorder.hpp"
#include "mdp3/if/InstrumentRecoveryRecorder.hpp"

/**
* @brief create mdp3 for full recovery etc
*/
static auto
create_all_mdp3(
    const std::string _chan_nam,
    mdp3::feed_handler_if *_cb,
    in_port_t _port_dr,
    in_port_t _port_ir,
    const char *_group_dr,
    const char *_group_ir,
    bool debug_recovery_processor,
    bool dorecovery,
    bool recovery_on_start,
    bool disable_mbo,
    uint32_t max_mbp_level,
    bool debug_decoder,
    int spinbuf,
    [[maybe_unused]] bool blocksock,
    in_port_t porta,
    in_port_t portb,
    const char *groupa,
    const char *groupb,
    const char *_interfacea,
    const char *_interfaceb,
    const char *_desc
    )
{

    std::cerr << "create_all_mdp3" << std::endl;
    std::cerr << "  _chan_nam: " << _chan_nam << std::endl;
    std::cerr << "  _port_dr: " << _port_dr << std::endl;
    std::cerr << "  _port_ir: " << _port_ir << std::endl;
    std::cerr << "  _group_dr: " << _group_dr << std::endl;
    std::cerr << "  _group_ir: " << _group_ir << std::endl;
    std::cerr << "  porta: " << porta << std::endl;
    std::cerr << "  portb: " << portb << std::endl;
    std::cerr << "  groupa: " << groupa << std::endl;
    std::cerr << "  groupb: " << groupb << std::endl;
    std::cerr << "  _interfacea: " << _interfacea << std::endl;
    std::cerr << "  _interfaceb: " << _interfaceb << std::endl;
    std::cerr << "  _desc: " << _desc << std::endl;

    auto recovery_processor = create_RecoveryProcessor(
        _chan_nam,
        _cb,
        _port_dr,
        _port_ir,
        _group_dr,
        _group_ir,
        _interfacea,
        debug_recovery_processor);

    auto message_processor = create_MessageProcessor(
        _chan_nam,
        recovery_processor,
        _cb,
        dorecovery,
        recovery_on_start,
        disable_mbo,
        max_mbp_level,
        debug_decoder);

    auto msg_buf_a = create_MsgBuf_32(
        _chan_nam,
        spinbuf,
        'A',
        message_processor);

    auto socket_processor_a =
        create_SocketReader_32_0(
            _chan_nam,
            true, // use new loop
            'A',
            msg_buf_a,
            porta,
            groupa,
            _interfacea,
            _desc);

    auto socket_processor_b =
        create_SocketReader_32_0(
            _chan_nam,
            true, // use new loop
            'B',
            msg_buf_a,
            portb,
            groupb,
            _interfaceb,
            _desc
            );

    return std::vector<cfsmp>{
        recovery_processor,  // 0
        message_processor,   // 1
        msg_buf_a,           // 2
        nullptr,           // 3 removed msg_buf_b
        socket_processor_a,  // 4
        socket_processor_b}; // 5
}

/**
* @brief create mdp3 for PCAP file playback
*/
static auto
create_all_mdp3_pcap(
    const std::string &_chan_nam,
    mdp3::feed_handler_if *_cb,
    const std::string &_pcap_file_a,
    const std::string &_pcap_file_b,
    bool disable_mbo,
    uint32_t max_mbp_level,
    bool debug_decoder,
    int spinbuf
    )
{
    std::cerr << "create_all_mdp3_pcap" << std::endl;
    std::cerr << "  _chan_nam: " << _chan_nam << std::endl;
    std::cerr << "  _pcap_file_a: " << _pcap_file_a << std::endl;
    std::cerr << "  _pcap_file_b: " << _pcap_file_b << std::endl;

    // Recovery not supported in PCAP mode
    auto recovery_processor = nullptr;

    auto message_processor = create_MessageProcessor(
        _chan_nam,
        recovery_processor,
        _cb,
        false,  // no recovery in PCAP mode
        false,  // no recovery on start
        disable_mbo,
        max_mbp_level,
        debug_decoder);

    auto msg_buf_a = create_MsgBuf_32(
        _chan_nam,
        spinbuf,
        'A',
        message_processor);

    actor_ptr pcap_reader_a = nullptr;
    actor_ptr pcap_reader_b = nullptr;

    if (!_pcap_file_a.empty()) {
        pcap_reader_a = create_PCAPReader_32_0(
            _chan_nam,
            msg_buf_a,
            _pcap_file_a,
            false);  // big_endian
    }

    if (!_pcap_file_b.empty()) {
        pcap_reader_b = create_PCAPReader_32_0(
            _chan_nam,
            msg_buf_a,
            _pcap_file_b,
            false);  // big_endian
    }

    return std::vector<cfsmp>{
        recovery_processor,  // 0 - nullptr in PCAP mode
        message_processor,   // 1
        msg_buf_a,           // 2
        nullptr,             // 3 removed msg_buf_b
        pcap_reader_a,       // 4
        pcap_reader_b};      // 5
}
