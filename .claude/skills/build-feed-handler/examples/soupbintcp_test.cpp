// Encode and decode through the header generated from soupbintcp.tbl, with the
// ITCH header for the same venue in the same translation unit.
#include "itch50.hpp"
#include "soupbintcp.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <string_view>

int main()
{
  using namespace nasdaq;
  char buf[128];

  // A variable-length payload is written after the struct and counted in the prefix.
  soup::to_nasdaq::UnsequencedData u{};
  const size_t n = soup::to_nasdaq::encode(buf, sizeof buf, u, "OUCHMSG");
  assert(n == 2 + 1 + 7);
  assert(buf[0] == 0 && buf[1] == 8 && buf[2] == 'U');
  assert(std::string_view(buf + 3, 7) == "OUCHMSG");

  // Encoding refuses a buffer too small, and a payload the 2-byte prefix cannot count.
  assert(soup::to_nasdaq::encode(buf, 9, u, "OUCHMSG") == 0);
  std::string big(70000, 'x');
  std::string out(80000, '\0');
  assert(soup::to_nasdaq::encode(out.data(), out.size(), u, big) == 0);

  // The same bytes framed as a server packet decode to the payload and its size.
  buf[2] = 'S';
  auto d = soup::from_nasdaq::decode(buf, sizeof buf);
  assert(d && std::holds_alternative<soup::from_nasdaq::SequencedData>(d.msg));
  assert(d.size == 10 && std::string_view(d.tail, d.tail_len) == "OUCHMSG");

  // An ITCH message carried in it decodes with the ITCH header.
  unsigned char itch[] = {0, 12, 'S', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'Q'};
  auto e = itch::from_nasdaq::decode((const char *)itch, sizeof itch);
  assert(e && std::get<itch::from_nasdaq::SystemEvent>(e.msg).EventCode == 'Q');

  // Two packets back to back: size steps from one to the next.
  char two[64] = {0, 1, 'H', 0, 2, 'J', 'A'};
  auto a = soup::from_nasdaq::decode(two, 7);
  assert(a && a.size == 3 && std::holds_alternative<soup::from_nasdaq::ServerHeartbeat>(a.msg));
  auto b = soup::from_nasdaq::decode(two + a.size, 7 - a.size);
  assert(b && std::get<soup::from_nasdaq::LoginRejected>(b.msg).RejectReasonCode == 'A');

  // A partial read is incomplete; decoding again with more bytes succeeds.
  assert(soup::from_nasdaq::decode(two, 1).st == status::incomplete);
  assert(soup::from_nasdaq::decode(two + 3, 3).st == status::incomplete);

  // A prefix shorter than the fixed part is malformed and gives the size to skip.
  char shortp[64] = {0, 5, 'A'};
  auto s = soup::from_nasdaq::decode(shortp, sizeof shortp);
  assert(s.st == status::malformed && s.size == 7);

  // An unknown packet type is reported with the size to skip.
  char unk[8] = {0, 1, '?'};
  auto k = soup::from_nasdaq::decode(unk, sizeof unk);
  assert(k.st == status::unknown && k.size == 3);

  // Text fields are padded and aligned as the table says.
  soup::to_nasdaq::LoginRequest l{};
  l.set_Username("ab");
  l.set_RequestedSequenceNumber("1");
  l.set_RequestedSession("");
  assert(std::string_view(l.Username, 6) == "ab    " && l.get_Username() == "ab");
  assert(std::string_view(l.RequestedSequenceNumber, 20) == std::string(19, ' ') + "1");
  assert(l.get_RequestedSequenceNumber() == "1");
  assert(std::string_view(l.RequestedSession, 10) == std::string(10, ' '));
  assert(soup::to_nasdaq::encode(buf, sizeof buf, l) == 49 && buf[1] == 47 && buf[2] == 'L');

  std::cout << "soupbintcp OK\n";
}
