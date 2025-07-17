CC = g++
CFLAGS = -fPIC -std=c++17 -pthread
SHARED_FLAGS = -shared
TEST_FLAGS = -DTEST_BUILD # Enable test code compilation

# Build the shared library (libasync.so)
libasync.so: async.cpp async.h
	$(CC) $(CFLAGS) $(SHARED_FLAGS) -o libasync.so async.cpp

# Build the test executable (only if you need it)
test: main.cpp libasync.so
	$(CC) $(CFLAGS) $(TEST_FLAGS) -o test main.cpp -L. libasync.so
