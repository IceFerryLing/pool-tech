/**
 * @file ngx_palloc.h
 * @brief Nginx 风格内存池实现
 * @author IceFerring/baidu
 * @date 2025-12-07
 * 
 * 这个头文件定义了内存池的所有数据结构和函数接口。
 * 内存池是一种高效的内存管理技术，特别适合需要频繁分配/释放内存的场景。
 * 
 * 主要特性：
 * 1. 两级内存管理（小块内存+大块内存）
 * 2. 自动清理机制
 * 3. 内存对齐支持
 * 4. 统计和调试功能
 */

#ifndef _NGX_PALLOC_H_INCLUDED_
#define _NGX_PALLOC_H_INCLUDED_

#include <stddef.h>     /* 标准类型定义，如 size_t */
#include <stdint.h>     /* 整数类型定义，如 uintptr_t */
#include <stdio.h>      /* 标准输入输出，用于日志 */

/* 
 * ============================
 * 内存对齐相关定义
 * ============================
 * 内存对齐可以提高 CPU 访问内存的效率，避免跨缓存行访问。
 * 对齐原则：数据地址应该是其大小的整数倍。
 */
#ifndef NGX_ALIGNMENT
#define NGX_ALIGNMENT   sizeof(unsigned long)  /* 通常与 CPU 字长相同 */
#endif

/* 
 * 对齐宏函数：
 * - ngx_align: 将值 d 向上对齐到 a 的倍数
 * - ngx_align_ptr: 将指针 p 向上对齐到 a 的倍数
 * 
 * 原理：a-1 的二进制是全1，取反后是掩码，
 *       用这个掩码与 (p + a-1) 进行与运算，就能得到对齐后的地址。
 */
#define ngx_align(d, a)     (((d) + (a - 1)) & ~(a - 1))
#define ngx_align_ptr(p, a)                                                   \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))

/* 
 * ============================
 * 配置常量定义
 * ============================
 */
#ifndef NGX_MAX_ALLOC_FROM_POOL
#define NGX_MAX_ALLOC_FROM_POOL  (4096 - 1)  /* 小块内存的最大值，通常为 4KB-1 */
#endif

#ifndef NGX_DEFAULT_POOL_SIZE
#define NGX_DEFAULT_POOL_SIZE    (16 * 1024)  /* 默认内存池大小：16KB */
#endif

/* 
 * ============================
 * 错误码定义
 * ============================
 */
#define NGX_ERROR      -1  /* 通用错误码 */

/* 
 * ============================
 * 类型别名定义
 * ============================
 * 使用类型别名提高代码可读性和可移植性。
 */
typedef unsigned char u_char;     /* 无符号字符，常用于处理原始内存 */
typedef int ngx_int_t;            /* 有符号整数 */
typedef unsigned int ngx_uint_t;  /* 无符号整数 */

/* 
 * ============================
 * 日志结构（简化版）
 * ============================
 * 在实际的 Nginx 中，日志系统更复杂。
 * 这里简化为一个文件指针和日志级别。
 */
typedef struct {
    FILE *file;    /* 日志输出文件，如 stderr 或日志文件 */
    int   level;   /* 日志级别：DEBUG, INFO, WARN, ERROR 等 */
} ngx_log_t;

/* 
 * ============================
 * 链表结构
 * ============================
 * 单向链表节点，用于内存池中的链式结构。
 * 这是 Nginx 中常用的通用链表结构。
 */
typedef struct ngx_chain_s ngx_chain_t;
struct ngx_chain_s {
    ngx_chain_t  *next;  /* 指向下一个节点的指针 */
};

/* 
 * ============================
 * 大内存块结构
 * ============================
 * 大内存块单独管理，使用链表连接。
 * 为什么不直接放在内存池中？
 * 答：大块内存如果放在池中，会浪费空间（池中其他部分无法使用），
 *     且大内存的分配/释放频率可能不同。
 */
typedef struct ngx_pool_large_s ngx_pool_large_t;
struct ngx_pool_large_s {
    ngx_pool_large_t *next;   /* 下一个大内存块 */
    void             *alloc;  /* 实际分配的内存地址 */
};

/* 
 * ============================
 * 清理回调函数类型
 * ============================
 * 用户注册的清理函数，在内存池销毁时自动调用。
 * 用于释放外部资源（如文件描述符、套接字等）。
 */
typedef void (*ngx_pool_cleanup_pt)(void *data);

/* 
 * ============================
 * 清理回调结构
 * ============================
 * 清理回调的链表节点，每个节点包含一个清理函数和相关数据。
 */
typedef struct ngx_pool_cleanup_s ngx_pool_cleanup_t;
struct ngx_pool_cleanup_s {
    ngx_pool_cleanup_pt  handler;  /* 清理函数指针 */
    void                *data;     /* 传递给清理函数的参数 */
    ngx_pool_cleanup_t  *next;     /* 下一个清理节点 */
};

/* 
 * ============================
 * 内存池数据块
 * ============================
 * 这是内存池的核心数据部分，表示一个内存块。
 * 多个这样的块通过 next 指针连接成链表。
 */
typedef struct {
    u_char        *last;      /* 当前内存块中可用内存的起始位置 */
    u_char        *end;       /* 当前内存块的结束位置 */
    ngx_pool_t    *next;      /* 下一个内存池 */
    ngx_uint_t     failed;    /* 该内存块分配失败的次数（用于优化） */
} ngx_pool_data_t;

/* 
 * ============================
 * 内存池主结构（前向声明）
 * ============================
 * 这里使用前向声明，因为结构体内部需要引用自身。
 */
typedef struct ngx_pool_s ngx_pool_t;

/* 
 * ============================
 * 内存池主结构定义
 * ============================
 * 这是内存池的控制中心，包含所有管理信息。
 */
struct ngx_pool_s {
    /* 内存池数据部分，必须放在结构体开头（内存布局考虑） */
    ngx_pool_data_t       d;         /* 内存池数据块 */
    
    /* 管理信息 */
    size_t                max;       /* 小块内存的最大值，超过此值则使用大内存分配 */
    ngx_pool_t           *current;   /* 当前正在使用的内存池（智能指针，跳过频繁失败的池） */
    
    /* 各种链表 */
    ngx_chain_t          *chain;     /* 通用链表，可用于外部用途 */
    ngx_pool_large_t     *large;     /* 大块内存链表 */
    ngx_pool_cleanup_t   *cleanup;   /* 清理回调链表 */
    
    /* 日志 */
    ngx_log_t            *log;       /* 日志对象，用于输出错误和调试信息 */
};

/* 
 * ============================
 * 内存池统计信息结构
 * ============================
 * 用于收集内存池的使用情况，方便监控和调试。
 */
typedef struct {
    size_t total_allocated;     /* 总分配字节数 */
    size_t total_freed;         /* 总释放字节数 */
    size_t pool_count;          /* 内存池块数量 */
    size_t large_count;         /* 大内存块数量 */
    size_t small_count;         /* 小内存块数量 */
} ngx_pool_stats_t;

/* 
 * ====================================================================
 * 内存池操作函数声明
 * ====================================================================
 * 这些函数提供了内存池的核心功能。
 */

/**
 * @brief 创建内存池
 * @param size 内存池初始大小（字节）
 * @param log  日志对象指针
 * @return 成功返回内存池指针，失败返回 NULL
 * 
 * 这个函数分配指定大小的内存，并初始化内存池结构。
 * 注意：实际可用的内存比 size 小，因为部分空间被管理结构占用。
 */
ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log);

/**
 * @brief 销毁内存池
 * @param pool 要销毁的内存池指针
 * 
 * 这个函数执行以下操作：
 * 1. 调用所有注册的清理函数
 * 2. 释放所有大内存块
 * 3. 释放所有内存池块
 * 4. 内存池指针 pool 在此之后不再有效
 */
void ngx_destroy_pool(ngx_pool_t *pool);

/**
 * @brief 重置内存池
 * @param pool 要重置的内存池指针
 * 
 * 这个函数重置内存池到初始状态，但不释放内存。
 * 适用于需要重复使用内存池的场景。
 * 操作包括：
 * 1. 释放所有大内存块
 * 2. 重置所有内存块的 last 指针
 * 3. 重置统计信息
 */
void ngx_reset_pool(ngx_pool_t *pool);

/**
 * @brief 分配内存（对齐版本）
 * @param pool 内存池指针
 * @param size 要分配的内存大小
 * @return 成功返回分配的内存指针，失败返回 NULL
 * 
 * 这是最常用的分配函数，分配的内存会进行对齐。
 * 对齐可以提高 CPU 访问效率。
 */
void *ngx_palloc(ngx_pool_t *pool, size_t size);

/**
 * @brief 分配内存（不对齐版本）
 * @param pool 内存池指针
 * @param size 要分配的内存大小
 * @return 成功返回分配的内存指针，失败返回 NULL
 * 
 * 与 ngx_palloc 的区别是不进行内存对齐。
 * 在对齐要求不高或需要节省内存时使用。
 */
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);

/**
 * @brief 分配内存并清零
 * @param pool 内存池指针
 * @param size 要分配的内存大小
 * @return 成功返回分配的内存指针，失败返回 NULL
 * 
 * 这个函数分配内存后，用 0 填充所有字节。
 * 适用于需要初始化内存的场景。
 */
void *ngx_pcalloc(ngx_pool_t *pool, size_t size);

/**
 * @brief 释放大块内存
 * @param pool 内存池指针
 * @param p 要释放的内存指针
 * @return 成功返回 0，失败返回 NGX_ERROR
 * 
 * 注意：这个函数只能释放大块内存，小块内存无法单独释放。
 * 这是内存池设计的特点：要么全部释放，要么全部保留。
 */
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p);

/**
 * @brief 注册清理回调函数
 * @param pool 内存池指针
 * @param size 需要额外分配的数据大小
 * @return 成功返回清理回调结构指针，失败返回 NULL
 * 
 * 这个函数允许用户注册清理函数，这些函数会在内存池销毁时自动调用。
 * 如果 size > 0，还会分配额外的内存作为回调函数的参数。
 */
ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *pool, size_t size);

/**
 * @brief 获取内存池统计信息
 * @param pool 内存池指针
 * @param stats 统计信息结构指针
 * 
 * 这个函数收集内存池的使用情况，填充到 stats 结构中。
 */
void ngx_pool_get_stats(ngx_pool_t *pool, ngx_pool_stats_t *stats);

/**
 * @brief 打印内存池信息（调试用）
 * @param pool 内存池指针
 * @param tag  调试标签，用于区分不同的调试输出
 * 
 * 这个函数打印内存池的详细信息，包括：
 * 1. 所有内存块的信息
 * 2. 大内存块的信息
 * 3. 清理回调的信息
 */
void ngx_pool_dump(ngx_pool_t *pool, const char *tag);

/* 
 * ====================================================================
 * 内部使用的函数声明（通常不直接调用）
 * ====================================================================
 */

/**
 * @brief 分配小块内存（内部函数）
 * @param pool 内存池指针
 * @param size 要分配的大小
 * @param align 是否对齐（1=对齐，0=不对齐）
 * @return 分配的内存指针
 * 
 * 这是内存池的核心分配函数，只处理小块内存。
 * 如果当前内存块空间不足，会创建新的内存块。
 */
void *ngx_palloc_small(ngx_pool_t *pool, size_t size, ngx_uint_t align);

/**
 * @brief 分配大块内存（内部函数）
 * @param pool 内存池指针
 * @param size 要分配的大小
 * @return 分配的内存指针
 * 
 * 这个函数分配大块内存，并使用单独的链表管理。
 */
void *ngx_palloc_large(ngx_pool_t *pool, size_t size);

/**
 * @brief 创建新的内存块（内部函数）
 * @param pool 内存池指针
 * @param size 需要分配的大小
 * @return 新内存块中的可用内存指针
 * 
 * 当现有内存块空间不足时，调用这个函数创建新的内存块。
 * 新内存块的大小与第一个内存块相同。
 */
void *ngx_palloc_block(ngx_pool_t *pool, size_t size);

/**
 * @brief 对齐分配内存（内部函数）
 * @param alignment 对齐要求
 * @param size 要分配的大小
 * @param log 日志对象
 * @return 分配的内存指针
 * 
 * 这个函数是对系统内存分配函数的封装，增加了对齐功能。
 */
void *ngx_memalign(size_t alignment, size_t size, ngx_log_t *log);

/**
 * @brief 普通内存分配（内部函数）
 * @param size 要分配的大小
 * @param log 日志对象
 * @return 分配的内存指针
 * 
 * 这个函数是对 malloc 的简单封装，增加了日志功能。
 */
void *ngx_alloc(size_t size, ngx_log_t *log);

/**
 * @brief 内存释放（内部函数）
 * @param p 要释放的内存指针
 * 
 * 这个函数是对 free 的简单封装。
 */
void ngx_free(void *p);

#endif /* _NGX_PALLOC_H_INCLUDED_ */