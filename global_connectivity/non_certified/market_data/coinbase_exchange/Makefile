# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Coinbase Exchange market data (not certified): the L3 sync, the public L2
# sync, the WebSocket feed actor, the cbx_md tool and the unit tests. Not
# wired into the top-level build. Needs the kaspar libraries (frame, logger,
# mq0, chutil, actors), Boost (Beast), OpenSSL, ZeroMQ and Google Test:
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                     # build and run the unit tests
#   make tool                     # build/cbx_md (live, record, replay, verify)
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
CXX ?= c++

SIM = $(KSPRPROJ)/sim/src
SRC = src/CoinbaseExchangeSync.cpp src/CoinbaseExchangeL2Sync.cpp
FEED = src/CoinbaseExchangeFeed.cpp src/Rest.cpp src/Verifier.cpp
TESTS = $(wildcard tests/test_*.cpp)

FLAGS = $(STANDARD) -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -Iinclude -Isrc -I$(KSPRPROJ)/sim/include -I$(KSPRPROJ)/unit_test/include $(INCL) -I$(GTEST_PATH)/include -I$(OPENSSL_PATH)/include
KLIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
        -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread
SSL = -L$(OPENSSL_PATH)/lib -lssl -lcrypto
GTEST = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

LIB_OBJ = $(patsubst %.cpp,build/obj/%.o,$(SRC)) build/obj/sim/Mbp2Mbo.o build/obj/sim/LevelWindow.o
FEED_OBJ = $(patsubst %.cpp,build/obj/%.o,$(FEED))
TEST_OBJ = $(patsubst %.cpp,build/obj/%.o,$(TESTS))

.PHONY: all test tool clean
all: build/run_cbx_md_tests build/cbx_md

build/obj/%.o: %.cpp $(wildcard include/coinbase_exchange/*.hpp include/coinbase_exchange/act/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/obj/sim/%.o: $(SIM)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/run_cbx_md_tests: $(LIB_OBJ) $(FEED_OBJ) $(TEST_OBJ)
	$(CXX) $^ -o $@ $(KLIBS) $(SSL) $(GTEST)

build/cbx_md: $(LIB_OBJ) $(FEED_OBJ) build/obj/src/cbx_md.o
	$(CXX) $^ -o $@ $(KLIBS) $(SSL)

test: build/run_cbx_md_tests
	KSPRPROJ=$(KSPRPROJ) ./build/run_cbx_md_tests

tool: build/cbx_md

clean:
	rm -rf build
