#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <iostream>
#include <optional>

#include "ilink_v8/NegotiationResponse501.h"

namespace m2::ilink::sockhelp
{

    constexpr auto SOFH_AND_SBE_HEADER_SIZE = sizeof(uint16_t) * 6;
    constexpr auto SOFH_HEADER_SIZE = sizeof(uint16_t) * 2;
    const ssize_t CRED_SZ = 2;

    /**
     * @brief Create a socket object
     *
     * @return auto
     */
    static auto create_socket() noexcept
    {
        int sock;
        if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        {
            std::cerr << "socket() failed" << std::endl;
            abort();
        }
        // Set TCP_NODELAY option
        int flag = 1;
        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) == -1)
        {
            std::cerr << "Could not set TCP_NODELAY\n";
            close(sock);
            abort();
        }
        return sock;
    }

    /**
     * @brief Connect to the socket
     *
     */
    static void connect_socket(int sock, int port, std::string &host) noexcept
    {
        struct sockaddr_in echoServAddr;
        memset(&echoServAddr, 0, sizeof(echoServAddr));
        echoServAddr.sin_family = AF_INET;
        echoServAddr.sin_addr.s_addr = inet_addr(host.c_str());
        echoServAddr.sin_port = htons(port);
        if (connect(sock, (struct sockaddr *)&echoServAddr, sizeof(echoServAddr)) < 0)
        {
            std::cerr << "connect() failed";
            std::cerr << " host: " << host;
            std::cerr << " port: " << port << std::endl;
            perror("connect");
            abort();
        }
    }

    struct cme_msg_header_t
    {
        uint16_t MsgSize;
        uint16_t EncodingType;
        uint16_t BlockLength;
        uint16_t TemplateID;
        uint16_t SchemaID;
        uint16_t Version;
    };

    /**
     * @brief Send a message to the socket
     *
     * @param msg
     * @return auto
     */
    static auto send_message(int sock, const char *msg, int sz, bool add_cred = false) noexcept
    {
        ssize_t totmsgsz = sz + SOFH_AND_SBE_HEADER_SIZE;
        if (add_cred)
            totmsgsz += CRED_SZ;
        // encoding type is 0xCAFE
        auto header = (uint16_t *)msg;
        *header++ = totmsgsz;
        *header++ = 0xCAFE;
        auto bytes = send(sock, msg, totmsgsz, 0);
        if (bytes < totmsgsz)
        {
            std::cerr << "send() failed" << std::endl;
            abort();
        }
        return bytes;
    }

    /**
     * @brief Receive a message from the socket
     * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/iLink+3+Message+Header
     *
     * received messge is placed in msg_buf
     *
     * @return auto
     */

    // #define DEBUG_recv_message

    static std::optional<cme_msg_header_t>
    recv_message(int sock, char *msg_buf, bool block = true) noexcept
    {
        char buffer[SOFH_AND_SBE_HEADER_SIZE];
        int flags = 0;
        if (!block)
        {
            auto avail_bytes = recv(sock, buffer, SOFH_AND_SBE_HEADER_SIZE, MSG_PEEK | MSG_DONTWAIT);
            if (avail_bytes < (decltype(avail_bytes))SOFH_AND_SBE_HEADER_SIZE)
                return {};
        }
        auto bytes_h = recv(sock, buffer, SOFH_AND_SBE_HEADER_SIZE, MSG_WAITALL);
        if (bytes_h < (decltype(bytes_h))SOFH_AND_SBE_HEADER_SIZE)
        {
            std::cerr << "recv() CME_HEADER failed " << sock << " " << bytes_h << std::endl;
            perror("recv header");
            return {};
        }
        auto databuf = (uint16_t *)buffer;
        cme_msg_header_t header;
        header.MsgSize = *databuf++;
        header.EncodingType = *databuf++;
        header.BlockLength = *databuf++;
        header.TemplateID = *databuf++;
        header.SchemaID = *databuf++;
        header.Version = *databuf;

#ifdef DEBUG_recv_message
        std::cerr
            << "recv_message: SOFH_AND_SBE_HEADER_SIZE (MsgSize - BlockLength) MsgSize EncodingType BlockLength TemplateID SchemaID Version: "
            << SOFH_AND_SBE_HEADER_SIZE << " "
            << header.MsgSize - header.BlockLength << " "
            << header.MsgSize << " "
            << header.EncodingType << " "
            << header.BlockLength << " "
            << header.TemplateID << " "
            << header.SchemaID << " "
            << header.Version << std::endl;
#endif
        size_t msgsz = header.BlockLength;
        if (header.TemplateID == sbe::NegotiationResponse501::sbeTemplateId())
        {
            assert(header.MsgSize - header.BlockLength == SOFH_AND_SBE_HEADER_SIZE + CRED_SZ);
            msgsz += CRED_SZ;
#ifdef DEBUG_recv_message
            std::cerr << "recv_message: NegotiationResponse501: " << msgsz << std::endl;
#endif
        }
        else
        {
            // some messages are longer than block size
            auto padded_msgsz = header.MsgSize - SOFH_AND_SBE_HEADER_SIZE;
            if (padded_msgsz > msgsz)
            {
#ifdef DEBUG_recv_message
                std::cerr << "recv_message: padded_msgsz > msgsz: "
                          << padded_msgsz << " " << msgsz
                          << std::endl;
#endif
                msgsz = padded_msgsz;
            }
        }

        flags = MSG_WAITALL;
        auto bytes = recv(sock, msg_buf, msgsz, flags);
        if (bytes < 0)
        {
            std::cerr << "recv_message: recv() body failed" << std::endl;
            perror("recv body");
            return {};
        }
        if (bytes != (ssize_t)msgsz)
        {
            std::cerr << "recv_message: recv() body failed "
                      << "sock bytes msgsz: "
                      << sock << " " << bytes << " " << msgsz
                      << std::endl;
            perror("recv body");
            return {};
        }
        assert(bytes == (ssize_t)msgsz);

        return header;
    }

}
