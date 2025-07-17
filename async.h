#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <map>
#include <iomanip>

#ifndef ASYNC_H
#define ASYNC_H

#include <cstddef> // For size_t

namespace async {
    void* connect(size_t bulk_size);
    void receive(void* context, const char* buffer, size_t size);
    void disconnect(void* context);
}

#endif