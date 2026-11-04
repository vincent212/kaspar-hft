#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The books on the CPU: one 10-level book per contract of the filter's table,
 * built from the card's packed events (events.hpp). Plain C++, used by the
 * FpgaFeed actor and by the testbenches.
 */

#include <cstdint>
#include <cstring>
#include <vector>

#include "mdp3fpga/events.hpp"

namespace mdp3fpga {

struct BookLevel
{
  long long price;
  int size;
  int orders;
};

struct Book
{
  BookLevel bid[kLevels];
  BookLevel ask[kLevels];
};

// One event as the card packed it.
struct Event
{
  uint64_t lo;
  uint64_t hi;

  long long price() const { return static_cast<long long>(lo); }
  int size() const { return static_cast<int32_t>(hi); }
  int orders() const { return static_cast<uint16_t>(hi >> 32); }
  unsigned contract() const { return (hi >> 48) & 0xff; }
  unsigned level() const { return (hi >> 56) & 15; }
  unsigned action() const { return (hi >> 60) & 7; }
  bool offer() const { return hi >> 63; }
};

class Books
{
  std::vector<Book> books_;

  static void clear(BookLevel *s) { std::memset(s, 0, kLevels * sizeof(BookLevel)); }

public:
  explicit Books(size_t contracts) : books_(contracts) { std::memset(books_.data(), 0, contracts * sizeof(Book)); }

  const Book &book(size_t contract) const { return books_[contract]; }

  void apply(const Event &e)
  {
    const unsigned action = e.action(), level = e.level();
    if (action == kActionTrade)
      return;
    if (action == kActionOther)
    {
      if (level == kOtherChannelReset)
        for (Book &b : books_)
        {
          clear(b.bid);
          clear(b.ask);
        }
      else if (level == kOtherBookReset && e.contract() < books_.size())
      {
        clear(books_[e.contract()].bid);
        clear(books_[e.contract()].ask);
      }
      return;
    }
    if (e.contract() >= books_.size() || level < 1 || level > kLevels)
      return;
    Book &b = books_[e.contract()];
    BookLevel *s = e.offer() ? b.ask : b.bid;
    const int i = level - 1;
    const BookLevel l{e.price(), e.size(), e.orders()};
    switch (action)
    {
    case 0:   // new
      std::memmove(s + i + 1, s + i, (kLevels - 1 - i) * sizeof(BookLevel));
      s[i] = l;
      break;
    case 2:   // delete
      std::memmove(s + i, s + i + 1, (kLevels - 1 - i) * sizeof(BookLevel));
      s[kLevels - 1] = BookLevel{0, 0, 0};
      break;
    case 3:   // delete thru
      clear(s);
      break;
    case 4:   // delete from: the top level levels
      std::memmove(s, s + i + 1, (kLevels - 1 - i) * sizeof(BookLevel));
      std::memset(s + kLevels - 1 - i, 0, (i + 1) * sizeof(BookLevel));
      break;
    default:  // change, overlay
      s[i] = l;
      break;
    }
  }
};

} // namespace mdp3fpga
