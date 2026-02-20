CXX ?= g++
CXXFLAGS = -std=c++17 -Iinclude -Wall -O2
LIB_SRCS = src/types.cpp src/block.cpp src/crypto.cpp src/position_map.cpp \
	src/storage_mem.cpp src/storage_file.cpp src/sub_oram.cpp src/roram.cpp
LIB_OBJS = $(LIB_SRCS:.cpp=.o)

libroram.a: $(LIB_OBJS)
	ar rcs $@ $^

roram_main: src/main.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ src/main.o $(LIB_OBJS)

.PHONY: clean
clean:
	rm -f $(LIB_OBJS) src/main.o libroram.a roram_main
