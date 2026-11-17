# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.

# bnsim: the simulator on live Binance, Coinbase or Hyperliquid data
# (sim/BINANCE.md, sim/CRYPTO.md).

include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

ifeq ($(UNAME_S),Darwin)
    OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
else
    OPENSSL_PATH ?= /usr
endif

EXTRA_INCL = -I$(KSPRPROJ)/sim/include -I$(OPENSSL_PATH)/include

SRC = main.cpp BnSimKaspr.cpp BinanceFeed.cpp BinanceSync.cpp CoinbaseFeed.cpp CoinbaseSync.cpp \
      HyperliquidFeed.cpp HyperliquidSync.cpp LevelWindow.cpp Mbp2Mbo.cpp SlippageProbe.cpp
APPNAM = bnsim
vpath %.cpp $(KSPRPROJ)/sim/src

# Only what bnsim uses, so it builds without the CME codecs (`make bnsim` at
# the top): no mdp3, ilink, db or mcast_recv.
LIBS_COMMON_OPT = -lpositionman -lmtd -llight -lframe -lmq0 -llogger -lchutil $(ACTORS_LIB_PATH)/libactors.a -lgsl -lgslcblas
LIBS_COMMON_DBG = -lpositionmang -lmtdg -llightg -lframeg -lmq0g -lloggerg -lchutilg $(ACTORS_LIB_PATH)/libactorsg.a -lgsl -lgslcblas

LIBSO += -L$(OPENSSL_PATH)/lib -lssl -lcrypto
LIBSG += -L$(OPENSSL_PATH)/lib -lssl -lcrypto

include $(KSPRPROJ)/mk_kaspr/app_template.mk
