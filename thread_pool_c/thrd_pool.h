#ifndef _THREAD_POOL_H_    
#define _THREAD_POOL_H_

// 防止头文件被重复包含（这是C/C++编程的好习惯）

// 线程池结构体的前向声明（先告诉编译器有这个东西，具体定义在.c文件里）
typedef struct thread_pool thread_pool_t;

// 任务处理函数的类型定义（函数指针）
// 相当于定义了一个"任务说明书"的格式
// handler_pt 就是一个可以指向函数的指针
// void* arg 表示任务可以接收任意类型的参数（万能参数）
typedef void (*handler_pt)(void* arg);

// ========== 线程池的四个核心操作 ==========

// 1. 创建线程池（相当于开一家新工厂）
// 参数：thrd_count - 雇佣多少工人
//       queue_size - 工作台能放多少个任务
// 返回值：指向新工厂的指针
thread_pool_t* thread_pool_create(int thrd_count, int queue_size);

// 2. 销毁线程池（工厂关门）
// 参数：pool - 要关闭的工厂
// 返回值：成功返回0，失败返回错误码
int thread_pool_destroy(thread_pool_t* pool);

// 3. 提交任务（客户下单）
// 参数：pool - 哪个工厂
//       func - 任务要做什么（函数指针）
//       arg - 任务的参数（比如要处理的数据）
// 返回值：成功返回0，失败返回错误码
int thread_pool_post(thread_pool_t* pool, handler_pt func, void* arg);

// 4. 等待所有任务完成（等工人做完所有活）
// 参数：pool - 要等待的工厂
// 返回值：成功返回0，失败返回错误码
int wait_all_done(thread_pool_t* pool);

#endif /* _THREAD_POOL_H_ */