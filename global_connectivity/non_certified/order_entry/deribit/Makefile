# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Deribit order manager (not certified): DeribitOM over the JSON-RPC
# WebSocket, SimDeribit, tests and the test-environment check. Not part of the
# top-level build. libframe is compiled here from this tree (the en::x codes
# for the crypto venues are newer than an installed libframe).
#
#   export KSPRPROJ=~/kaspar-hft
#   make test
#   make tools          # build/deribit_testnet_check (needs test keys to run)
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test
#
# Everything is built into build/, which is git-ignored.

HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KSPRPROJ ?= $(abspath $(HERE)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB    ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
CXX ?= c++

PT  = $(abspath $(HERE)/../../post_trade/deribit)
COMMON = $(abspath $(HERE)/../../post_trade/common)
B   = $(HERE)/build

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -I$(HERE)/include -I$(HERE)/tests -I$(HERE)/../common/include -I$(KSPRPROJ)/global_connectivity/non_certified/market_data/crypto_common/include \
        -I$(KSPRPROJ)/sim/include $(INCL) \
        -I$(OPENSSL_PATH)/include -I$(GTEST_PATH)/include
TOOLFLAGS = -I$(PT)/include -I$(COMMON)/include

FRAME_SRCS = RefData.cpp Timer.cpp OB.cpp point.cpp SOM.cpp SOM_if.cpp Cons_if.cpp BFA_if.cpp \
             BinRecorder_if.cpp OB_if.cpp Timer_if.cpp QLen_if.cpp LatencyProbe_if.cpp BrokerOrderBook.cpp
FRAME_OBJ  = $(patsubst %.cpp,$(B)/obj/frame/%.o,$(FRAME_SRCS))
FRAME_LIB  = $(B)/lib/libframe.a
LIBS  = $(FRAME_LIB) -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
        -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem \
        -L$(OPENSSL_PATH)/lib -lssl -lcrypto -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread
GTEST = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

SRC   = src/Signing.cpp src/DeribitConfig.cpp src/DeribitOM.cpp src/DeribitWsSession.cpp \
        src/SimDeribit.cpp
TESTS = tests/test_deribit_signing_config.cpp tests/test_deribit_om.cpp tests/test_sim_deribit.cpp
OBJ   = $(patsubst %.cpp,$(B)/obj/%.o,$(SRC))
HDRS  = $(wildcard $(HERE)/include/deribit_oe/*.hpp $(HERE)/include/deribit_oe/*/*.hpp $(HERE)/tests/*.hpp)
# The testnet check also runs the post-trade fills client.
PT_OBJ = $(B)/obj/pt/DeribitFills.o $(B)/obj/pt/DeribitFillsClient.o $(B)/obj/pt/DeribitFillsWsSession.o \
         $(B)/obj/pt/SimFills.o $(B)/obj/pt/TradeLedger.o

.PHONY: all test tools clean
all: $(B)/run_deribit_oe_tests tools
tools: $(B)/deribit_testnet_check

$(B)/obj/%.o: $(HERE)/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(if $(findstring tools/,$<),$(TOOLFLAGS)) -c $< -o $@

$(B)/obj/pt/%.o: $(PT)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(TOOLFLAGS) -c $< -o $@
$(B)/obj/pt/TradeLedger.o: $(COMMON)/src/TradeLedger.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(TOOLFLAGS) -c $< -o $@

$(B)/obj/frame/%.o: $(KSPRPROJ)/frame_kaspr/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -I$(KSPRPROJ)/frame_kaspr/include -O2 -g $(DEFINES_COMMON) $(DEFINES_OPT) -w -c $< -o $@

$(FRAME_LIB): $(FRAME_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(B)/run_deribit_oe_tests: $(patsubst %.cpp,$(B)/obj/%.o,$(TESTS)) $(OBJ) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS) $(GTEST)

$(B)/deribit_testnet_check: $(B)/obj/tools/deribit_testnet_check.o $(OBJ) $(PT_OBJ) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS)

test: $(B)/run_deribit_oe_tests
	cd $(HERE) && KSPRPROJ=$(KSPRPROJ) $(B)/run_deribit_oe_tests

clean:
	rm -rf $(B)
