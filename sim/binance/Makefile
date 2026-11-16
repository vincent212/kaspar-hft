# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.

# bnsim: the simulator on live Binance data (sim/BINANCE.md).

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

ifeq ($(UNAME_S),Darwin)
    OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
else
    OPENSSL_PATH ?= /usr
endif

EXTRA_INCL = -I$(KSPRPROJ)/sim/include -I$(OPENSSL_PATH)/include

SRC = main.cpp BnSimKaspr.cpp BinanceFeed.cpp BinanceSync.cpp Mbp2Mbo.cpp SlippageProbe.cpp
APPNAM = bnsim
vpath %.cpp $(KSPRPROJ)/sim/src

LIBSO += -L$(OPENSSL_PATH)/lib -lssl -lcrypto
LIBSG += -L$(OPENSSL_PATH)/lib -lssl -lcrypto

include $(KSPRPROJ)/mk_kaspr/app_template.mk
