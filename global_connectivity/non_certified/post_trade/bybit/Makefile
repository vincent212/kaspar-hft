# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Bybit V5 post-trade client (not certified): BybitFillsClient, its sources,
# unit tests and the testnet check tool. Not wired into the top-level build.
# Shares the signed REST and private WebSocket code with
# order_entry/bybit (include/bybit_oe/BybitPrivate.hpp, BybitAuth.hpp).
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                        # unit tests (no network)
#   make build/bybit_fills_check     # ladder step 3; needs ~/.kaspar/keys/bybit_testnet.json
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
CXX ?= c++

COMMON = ../common
OE = ../../order_entry/bybit
SRC = src/BybitFills.cpp src/BybitFillsClient.cpp src/RestWsFillSource.cpp src/SimFillSource.cpp \
      $(COMMON)/src/TradeLedger.cpp
TESTS = tests/test_bybit_fills.cpp tests/test_bybit_fills_client.cpp

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
        -Wno-reorder -Wno-sign-compare -Iinclude -Itests -I$(COMMON)/include -I$(OE)/include -I$(OE)/../common/include \
        -I../../market_data/bybit/include -I../../market_data/crypto_common/include \
        -I$(KSPRPROJ)/sim/include $(INCL) -I$(GTEST_PATH)/include -I$(OPENSSL_PATH)/include

LIBS = -L$(KSPRLIB) -llogger -lmq0 -lchutil $(ACTORS_LIB) \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq \
       -L$(ZLIB_PATH)/lib -lz -L$(OPENSSL_PATH)/lib -lssl -lcrypto -lpthread
GTEST_LIBS = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

obj = $(patsubst %.cpp,build/obj/%.o,$(notdir $(1)))
LIBOBJ = $(call obj,$(SRC))
HDRS = $(wildcard include/bybit_post_trade/*.hpp include/bybit_post_trade/*/*.hpp tests/*.hpp \
       $(OE)/include/bybit_oe/BybitPrivate.hpp $(OE)/include/bybit_oe/BybitAuth.hpp)

.PHONY: all test clean
all: build/run_bybit_pt_tests build/bybit_fills_check

build/obj/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
build/obj/%.o: $(COMMON)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
build/obj/%.o: tests/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
build/obj/%.o: tools/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/run_bybit_pt_tests: $(LIBOBJ) $(call obj,$(TESTS))
	$(CXX) $(LIBOBJ) $(call obj,$(TESTS)) -o $@ $(GTEST_LIBS) $(LIBS)
build/bybit_fills_check: $(LIBOBJ) build/obj/bybit_fills_check.o
	$(CXX) $(LIBOBJ) build/obj/bybit_fills_check.o -o $@ $(LIBS)

test: build/run_bybit_pt_tests
	KSPRPROJ=$(KSPRPROJ) ./build/run_bybit_pt_tests

clean:
	rm -rf build
