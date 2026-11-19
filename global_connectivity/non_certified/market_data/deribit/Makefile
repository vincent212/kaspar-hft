# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Deribit market data (not certified): Starbase SBE decoder and book, JSON-RPC
# book sync, feed actor, tools and tests. Not wired into the top-level build.
#
#   export KSPRPROJ=~/kaspar-hft
#   make test          # generate the SBE codecs, build, run the unit tests
#   make tools         # build/starbase_pcap, build/deribit_live
#   make pcap-check    # decode Deribit's sample PCAP and check the books
#   make sdk-check     # the same PCAP through Deribit's own Python SDK; diff
#   make live          # 30 minutes of the public BTC-PERPETUAL book (no account)
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test
#
# The SBE codecs are generated from Deribit's published market-data schema
# (build/docs/deribit-sbe-market-data-api.xml, downloaded by `make docs`) with
# the real-logic SBE tool, the same tool and invocation as genschema/; they are
# git-ignored. The docs, the sample PCAP and the SDK are downloaded, not
# committed.

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
CXX ?= c++
PYTHON ?= python3
JAVA ?= java
SBE_JAR ?= $(firstword $(wildcard $(KSPRPROJ)/genschema/.cache/sbe-all-*.jar) $(HOME)/kaspar-hft/genschema/.cache/sbe-all-1.30.0.jar)
ifeq ($(UNAME_S),Darwin)
    OPENSSL_PATH ?= /opt/homebrew/opt/openssl@3
else
    OPENSSL_PATH ?= /usr
endif

DOCS = build/docs
SCHEMA = $(DOCS)/deribit-sbe-market-data-api.xml
PCAP = $(DOCS)/starbase-market-data.pcap
GEN = build/gen
GEN_STAMP = $(GEN)/.stamp

SIM = $(KSPRPROJ)/sim/src
LIBSRC = src/DeribitBookSync.cpp src/StarbaseBook.cpp src/Starbase.cpp src/DeribitFeed.cpp
SIMSRC = $(SIM)/Mbp2Mbo.cpp $(SIM)/LevelWindow.cpp
TESTS = tests/test_deribit_sync.cpp tests/test_starbase_decode.cpp tests/test_starbase_book.cpp \
        tests/test_chain_ob.cpp

FLAGS = $(STANDARD) -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
        -Wno-reorder -Wno-sign-compare -Iinclude -I$(GEN) -I$(KSPRPROJ)/sim/include \
        -I$(KSPRPROJ)/unit_test/include -I$(OPENSSL_PATH)/include $(INCL) -I$(GTEST_PATH)/include
# libframe and libchutil are rebuilt here from this tree's sources: the en::x
# codes for the crypto venues are newer than an installed libframe, whose
# inlined en::is_valid() rejects them (an ASSERT in data_pay_load::is_valid).
KLIB = build/klib
FRAME_SRC = RefData.cpp Timer.cpp OB.cpp point.cpp SOM.cpp SOM_if.cpp Cons_if.cpp BFA_if.cpp \
            BinRecorder_if.cpp OB_if.cpp Timer_if.cpp QLen_if.cpp LatencyProbe_if.cpp BrokerOrderBook.cpp
CHUTIL_SRC = theta.cpp Time.cpp CSVFileReader.cpp uniform_dist.cpp
LIBS = -L$(KLIB) -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq \
       -L$(ZLIB_PATH)/lib -lz -L$(OPENSSL_PATH)/lib -lssl -lcrypto -lpthread
GTEST = -L$(GTEST_PATH)/lib -lgtest -lgtest_main

obj = $(patsubst %.cpp,build/obj/%.o,$(notdir $(1)))
LIBOBJ = $(call obj,$(LIBSRC) $(SIMSRC))

.PHONY: all docs gen klib test tools pcap-check sdk-check live clean
all: build/run_tests tools

docs: $(SCHEMA) $(PCAP)
$(SCHEMA):
	@mkdir -p $(DOCS)
	curl -sSfL -o $@ https://docs.deribit.com/specifications/deribit-sbe-xmls/deribit-sbe-market-data-api.xml
$(PCAP):
	@mkdir -p $(DOCS)
	curl -sSfL -o $@ https://statics.deribit.com/files/starbase-market-data.pcap
$(DOCS)/sdk/.stamp:
	@mkdir -p $(DOCS)/sdk
	curl -sSfL -o $(DOCS)/md-sdk.zip https://docs.deribit.com/starbase/starbase-deribit-md-sdk-2.0.zip
	cd $(DOCS)/sdk && unzip -oq ../md-sdk.zip && for w in *.whl; do unzip -oq $$w; done && touch .stamp

gen: $(GEN_STAMP)
$(GEN_STAMP): $(SCHEMA)
	rm -rf $(GEN) && mkdir -p $(GEN)
	$(JAVA) -Dsbe.target.language=CPP -Dsbe.output.dir=$(GEN) -jar $(SBE_JAR) $(SCHEMA)
	touch $@

build/obj/%.o: src/%.cpp $(GEN_STAMP) $(wildcard include/deribit_md/*.hpp include/deribit_md/*/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
build/obj/%.o: $(SIM)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
build/obj/%.o: tests/%.cpp $(GEN_STAMP) $(wildcard include/deribit_md/*.hpp tests/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -DDERIBIT_TEST_DATA=\"$(CURDIR)/tests/data\" -DDERIBIT_PCAP=\"$(CURDIR)/$(PCAP)\" -c $< -o $@
build/obj/%.o: tools/%.cpp $(GEN_STAMP) $(wildcard include/deribit_md/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(KLIB)/obj/frame/%.o: $(KSPRPROJ)/frame_kaspr/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -I$(KSPRPROJ)/frame_kaspr/include -c $< -o $@
$(KLIB)/obj/chutil/%.o: $(KSPRPROJ)/chutil/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
$(KLIB)/libframe.a: $(patsubst %.cpp,$(KLIB)/obj/frame/%.o,$(FRAME_SRC))
	rm -f $@ && ar rcs $@ $^
$(KLIB)/libchutil.a: $(patsubst %.cpp,$(KLIB)/obj/chutil/%.o,$(CHUTIL_SRC))
	rm -f $@ && ar rcs $@ $^
klib: $(KLIB)/libframe.a $(KLIB)/libchutil.a

build/run_tests: $(KLIB)/libframe.a $(KLIB)/libchutil.a $(LIBOBJ) $(call obj,$(TESTS))
	$(CXX) $(filter %.o,$^) -o $@ $(GTEST) $(LIBS)

build/starbase_pcap: $(KLIB)/libframe.a $(KLIB)/libchutil.a $(LIBOBJ) build/obj/starbase_pcap.o
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS)
build/deribit_live: $(KLIB)/libframe.a $(KLIB)/libchutil.a $(LIBOBJ) build/obj/deribit_live.o
	$(CXX) $(filter %.o,$^) -o $@ $(LIBS)
tools: build/starbase_pcap build/deribit_live

test: build/run_tests $(PCAP)
	KSPRPROJ=$(KSPRPROJ) ./build/run_tests

pcap-check: build/starbase_pcap $(PCAP)
	KSPRPROJ=$(KSPRPROJ) ./build/starbase_pcap $(PCAP) --ob

# Deribit's own Python decoder over the same PCAP; the two dumps must be equal.
sdk-check: build/starbase_pcap $(PCAP) $(DOCS)/sdk/.stamp
	./build/starbase_pcap $(PCAP) --dump > build/dump_cpp.txt
	PYTHONPATH=$(DOCS)/sdk $(PYTHON) tools/sdk_dump.py $(PCAP) > build/dump_sdk.txt
	cmp build/dump_cpp.txt build/dump_sdk.txt && wc -l build/dump_cpp.txt

live: build/deribit_live
	KSPRPROJ=$(KSPRPROJ) ./build/deribit_live --minutes 30 --record build/live_frames.bin

clean:
	rm -rf build/obj build/gen build/run_tests build/starbase_pcap build/deribit_live
