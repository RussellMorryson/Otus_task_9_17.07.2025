#include <iostream>
#include <thread>
#include <chrono>
#include "async.h"

int main() {
    std::cout << "Main -- Starting test..." << std::endl;
    size_t bulk_size = 3;
    void* context = async::connect(bulk_size);

    if (context == nullptr) {
        std::cerr << "Main -- Error: connect() returned null." << std::endl;
        return 1;
    }

    std::cout << "Main -- Context created: " << context << std::endl;

    // Команды:
    async::receive(context, "cmd1", 4);
    async::receive(context, "cmd2", 4); // Should trigger a flush
    async::receive(context, "cmd3", 4); // Should trigger another flush immediately
    async::receive(context, "{", 1); // Should trigger another flush immediately
    async::receive(context, "cmd4", 4); // Should trigger another flush immediately
    async::receive(context, "cmd5", 4); // Should trigger another flush immediately
    async::receive(context, "}", 1); // Should trigger another flush immediately
    async::receive(context, "cmd6", 4); // Should trigger another flush immediately
    async::receive(context, "cmd7", 4); // Should trigger another flush immediately

    std::cout << "Main -- Commands sent." << std::endl;

    // Ожидание окончания обработки потоков (важно!).
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    std::cout << "Main -- Disconnecting..." << std::endl;
    async::disconnect(context);

    std::cout << "Main -- Test finished." << std::endl;

    system("pause");
    return 0;
}