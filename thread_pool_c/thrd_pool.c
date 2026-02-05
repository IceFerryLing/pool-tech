/**
 * @file thrd_pool.c
 * @brief 线程池实现 - 生产者消费者模型
 * 
 * 学习目标（适合大一新生）：
 * 1. 理解多线程基本概念：线程创建、同步、通信
 * 2. 掌握生产者-消费者模型的实际应用
 * 3. 学习环形缓冲区的实现方法
 * 4. 理解互斥锁和条件变量的使用场景
 * 5. 掌握资源管理的正确方法（分配与释放）
 * 
 * 前置知识建议：
 * - C语言基础（结构体、指针、函数指针）
 * - 操作系统基本概念（进程、线程）
 * - 简单的数据结构（队列）
 * 
 * 警告：多线程编程容易出错！常见错误：
 * 1. 死锁：两个线程互相等待对方释放锁
 * 2. 竞态条件：多个线程同时访问共享数据
 * 3. 资源泄漏：忘记释放内存或销毁同步原语
 * 
 * @author IceFerring/baidu
 * @date 2025-12-07
 * @version 2.1
 */

#include <pthread.h>    // POSIX线程库，Linux/macOS可用
#include <stdint.h>     // 提供固定大小的整数类型
#include <stdlib.h>     // malloc/free等内存管理函数
#include "thrd_pool.h"

/**
 * @brief 任务结构体
 * 
 * 类比：就像快递员要送的包裹
 * - func: 包裹里写着"做什么"（比如：送货上门）
 * - arg:  包裹本身（比如：具体送什么货）
 * 
 * 新手提示：函数指针是C语言的强大特性，允许我们把函数当作数据传递
 */
typedef struct task_t {
    handler_pt func;     /**< 任务处理函数指针 - 决定"做什么" */
    void* arg;           /**< 任务参数 - 决定"对什么做" */
} task_t;

/**
 * @brief 任务队列结构体（环形缓冲区）
 * 
 * 为什么要用环形缓冲区？
 * 1. 避免频繁的内存分配释放（性能考虑）
 * 2. 固定大小，容易管理
 * 3. 头尾指针循环移动，空间利用率高
 * 
 * 环形缓冲区工作原理（想象一个圆形的传送带）：
 * 头指针head：指向下一个要处理的任务（出队位置）
 * 尾指针tail：指向下一个空闲位置（入队位置）
 * 计数器count：当前队列中有多少任务
 * 
 * 新手常见问题：为什么用%取模运算？
 * 答：当指针到达数组末尾时，通过取模让它回到开头，形成环形
 */
typedef struct task_queue_t{
    uint32_t head;       /**< 队列头部索引（出队位置） */
    uint32_t tail;       /**< 队列尾部索引（入队位置） */
    uint32_t count;      /**< 当前任务数量 */
    task_t* queue;       /**< 任务数组 - 固定大小的连续内存 */
}task_queue_t;

/**
 * @brief 线程池主结构体
 * 
 * 类比：一个工厂的车间
 * - threads: 工人（多个工作线程）
 * - task_queue: 工作台（放待处理的任务）
 * - mutex: 车间的门锁（一次只能进一个人）
 * - condition: 车间里的铃铛（有任务时响铃通知工人）
 * 
 * 多线程核心概念：
 * 1. 共享资源：task_queue是多个线程都要访问的，需要保护
 * 2. 同步机制：mutex（互斥锁）保护共享资源，condition（条件变量）协调线程工作
 */
typedef struct thread_pool_t{
    pthread_mutex_t mutex;        /**< 互斥锁 - 保护共享资源的"门锁" */
    pthread_cond_t condition;     /**< 条件变量 - 线程间的"通信铃铛" */
    pthread_t *threads;           /**< 工作线程ID数组 - 所有工人的工号 */
    task_queue_t* task_queue;     /**< 任务队列指针 - 车间的工作台 */

    int closed;                   /**< 线程池关闭标志：类似"工厂下班通知" */
    int started;                  /**< 已启动的线程数：实际在岗工人数 */

    int thrd_count;               /**< 线程总数：计划雇佣的工人数 */
    int queue_size;               /**< 队列容量：工作台能放多少个任务 */
}thread_pool_t;

/**
 * @brief 工作线程入口函数
 * 
 * 每个工作线程的生命周期：
 * 1. 等待任务（睡觉）
 * 2. 获取任务（醒来拿任务）
 * 3. 执行任务（干活）
 * 4. 回到第1步（继续睡觉）
 * 5. 直到收到"下班通知"（closed=1）
 * 
 * 重点理解：pthread_cond_wait 的三步曲
 * 1. 释放mutex（让别人能进车间）
 * 2. 等待condition信号（睡觉等铃铛响）
 * 3. 被唤醒后重新获取mutex（醒来到门口排队）
 * 
 * 为什么要在循环中检查条件？
 * 答：因为有"虚假唤醒"的可能（铃铛可能意外响，不是因为真有任务）
 * 
 * @param arg 线程池指针
 */
void* thread_worker(void* arg){
    thread_pool_t* pool = (thread_pool_t *)arg;
    task_queue_t* que;
    task_t task;
    
    // 无限循环直到线程池关闭
    while(1){
        // 第一步：获取锁（进入车间）
        pthread_mutex_lock(&(pool->mutex));
        que = pool->task_queue;

        /**
         * 第二步：等待条件满足
         * 条件1：队列不为空（有活干）
         * 条件2：线程池没关闭（没下班）
         * 
         * 注意：这里用while不是if！
         * 原因：被唤醒后需要重新检查条件是否真正满足
         */
        while(que->count == 0 && !pool->closed){
            // 释放锁并等待信号（去休息室睡觉）
            pthread_cond_wait(&(pool->condition), &(pool->mutex));
        }
        
        // 第三步：检查是否该下班了
        if(pool->closed == 1){
            break;  // 跳出循环，准备下班
        }
        
        // 第四步：从队列取任务（从工作台拿一个包裹）
        task = que->queue[que->head];
        que->head = (que->head + 1) % pool->queue_size;  // 头指针后移
        que->count--;  // 任务数减1
        
        // 第五步：释放锁（离开车间，让别人可以进来）
        pthread_mutex_unlock(&(pool->mutex));
        
        // 第六步：执行任务（在外面送货，不占用车间）
        (*(task.func))(task.arg);
    }
    
    // 线程退出前的清理工作
    pool->started--;  // 登记：一个工人下班了
    pthread_mutex_unlock(&(pool->mutex));  // 重要！退出前必须释放锁
    pthread_exit(NULL);
    return NULL;
}

/**
 * @brief 创建线程池
 * 
 * 初始化顺序很重要（避免竞态条件）：
 * 1. 分配内存
 * 2. 初始化数据
 * 3. 初始化锁和条件变量
 * 4. 最后创建线程
 * 
 * 为什么最后创建线程？
 * 答：如果先创建线程，它们可能访问未初始化的数据
 * 
 * 内存管理口诀：
 * 1. 谁分配，谁释放
 * 2. 分配后检查NULL
 * 3. 失败时清理已分配的资源
 * 
 * @param thrd_count 线程数（建议：CPU核心数+1）
 * @param queue_size 队列大小（太小容易满，太大占用内存）
 */
thread_pool_t *thread_pool_create(int thrd_count, int queue_size){
    thread_pool_t* pool;
    
    // 防御性编程：检查参数有效性
    if (thrd_count <= 0 || queue_size <= 0) {
        return NULL;
    }

    // 分配主结构体
    pool = (thread_pool_t*)malloc(sizeof(*pool));
    if (pool == NULL){
        return NULL;  // 分配失败
    }

    // 初始化基本字段（重要：先把所有字段设为安全值）
    pool->closed = 0;      // 0表示运行中
    pool->started = 0;     // 还没有启动线程
    pool->thrd_count = 0;  // 当前线程数为0
    pool->queue_size = queue_size;

    // 分配任务队列结构体
    pool->task_queue = (task_queue_t*)malloc(sizeof(task_queue_t));
    if (pool->task_queue == NULL) {
        free(pool);  // 清理：释放已分配的内存
        return NULL;
    }
    
    // 初始化任务队列
    pool->task_queue->head = 0;
    pool->task_queue->tail = 0;
    pool->task_queue->count = 0;
    
    // 分配任务数组（连续内存块）
    pool->task_queue->queue = (task_t*)malloc(sizeof(task_t) * queue_size);
    if(pool->task_queue->queue == NULL){
        thread_pool_free(pool);
        return NULL;
    }

    // 分配线程ID数组
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * thrd_count);
    if (pool->threads == NULL){
        thread_pool_free(pool);
        return NULL;
    } 

    /**
     * 初始化同步原语（先于线程创建）
     * 类比：先装好门锁和铃铛，再让工人进来
     */
    pthread_mutex_init(&(pool->mutex), NULL);   // 初始化互斥锁
    pthread_cond_init(&(pool->condition), NULL); // 初始化条件变量
    
    // 创建所有工作线程
    for (int i = 0; i < thrd_count; i++){
        /**
         * pthread_create参数解释：
         * 1. &(pool->threads[i])：存储新线程ID的位置
         * 2. NULL：使用默认线程属性
         * 3. thread_worker：线程入口函数
         * 4. (void*)pool：传递给线程的参数
         */
        if (pthread_create(&(pool->threads[i]), NULL, thread_worker, (void*)pool) != 0){
            // 创建失败：销毁已创建的部分
            thread_pool_destroy(pool);
            return NULL;
        }
        pool->thrd_count++;
        pool->started++;
    }
    
    return pool;
}

/**
 * @brief 释放线程池资源
 * 
 * 释放顺序很重要（与分配顺序相反）：
 * 1. 停止所有线程（通过设置closed标志）
 * 2. 等待线程退出（join）
 * 3. 销毁同步原语
 * 4. 释放内存
 * 
 * 内存泄漏检查技巧：
 * 1. 每次malloc都要有对应的free
 * 2. 注意嵌套结构体的释放顺序
 * 3. 指针置NULL防止重复释放
 */
static int thread_pool_free(thread_pool_t* pool){
    if(pool == NULL){
        return -1;
    }

    // 释放线程数组
    if(pool->threads){
        free(pool->threads);
        pool->threads = NULL;  // 防止野指针
    }
    
    // 销毁同步原语（必须在释放内存前）
    if(pthread_mutex_destroy(&(pool->mutex)) != 0){
        // 处理错误（实际项目中应该记录日志）
    }
    if(pthread_cond_destroy(&(pool->condition)) != 0){
        // 处理错误
    }
    
    // 释放任务队列（注意嵌套结构）
    if(pool->task_queue){
        if(pool->task_queue->queue){
            free(pool->task_queue->queue);
            pool->task_queue->queue = NULL;
        }
        free(pool->task_queue);
        pool->task_queue = NULL;
    }
    
    // 最后释放主结构体
    free(pool);
    return 0;
}

/**
 * @brief 销毁线程池
 * 
 * 线程池销毁的正确流程：
 * 1. 设置关闭标志（告诉所有线程准备下班）
 * 2. 广播通知所有等待的线程（摇铃铛叫醒所有人）
 * 3. 等待所有线程退出（等所有工人离开）
 * 4. 清理资源（关灯锁门）
 * 
 * 死锁风险：如果忘记唤醒等待的线程，它们会永远等待！
 */
int thread_pool_destroy(thread_pool_t* pool){
    if(pool == NULL){
        return -1;
    }

    // 第一步：获取锁（进入车间）
    if(pthread_mutex_lock(&(pool->mutex)) != 0){
        return -2;  // 获取锁失败
    }

    // 检查是否已经关闭过（防止重复关闭）
    if(pool->closed == 1){
        pthread_mutex_unlock(&(pool->mutex));  // 必须先解锁！
        return -3;
    }

    // 第二步：设置关闭标志（贴出下班通知）
    pool->closed = 1;

    // 第三步：广播通知所有等待的线程（大声喊：下班了！）
    if(pthread_cond_broadcast(&(pool->condition)) != 0){
        pthread_mutex_unlock(&(pool->mutex));
        return -2;
    }
    
    // 第四步：释放锁（离开车间）
    if(pthread_mutex_unlock(&(pool->mutex)) != 0){
        return -2;
    }

    // 第五步：等待所有线程结束（等最后一个工人离开）
    wait_all_done(pool);
    
    // 第六步：释放资源
    thread_pool_free(pool);
    return 0;
}

/**
 * @brief 向线程池提交任务
 * 
 * 生产者-消费者模型中的"生产者"部分
 * 
 * 完整流程：
 * 1. 检查参数有效性
 * 2. 获取锁
 * 3. 检查线程池状态
 * 4. 检查队列是否已满
 * 5. 添加任务到队列
 * 6. 通知消费者
 * 7. 释放锁
 * 
 * 特别注意：任何可能的退出路径都要释放锁！
 */
int thread_pool_post(thread_pool_t* pool, handler_pt func, void* arg){
    if(pool == NULL || func == NULL){
        return -1;
    }

    // 获取锁
    if(pthread_mutex_lock(&(pool->mutex)) != 0){
        return -2;
    }

    // 检查线程池是否已关闭（车间下班了不收新包裹）
    if(pool->closed == 1){
        pthread_mutex_unlock(&(pool->mutex));
        return -3;
    }

    // 检查队列是否已满（工作台放不下了）
    if(pool->queue_size == pool->task_queue->count){
        pthread_mutex_unlock(&(pool->mutex));
        return -4;  // 队列满，拒绝新任务
    }

    // 添加任务到队列尾部
    task_queue_t *task_queue = pool->task_queue;
    task_t *task = &(task_queue->queue[task_queue->tail]);
    task->func = func;
    task->arg = arg;
    task_queue->tail = (task_queue->tail + 1) % pool->queue_size;  // 环形移动
    task_queue->count++;  // 任务数加1

    // 通知一个等待的线程（铃铛响一声，叫醒一个工人）
    if(pthread_cond_signal(&pool->condition) != 0){
        pthread_mutex_unlock(&(pool->mutex));
        return -5;
    }
    
    // 释放锁
    pthread_mutex_unlock(&(pool->mutex));
    return 0;  // 成功
}

/**
 * @brief 等待所有线程完成
 * 
 * pthread_join的作用：
 * 1. 等待指定线程结束（阻塞当前线程）
 * 2. 回收线程资源（类似进程的wait）
 * 
 * 如果不join会怎样？
 * 答：线程资源不会释放，造成资源泄漏
 * 
 * 注意：join只能调用一次，重复join会导致未定义行为
 */
int wait_all_done(thread_pool_t* pool){
    int ret = 0;
    for(int i = 0; i < pool->thrd_count; i++){
        /**
         * pthread_join参数：
         * 1. pool->threads[i]：要等待的线程ID
         * 2. NULL：不关心线程返回值
         */
        if(pthread_join((pool->threads[i]), NULL) != 0){
            ret = 1;  // 记录错误，但继续等待其他线程
        }
    }
    return ret;
}