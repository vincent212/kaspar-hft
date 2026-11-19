# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Kraken post-trade fills client (not certified): KrakenFillsClient on
# post_trade/common's TradeLedger, the futures (demo) and spot (production
# only, never run) fills sessions, SimFills and tests. Not part of the
# top-level build. Uses the Kraken order-entry sources for signing and the
# endpoint guard, and compiles libframe from this tree (see
# ../../order_entry/kraken/Makefile).
#
#   export KSPRPROJ=~/kaspar-hft
#   make test
#   make KSPRLIB=/path/lib ACTORS_LIB=/path/libactors.a CXX=clang++ test

HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KSPRPROJ ?= $(abspath $(HERE)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB    ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
ifeq ($(UNAME_S),Darwin)
    OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
else
    OPENSSL_PATH ?= /usr
endif
CXX ?= c++

MD     = $(abspath $(HERE)/../../market_data/kraken)
OE     = $(abspath $(HERE)/../../order_entry/kraken)
COMMON = $(abspath $(HERE)/../common)
B      = $(HERE)/build

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -I$(HERE)/include -I$(COMMON)/include -I$(OE)/include -I$(OE)/../common/include -I$(MD)/include \
        -I$(MD)/../crypto_common/include -I$(KSPRPROJ)/sim/include $(INCL) \
        -I$(OPENSSL_PATH)/include -I$(GTEST_PATH)/include

FRAME_SRCS = RefData.cpp Timer.cpp OB.cpp point.cpp SOM.cpp SOM_if.cpp Cons_if.cpp BFA_if.cpp \
             BinRecorder_if.cpp OB_if.cpp Timer_if.cpp QLen_if.cpp LatencyProbe_if.cpp BrokerOrderBook.cpp
FRAME_OBJ  = $(patsubst %.cpp,$(B)/obj/frame/%.o,$(FRAME_SRCS))
FRAME_LIB  = $(B)/lib/libframe.a
LIBS  = $(FRAME_LIB) -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
        -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem \
        -L$(OPENSSL_PATH)/lib -lssl -lcrypto -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread
GTEST = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

SRC   = src/KrakenFills.cpp src/KrakenFillsClient.cpp src/KrakenFillsSessions.cpp
TESTS = tests/test_kraken_fills.cpp
OESRC = KrakenAuth.cpp KrakenConfig.cpp KrakenOM.cpp KrakenSpotCodec.cpp KrakenFuturesSession.cpp KrakenFuturesCodec.cpp
EXT   = $(patsubst %.cpp,$(B)/obj/oe/%.o,$(OESRC)) $(B)/obj/ext/KrakenJson.o $(B)/obj/ext/TradeLedger.o
OBJ   = $(patsubst %.cpp,$(B)/obj/%.o,$(SRC)) $(EXT)
HDRS  = $(wildcard $(HERE)/include/kraken_pt/*.hpp $(HERE)/include/kraken_pt/*/*.hpp $(OE)/include/kraken_oe/*.hpp)

.PHONY: all test clean
all: $(B)/run_kraken_pt_tests

$(B)/obj/%.o: $(HERE)/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/obj/oe/%.o: $(OE)/src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
$(B)/obj/ext/KrakenJson.o: $(MD)/src/KrakenJson.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
$(B)/obj/ext/TradeLedger.o: $(COMMON)/src/TradeLedger.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/obj/frame/%.o: $(KSPRPROJ)/frame_kaspr/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -I$(KSPRPROJ)/frame_kaspr/include -O2 -g $(DEFINES_COMMON) $(DEFINES_OPT) -w -c $< -o $@

$(FRAME_LIB): $(FRAME_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(B)/run_kraken_pt_tests: $(patsubst %.cpp,$(B)/obj/%.o,$(TESTS)) $(OBJ) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS) $(GTEST)

test: $(B)/run_kraken_pt_tests
	cd $(HERE) && KSPRPROJ=$(KSPRPROJ) $(B)/run_kraken_pt_tests

clean:
	rm -rf $(B)
