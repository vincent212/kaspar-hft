# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# EBS Market on CME Globex over MDP 3.0 (not certified): decode and dispatch
# tests against Kaspar's existing MDP 3.0 decoder. Not wired into the top-level
# build. Nothing here modifies mdp3/, genschema/ or kaspr/config.
#
#   export KSPRPROJ=~/kaspar-hft
#   make TEMPLATE=/path/to/templates_FixBinary.xml test
#   make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a SBE_JAR=/path/to/sbe-all-1.30.0.jar test
#
# The MDP 3.0 codecs are generated into build/mdp3_sbe from CME's
# templates_FixBinary.xml with the same steps as genschema/genschema.py
# (package rewritten to `sbe`, real-logic sbe-all CPP target). They are not
# committed. TEMPLATE is CME's file (SFTP SBEFix/Production/Templates); the
# default is the copy ./build.sh leaves in genschema/.

KSPRPROJ ?= $(abspath $(CURDIR)/../../../..)
include $(KSPRPROJ)/mk_kaspr/glob_begin.mk

KSPRLIB ?= $(KSPRPROJ)/lib
ACTORS_LIB ?= $(KSPRPROJ)/actors/cpp/libactors.a
TEMPLATE ?= $(KSPRPROJ)/genschema/templates_FixBinary.xml
SBE_JAR ?= $(KSPRPROJ)/genschema/.cache/sbe-all-1.30.0.jar
JAVA ?= java
CXX ?= c++

CODECS = build/mdp3_sbe/.stamp
SIM_SRC = $(KSPRPROJ)/sim/src/Mbp2Mbo.cpp $(KSPRPROJ)/sim/src/LevelWindow.cpp
TESTS = tests/ebs_mdp3_test.cpp

# build/ first, so "mdp3_sbe/X.h" resolves to the codecs generated here and not
# to $(KSPRPROJ)/mdp3_sbe (empty in a fresh checkout).
FLAGS = $(STANDARD) -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
        -Wno-reorder -Wno-unused-variable -Ibuild -Itests -I$(KSPRPROJ)/sim/include \
        -I$(KSPRPROJ)/unit_test/include $(INCL) -I$(GTEST_PATH)/include
LIBS = -L$(KSPRLIB) -llogger -lchutil $(ACTORS_LIB) \
       -L$(GTEST_PATH)/lib -lgtest -lgtest_main \
       -L$(BOOST_PATH)/lib -lboost_thread -lboost_filesystem -L$(ZMQ_PATH)/lib -lzmq -lpthread

OBJ = $(patsubst %.cpp,build/obj/%.o,$(TESTS)) build/obj/Mbp2Mbo.o build/obj/LevelWindow.o

.PHONY: all codecs test clean
all: build/run_ebs_tests

codecs: $(CODECS)

$(CODECS): $(TEMPLATE)
	@test -f $(SBE_JAR) || { echo "SBE_JAR not found: $(SBE_JAR)"; exit 1; }
	@rm -rf build/mdp3_sbe build/sbe_out && mkdir -p build/mdp3_sbe build/sbe_out
	perl -pe 's/(messageSchema[^>]*?\bpackage=")[^"]*/$${1}sbe/' $(TEMPLATE) > build/templates_FixBinary.xml
	@grep -q 'package="sbe"' build/templates_FixBinary.xml || { echo "package rewrite failed"; exit 1; }
	$(JAVA) -Dsbe.target.language=CPP -Dsbe.output.dir=build/sbe_out -jar $(SBE_JAR) build/templates_FixBinary.xml
	cp build/sbe_out/sbe/*.h build/mdp3_sbe/
	@grep -o 'version="[0-9]*"' build/templates_FixBinary.xml | head -1 > build/mdp3_sbe/SCHEMA_VERSION
	@touch $@

build/obj/%.o: %.cpp $(CODECS) $(wildcard tests/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/obj/Mbp2Mbo.o: $(KSPRPROJ)/sim/src/Mbp2Mbo.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/obj/LevelWindow.o: $(KSPRPROJ)/sim/src/LevelWindow.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

build/run_ebs_tests: $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LIBS)

test: build/run_ebs_tests
	KSPRPROJ=$(KSPRPROJ) ./build/run_ebs_tests

clean:
	rm -rf build
