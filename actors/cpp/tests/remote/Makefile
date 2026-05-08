# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

# Makefile for remote actor tests

CXX = g++
CXXFLAGS = -std=c++20 -O0 -g -I../../include -I/usr/local/boost188/include
LDFLAGS = -L../.. -lactors -lgtest -lgtest_main -lpthread -lzmq

# Source files
TESTS = test_zmq_sender

# Build all tests
all: $(TESTS)

test_zmq_sender: test_zmq_sender.cpp ../../libactors.a
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# Run all tests
test: $(TESTS)
	@for t in $(TESTS); do \
		echo "=== Running $$t ==="; \
		./$$t || exit 1; \
	done

clean:
	rm -f $(TESTS) *.o

.PHONY: all test clean
