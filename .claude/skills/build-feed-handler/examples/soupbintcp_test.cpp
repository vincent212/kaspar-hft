// Encode and decode through the header generated from soupbintcp.tbl.
#include "soupbintcp.hpp"
#include <cassert>
#include <iostream>
#include <string_view>

int main()
{
  using namespace nasdaq;
  char buf[128];

  // A variable-length payload is written after the struct and counted in the prefix.
  soup::to_nasdaq::UnsequencedData u{};
  const size_t n = soup::to_nasdaq::encode(buf, u, "OUCHMSG");
  assert(n == 2 + 1 + 7);
  assert(buf[0] == 0 && buf[1] == 8 && buf[2] == 'U');
  assert(std::string_view(buf + 3, 7) == "OUCHMSG");

  // The same bytes framed as a server packet decode to the payload and its size.
  buf[2] = 'S';
  auto d = soup::from_nasdaq::decode(buf, sizeof buf);
  assert(d && std::holds_alternative<soup::from_nasdaq::SequencedData>(d->msg));
  assert(d->size == 10 && std::string_view(d->tail, d->tail_len) == "OUCHMSG");

  // Two packets back to back: size steps from one to the next.
  char two[64] = {0, 1, 'H', 0, 2, 'J', 'A'};
  auto a = soup::from_nasdaq::decode(two, 7);
  assert(a && a->size == 3 && std::holds_alternative<soup::from_nasdaq::ServerHeartbeat>(a->msg));
  auto b = soup::from_nasdaq::decode(two + a->size, 7 - a->size);
  assert(b && std::get<soup::from_nasdaq::LoginRejected>(b->msg).RejectReasonCode == 'A');

  // A prefix shorter than the fixed part, or longer than the buffer, is refused.
  char shortp[64] = {0, 5, 'A'};
  assert(!soup::from_nasdaq::decode(shortp, sizeof shortp));
  char longp[8] = {0, 30, 'A'};
  assert(!soup::from_nasdaq::decode(longp, sizeof longp));

  // An unknown packet type is refused.
  char unk[8] = {0, 1, '?'};
  assert(!soup::from_nasdaq::decode(unk, sizeof unk));

  // Fixed-width text is padded on encode.
  soup::to_nasdaq::LoginRequest l{};
  set_str(l.Username, "ab");
  assert(std::string_view(l.Username, 6) == "ab    " && get_str(l.Username) == "ab");
  assert(soup::to_nasdaq::encode(buf, l) == 49 && buf[1] == 47 && buf[2] == 'L');

  std::cout << "soupbintcp OK\n";
}
