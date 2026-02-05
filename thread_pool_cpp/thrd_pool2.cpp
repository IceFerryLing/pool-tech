#include <iostream>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <functional>
#include <future>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <stdexcept>

// ============================================================================
// 前置声明：自定义任务结构体
// ============================================================================

/**
 * @struct TaskFunc
 * @brief 线程池任务包装器
 * @note 线程池需要统一的任务类型，但实际任务可能有不同参数和返回值。
 *       所以我们用std::function<void()>来统一接口：无论原始任务是什么，
 *       都包装成无参数、无返回值的函数对象（void()）。
 */
struct TaskFunc {
    // C++11新特性：std::function是可调用对象的通用包装器
    // 可以存储函数指针、lambda、成员函数指针、函数对象等
    std::function<void()> _func;  ///< 实际执行的任务（已消除参数和返回值差异）
};

/// 智能指针类型别名，简化代码并自动管理内存
/// C++11新特性：using比typedef更强大，可以定义模板别名
using TaskFuncPtr = std::shared_ptr<TaskFunc>;

// ============================================================================
// 线程池核心类
// ============================================================================

/**
 * @class ThreadPool
 * @brief 线程池类，管理一组工作线程并执行提交的任务
 * 
 * @设计思路：
 * 1. 生产者-消费者模式：主线程提交任务（生产者），工作线程执行任务（消费者）
 * 2. 类型擦除：通过std::function和模板，支持任意类型的任务
 * 3. 异步结果：通过std::future/promise模式，获取任务执行结果
 * 4. 线程安全：使用互斥锁和条件变量保护共享资源
 * 5. 优雅关闭：支持等待所有任务完成后安全关闭
 * 
 * @核心改进（相比简单版本）：
 * 1. 添加线程池状态管理（运行中、正在关闭、已关闭）
 * 2. 添加线程池优雅关闭功能
 * 3. 添加队列容量限制，避免无限制增长
 * 4. 添加等待所有任务完成的功能
 * 5. 添加异常处理，提高健壮性
 * 
 * @工作流程：
 * 主线程调用exec()提交任务 → 任务包装后入队 → 唤醒工作线程 → 
 * 工作线程从队列取任务 → 执行任务 → 可通过future获取结果
 */
class ThreadPool {
private:
    std::mutex _mutex;                          ///< 互斥锁：保护共享资源的线程安全访问
    std::condition_variable _conditionConsumer; ///< 条件变量：消费者等待（队列不空）
    std::condition_variable _conditionProducer; ///< 条件变量：生产者等待（队列不满）
    std::queue<TaskFuncPtr> _tasks;             ///< 任务队列：存储待执行的TaskFuncPtr
    
    std::vector<std::thread> _workers;          ///< 工作线程集合（C++11新特性）
    std::atomic<bool> _shutdown;                ///< 关闭标志（原子操作，线程安全）
    std::atomic<int> _activeTasks;              ///< 活跃任务计数（正在执行的任务数）
    
    size_t _maxQueueSize;                       ///< 任务队列最大容量
    size_t _threadCount;                        ///< 线程数量

public:
    /**
     * @brief 构造函数，创建线程池
     * 
     * @param threadNum 工作线程数量，默认4个
     * @param maxQueueSize 任务队列最大容量，默认1000
     * 
     * @技术点：
     * 1. 成员初始化列表：C++11推荐使用初始化列表初始化成员变量
     * 2. atomic类型：C++11原子操作，保证多线程安全访问
     * 3. vector::reserve：预分配内存，避免多次分配
     * 
     * @注意：构造函数中直接启动所有工作线程
     */
    ThreadPool(size_t threadNum = 4, size_t maxQueueSize = 1000)
        : _shutdown(false)
        , _activeTasks(0)
        , _maxQueueSize(maxQueueSize)
        , _threadCount(threadNum) {
        
        printf("[ThreadPool] 创建线程池：%zu个线程，队列容量%zu\n", threadNum, maxQueueSize);
        
        // 预分配线程对象内存
        _workers.reserve(threadNum);
        
        // 创建并启动所有工作线程
        for (size_t i = 0; i < threadNum; ++i) {
            // 使用emplace_back直接在容器中构造对象，避免临时对象（C++11新特性）
            _workers.emplace_back(&ThreadPool::workerThread, this, i);
        }
    }

    /**
     * @brief 析构函数，自动关闭线程池
     * 
     * @注意：确保线程池正确关闭，避免资源泄漏
     */
    ~ThreadPool() {
        if (!_shutdown.load()) {
            shutdown();
        }
    }

    /**
     * @brief 通用任务提交接口（模板函数）
     * 
     * @tparam F 任务函数类型（自动推导）
     * @tparam Args 任务函数参数类型包（可变参数模板，C++11新特性）
     * @param f 任务函数（万能引用，保持值类别）
     * @param args 任务函数参数（万能引用，保持值类别）
     * @return std::future<RetType> 用于获取异步执行结果的future对象
     * 
     * @技术要点：
     * 1. 万能引用（Universal Reference）：使用&&和类型推导，可接受左值/右值
     * 2. 完美转发（Perfect Forwarding）：std::forward保持参数原始类型
     * 3. 尾返回类型（Trailing Return Type）：-> decltype(...) 自动推导返回类型
     * 4. std::packaged_task：将任务与future绑定，支持异步获取结果
     * 5. std::bind：将带参数的任务转换为无参函数（参数绑定）
     * 
     * @异常处理：
     * 1. 线程池已关闭时抛出std::runtime_error
     * 2. 队列满时阻塞等待，直到有空位
     * 
     * @示例用法：
     * @code{.cpp}
     * // 提交普通函数
     * auto f1 = pool.exec(func, 1, 2.0);
     * // 提交lambda表达式
     * auto f2 = pool.exec([](int x){ return x*2; }, 10);
     * // 提交成员函数
     * auto f3 = pool.exec(&MyClass::method, &obj, arg1, arg2);
     * @endcode
     */    
    template <class F, class... Args>
    auto exec(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        // 第一步：检查线程池状态
        if (_shutdown.load()) {
            throw std::runtime_error("[ThreadPool] 错误：线程池已关闭，无法提交新任务");
        }
        
        // 第二步：推导任务函数的返回值类型
        // decltype是C++11的类型推导关键字，在编译期计算表达式的类型
        using RetType = decltype(f(args...));
        
        // 第三步：创建packaged_task来包装任务
        // std::packaged_task是C++11新特性，它包装一个可调用对象，
        // 并允许异步获取其结果（通过关联的std::future）
        auto task = std::make_shared<std::packaged_task<RetType()>>(
            // std::bind将函数和参数绑定，生成一个新的无参可调用对象
            // std::forward是完美转发，保持参数的左值/右值属性
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        // 第四步：获取与任务关联的future对象（在锁外创建，避免长时间持有锁）
        std::future<RetType> future = task->get_future();
        
        // 第五步：将packaged_task适配为线程池的统一任务格式
        TaskFuncPtr fPtr = std::make_shared<TaskFunc>();
        
        // 使用C++11的lambda表达式来捕获task智能指针
        fPtr->_func = [task, this]() {
            // 增加活跃任务计数
            _activeTasks++;
            
            try {
                // 执行实际的任务函数
                printf("[ThreadPool] 开始执行任务...\n");
                (*task)();
                printf("[ThreadPool] 任务执行完成\n");
            } catch (...) {
                // 捕获所有异常，避免工作线程异常退出
                printf("[ThreadPool] 警告：任务执行过程中发生异常\n");
            }
            
            // 减少活跃任务计数
            _activeTasks--;
        };
        
        // 第六步：线程安全地将任务加入队列（生产者部分）
        std::unique_lock<std::mutex> lock(_mutex);
        
        // 等待队列有空位（生产者等待条件）
        // 条件：队列未满 或 线程池已关闭
        _conditionProducer.wait(lock, [this]() {
            return _tasks.size() < _maxQueueSize || _shutdown.load();
        });
        
        // 再次检查线程池状态（可能在等待期间被关闭）
        if (_shutdown.load()) {
            throw std::runtime_error("[ThreadPool] 错误：线程池已关闭，无法提交新任务");
        }
        
        // 将任务加入队列
        _tasks.push(fPtr);
        
        // 通知一个等待的消费者线程（有新任务了）
        _conditionConsumer.notify_one();
        
        // 返回future对象，让调用者可以获取任务结果
        return future;
    }

    /**
     * @brief 获取线程池中当前活跃的任务数量
     * 
     * @return int 正在执行的任务数量
     * 
     * @技术点：
     * atomic::load()：原子加载操作，线程安全
     */
    int activeTaskCount() const {
        return _activeTasks.load();
    }

    /**
     * @brief 获取任务队列中的待处理任务数量
     * 
     * @return size_t 队列中的任务数量
     * 
     * @注意：需要在锁保护下访问共享数据
     */
    size_t pendingTaskCount() const {
        std::unique_lock<std::mutex> lock(std::mutex);
        return _tasks.size();
    }

    /**
     * @brief 获取线程池中的线程数量
     * 
     * @return size_t 工作线程数量
     */
    size_t threadCount() const {
        return _threadCount;
    }

    /**
     * @brief 等待所有任务完成
     * 
     * @流程：
     * 1. 等待队列为空
     * 2. 等待所有活跃任务完成
     * 
     * @注意：这个方法不会关闭线程池，只是等待当前任务完成
     */
    void waitAll() {
        // 等待队列为空
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _conditionConsumer.wait(lock, [this]() {
                return _tasks.empty();
            });
        }
        
        // 等待所有活跃任务完成
        while (_activeTasks.load() > 0) {
            // 使用yield让出CPU，避免忙等待
            std::this_thread::yield();
        }
        
        printf("[ThreadPool] 所有任务已完成\n");
    }

    /**
     * @brief 优雅关闭线程池
     * 
     * @流程：
     * 1. 设置关闭标志
     * 2. 通知所有等待的线程（生产者和消费者）
     * 3. 等待所有工作线程结束
     * 4. 清理资源
     * 
     * @注意：
     * 1. 关闭后不能再提交新任务
     * 2. 会等待所有已提交的任务完成
     */
    void shutdown() {
        printf("[ThreadPool] 开始关闭线程池...\n");
        
        // 第一步：设置关闭标志
        _shutdown.store(true);
        
        // 第二步：通知所有等待的线程
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _conditionConsumer.notify_all();  // 唤醒所有消费者
            _conditionProducer.notify_all();  // 唤醒所有生产者
        }
        
        // 第三步：等待所有工作线程结束
        for (std::thread& worker : _workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        
        // 第四步：清空工作线程集合
        _workers.clear();
        
        printf("[ThreadPool] 线程池已关闭\n");
    }

    /**
     * @brief 立即关闭线程池（不等待任务完成）
     * 
     * @警告：这个方法会丢弃队列中未执行的任务
     * 
     * @流程：
     * 1. 设置关闭标志
     * 2. 清空任务队列
     * 3. 通知所有线程
     * 4. 等待线程结束
     */
    void shutdownNow() {
        printf("[ThreadPool] 立即关闭线程池（丢弃未执行任务）...\n");
        
        // 第一步：设置关闭标志
        _shutdown.store(true);
        
        // 第二步：清空任务队列
        {
            std::unique_lock<std::mutex> lock(_mutex);
            // 使用swap技巧清空队列（C++11高效清空队列的方法）
            std::queue<TaskFuncPtr> emptyQueue;
            std::swap(_tasks, emptyQueue);
            
            // 通知所有等待的线程
            _conditionConsumer.notify_all();
            _conditionProducer.notify_all();
        }
        
        // 第三步：等待所有工作线程结束
        for (std::thread& worker : _workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        
        // 第四步：清空工作线程集合
        _workers.clear();
        
        printf("[ThreadPool] 线程池已立即关闭\n");
    }

private:
    /**
     * @brief 工作线程的主循环函数
     * 
     * @param id 线程ID（用于调试和日志）
     * 
     * @流程：
     * 1. 等待条件变量（有任务时被唤醒）
     * 2. 从任务队列取出任务
     * 3. 执行任务
     * 4. 循环继续，直到线程池关闭
     * 
     * @注意：
     * 1. 使用while循环避免虚假唤醒
     * 2. 执行任务在锁外进行，避免长时间持有锁
     * 3. 捕获任务执行中的异常，避免线程退出
     */
    void workerThread(size_t id) {
        printf("[ThreadPool] 工作线程%zu启动\n", id);
        
        // 无限循环，直到线程池关闭
        while (true) {
            TaskFuncPtr task;
            
            {
                // 使用unique_lock配合条件变量
                std::unique_lock<std::mutex> lock(_mutex);
                
                // 条件变量的wait方法：
                // 1. 如果谓词条件满足，直接继续执行
                // 2. 如果条件不满足，释放锁并阻塞，直到被notify唤醒
                // 3. 唤醒后重新获取锁，再次检查条件
                _conditionConsumer.wait(lock, [this]() { 
                    return !_tasks.empty() || _shutdown.load();
                });
                
                // 检查是否需要退出（线程池关闭且队列为空）
                if (_shutdown.load() && _tasks.empty()) {
                    printf("[ThreadPool] 工作线程%zu退出\n", id);
                    return;
                }
                
                // 从队列头部取出任务
                task = _tasks.front();
                _tasks.pop();
                
                // 通知生产者队列有空位了
                _conditionProducer.notify_one();
            }
            
            // 执行任务（在锁外执行，避免长时间持有锁）
            if (task) {
                task->_func();
            }
        }
    }
};

// ============================================================================
// 测试用例
// ============================================================================

/**
 * @brief 测试函数1：带多个参数和返回值的普通函数
 * 
 * @param a 整数参数
 * @param b 字符串参数
 * @param c 字符串参数
 * @return int 计算结果
 * 
 * @演示：线程池可以处理任意参数类型和返回类型的普通函数
 */
int func3(int a, std::string b, std::string c) {
    std::cout << "[任务执行] func3() 被调用: a=" << a 
            << ", b=" << b << ", c=" << c << std::endl;
    // 模拟一些计算工作
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return a + 100;
}

/**
 * @brief 测试函数2：lambda表达式
 * 
 * @演示：线程池可以处理lambda表达式，这是C++11的重要特性
 */
auto lambda_task = [](int x, int y) -> std::string {
    std::cout << "[任务执行] lambda表达式: " << x << " + " << y << std::endl;
    return "结果是: " + std::to_string(x + y);
};

/**
 * @brief 测试函数3：模拟耗时任务
 */
auto long_task = [](int id, int duration_ms) -> int {
    std::cout << "[长任务" << id << "] 开始执行，预计耗时" << duration_ms << "ms" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    std::cout << "[长任务" << id << "] 执行完成" << std::endl;
    return id * 100;
};

int main() {
    std::cout << "========== C++11完整线程池示例程序 ==========" << std::endl;
    std::cout << "演示特性: 模板、lambda、智能指针、future、条件变量、原子操作" << std::endl;
    
    // 第一步：创建线程池（使用RAII，自动管理生命周期）
    // 创建包含2个线程，队列容量为5的线程池
    ThreadPool threadpool(2, 5);
    
    // 第二步：提交不同类型任务到线程池
    
    // 任务1：提交普通函数（演示模板参数推导）
    std::cout << "\n[主线程] 提交任务1（普通函数）..." << std::endl;
    auto fut1 = threadpool.exec(func3, 20, "darren", "beibei");
    
    // 任务2：提交lambda表达式（演示lambda的线程池使用）
    std::cout << "[主线程] 提交任务2（lambda表达式）..." << std::endl;
    auto fut2 = threadpool.exec([](int x, int y) {
        std::cout << "[任务执行] 匿名lambda: " << x << " * " << y << std::endl;
        return x * y;
    }, 5, 6);
    
    // 任务3：使用已定义的lambda表达式
    std::cout << "[主线程] 提交任务3（预定义lambda）..." << std::endl;
    auto fut3 = threadpool.exec(lambda_task, 100, 200);
    
    // 第三步：获取任务结果（演示std::future的使用）
    
    // future::get() 是阻塞调用，会等待任务执行完成
    std::cout << "\n[主线程] 等待任务1结果..." << std::endl;
    int result1 = fut1.get();  // 阻塞直到任务1完成
    std::cout << "任务1结果: " << result1 << std::endl;
    
    std::cout << "[主线程] 等待任务2结果..." << std::endl;
    int result2 = fut2.get();
    std::cout << "任务2结果: " << result2 << std::endl;
    
    std::cout << "[主线程] 等待任务3结果..." << std::endl;
    std::string result3 = fut3.get();
    std::cout << "任务3结果: " << result3 << std::endl;
    
    // 第四步：演示多个任务并发执行
    std::cout << "\n[主线程] 提交5个并发任务..." << std::endl;
    std::vector<std::future<int>> futures;
    
    for (int i = 0; i < 5; ++i) {
        // 使用emplace_back直接构造future对象（C++11新特性）
        futures.emplace_back(
            threadpool.exec([i](int base) -> int {
                std::cout << "[并发任务" << i << "] 开始执行，基值=" << base << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(50 * (i + 1)));
                return base + i;
            }, 1000)  // 所有任务共享一个基值参数
        );
    }
    
    // 等待所有并发任务完成
    std::cout << "[主线程] 等待所有并发任务完成..." << std::endl;
    for (int i = 0; i < futures.size(); ++i) {
        std::cout << "任务" << i << "结果: " << futures[i].get() << std::endl;
    }
    
    // 第五步：演示线程池状态查询
    std::cout << "\n[主线程] 线程池状态查询：" << std::endl;
    std::cout << "工作线程数: " << threadpool.threadCount() << std::endl;
    std::cout << "活跃任务数: " << threadpool.activeTaskCount() << std::endl;
    std::cout << "待处理任务数: " << threadpool.pendingTaskCount() << std::endl;
    
    // 第六步：演示等待所有任务完成
    std::cout << "\n[主线程] 提交2个长耗时任务..." << std::endl;
    auto long_fut1 = threadpool.exec(long_task, 1, 500);
    auto long_fut2 = threadpool.exec(long_task, 2, 300);
    
    std::cout << "[主线程] 等待所有任务完成..." << std::endl;
    threadpool.waitAll();
    
    std::cout << "长任务1结果: " << long_fut1.get() << std::endl;
    std::cout << "长任务2结果: " << long_fut2.get() << std::endl;
    
    // 第七步：演示优雅关闭
    std::cout << "\n[主线程] 关闭线程池..." << std::endl;
    threadpool.shutdown();
    
    // 第八步：演示关闭后提交任务的异常处理
    std::cout << "\n[主线程] 尝试在关闭后提交任务（应该抛出异常）..." << std::endl;
    try {
        auto failed_fut = threadpool.exec([]() { return 42; });
    } catch (const std::exception& e) {
        std::cout << "捕获到预期异常: " << e.what() << std::endl;
    }
    
    std::cout << "\n========== 程序执行完成 ==========" << std::endl;
    
    return 0;
}

/**
 * @section C++11新特性总结
 * 
 * 本代码中使用的C++11新特性：
 * 
 * 1. 自动类型推导（auto）：让编译器推导变量类型
 *    auto fut = threadpool.exec(...);
 * 
 * 2. 尾返回类型（trailing return type）：
 *    auto func() -> std::future<decltype(...)>
 * 
 * 3. 基于范围的for循环（range-based for）：
 *    for (std::thread& worker : _workers) { ... }
 * 
 * 4. Lambda表达式：
 *    [task, this]() { (*task)(); }
 * 
 * 5. 智能指针（shared_ptr, unique_ptr）：
 *    std::make_shared<TaskFunc>()
 * 
 * 6. 移动语义和完美转发（std::move, std::forward）：
 *    std::forward<F>(f)
 * 
 * 7. 多线程支持（thread, mutex, condition_variable）：
 *    std::thread, std::mutex, std::condition_variable
 * 
 * 8. 异步操作（future, promise, packaged_task）：
 *    std::packaged_task, std::future
 * 
 * 9. 可变参数模板（variadic templates）：
 *    template <class F, class... Args>
 * 
 * 10. 类型推导（decltype）：
 *     decltype(f(args...))
 * 
 * 11. 初始化列表（initializer lists）：
 *     std::vector<std::future<int>> futures;
 * 
 * 12. nullptr：空指针常量
 * 
 * 13. 时间库（chrono）：
 *     std::this_thread::sleep_for(std::chrono::milliseconds(100))
 * 
 * 14. emplace_back：直接在容器中构造对象，避免临时对象
 * 
 * 15. 原子操作（atomic）：
 *     std::atomic<bool>, std::atomic<int>
 * 
 * 16. 线程本地存储（thread_local）：
 *     （本示例未使用，但C++11提供）
 * 
 * 17. 右值引用和移动语义：
 *     （在完美转发中使用）
 * 
 * 18. 异常处理增强：
 *     noexcept关键字（本示例未使用）
 * 
 * 19. 类型别名模板（using）：
 *     using TaskFuncPtr = std::shared_ptr<TaskFunc>;
 * 
 * 20. 统一的初始化语法：
 *     std::atomic<bool> _shutdown{false};
 * 
 * 通过学习这个完整的线程池实现，你可以全面了解现代C++的核心特性如何协同工作，
 * 构建类型安全、高效、易用且健壮的并发组件。
 */