# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

# tt_logon: standalone TT FIX session check. Build libttom first
# (make USE_TTOM=y from the project root), then: make opt

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

QUICKFIX_PREFIX ?= /usr/local
EXTRA_INCL += -I$(KSPRPROJ)/ttom/include -I$(QUICKFIX_PREFIX)/include -I$(QUICKFIX_PREFIX)/include/quickfix
# OpenSSL outside the default paths (e.g. Homebrew): OPENSSL_PREFIX=<dir>
ifdef OPENSSL_PREFIX
EXTRA_INCL += -I$(OPENSSL_PREFIX)/include
endif
ifeq ($(TTOM_SSL),y)
DEFINES_COMMON += -DUSESSLFIX
endif

SRC = tt_logon.cpp
APPNAM = tt_logon

# libttom goes first: it uses frame, logger and actors, which follow it.
LIBSO := -lttom $(LIBSO) -L$(QUICKFIX_PREFIX)/lib -lquickfix $(if $(OPENSSL_PREFIX),-L$(OPENSSL_PREFIX)/lib) -lssl -lcrypto
LIBSG := -lttomg $(LIBSG) -L$(QUICKFIX_PREFIX)/lib -lquickfix $(if $(OPENSSL_PREFIX),-L$(OPENSSL_PREFIX)/lib) -lssl -lcrypto

include $(KSPRPROJ)/mk_kaspr/app_template.mk
