#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*

multicast.c

The following program sends or receives multicast packets. If invoked
with one argument, it sends a packet containing the current time to an
arbitrarily chosen multicast group and UDP port. If invoked with no
arguments, it receives and prints these packets. Start it as a sender on
just one host and as a receiver on all the other hosts

*/

#include "chutil/Macros.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdio.h>
#include <strings.h>
#include <sys/ioctl.h>

namespace chutil::mcast
{

   inline static void create_udp_socket(
       in_port_t port, // multicast port
       int &sock,
       struct sockaddr_in &addr,
       std::size_t &addrlen) noexcept
   {
      sock = socket(AF_INET, SOCK_DGRAM, 0);
      if (sock < 0)
      {
         perror("socket");
         ERR("count not crete socket");
      }
      int optval = 1;
      setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
      bzero((char *)&addr, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      addr.sin_port = htons(port);
      addrlen = sizeof(addr);
      if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
      {
         perror("bind");
         ERR("bind failed");
      }
      // IP_MULTICAST_ALL is Linux-only
#ifdef IP_MULTICAST_ALL
      int mc_all = 0;
      if ((setsockopt(sock, IPPROTO_IP, IP_MULTICAST_ALL, (void *)&mc_all, sizeof(mc_all))) < 0)
      {
         perror("setsockopt() failed");
      }
#endif
   }

   inline static auto join_group(
       int sock,
       const char *group,
       const char *inf = 0) noexcept
   {

      struct ip_mreq mreq;
      mreq.imr_multiaddr.s_addr = inet_addr(group);
      if (inf)
         mreq.imr_interface.s_addr = inet_addr(inf);
      else
         mreq.imr_interface.s_addr = htonl(INADDR_ANY);
      if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     &mreq, sizeof(mreq)) < 0)
      {
         perror("setsockopt IP_ADD_MEMBERSHIP");
         std::cerr << "group: " << group << std::endl;
         std::cerr << "inf: " << inf << std::endl;
         std::cerr << "sock: " << sock << std::endl;
         ERR("cound not join mcast group")
      }
      return mreq;
   }

   inline static auto drop_group(
       int sock,
       struct ip_mreq *mreq) noexcept
   {
      std::cout << "leaveGroup called" << std::endl;
      setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, mreq, sizeof(*mreq));
   }

   inline static auto send_to_socket(
       int sock,
       const char *message,
       std::size_t len,
       const struct sockaddr_in &addr,
       std::size_t addrlen) noexcept
   {
      auto cnt = sendto(sock, message, len, 0,
                        (struct sockaddr *)&addr, addrlen);
      if (cnt < 0)
      {
         perror("sendto");
         ERR("count not send to socket");
      }
      return cnt;
   }

   inline static void wait_for_data(int sock)
   {
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(sock, &read_fds);
      // Block until data is available on the socket
      int result = select(sock + 1, &read_fds, NULL, NULL, NULL);
      if (result < 0)
      {
         perror("select");
         return;
      }
   }

   inline static bool has_more(
       int sock) noexcept
   {
      int bytes_available = 0;
      if (ioctl(sock, FIONREAD, &bytes_available) < 0)
      {
         perror("ioctl FIONREAD");
         return false;
      }
      return bytes_available > 0;
   }

   //
   // note: datagrams are always received completely
   //
   inline static std::size_t receive(
       int sock,
       const char *message,
       std::size_t len,
       const struct sockaddr_in &addr,
       socklen_t addrlen) noexcept
   {
      auto cnt = recvfrom(sock, (void *)message, len, 0 /*flags*/,
                          (struct sockaddr *)&addr, &addrlen);
      if CHUNLIKELY (cnt < 0)
      {
         if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
         perror("recvfrom");
         return 0;
      }
      return cnt;
   }
}
