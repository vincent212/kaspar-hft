# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

# tt_logon: standalone TT FIX session check. Build libttom first
# (make USE_TTOM=y from the project root), then: make opt

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

QUICKFIX_PREFIX ?= /usr/local
EXTRA_INCL += -I$(KSPRPROJ)/ttom/include -I$(QUICKFIX_PREFIX)/include -I$(QUICKFIX_PREFIX)/include/quickfix
ifeq ($(TTOM_SSL),y)
DEFINES_COMMON += -DUSESSLFIX
endif

SRC = tt_logon.cpp
APPNAM = tt_logon

# libttom goes first: it uses frame, logger and actors, which follow it.
LIBSO := -lttom $(LIBSO) -L$(QUICKFIX_PREFIX)/lib -lquickfix -lssl -lcrypto
LIBSG := -lttomg $(LIBSG) -L$(QUICKFIX_PREFIX)/lib -lquickfix -lssl -lcrypto

include $(KSPRPROJ)/mk_kaspr/app_template.mk
