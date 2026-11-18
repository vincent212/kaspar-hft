# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Bybit V5 order manager (not certified): BybitOM, the REST + private
# WebSocket session, SimBybit, unit tests, and the testnet check tool. Not
# wired into the top-level build. Needs the kaspar libraries (logger, mq0,
# chutil, actors), Boost (Beast), OpenSSL, nlohmann/json, ZeroMQ, Google Test:
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                         # unit tests (no network)
#   make build/bybit_testnet_check    # ladder step 3; needs ~/.kaspar/keys/bybit_testnet.json
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
CXX ?= c++

SRC = src/BybitOM.cpp src/BybitConfig.cpp src/RestWsSession.cpp src/SimBybit.cpp
TESTS = tests/test_bybit_auth.cpp tests/test_bybit_om.cpp tests/test_sim_bybit.cpp

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
        -Wno-reorder -Wno-sign-compare -Iinclude -Itests -I$(KSPRPROJ)/sim/include $(INCL) \
        -I$(GTEST_PATH)/include -I$(OPENSSL_PATH)/include

# libframe compiled from this checkout (see market_data/bybit/Makefile: a
# libframe older than the BYBIT/BYBITF codes rejects them).
LOCAL_FRAME ?= 1
FRAME_SRC = RefData.cpp Timer.cpp OB.cpp point.cpp SOM.cpp SOM_if.cpp Cons_if.cpp BFA_if.cpp \
            BinRecorder_if.cpp OB_if.cpp Timer_if.cpp QLen_if.cpp LatencyProbe_if.cpp BrokerOrderBook.cpp
ifeq ($(LOCAL_FRAME),1)
FRAME_LIB = build/kl/libframe_local.a
LINKDEPS = build/kl/libframe_local.a
else
FRAME_LIB = -lframe
endif
LIBS = -L$(KSPRLIB) $(FRAME_LIB) -llogger -lmq0 -lchutil $(ACTORS_LIB) \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq \
       -L$(ZLIB_PATH)/lib -lz -L$(OPENSSL_PATH)/lib -lssl -lcrypto -lpthread
GTEST_LIBS = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

obj = $(patsubst %.cpp,build/obj/%.o,$(notdir $(1)))
LIBOBJ = $(call obj,$(SRC))
HDRS = $(wildcard include/bybit_oe/*.hpp include/bybit_oe/*/*.hpp tests/*.hpp)

.PHONY: all test clean
all: build/run_bybit_oe_tests build/bybit_testnet_check

build/obj/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
build/obj/%.o: tests/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
build/obj/%.o: tools/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
build/kl/obj/%.o: $(KSPRPROJ)/frame_kaspr/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) $(DEFINES_COMMON) -I$(KSPRPROJ)/frame_kaspr/include -O2 -g -w -c $< -o $@
build/kl/libframe_local.a: $(patsubst %.cpp,build/kl/obj/%.o,$(FRAME_SRC))
	ar rcs $@ $^

build/run_bybit_oe_tests: $(LIBOBJ) $(call obj,$(TESTS)) $(LINKDEPS)
	$(CXX) $(LIBOBJ) $(call obj,$(TESTS)) -o $@ $(GTEST_LIBS) $(LIBS)
build/bybit_testnet_check: $(LIBOBJ) build/obj/bybit_testnet_check.o $(LINKDEPS)
	$(CXX) $(LIBOBJ) build/obj/bybit_testnet_check.o -o $@ $(LIBS)

test: build/run_bybit_oe_tests
	KSPRPROJ=$(KSPRPROJ) ./build/run_bybit_oe_tests

clean:
	rm -rf build
