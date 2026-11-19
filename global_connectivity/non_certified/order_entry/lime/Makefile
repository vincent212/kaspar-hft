# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Lime (Lime Trading Corp.) C++ Trading API order manager (not certified):
# unit tests. Not wired into the top-level build. Needs the kaspar libraries
# (frame, logger, mq0, chutil, actors), Boost, ZeroMQ and Google Test:
#
#   export KSPRPROJ=~/kaspar-hft
#   make test                     # build and run both test binaries
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a test
#   make syntax                   # g++ -fsyntax-only on every source
#
# Two binaries:
#   build/run_lime_tests      LimeOM against FakeLime and SimLime; SdkSession
#                             built without an SDK (sdk_available() is false)
#   build/run_lime_sdk_tests  SdkSession built against
#                             sdk_doc/LimeTradingApiDoc.hh (a transcription of
#                             the public overview, NOT the vendor header) and
#                             tests/SdkStub.cpp in place of the library
#
# With the vendor SDK (not public; never done):
#   make LIME_SDK_HEADER='"TradingApi.hh"' LIME_SDK_INC=-I/path/to/sdk/include \
#        LIME_SDK_LIB='-L/path/to/sdk/lib -l...' build/run_lime_tests
#   (builds build/obj/src/SdkSession.o against the vendor header and links the
#   vendor library into run_lime_tests; the header file name is not public)

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
CXX ?= c++
GXX ?= g++-15

SRC = src/LimeOM.cpp src/LimeConfig.cpp src/SimLime.cpp
TESTS = tests/test_lime_om.cpp tests/test_sim_lime.cpp
SDK_TESTS = tests/test_sdk_session.cpp tests/SdkStub.cpp
BOOK = $(KSPRPROJ)/frame_kaspr/src/BrokerOrderBook.cpp

FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-reorder -Iinclude -Itests $(INCL) -I$(GTEST_PATH)/include
DOC_SDK = -Isdk_doc -DLIME_SDK_HEADER='"LimeTradingApiDoc.hh"'
LIBS = -L$(KSPRLIB) -lframe -llogger -lmq0 -lchutil $(ACTORS_LIB) \
       -L$(GTEST_PATH)/lib -lgtest -lgtest_main \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq -L$(ZLIB_PATH)/lib -lz -lpthread

COMMON = $(patsubst %.cpp,build/obj/%.o,$(SRC)) build/obj/BrokerOrderBook.o
OBJ = $(COMMON) build/obj/src/SdkSession.o $(patsubst %.cpp,build/obj/%.o,$(TESTS))
SDK_OBJ = $(COMMON) build/obj/sdk/src/SdkSession.o $(patsubst %.cpp,build/obj/sdk/%.o,$(SDK_TESTS))
DEPS = $(wildcard include/lime_om/*.hpp include/lime_om/*/*.hpp tests/*.hpp sdk_doc/*.hh)

.PHONY: all test syntax clean
all: build/run_lime_tests build/run_lime_sdk_tests

build/obj/%.o: %.cpp $(DEPS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/obj/sdk/%.o: %.cpp $(DEPS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(DOC_SDK) -c $< -o $@

# The real session: against the vendor SDK when LIME_SDK_HEADER is given,
# otherwise without it (sdk_available() is false).
REAL_SDK = $(if $(LIME_SDK_HEADER),$(LIME_SDK_INC) -DLIME_SDK_HEADER='$(LIME_SDK_HEADER)')
build/obj/src/SdkSession.o: src/SdkSession.cpp $(DEPS)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) $(REAL_SDK) -c $< -o $@

build/obj/BrokerOrderBook.o: $(BOOK)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/run_lime_tests: $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LIBS) $(LIME_SDK_LIB)

build/run_lime_sdk_tests: $(SDK_OBJ)
	$(CXX) $(SDK_OBJ) -o $@ $(LIBS)

test: all
	KSPRPROJ=$(KSPRPROJ) ./build/run_lime_tests
	KSPRPROJ=$(KSPRPROJ) ./build/run_lime_sdk_tests

# The kaspar headers use glibc's uint and ushort, which g++ on macOS does not
# declare; build/glibc_types.h supplies them for this check only.
syntax:
	@mkdir -p build
	@printf 'typedef unsigned int uint;\ntypedef unsigned short ushort;\n' > build/glibc_types.h
	for f in $(SRC) src/SdkSession.cpp $(TESTS); do $(GXX) $(FLAGS) -include build/glibc_types.h -fsyntax-only $$f || exit 1; done
	for f in src/SdkSession.cpp $(SDK_TESTS); do $(GXX) $(FLAGS) $(DOC_SDK) -include build/glibc_types.h -fsyntax-only $$f || exit 1; done

clean:
	rm -rf build
