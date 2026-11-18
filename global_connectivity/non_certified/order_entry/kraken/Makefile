# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Kraken order manager (not certified): KrakenOM, the Kraken Futures REST
# session (demo), the spot WebSocket v2 session (production only, never run),
# SimKraken, and tests. Not part of the top-level build. Needs the Kaspar
# libraries (logger, mq0, chutil, actors), Boost (Beast), OpenSSL,
# nlohmann/json, ZeroMQ and Google Test; libframe is compiled here from this
# tree (its BrokerOrderBook and the KRAKEN/KRAKENF venue codes are newer than
# an installed libframe may be).
#
#   export KSPRPROJ=~/kaspar-hft
#   make test
#   make tools          # build/kraken_demo_check (step 3; needs demo keys to run)
#   make KSPRLIB=/path/lib ACTORS_LIB=/path/libactors.a CXX=clang++ test
#
# Everything is built into build/, which is git-ignored.

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

MD  = $(abspath $(HERE)/../../market_data/kraken)
SIM = $(KSPRPROJ)/sim
B   = $(HERE)/build

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -I$(HERE)/include -I$(HERE)/tests -I$(MD)/include -I$(SIM)/include $(INCL) \
        -I$(OPENSSL_PATH)/include -I$(GTEST_PATH)/include

FRAME_SRCS = RefData.cpp Timer.cpp OB.cpp point.cpp SOM.cpp SOM_if.cpp Cons_if.cpp BFA_if.cpp \
             BinRecorder_if.cpp OB_if.cpp Timer_if.cpp QLen_if.cpp LatencyProbe_if.cpp BrokerOrderBook.cpp
FRAME_OBJ  = $(patsubst %.cpp,$(B)/obj/frame/%.o,$(FRAME_SRCS))
FRAME_LIB  = $(B)/lib/libframe.a
LIBS  = $(FRAME_LIB) -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
        -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem \
        -L$(OPENSSL_PATH)/lib -lssl -lcrypto -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread
GTEST = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

SRC   = src/KrakenAuth.cpp src/KrakenConfig.cpp src/KrakenOM.cpp src/KrakenSpotCodec.cpp \
        src/KrakenFuturesCodec.cpp src/KrakenFuturesSession.cpp src/KrakenSpotSession.cpp src/SimKraken.cpp
TESTS = tests/test_auth_config.cpp tests/test_codecs.cpp tests/test_kraken_om.cpp \
        tests/test_futures_session.cpp tests/test_sim_kraken.cpp

OBJ    = $(patsubst %.cpp,$(B)/obj/%.o,$(SRC)) $(B)/obj/md/KrakenJson.o
HDRS   = $(wildcard $(HERE)/include/kraken_oe/*.hpp $(HERE)/include/kraken_oe/*/*.hpp $(HERE)/tests/*.hpp)

PT     = $(abspath $(HERE)/../../post_trade/kraken)
COMMON = $(abspath $(HERE)/../../post_trade/common)
PTOBJ  = $(B)/obj/pt/KrakenFills.o $(B)/obj/pt/KrakenFillsClient.o $(B)/obj/pt/KrakenFillsSessions.o \
         $(B)/obj/pt/TradeLedger.o

.PHONY: all test tools clean
all: $(B)/run_kraken_oe_tests tools
tools: $(B)/kraken_demo_check

$(B)/obj/pt/TradeLedger.o: $(COMMON)/src/TradeLedger.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -I$(COMMON)/include -c $< -o $@
$(B)/obj/pt/%.o: $(PT)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -I$(PT)/include -I$(COMMON)/include -c $< -o $@
$(B)/obj/tools/%.o: $(HERE)/tools/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -I$(PT)/include -I$(COMMON)/include -c $< -o $@

$(B)/kraken_demo_check: $(B)/obj/tools/kraken_demo_check.o $(OBJ) $(PTOBJ) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS)

$(B)/obj/%.o: $(HERE)/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/obj/md/KrakenJson.o: $(MD)/src/KrakenJson.cpp $(MD)/include/kraken_md/KrakenJson.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/obj/frame/%.o: $(KSPRPROJ)/frame_kaspr/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -I$(KSPRPROJ)/frame_kaspr/include -O2 -g $(DEFINES_COMMON) $(DEFINES_OPT) -w -c $< -o $@

$(FRAME_LIB): $(FRAME_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(B)/run_kraken_oe_tests: $(patsubst %.cpp,$(B)/obj/%.o,$(TESTS)) $(OBJ) $(FRAME_LIB)
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS) $(GTEST)

test: $(B)/run_kraken_oe_tests
	cd $(HERE) && KSPRPROJ=$(KSPRPROJ) $(B)/run_kraken_oe_tests

clean:
	rm -rf $(B)
