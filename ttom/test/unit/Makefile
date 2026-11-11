# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# TTFIXOM unit tests against an in-process fake TT gateway. Needs QuickFIX,
# Google Test and libttom (build the libraries with make USE_TTOM=y first):
#
#   export KSPRPROJ=~/kaspar-hft
#   eval "$(mk_kaspr/detect_paths.sh)"     # must set GTEST_PATH
#   make && ./run_ttom_tests
#
# Or from the top level: make test USE_TTOM=y

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

QUICKFIX_PREFIX ?= /usr/local
EXTRA_INCL += -I$(KSPRPROJ)/ttom/include -I$(QUICKFIX_PREFIX)/include -I$(QUICKFIX_PREFIX)/include/quickfix -I$(GTEST_PATH)/include
ifdef OPENSSL_PREFIX
EXTRA_INCL += -I$(OPENSSL_PREFIX)/include
endif
ifeq ($(TTOM_SSL),y)
DEFINES_COMMON += -DUSESSLFIX
endif

SRC = test_ttfixom.cpp
APPNAM = run_ttom_tests

LIBSO := -lttom $(LIBSO) -L$(QUICKFIX_PREFIX)/lib -lquickfix $(if $(OPENSSL_PREFIX),-L$(OPENSSL_PREFIX)/lib) -lssl -lcrypto -L$(GTEST_PATH)/lib -lgtest -lgtest_main
LIBSG := -lttomg $(LIBSG) -L$(QUICKFIX_PREFIX)/lib -lquickfix $(if $(OPENSSL_PREFIX),-L$(OPENSSL_PREFIX)/lib) -lssl -lcrypto -L$(GTEST_PATH)/lib -lgtest -lgtest_main

include $(KSPRPROJ)/mk_kaspr/app_template.mk
