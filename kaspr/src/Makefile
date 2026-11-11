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

# Uses default LIBSO/LIBSG from glob_begin.mk (includes the std::stacktrace libs)
# Add pcap library for PCAP file reading
LIBSO += -lpcap
LIBSG += -lpcap

# Optional: TT FIX order routing (make USE_TTOM=y). Compiles in
# Kaspr::create_tt(), adds ttom's headers and links libttom, QuickFIX and
# OpenSSL. Without it kaspr has no QuickFIX or OpenSSL dependency.
ifeq ($(USE_TTOM),y)
QUICKFIX_PREFIX ?= /usr/local
EXTRA_INCL += -I$(KSPRPROJ)/ttom/include -I$(QUICKFIX_PREFIX)/include -I$(QUICKFIX_PREFIX)/include/quickfix
# OpenSSL outside the default paths (e.g. Homebrew): OPENSSL_PREFIX=<dir>
ifdef OPENSSL_PREFIX
EXTRA_INCL += -I$(OPENSSL_PREFIX)/include
endif
DEFINES_COMMON += -DUSE_TTOM
# libttom goes first: it uses frame, logger and actors, which follow it.
LIBSO := -lttom $(LIBSO) -L$(QUICKFIX_PREFIX)/lib -lquickfix $(if $(OPENSSL_PREFIX),-L$(OPENSSL_PREFIX)/lib) -lssl -lcrypto
LIBSG := -lttomg $(LIBSG) -L$(QUICKFIX_PREFIX)/lib -lquickfix $(if $(OPENSSL_PREFIX),-L$(OPENSSL_PREFIX)/lib) -lssl -lcrypto
endif

include $(KSPRPROJ)/mk_kaspr/app_template.mk
