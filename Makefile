CXX ?= g++
CXXFLAGS = -std=c++17 -Iinclude -Wall -O2

# OpenSSL support: make OPENSSL=1
# Detects Homebrew openssl@3 on Apple Silicon / Intel; falls back to system paths.
ifeq ($(OPENSSL),1)
  OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null || echo /usr/local)
  CXXFLAGS += -DRORAM_USE_OPENSSL -I$(OPENSSL_PREFIX)/include
  LDFLAGS  += -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
endif

LIB_SRCS = src/types.cpp src/block.cpp src/crypto.cpp src/position_map.cpp \
	src/storage_mem.cpp src/storage_file.cpp src/sub_oram.cpp src/roram.cpp src/path_oram.cpp
LIB_OBJS = $(LIB_SRCS:.cpp=.o)

libroram.a: $(LIB_OBJS)
	ar rcs $@ $^

roram_main: src/main.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ src/main.o $(LIB_OBJS) $(LDFLAGS)

tests_basic: tests/basic_tests.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ tests/basic_tests.o $(LIB_OBJS) $(LDFLAGS)

.PHONY: clean test test-openssl
clean:
	rm -f $(LIB_OBJS) src/main.o tests/basic_tests.o libroram.a roram_main tests_basic

# Run tests (no-op crypto, all platforms)
test: tests_basic roram_main
	./tests_basic

# Run tests with OpenSSL AES-128-GCM enabled
test-openssl:
	$(MAKE) clean
	$(MAKE) OPENSSL=1 tests_basic roram_main
	./tests_basic
