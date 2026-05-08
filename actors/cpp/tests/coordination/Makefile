# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

# Makefile for coordination/GroupManager tests
#
# Usage:
#   make all      # Build all tests
#   make test     # Build and run all tests
#   make clean    # Remove build artifacts

CXX = g++
CXXFLAGS = -std=c++20 -O0 -g -Wall -Wextra
CXXFLAGS += -I../../include -I/usr/local/boost188/include
LDFLAGS = -L../.. -lactors -lgtest -lgtest_main -lpthread -lzmq

# GroupManager source (not in libactors.a)
GM_SRC = ../../src/coordination/GroupManager.cpp

# Test binaries
TESTS = test_messages test_group_manager test_coordinated_group test_permission_timeout

# Build all tests
all: $(TESTS)

test_messages: test_messages.cpp ../../libactors.a
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

test_group_manager: test_group_manager.cpp $(GM_SRC) ../../libactors.a
	$(CXX) $(CXXFLAGS) $< $(GM_SRC) -o $@ $(LDFLAGS)

test_coordinated_group: test_coordinated_group.cpp ../../libactors.a
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

test_permission_timeout: test_permission_timeout.cpp $(GM_SRC) ../../libactors.a
	$(CXX) $(CXXFLAGS) $< $(GM_SRC) -o $@ $(LDFLAGS)

# Run all tests
test: $(TESTS)
	@for t in $(TESTS); do \
		echo "=== Running $$t ==="; \
		./$$t || exit 1; \
	done

# Run specific test
test-%: %
	./$<

clean:
	rm -f $(TESTS) *.o

.PHONY: all test clean
