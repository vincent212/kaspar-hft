# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Coinbase Exchange FIX drop copy (not certified): CbxDropClient, SimDropCopy
# and the tests. Uses the order-entry directory's session (CbxSession,
# QfSession) and post_trade/common (TradeCapture, TradeLedger). The QuickFIX
# session is built only with QUICKFIX_PATH (see ../../order_entry/coinbase_exchange/Makefile).
#
#   export KSPRPROJ=~/kaspar-hft
#   make test [QUICKFIX_PATH=/path/to/quickfix]

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
QUICKFIX_PATH ?=
CXX ?= c++

NC = $(KSPRPROJ)/global_connectivity/non_certified
MD = $(NC)/market_data/coinbase_exchange
OE = $(NC)/order_entry/coinbase_exchange
COMMON = $(NC)/post_trade/common
SRC = src/CbxDropClient.cpp src/SimDropCopy.cpp $(OE)/src/QfSession.cpp $(OE)/src/CbxSessionUtil.cpp $(MD)/src/Rest.cpp \
      $(COMMON)/src/TradeLedger.cpp
TESTS = $(wildcard tests/test_*.cpp)

QF_FLAGS = $(if $(QUICKFIX_PATH),-DCBX_WITH_QUICKFIX -DHAVE_SSL=1 -I$(QUICKFIX_PATH)/include,)
QF_LIBS = $(if $(QUICKFIX_PATH),$(QUICKFIX_PATH)/libquickfix.a,)

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -Iinclude -I$(OE)/include -I$(OE)/tests -I$(MD)/include -I$(COMMON)/include -I$(KSPRPROJ)/sim/include \
        $(INCL) -I$(GTEST_PATH)/include -I$(OPENSSL_PATH)/include $(QF_FLAGS)
LIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) $(QF_LIBS) \
       -L$(OPENSSL_PATH)/lib -lssl -lcrypto -L$(GTEST_PATH)/lib -lgtest -lgtest_main \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread

OBJ = $(patsubst %.cpp,build/obj/%.o,$(notdir $(SRC) $(TESTS)))
vpath %.cpp src tests $(OE)/src $(MD)/src $(COMMON)/src

.PHONY: all test clean
all: build/run_cbx_drop_tests

build/obj/%.o: %.cpp $(wildcard include/coinbase_exchange_pt/*.hpp include/coinbase_exchange_pt/*/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/run_cbx_drop_tests: $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LIBS)

test: build/run_cbx_drop_tests
	KSPRPROJ=$(KSPRPROJ) ./build/run_cbx_drop_tests

clean:
	rm -rf build
