# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

SRC = dbento_pcap_to_bin.cpp
APPNAM = dbento_pcap_to_bin

# libpcap: capture parsing (.zst handled by piping through zstdcat)
LIBSO += -lpcap
LIBSG += -lpcap

include $(KSPRPROJ)/mk_kaspr/app_template.mk
