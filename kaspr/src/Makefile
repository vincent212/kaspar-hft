# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

SRC = kaspr.cpp
APPNAM = kaspr

# Optional: build with TachBook MBO L3 order books (make USE_TACHBOOK=1)
ifdef USE_TACHBOOK
DEFINES_COMMON += -DUSE_TACHBOOK
endif

# Uses default LIBSO/LIBSG from glob_begin.mk (includes -lstdc++exp -lbacktrace for stacktrace)
# Add pcap library for PCAP file reading
LIBSO += -lpcap
LIBSG += -lpcap

include $(KSPRPROJ)/mk_kaspr/app_template.mk
