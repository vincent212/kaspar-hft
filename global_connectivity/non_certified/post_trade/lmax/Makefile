# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# LMAX post-trade client (not certified): unit tests and loopback tests
# against SimLmaxGateway. Not part of the top-level build. Builds and links
# the order manager's library (../../order_entry/lmax: the session, the report
# parsers, the shared FIX code) and post_trade/common's TradeLedger. Needs
# what that library needs (README there):
#
#   export KSPRPROJ=~/kaspar-hft
#   make test QF_DIR=/path/to/qfbuild

HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KSPRPROJ ?= $(abspath $(HERE)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB    ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
QF_DIR     ?= $(HOME)/local/quickfix
OPENSSL    ?= /opt/homebrew/opt/openssl@3
CXX        ?= c++

OE = $(abspath $(HERE)/../../order_entry/lmax)
MD = $(abspath $(HERE)/../../market_data/lmax)
COMMON = $(abspath $(HERE)/../common)
OBJDIR = $(HERE)/build/obj

LIB_SRC = src/TradeMap.cpp src/LmaxTradeClient.cpp
TEST_SRC = tests/test_trade_map.cpp tests/test_trade_client.cpp

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -I$(HERE)/include -I$(COMMON)/include -I$(OE)/include -I$(OE)/tests -I$(MD)/include -I$(MD)/tests \
        -I$(KSPRPROJ)/sim/include -I$(QF_DIR)/include $(INCL) -I$(OPENSSL)/include -I$(GTEST_PATH)/include \
        -DHAVE_SSL=1 -DLMAX_TRADING_DICT='"$(OE)/dict/LMAX-FIX44-Trading.xml"' \
        -DLMAX_MD_DICT='"$(MD)/dict/LMAX-FIX44-MarketData.xml"'
QF_LIBS = $(QF_DIR)/libquickfix.a -L$(OPENSSL)/lib -lssl -lcrypto
LIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) $(QF_LIBS) \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread

OBJ = $(patsubst %.cpp,$(OBJDIR)/%.o,$(LIB_SRC) $(TEST_SRC)) $(OBJDIR)/common/TradeLedger.o
OM_LIB = $(OE)/build/liblmax_om.a

.PHONY: all test om-lib clean
all: build/run_lmax_post_trade_tests

om-lib:
	$(MAKE) -C $(OE) build/liblmax_om.a

$(OBJDIR)/%.o: $(HERE)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -MMD -c $< -o $@

$(OBJDIR)/common/TradeLedger.o: $(COMMON)/src/TradeLedger.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -MMD -c $< -o $@

build/run_lmax_post_trade_tests: $(OBJ) om-lib
	$(CXX) $(OBJ) $(OM_LIB) -o $@ -L$(GTEST_PATH)/lib -lgtest -lgtest_main $(LIBS)

test: build/run_lmax_post_trade_tests
	cd $(HERE) && KSPRPROJ=$(KSPRPROJ) ./build/run_lmax_post_trade_tests

clean:
	rm -rf build

-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)
