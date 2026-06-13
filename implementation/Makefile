CXX ?= g++
CXXFLAGS ?= -O3 -std=c++14 -w -mavx -march=native -DNDEBUG -flto -fno-exceptions -fno-rtti -fno-math-errno -funroll-loops
CPPFLAGS_COMMON = -Iinclude -DRA_COLUMN_ORB_EXECUTOR
RECAST_SOURCES = $(wildcard src/recast/*.cpp)
L2_OBJECTS = build/recast_l2.o build/recast_range_l2.o
KNN_L2_OBJECTS = build/recast_l2.o build/recast_knn_l2.o
QFD_OBJECTS = build/recast_qfd.o build/recast_range_qfd.o

.PHONY: all clean

all: bin/recast_l2 bin/recast_knn_l2 bin/recast_knn_lite_l2 bin/recast_qfd

bin build:
	mkdir -p $@

build/recast_l2.o: src/recast_index.cpp $(RECAST_SOURCES) include/recast/recast_index.h | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS_COMMON) -D L2 -c $< -o $@

build/recast_qfd.o: src/recast_index.cpp $(RECAST_SOURCES) include/recast/recast_index.h | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS_COMMON) -D QFD -c $< -o $@

build/recast_range_l2.o: apps/recast_range.cpp include/recast/recast_index.h | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS_COMMON) -D L2 -c $< -o $@

build/recast_knn_l2.o: apps/recast_knn.cpp include/recast/recast_index.h | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS_COMMON) -D L2 -c $< -o $@

build/recast_range_qfd.o: apps/recast_range.cpp include/recast/recast_index.h | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS_COMMON) -D QFD -c $< -o $@

bin/recast_l2: $(L2_OBJECTS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/recast_knn_l2: $(KNN_L2_OBJECTS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/recast_knn_lite_l2: apps/recast_knn_lite_l2.cpp | bin
	$(CXX) $(CXXFLAGS) -D L2 $< -o $@

bin/recast_qfd: $(QFD_OBJECTS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf bin build
