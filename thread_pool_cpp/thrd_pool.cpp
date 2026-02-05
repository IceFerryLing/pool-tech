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
 * 
 * @核心成员：
 * - _tasks：任务队列（存储待执行的任务）
 * - _mutex, _condition：线程同步工具（保证队列线程安全和工作线程唤醒）
 * 
 * @工作流程：
 * 主线程调用exec()提交任务 → 任务包装后入队 → 唤醒工作线程 → 
 * 工作线程从队列取任务 → 执行任务 → 可通过future获取结果
 */
class ThreadPool {
private:
    std::mutex _mutex;                   ///< 互斥锁：保护任务队列的线程安全访问
    std::condition_variable _condition;  ///< 条件变量：工作线程的等待/唤醒机制
    std::queue<TaskFuncPtr> _tasks;      ///< 任务队列：存储待执行的TaskFuncPtr

public:
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
    auto exec(F&& f, Args&&... args) -> std::future<decltype(f(args...))>
    {
        // 第一步：推导任务函数的返回值类型
        // decltype是C++11的类型推导关键字，在编译期计算表达式的类型
        // 这里推导的是f(args...)这个函数调用的返回值类型
        using RetType = decltype(f(args...));
        
        // 第二步：创建packaged_task来包装任务
        // std::packaged_task是C++11新特性，它包装一个可调用对象，
        // 并允许异步获取其结果（通过关联的std::future）
        // 注意：packaged_task的模板参数是函数签名，这里是RetType()
        auto task = std::make_shared<std::packaged_task<RetType()>>(
            // std::bind将函数和参数绑定，生成一个新的无参可调用对象
            // std::forward是完美转发，保持参数的左值/右值属性
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        // 第三步：将packaged_task适配为线程池的统一任务格式
        // 线程池的任务队列只接受TaskFuncPtr（即std::function<void()>）
        // 所以我们需要做一层包装
        TaskFuncPtr fPtr = std::make_shared<TaskFunc>();
        
        // 这里使用了C++11的lambda表达式来捕获task智能指针
        // [task]：按值捕获task（实际上是捕获shared_ptr，增加引用计数）
        // -> void：lambda的返回类型，这里是无返回值
        fPtr->_func = [task](){
            // 实际执行任务的代码块
            printf("[ThreadPool] 开始执行任务...\n");
            
            // (*task)() 调用packaged_task对象，执行实际的任务函数
            // 任务函数的返回值会被packaged_task存储，可以通过future获取
            (*task)();
            
            printf("[ThreadPool] 任务执行完成\n");
        };
        
        // 第四步：线程安全地将任务加入队列
        // std::unique_lock是C++11的RAII锁，比std::lock_guard更灵活
        // 它在构造时加锁，析构时自动解锁，支持手动解锁和条件变量
        std::unique_lock<std::mutex> lock(_mutex);
        _tasks.push(fPtr);
        
        // 第五步：唤醒一个等待的工作线程
        // notify_one是条件变量的方法，唤醒一个正在wait的线程
        // 如果有多个线程在等待，只有一个会被唤醒（减少不必要的竞争）
        _condition.notify_one();
        
        // 第六步：返回future对象，让调用者可以获取任务结果
        // packaged_task::get_future()返回一个与任务结果关联的future对象
        // 调用者可以通过future::get()获取结果（会阻塞直到任务完成）
        return task->get_future();
    }

    // ============================================================================
    // 线程池的基础设施方法（简化版，保证代码可运行）
    // ============================================================================

    /**
     * @brief 工作线程的主循环函数
     * 
     * @流程：
     * 1. 等待条件变量（有任务时被唤醒）
     * 2. 从任务队列取出任务
     * 3. 执行任务
     * 4. 循环继续
     * 
     * @注意：这是一个简化的实现，实际生产环境需要处理线程池的优雅关闭
     *        和异常处理等。
     */
    void run(){
        // 无限循环，实际使用中应该有一个停止条件
        while (true) {
            TaskFuncPtr task;
            
            {
                // 使用unique_lock配合条件变量
                // 条件变量要求使用unique_lock而不是lock_guard
                // 因为wait()方法需要在等待时释放锁，唤醒后重新获取锁
                std::unique_lock<std::mutex> lock(_mutex);
                
                // 条件变量的wait方法：
                // 1. 如果谓词条件满足（!tasks.empty()），直接继续执行
                // 2. 如果条件不满足，释放锁并阻塞，直到被notify唤醒
                // 3. 唤醒后重新获取锁，再次检查条件
                // 这样可以避免虚假唤醒（spurious wakeup）问题
                _condition.wait(lock, [this]() { 
                    return !_tasks.empty(); 
                });
                
                // 从队列头部取出任务
                task = _tasks.front();
                _tasks.pop();
            }
            
            // 执行任务（注意：这里在锁外执行，避免长时间持有锁）
            task->_func();
        }
    }

    /**
     * @brief 启动线程池，创建指定数量的工作线程
     * 
     * @param threadNum 工作线程数量，默认4个
     * 
     * @技术点：
     * 1. std::thread：C++11的线程类，替代pthread等平台相关API
     * 2. detach()：分离线程，让线程在后台独立运行
     * 3. 线程函数绑定：std::thread(&ThreadPool::run, this)
     *    - &ThreadPool::run：成员函数指针
     *    - this：隐式的this指针，作为成员函数的第一个参数
     * 
     * @注意：实际项目中应该保存线程句柄，以便在析构时join等待线程结束
     */
    void start(int threadNum = 4) {
        printf("[ThreadPool] 启动线程池，创建%d个工作线程\n", threadNum);
        
        for (int i = 0; i < threadNum; ++i) {
            // 创建线程并立即分离（后台运行）
            std::thread(&ThreadPool::run, this).detach();
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

int main() {
    std::cout << "========== C++11线程池示例程序 ==========" << std::endl;
    std::cout << "演示特性: 模板、lambda、智能指针、future、条件变量" << std::endl;
    
    // 第一步：创建并启动线程池
    ThreadPool threadpool;
    threadpool.start(2);  // 启动2个工作线程
    
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
    
    std::cout << "\n========== 程序执行完成 ==========" << std::endl;
    
    // 注意：这个简化的线程池没有实现优雅关闭
    // 实际程序应该等待所有任务完成后再退出
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
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
 *    for (auto& fut : futures) { ... }
 * 
 * 4. Lambda表达式：
 *    [task]() { (*task)(); }
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
 * 通过学习这个线程池实现，你可以全面了解现代C++的核心特性如何协同工作，
 * 构建类型安全、高效、易用的并发组件。
 */