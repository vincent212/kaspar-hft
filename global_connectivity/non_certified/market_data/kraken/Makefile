# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Kraken spot market data (not certified): sync classes, feed actor, recorder,
# verify tool and unit tests. Not part of the top-level build. Needs the Kaspar
# libraries (frame, logger, mq0, chutil, actors), Boost (Beast), OpenSSL,
# zlib, nlohmann/json and Google Test.
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                                   # build and run the unit tests
#   make tools                                  # build/kraken_verify, build/kraken_feed
#   make KSPRLIB=/path/lib ACTORS_LIB=/path/libactors.a CXX=clang++ test

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
CXX ?= c++
ifeq ($(UNAME_S),Darwin)
    OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
else
    OPENSSL_PATH ?= /usr
endif

SIM = $(KSPRPROJ)/sim/src
SRC = src/KrakenJson.cpp src/KrakenBookSync.cpp src/KrakenL3Sync.cpp src/KrakenFeed.cpp
SIMSRC = $(SIM)/Mbp2Mbo.cpp $(SIM)/LevelWindow.cpp
TESTS = tests/test_kraken_json.cpp tests/test_book_sync.cpp tests/test_l3_sync.cpp tests/test_book_ob.cpp

# KRAKEN_OB_VENUE: the en::x code on the records the OB checks send. Kaspar
# libraries built before KRAKEN was added to exch_code.enum reject it; build
# against those with OB_VENUE=COINBASE.
OB_VENUE ?= KRAKEN
FLAGS = -DKRAKEN_OB_VENUE=$(OB_VENUE) $(STANDARD) -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder \
        -Iinclude -I$(KSPRPROJ)/sim/include -I$(KSPRPROJ)/unit_test/include -I$(OPENSSL_PATH)/include \
        $(INCL) -I$(GTEST_PATH)/include
LIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq \
       -L$(OPENSSL_PATH)/lib -lssl -lcrypto -L$(ZLIB_PATH)/lib -lz -lpthread
GTEST = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

OBJ = $(patsubst %.cpp,build/obj/%.o,$(SRC)) build/obj/sim/Mbp2Mbo.o build/obj/sim/LevelWindow.o
TOBJ = $(patsubst %.cpp,build/obj/%.o,$(TESTS))

.PHONY: all test tools clean
all: build/run_kraken_md_tests tools
tools: build/kraken_verify build/kraken_feed

build/obj/%.o: %.cpp $(wildcard include/kraken_md/*.hpp include/kraken_md/act/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/obj/sim/%.o: $(SIM)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/run_kraken_md_tests: $(OBJ) $(TOBJ)
	$(CXX) $^ -o $@ $(LIBS) $(GTEST)

build/kraken_verify: build/obj/tools/kraken_verify.o $(OBJ)
	$(CXX) $^ -o $@ $(LIBS)

build/kraken_feed: build/obj/tools/kraken_feed.o $(OBJ)
	$(CXX) $^ -o $@ $(LIBS)

test: build/run_kraken_md_tests
	cd tests && KSPRPROJ=$(KSPRPROJ) ../build/run_kraken_md_tests

clean:
	rm -rf build
