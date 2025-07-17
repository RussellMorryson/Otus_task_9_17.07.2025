#include "async.h"

namespace async {   
    class BulkLoggerContext; // Доп определение

    // Структура данных для хранения большого объема данных и временной метки
    struct BulkData {
        std::vector<std::string> commands;
        long long timestamp;
    };

    // Потокобезопасная очередь для передачи большого объема данных между потоками
    class ThreadSafeQueue {
        private:
            std::queue<BulkData> queue_;
            std::mutex mutex_;
            std::condition_variable condition_;
            bool stop_ = false;

        public:
            void enqueue(BulkData data) {
                std::unique_lock<std::mutex> lock(mutex_);
                queue_.push(std::move(data));
                condition_.notify_one();
            }

            BulkData dequeue() {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) {
                    return {};
                }
                BulkData data = std::move(queue_.front());
                queue_.pop();
                return data;
            }

            bool empty() {
                std::lock_guard<std::mutex> lock(mutex_);
                return queue_.empty();
            }

            void stop() {
                std::unique_lock<std::mutex> lock(mutex_);
                stop_ = true;
                condition_.notify_all();
            }
    };

    // Класс Logger, отвечающий за фактическую регистрацию в консоли и файлах
    class Logger {
        private:
            static constexpr size_t NUM_FILES = 2;
            std::mutex console_mutex_;
            std::mutex file_mutex_[NUM_FILES];
            size_t file_index_;
            std::string filename = "";

        public:
            Logger() : file_index_(0) {
                //std::cerr << "Logger::Logger() - Logger instance created." << std::endl;
            }

            ~Logger() {
                //std::cerr << "Logger::~Logger() - Logger instance destroyed." << std::endl;
            }

            void logToConsole(const BulkData& data) {
                std::lock_guard<std::mutex> lock(console_mutex_);
                //std::cerr << "Logger::logToConsole() - Attempting to log to console." << std::endl;

                std::cout << "bulk: ";
                for (size_t i = 0; i < data.commands.size(); ++i) {
                    std::cout << data.commands[i];
                    if (i < data.commands.size() - 1) {
                        std::cout << ", ";
                    }
                }
                std::cout << std::endl;
            }

            void logToFile(const BulkData& data) {
                std::lock_guard<std::mutex> lock(file_mutex_[file_index_]);
                // Changed to current directory
                if (filename.empty()) {
                    filename = "./bulk" + std::to_string(data.timestamp) + ".log";
                }
                
                //std::cerr << "Logger::logToFile() - Attempting to log to file: " << filename << std::endl;
                std::ofstream log_file(filename, std::ios::app);

                if (!log_file.is_open()) {
                    std::cerr << "Logger::logToFile() - ERROR: Could not open file '" << filename << "' for writing. Error code: " << errno << std::endl;
                    return;
                }

                log_file << "bulk: ";
                for (size_t i = 0; i < data.commands.size(); ++i) {
                    log_file << data.commands[i];
                    if (i < data.commands.size() - 1) {
                        log_file << ", ";
                    }
                }
                log_file << std::endl;

                log_file.flush();
                if (log_file.bad()) {
                    std::cerr << "Logger::logToFile() - ERROR: Problem writing to file '" << filename << "'" << std::endl;
                }

                log_file.close();
                if (log_file.is_open()) {
                    std::cerr << "Logger::logToFile() - ERROR: File '" << filename << "' still open after close()!" << std::endl;
                }

                //std::cerr << "Logger::logToFile() - Successfully logged to file: " << filename << std::endl;
                file_index_ = (file_index_ + 1) % NUM_FILES;
                //std::cerr << "Logger::logToFile() - New file index: " << file_index_ << std::endl;
            }
    };

    // Пул потоков для ведения журнала задач
    class ThreadPool {
        private:
            std::vector<std::thread> threads_;
            ThreadSafeQueue data_queue_;
            Logger& logger_;
            std::mutex queue_mutex_;
            std::condition_variable data_condition_;
            bool stop_;

        public:
            ThreadPool(size_t num_threads, Logger& logger) : logger_(logger), stop_(false) {
                for (size_t i = 0; i < num_threads; ++i) {
                    threads_.emplace_back([this] {
                        while (true) {
                            BulkData data;

                            std::unique_lock<std::mutex> lock(queue_mutex_);
                            data_condition_.wait(lock, [this] { return stop_ || !data_queue_.empty(); });
                            if (stop_ && data_queue_.empty()) {
                                //std::cerr << "ThreadPool: Thread exiting." << std::endl;
                                break;
                            }

                            data = data_queue_.dequeue();
                            if (data.commands.empty() && stop_) {
                                std::cerr << "ThreadPool: Thread received empty data and stop signal, exiting." << std::endl;
                                break;
                            }
                            logger_.logToConsole(data);
                            logger_.logToFile(data);
                        }
                    });
                }
            }

            ~ThreadPool() {
                //std::cerr << "ThreadPool: Destructor called." << std::endl;
                std::unique_lock<std::mutex> lock(queue_mutex_);
                stop_ = true;            
                data_condition_.notify_all();

                // Ожидание завершения потоков
                for (auto& thread : threads_) {
                    if (thread.joinable()) {
                        thread.join();
                    }
                }
                //std::cerr << "ThreadPool: All threads joined." << std::endl;
            }

            void enqueue(BulkData data) {
                data_queue_.enqueue(std::move(data));
            }

            void stop() {
                //std::cerr << "ThreadPool: Stopping thread pool." << std::endl;
                std::unique_lock<std::mutex> lock(queue_mutex_);
                stop_ = true;
                data_condition_.notify_all();
                data_queue_.stop();
            }
    };

    // Контекстный объект для каждого независимого экземпляра BulkLogger
    class BulkLoggerContext {
        private:
            size_t dynamic_bulk_size = 0;
            size_t bulk_size_;
            bool in_dynamic_block_;
            std::vector<std::string> commands_;
            ThreadPool& thread_pool_;

        public:
            BulkLoggerContext(size_t bulk_size, ThreadPool& thread_pool) : bulk_size_(bulk_size), in_dynamic_block_(false), thread_pool_(thread_pool) {
                //std::cerr << "BulkLoggerContext: Context created with bulk_size = " << bulk_size << std::endl; 
            }

            ~BulkLoggerContext() {
                //std::cerr << "BulkLoggerContext: Context being destroyed." << std::endl;
                flush();
            }

            void process_command(const std::string& command) {
                // std::cerr << "BulkLoggerContext: Processing command: " << command << std::endl;
                if (command == "{") {
                    dynamic_bulk_size += 1;
                    start_dynamic_block();
                } else if (command == "}") {
                    dynamic_bulk_size -= 1;
                    if (dynamic_bulk_size == 0) {
                        end_dynamic_block();
                    }
                } else {
                    commands_.push_back(command);
                    if (!in_dynamic_block_ && commands_.size() == bulk_size_) {
                        flush_bulk();
                    }
                }
            }

            void flush() {
                // std::cerr << "BulkLoggerContext: Flushing context." << std::endl;
                if (!commands_.empty()) {
                    flush_bulk();
                }
            }

        private:
            void start_dynamic_block() {
                //std::cerr << "BulkLoggerContext: Start dynamic block." << std::endl; 
                if (!in_dynamic_block_) {
                    flush_bulk();
                    in_dynamic_block_ = true;
                }
            }

            void end_dynamic_block() {
                //std::cerr << "BulkLoggerContext: End dynamic block." << std::endl;
                if (in_dynamic_block_) {
                    in_dynamic_block_ = false;
                    flush_bulk();
                }
            }

            void flush_bulk() {
                if (commands_.empty()) {
                    //std::cerr << "BulkLoggerContext: No commands to flush." << std::endl;
                    return;
                }

                long long timestamp = std::chrono::system_clock::now().time_since_epoch().count() / 1000000000;
                BulkData data;
                data.commands = std::move(commands_);
                data.timestamp = timestamp;
                commands_.clear();

                //std::cerr << "BulkLoggerContext: Enqueuing bulk data." << std::endl;
                thread_pool_.enqueue(std::move(data));
            }
    };

    // Глобальные переменные для хранения контекстных объектов, защищенных мьютексом
    std::map<void*, std::shared_ptr<BulkLoggerContext>> contexts;
    std::mutex contexts_mutex;
    std::unique_ptr<Logger> logger;
    std::unique_ptr<ThreadPool> thread_pool;
    std::once_flag init_flag;

    // Инициализация logger и thread_pool
    void initialize() {
        //std::cerr << "Initialization: Initializing logger and thread pool." << std::endl;
        logger = std::make_unique<Logger>();
        thread_pool = std::make_unique<ThreadPool>(3, *logger);
        //std::cerr << "Initialization: Logger and thread pool initialized." << std::endl; 
    }

    // API функции для взаимодействия с namespace async
    void* connect(size_t bulk_size) {
        std::call_once(init_flag, initialize);
        std::lock_guard<std::mutex> lock(contexts_mutex);
        
        void* context_id = (void*)new BulkLoggerContext(bulk_size, *thread_pool);
        contexts[context_id] = std::shared_ptr<BulkLoggerContext>((BulkLoggerContext*)context_id, [](BulkLoggerContext* p){ delete p; });
        //std::cerr << "Connect: New context created with ID: " << context_id << std::endl;
        return context_id;
    }

    void receive(void* context, const char* buffer, size_t size) {
        std::lock_guard<std::mutex> lock(contexts_mutex);
        auto it = contexts.find(context);
        if (it != contexts.end()) {
            std::string command(buffer, size);
            //std::cerr << "Receive: Received command '" << command << "' for context: " << context << std::endl;
            it->second->process_command(command);
        } else {
            std::cerr << "Receive: Error - Invalid context in receive(): " << context << std::endl;
        }
    }

    void disconnect(void* context) {
        //std::cerr << "Disconnect: Disconnecting context: " << context << std::endl;
        std::lock_guard<std::mutex> lock(contexts_mutex);
        auto it = contexts.find(context);
        if (it != contexts.end()) {
            it->second->flush();
            contexts.erase(it);
            //std::cerr << "Disconnect: Context " << context << " disconnected and erased." << std::endl;
        } else {
            std::cerr << "Disconnect: Error - Invalid context in disconnect(): " << context << std::endl;
        }

        //Проверка пусты ли контексты, а затем остановка пула потоков
        if (contexts.empty()) {
            //std::cerr << "Disconnect: No more contexts, stopping thread pool." << std::endl;
            if (thread_pool) {
                thread_pool->stop(); // Остановка
                thread_pool.reset(); // Очистка пула потоков
            }

            if (logger) {
                logger.reset(); // Сохранение в лог файл
            }
            //std::cerr << "Disconnect: Thread pool stopped." << std::endl;
        }
    }
}