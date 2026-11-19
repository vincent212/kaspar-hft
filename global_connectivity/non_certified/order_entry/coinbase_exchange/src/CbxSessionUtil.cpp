/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The Logon signature and the log masking, shared by the order manager and
// the drop-copy client.

#include <cstring>

#include "coinbase_exchange_oe/CbxSession.hpp"

namespace coinbase_exchange::oe
{
using namespace fix;

// ---- Logon signature ---------------------------------------------------------------------------

bool sign_logon(FixMsg &logon, const std::string &sending_time, const std::string &seq_num, const std::string &key,
                const std::string &secret, const std::string &passphrase, char cancel_on_disconnect, bool drop_copy)
{
  auto sig = sign(secret, fix_logon_prehash(sending_time, "A", seq_num, key, "Coinbase", passphrase));
  if (!sig)
    return false;
  logon.set(EncryptMethod, "0");
  logon.set(Username, key);
  logon.set(Password, passphrase);
  logon.set(RawDataLength, std::to_string(sig->size()));
  logon.set(RawData, *sig);
  logon.set(DefaultApplVerID, "9");
  if (cancel_on_disconnect && !drop_copy)
    logon.set(CancelOrdersOnDisconnect, cancel_on_disconnect);
  if (drop_copy)
    logon.set(DropCopyFlag, 'Y');
  return true;
}

std::string mask_secrets(const std::string &raw)
{
  std::string out = raw;
  for (const char *tag : {"\x01" "554=", "\x01" "96="})
  {
    size_t pos = 0;
    while ((pos = out.find(tag, pos)) != std::string::npos)
    {
      size_t start = pos + std::strlen(tag);
      size_t end = out.find('\x01', start);
      if (end == std::string::npos)
        end = out.size();
      out.replace(start, end - start, "***");
      pos = start;
    }
  }
  return out;
}

}
