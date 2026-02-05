/**
 * @file async_vector_mean.cpp
 * @brief 工业级异步向量平均值计算示例 - 面向大一学生的异步编程教学
 * @version 1.0.0
 * @date 2025-12-08
 * 
 * @license MIT License
 * @copyright (c) 2025 Industrial Async Solutions Inc.
 * 
 * 【大一学生学习路线图】
 * 
 * ============ 学习阶段1：同步编程（你已经掌握的）============
 * 1. 顺序执行：代码一行行执行，A完成后才执行B
 * 2. 阻塞操作：遇到耗时操作，整个程序停止等待
 * 3. 简单直观：代码流程清晰，易于调试
 * 缺点：CPU利用率低，响应速度慢
 * 
 * ============ 学习阶段2：异步编程（本文件重点）============
 * 1. 非阻塞执行：发起操作后立即返回，不等待结果
 * 2. 并行处理：多个任务同时进行，提高效率
 * 3. 回调/等待：通过future在需要时获取结果
 * 优点：高并发、高响应、高CPU利用率
 * 
 * 【C++语法特性学习重点】
 * 标记说明：
 *  ★★★ - 必须掌握的核心概念
 *  ★★☆ - 重要，需要理解
 *  ★☆☆ - 了解即可，大二会深入学习
 */

#pragma once
#ifndef ASYNC_VECTOR_MEAN_H
#define ASYNC_VECTOR_MEAN_H

// ==================== 标准库头文件 ====================
#include <iostream>             // ★★★ I/O流，所有C++程序基础
#include <vector>               // ★★★ 动态数组，替代C原生数组
#include <algorithm>            // ★★☆ STL算法，比手写循环更高效
#include <future>               // ★★★ 异步编程核心头文件
#include <thread>               // ★★★ 线程管理，异步底层实现
#include <chrono>               // ★★☆ 时间库，性能测量必备
#include <random>               // ★★☆ 现代随机数，替代过时的rand()
#include <numeric>              // ★★☆ 数值算法，如accumulate
#include <stdexcept>            // ★★★ 异常处理，健壮程序必备
#include <atomic>               // ★☆☆ 原子操作，线程安全基础（大二重点）
#include <mutex>                // ★☆☆ 互斥锁，保护共享资源
#include <memory>               // ★★★ 智能指针，自动内存管理
#include <sstream>              // ★★☆ 字符串流，格式化输出
#include <iomanip>              // ★☆☆ I/O格式化，控制输出格式
#include <string>               // ★★★ string类，比C字符串安全
#include <cstdlib>              // ★☆☆ 标准库函数
#include <cassert>              // ★★☆ 断言，调试神器

using namespace std;

// ==================== 编译期配置 ====================
// 【C++语法】#ifdef/#endif - 条件编译，根据调试/发布模式不同编译
#ifdef NDEBUG
    #define ASYNC_LOG_LEVEL 0      // 发布模式：减少日志输出
#else
    #define ASYNC_LOG_LEVEL 2      // 调试模式：详细日志
#endif

// ==================== 常量定义 ====================
namespace Config {
    // 【C++语法】constexpr - 编译期常量，比const更严格
    constexpr int RANDOM_MAX = 10000;           // 随机数范围上限
    constexpr size_t VECTOR_SIZE = 100;         // 向量大小
    constexpr int WORKER_SLEEP_MS = 500;        // 模拟计算耗时
    
    // 【异步vs同步关键参数】★★★
    constexpr int MAIN_SLEEP_SEC = 2;           // 主线程工作耗时
    
    // 【同步执行总耗时】≈ WORKER_SLEEP_MS + MAIN_SLEEP_SEC * 1000
    // 【异步执行总耗时】≈ max(WORKER_SLEEP_MS, MAIN_SLEEP_SEC * 1000)
    // 【关键理解】异步优势 = 并行执行时间最长的任务，而不是串行累加
    
    constexpr float EMPTY_VECTOR_VALUE = 0.0f;  // 哨兵值
}

// ==================== 日志系统（简化版） ====================
/**
 * @class Logger
 * @brief 生产环境日志系统（简化版）
 * 
 * 【大一学习重点】理解日志在调试中的重要性
 * 调试技巧：日志能告诉你程序在"想什么"，而不是只看到最终结果
 */
class Logger {
private:
    // 【C++语法】static方法 - 无需创建对象即可调用
    // 【C++语法】thread_local - 每个线程独立副本（★★☆ 高级特性）
    static thread_local ostringstream tls_buffer_;
public:
    enum class LogLevel {
        DEBUG,   // 调试信息
        INFO,    // 一般信息
        WARN,    // 警告信息
        ERROR,   // 错误信息
        CRITICAL // 严重错误
    };
    
public:
    template<typename... Args>
    static void log(LogLevel level, const char* file, int line, const char* function, const char* format, Args... args) {
        // 【条件编译】根据日志级别决定是否输出
        if (static_cast<int>(level) < ASYNC_LOG_LEVEL) {
            return;
        }
        
        // 【C++语法】snprintf - C风格格式化，C++20前常用
        char message[1024];
        snprintf(message, sizeof(message), format, args...);
        
        ostream& stream = (level >= LogLevel::ERROR) ? cerr : cout;
        
        // 【时间戳】获取当前时间
        auto now = chrono::system_clock::now();
        auto now_time = chrono::system_clock::to_time_t(now);
        
        stream << put_time(localtime(&now_time), "%H:%M:%S") 
            << " [" << this_thread::get_id() << "] "
            << message << endl;
    }
};

// 【宏定义】简化日志调用，生产环境常用技巧
#define LOG_DEBUG(...)   Logger::log(Logger::LogLevel::DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...)    Logger::log(Logger::LogLevel::INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...)   Logger::log(Logger::LogLevel::ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

// ==================== 断言系统 ====================
/**
 * @macro ASYNC_ASSERT
 * @brief 条件断言，调试助手
 * 
 * 【调试技巧】★★★
 * 断言用于检查"不应该发生"的情况
 * 发布模式会自动禁用，不影响性能
 */
#if ASYNC_LOG_LEVEL >= 2
    #define ASYNC_ASSERT(expr, ...) \
        do { \
            if (!(expr)) { \
                LOG_ERROR("Assertion failed: %s", #expr); \
                LOG_ERROR(__VA_ARGS__); \
                abort(); \
            } \
        } while (0)
#else
    #define ASYNC_ASSERT(expr, ...) ((void)0) // 发布模式：无操作
#endif

// ==================== 随机数生成器类 ====================
/**
 * @class ThreadSafeRandom
 * @brief 线程安全随机数生成器
 * 
 * 【C++语法】类设计（面向对象基础）★★★
 * 1. 封装：数据和操作封装在类中
 * 2. 构造函数：对象初始化
 * 3. 运算符重载：让对象像函数一样使用
 */
class ThreadSafeRandom {
private:
    // 【多线程安全】thread_local - 每个线程有独立实例，避免锁开销
    static thread_local mt19937 generator_;
    static thread_local bool initialized_;
    
    uniform_int_distribution<int> distribution_;
    
    static void initialize_generator() {
        if (!initialized_) {
            // 【随机数质量】random_device - 硬件随机源（真随机）
            random_device rd;
            generator_ = mt19937(rd()); // Mersenne Twister - 高质量伪随机
            initialized_ = true;
        }
    }
    
public:
    /**
     * @brief 构造函数
     * @param min 最小值
     * @param max 最大值
     * 
     * 【C++语法】explicit - 防止隐式转换
     * 对比：ThreadSafeRandom rng = 10; // 错误！需要explicit
     *      ThreadSafeRandom rng(10);  // 正确
     */
    explicit ThreadSafeRandom(int min, int max) 
        : distribution_(min, max) {
        
        // 【防御性编程】检查参数有效性
        ASYNC_ASSERT(min <= max, "随机数范围无效: %d > %d", min, max);
        
        initialize_generator();
    }
    
    /**
     * @brief 生成随机数
     * @return int 范围内的随机整数
     * 
     * 【C++语法】operator() - 函数调用运算符重载
     * 调用方式：int num = rng(); // 像函数一样使用对象
     */
    [[nodiscard]] int operator()() {
        return distribution_(generator_);
    }
    
    // 【C++11特性】delete关键字 - 显式删除函数
    ThreadSafeRandom(const ThreadSafeRandom&) = delete;
    ThreadSafeRandom& operator=(const ThreadSafeRandom&) = delete;
    
    // 【C++11特性】移动语义 - 提高性能
    ThreadSafeRandom(ThreadSafeRandom&&) noexcept = default;
    ThreadSafeRandom& operator=(ThreadSafeRandom&&) noexcept = default;
};

// ==================== 统计计算类 ====================
/**
 * @class StatisticsCalculator
 * @brief 统计计算工具类
 * 
 * 【设计模式】工具类 - 只包含静态方法，无需实例化
 * 使用方式：StatisticsCalculator::mean(data)
 */
class StatisticsCalculator {
public:
    /**
     * @brief 计算向量平均值
     * @tparam T 数值类型
     * @param data 输入向量
     * @return T 平均值
     * 
     * 【C++语法】模板函数 ★★★
     * 一次编写，支持多种类型（float, double, int...）
     * 
     * 【C++17特性】[[nodiscard]] - 提醒调用者必须使用返回值
     * 防止错误：mean(data); // 警告：忽略返回值
     */
    template<typename T>
    [[nodiscard]] static T mean(const vector<T>& data) {
        // 【防御性编程】检查空向量
        if (data.empty()) {
            LOG_ERROR("计算平均值时输入向量为空");
            throw runtime_error("输入向量为空");
        }
        
        // 【同步计算示例】顺序累加
        T sum = T(0);
        for (const T& value : data) {
            sum += value;
        }
        
        return sum / static_cast<T>(data.size());
    }
    
    /**
     * @brief 简化版平均值计算（对比用）
     * 【同步版本】直接计算，阻塞当前线程
     */
    static float mean_sync(const vector<float>& data) {
        LOG_INFO("【同步】开始计算平均值");
        
        // 模拟计算耗时
        this_thread::sleep_for(chrono::milliseconds(Config::WORKER_SLEEP_MS));
        
        float sum = 0;
        for (float val : data) sum += val;
        
        LOG_INFO("【同步】计算完成");
        return data.empty() ? 0 : sum / data.size();
    }
    
    /**
     * @brief 异步版本平均值计算
     * 【异步版本】返回future，不阻塞当前线程
     */
    static future<float> mean_async(const vector<float>& data) {
        LOG_INFO("【异步】启动异步计算任务");
        
        // 【核心异步API】std::async ★★★
        // 参数1：launch::async - 立即在新线程执行
        // 参数2：lambda函数 - 要执行的任务
        // 参数3：data - 传递给lambda的参数
        
        return async(launch::async, [data]() {
            LOG_INFO("【异步工作线程】开始计算");
            
            // 模拟计算耗时（与同步版本相同）
            this_thread::sleep_for(chrono::milliseconds(Config::WORKER_SLEEP_MS));
            
            float sum = 0;
            for (float val : data) sum += val;
            
            float result = data.empty() ? 0 : sum / data.size();
            LOG_INFO("【异步工作线程】计算完成: %f", result);
            
            return result;
        });
    }
};

// ==================== 异步处理器类 ====================
/**
 * @class AsyncVectorProcessor
 * @brief 异步向量处理器
 * 
 * 【设计模式】RAII（Resource Acquisition Is Initialization）★★★
 * 构造函数获取资源，析构函数释放资源
 * 确保异常安全：即使抛出异常，资源也会被正确释放
 */
class AsyncVectorProcessor {
private:
    atomic<bool> is_running_;           // 原子布尔，线程安全
    future<float> async_result_;        // 【核心】future对象，异步结果占位符
    
public:
    AsyncVectorProcessor() noexcept : is_running_(false) {
        LOG_INFO("异步处理器创建");
    }
    
    ~AsyncVectorProcessor() {
        // 【RAII模式】析构函数自动清理
        try {
            if (is_running_) {
                LOG_INFO("等待异步任务完成...");
                // 等待任务完成（最多等待1秒）
                if (async_result_.valid()) {
                    auto status = async_result_.wait_for(chrono::seconds(1));
                    if (status == future_status::timeout) {
                        LOG_ERROR("异步任务超时");
                    }
                }
            }
        } catch (...) {
            // 析构函数不抛出异常
            LOG_ERROR("析构函数中发生异常");
        }
    }
    
    /**
     * @brief 启动异步计算
     * 
     * 【异步编程三步曲】★★★
     * 1. 启动异步任务（不阻塞）
     * 2. 主线程继续其他工作
     * 3. 需要时获取结果（可能阻塞）
     */
    void start_async_computation(const vector<float>& data) {
        if (is_running_.exchange(true)) {
            throw runtime_error("已有异步任务在运行");
        }
        
        LOG_INFO("步骤1: 启动异步任务");
        
        // 【核心代码】启动异步任务
        async_result_ = async(launch::async, [this, data]() {
            // 这个lambda函数在新线程中执行
            thread::id worker_id = this_thread::get_id();
            LOG_INFO("异步任务在工作线程[%ld]中开始", worker_id);
            
            // 模拟耗时计算
            this_thread::sleep_for(chrono::milliseconds(Config::WORKER_SLEEP_MS));
            
            // 实际计算
            float sum = 0;
            for (float val : data) sum += val;
            float result = data.empty() ? 0 : sum / data.size();
            
            LOG_INFO("异步任务完成，结果: %f", result);
            return result;
        });
        
        LOG_INFO("步骤1完成: 异步任务已启动，主线程继续执行");
    }
    
    /**
     * @brief 获取异步结果
     * 
     * 【关键区别】同步vs异步 ★★★
     * 同步：调用函数时立即等待结果
     * 异步：需要结果时才等待（通过future.get()）
     */
    [[nodiscard]] pair<bool, float> get_result() {
        if (!is_running_) {
            return {false, Config::EMPTY_VECTOR_VALUE};
        }
        
        try {
            LOG_INFO("步骤3: 获取异步结果");
            
            // 【关键阻塞点】如果任务未完成，这里会等待
            // 如果任务已完成，立即返回
            float result = async_result_.get();
            
            is_running_ = false;
            return {true, result};
            
        } catch (const exception& e) {
            LOG_ERROR("获取异步结果异常: %s", e.what());
            is_running_ = false;
            return {false, Config::EMPTY_VECTOR_VALUE};
        }
    }
    
    /**
     * @brief 演示同步与异步对比
     * 
     * 【学习重点】直观理解同步和异步的区别
     */
    static void demo_sync_vs_async() {
        vector<float> test_data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        
        cout << "\n========== 同步执行示例 ==========\n";
        {
            auto start = chrono::steady_clock::now();
            
            cout << "开始同步计算...\n";
            float result = StatisticsCalculator::mean_sync(test_data);
            
            cout << "同步计算完成，耗时: " 
                << chrono::duration_cast<chrono::milliseconds>(
                    chrono::steady_clock::now() - start).count()
                << "ms\n";
            cout << "结果: " << result << "\n";
            
            // 注意：主线程在计算期间完全阻塞
            cout << "同步特点：主线程等待计算完成\n";
        }
        
        cout << "\n========== 异步执行示例 ==========\n";
        {
            auto start = chrono::steady_clock::now();
            
            cout << "开始异步计算...\n";
            future<float> fut = StatisticsCalculator::mean_async(test_data);
            
            // 主线程继续执行其他工作
            cout << "主线程继续其他工作...\n";
            this_thread::sleep_for(chrono::milliseconds(200));
            cout << "主线程工作完成\n";
            
            // 需要结果时等待
            float result = fut.get();
            
            cout << "异步总耗时: " 
                << chrono::duration_cast<chrono::milliseconds>(
                    chrono::steady_clock::now() - start).count()
                << "ms\n";
            cout << "结果: " << result << "\n";
            
            cout << "异步特点：计算与主线程工作并行执行\n";
        }
    }
};

// ==================== 主程序 ====================
int main() {
    // 【C++性能优化】I/O加速
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);
    
    cout << "========================================\n";
    cout << "异步编程教学示例 - 面向大一学生\n";
    cout << "========================================\n\n";
    
    // ========== 学习阶段1：理解同步 ==========
    cout << "【学习阶段1】理解同步编程\n";
    cout << "同步 = 顺序执行，一个接一个\n\n";
    
    {
        cout << "同步示例：煮面条\n";
        cout << "1. 烧水（等待水开）\n";
        cout << "2. 下面条（等待面熟）\n";
        cout << "3. 准备调料\n";
        cout << "特点：必须等上一步完成才能做下一步\n\n";
    }
    
    // ========== 学习阶段2：理解异步 ==========
    cout << "【学习阶段2】理解异步编程\n";
    cout << "异步 = 并行执行，多个任务同时进行\n\n";
    
    {
        cout << "异步示例：煮面条（高效版）\n";
        cout << "1. 烧水（开始烧水后不等待）\n";
        cout << "2. 同时准备调料\n";
        cout << "3. 水开后下面条\n";
        cout << "特点：多个任务并行，总时间更短\n\n";
    }
    
    // ========== 学习阶段3：同步vs异步演示 ==========
    AsyncVectorProcessor::demo_sync_vs_async();
    
    // ========== 学习阶段4：完整异步示例 ==========
    cout << "\n【学习阶段4】完整异步程序\n";
    
    // 准备数据
    vector<float> data;
    data.reserve(Config::VECTOR_SIZE);
    
    ThreadSafeRandom rng(0, Config::RANDOM_MAX);
    for (size_t i = 0; i < Config::VECTOR_SIZE; ++i) {
        data.push_back(static_cast<float>(rng()) / Config::RANDOM_MAX);
    }
    
    // 创建异步处理器
    AsyncVectorProcessor processor;
    
    cout << "\n========== 异步计算流程 ==========\n";
    
    // 步骤1：启动异步任务（不阻塞）
    cout << "【主线程】步骤1: 启动异步任务\n";
    cout << "注意：这里不会等待，立即继续执行\n";
    
    processor.start_async_computation(data);
    
    // 步骤2：主线程并行工作
    cout << "\n【主线程】步骤2: 执行其他工作\n";
    cout << "模拟主线程有 " << Config::MAIN_SLEEP_SEC << " 秒工作\n";
    
    for (int i = 1; i <= Config::MAIN_SLEEP_SEC; ++i) {
        cout << "  主线程工作第 " << i << " 秒...\n";
        this_thread::sleep_for(chrono::seconds(1));
    }
    
    // 步骤3：获取异步结果
    cout << "\n【主线程】步骤3: 获取异步结果\n";
    cout << "如果异步任务未完成，这里会等待\n";
    cout << "如果已完成，立即返回结果\n";
    
    auto [success, result] = processor.get_result();
    
    if (success) {
        cout << "\n异步计算成功！结果: " << result << "\n";
    }
    
    // ========== 学习阶段5：关键概念总结 ==========
    cout << "\n【学习总结】关键概念对比\n";
    cout << string(40, '-') << "\n";
    
    cout << left << setw(25) << "特性" 
        << left << setw(15) << "同步" 
        << left << setw(15) << "异步" << "\n";
    cout << string(55, '-') << "\n";
    
    cout << left << setw(25) << "执行方式" 
        << left << setw(15) << "顺序执行" 
        << left << setw(15) << "并行执行" << "\n";
    
    cout << left << setw(25) << "阻塞性" 
        << left << setw(15) << "阻塞" 
        << left << setw(15) << "非阻塞" << "\n";
    
    cout << left << setw(25) << "CPU利用率" 
        << left << setw(15) << "低" 
        << left << setw(15) << "高" << "\n";
    
    cout << left << setw(25) << "代码复杂度" 
        << left << setw(15) << "简单" 
        << left << setw(15) << "复杂" << "\n";
    
    cout << left << setw(25) << "适用场景" 
        << left << setw(15) << "简单任务" 
        << left << setw(15) << "耗时任务" << "\n";
    
    // ========== 学习阶段6：C++语法要点 ==========
    cout << "\n【C++语法要点回顾】\n";
    
    cout << "\n1. Lambda表达式（匿名函数）\n";
    cout << "   [](参数){ 函数体 } // 最简单的lambda\n";
    cout << "   [&data] 捕获外部变量data的引用\n";
    cout << "   用途：快速定义函数，常用于异步任务\n\n";
    
    cout << "2. std::async 和 std::future\n";
    cout << "   future<float> fut = async(任务); // 启动异步\n";
    cout << "   float result = fut.get();        // 获取结果\n\n";
    
    cout << "3. auto 类型推导\n";
    cout << "   auto x = 10;       // x被推导为int\n";
    cout << "   auto& ref = data;  // ref被推导为引用\n";
    cout << "   简化代码，避免冗长类型声明\n\n";
    
    cout << "4. 范围for循环\n";
    cout << "   for (float val : data) { // 遍历vector\n";
    cout << "       sum += val;\n";
    cout << "   }\n";
    cout << "   比传统for循环更简洁\n\n";
    
    cout << "5. 结构化绑定（C++17）\n";
    cout << "   auto [success, result] = func();\n";
    cout << "   同时接收多个返回值，替代pair/tuple\n\n";
    
    // ========== 学习阶段7：进一步学习建议 ==========
    cout << "【进一步学习建议】\n";
    cout << "1. 修改Config中的时间参数，观察程序行为变化\n";
    cout << "2. 尝试将异步任务改为抛出异常，观察异常处理\n";
    cout << "3. 实现多个异步任务并行执行\n";
    cout << "4. 学习线程池（thread pool）避免频繁创建线程\n";
    cout << "5. 了解生产者-消费者模式\n";
    
    cout << "\n========================================\n";
    cout << "异步编程入门完成！\n";
    cout << "========================================\n";
    
    return 0;
}

// 静态成员初始化（C++语法要求）
thread_local mt19937 ThreadSafeRandom::generator_;
thread_local bool ThreadSafeRandom::initialized_ = false;

#endif // ASYNC_VECTOR_MEAN_H