#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <netdb.h> // Include this header for NI_MAXHOST
#include <ifaddrs.h>


namespace chutil
{

  inline static bool connect_with_timeout(int sockfd, const std::string &ip, int port, int timeout_seconds)
  {

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &(serv_addr.sin_addr)) <= 0)
    {
      perror("connect_with_timeout inet_pton");
      return false;
    }

    // Set the socket to non-blocking mode
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    // Start the connection attempt
    int result = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (result < 0 && errno != EINPROGRESS)
    {
      perror("connect_with_timeout connect");
      return false;
    }

    if (result == 0)
    {
      // Connection completed immediately
      fcntl(sockfd, F_SETFL, flags); // Restore original flags
      return true;
    }

    // Wait for the connection to complete or timeout
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sockfd, &writefds);

    struct timeval timeout;
    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    result = select(sockfd + 1, NULL, &writefds, NULL, &timeout);
    if (result <= 0)
    {
      // Timeout or error
      if (result == 0)
      {
        std::cerr << "*** connect_with_timeout timeout "
                  << ip << ":" << port
                  << std::endl;
      }
      else
      {
        perror("connect_with_timeout select");
      }
      close(sockfd);
      return false;
    }

    // Check for connection errors
    int so_error;
    socklen_t len = sizeof(so_error);
    getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error != 0)
    {
      errno = so_error;
      perror("connect_with_timeout connect");
      close(sockfd);
      return false;
    }

    // Restore original flags
    fcntl(sockfd, F_SETFL, flags);

    return true;
  }

  inline static std::string get_ip_address(const std::string &interface_name)
  {
    struct ifaddrs *ifaddr, *ifa;
    char ip[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1)
    {
      perror("getifaddrs");
      ERR("get_ip_address failed");
    }

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
      if (ifa->ifa_addr == nullptr)
        continue;

      if (ifa->ifa_addr->sa_family == AF_INET && interface_name == ifa->ifa_name)
      {
        if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), ip, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST) != 0)
        {
          perror("getnameinfo");
          freeifaddrs(ifaddr);
          ERR("get_ip_address failed");
        }
        freeifaddrs(ifaddr);
        return std::string(ip);
      }
    }

    freeifaddrs(ifaddr);
    return "";
  }
}