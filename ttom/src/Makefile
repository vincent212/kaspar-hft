# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

# QuickFIX is installed under QUICKFIX_PREFIX (default /usr/local). Its headers
# include a bare "config.h", so its own include directory is added as well.
QUICKFIX_PREFIX ?= /usr/local
EXTRA_INCL += -I$(KSPRPROJ)/ttom/include -I$(QUICKFIX_PREFIX)/include -I$(QUICKFIX_PREFIX)/include/quickfix
# OpenSSL outside the default paths (e.g. Homebrew): OPENSSL_PREFIX=<dir>
ifdef OPENSSL_PREFIX
EXTRA_INCL += -I$(OPENSSL_PREFIX)/include
endif

# TLS inside QuickFIX instead of through stunnel (make USE_TTOM=y TTOM_SSL=y):
# needs QuickFIX built with OpenSSL.
ifeq ($(TTOM_SSL),y)
DEFINES_COMMON += -DUSESSLFIX
endif

LIBSRC = TTFixClient.cpp ttfixom.cpp

NAM = ttom

include $(KSPRPROJ)/mk_kaspr/lib_template.mk
