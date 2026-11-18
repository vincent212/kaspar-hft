// Decode through the header generated from itch50.tbl.
#include "itch50.hpp"
#include <cassert>
#include <iostream>
int main() {
  using namespace nasdaq::itch::from_nasdaq;
  // AddOrder from bytes: length 36, 'A', locate 7, tracking 0, ts 0x010203040506, oref 42, 'B', 100 shares, "AAPL", 1234500
  unsigned char b[38] = {0, 36, 'A', 0, 7, 0, 0, 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 42, 'B', 0, 0, 0, 100,
                         'A', 'A', 'P', 'L', ' ', ' ', ' ', ' ', 0x00, 0x12, 0xD6, 0x44};
  auto d = decode((const char *)b, sizeof b);
  assert(d && d.size == 38 && std::holds_alternative<AddOrder>(d.msg));
  const auto &a = std::get<AddOrder>(d.msg);
  assert(a.MessageLength == 36 && a.StockLocate == 7 && uint64_t(a.Timestamp) == 0x010203040506ull);
  assert(a.OrderReferenceNumber == 42 && a.Shares == 100 && a.Price == 1234500 && nasdaq::get_str(a.Stock) == "AAPL");
  // A length prefix shorter than the message is malformed, not filled from the next message.
  auto copy = a;
  b[1] = 30;
  auto bad = decode((const char *)b, sizeof b);
  assert(!bad && bad.st == nasdaq::status::malformed && bad.size == 32);
  // A type the table does not list is skipped by its length prefix.
  unsigned char r[] = {0, 3, 'R', 0, 0};
  auto u = decode((const char *)r, sizeof r);
  assert(u.st == nasdaq::status::unknown && u.size == 5);
  std::cout << copy << "\n" << name(d.msg) << " OK\n";
}
