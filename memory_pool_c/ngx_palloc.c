/**
 * @file ngx_palloc.c
 * @brief Nginx 风格内存池实现 - 核心功能
 * @author IceFerring/baidu
 * @date 2025-12-07
 * 
 * 这个文件实现了内存池的所有核心功能。
 * 建议结合 ngx_palloc.h 中的注释一起阅读。
 */

#include "ngx_palloc.h"  /* 包含头文件，获取所有声明和定义 */
#include <stdlib.h>      /* 标准库：malloc, free, memalign 等 */
#include <string.h>      /* 字符串操作：memset, memcpy 等 */
#include <assert.h>      /* 断言，用于调试 */

/* 
 * ============================
 * 配置常量定义
 * ============================
 */
#define NGX_POOL_ALIGNMENT  16           /* 内存池对齐要求：16字节 */
#define NGX_MIN_POOL_SIZE   sizeof(ngx_pool_t) + sizeof(ngx_pool_data_t)  /* 最小内存池大小 */

/* 
 * ============================
 * 调试宏定义
 * ============================
 * 使用条件编译控制调试输出，发布版本可以关闭调试信息。
 */
#ifdef NGX_DEBUG_MALLOC
#define ngx_debug_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define ngx_debug_printf(...)  /* 定义为空，消除调试输出 */
#endif

/* 
 * ====================================================================
 * 内存分配辅助函数（底层内存操作）
 * ====================================================================
 * 这些函数封装了系统的内存分配函数，增加了错误处理和调试功能。
 */

/**
 * @brief 普通内存分配函数
 * @param size 要分配的内存大小
 * @param log 日志对象，用于输出错误信息
 * @return 成功返回分配的内存指针，失败返回 NULL
 * 
 * 这个函数是对 malloc 的简单封装，主要添加了：
 * 1. 错误处理：分配失败时记录日志
 * 2. 调试输出：可以输出分配信息
 */
void *ngx_alloc(size_t size, ngx_log_t *log)
{
    /* 调用标准库的 malloc 函数 */
    void *p = malloc(size);
    
    /* 检查分配是否成功 */
    if (p == NULL) {
        /* 分配失败，输出日志（如果有日志对象） */
        if (log != NULL && log->file != NULL) {
            fprintf(log->file, "malloc(%zu) failed\n", size);
        }
        return NULL;
    }
    
    /* 调试输出分配信息 */
    ngx_debug_printf("ngx_alloc: allocated %zu bytes at %p\n", size, p);
    
    return p;
}

/**
 * @brief 对齐内存分配函数
 * @param alignment 对齐要求（字节数）
 * @param size 要分配的内存大小
 * @param log 日志对象，用于输出错误信息
 * @return 成功返回分配的内存指针，失败返回 NULL
 * 
 * 这个函数分配对齐的内存，不同平台使用不同的方法：
 * - Windows: _aligned_malloc
 * - macOS: posix_memalign
 * - Linux: memalign
 * 如果对齐分配失败，回退到普通分配。
 */
void *ngx_memalign(size_t alignment, size_t size, ngx_log_t *log)
{
    void *p;
    
    /* 根据平台选择对齐分配函数 */
#ifdef _WIN32
    /* Windows 平台使用 _aligned_malloc */
    p = _aligned_malloc(size, alignment);
#elif defined(__APPLE__)
    /* macOS 使用 posix_memalign，这个函数通过参数返回指针 */
    if (posix_memalign(&p, alignment, size) != 0) {
        p = NULL;  /* 分配失败 */
    }
#else
    /* Linux 和其他类 Unix 系统使用 memalign */
    p = memalign(alignment, size);
#endif
    
    /* 如果对齐分配失败，回退到普通分配 */
    if (p == NULL) {
        p = ngx_alloc(size, log);
    }
    
    /* 检查最终分配结果 */
    if (p == NULL) {
        if (log != NULL && log->file != NULL) {
            fprintf(log->file, "memalign(%zu, %zu) failed\n", alignment, size);
        }
        return NULL;
    }
    
    /* 调试输出 */
    ngx_debug_printf("ngx_memalign: allocated %zu bytes aligned to %zu at %p\n", size, alignment, p);
    return p;
}

/**
 * @brief 内存释放函数
 * @param p 要释放的内存指针
 * 
 * 这个函数是对 free 的简单封装，增加了调试输出。
 * 注意：p 可以为 NULL，这时什么都不做。
 */
void ngx_free(void *p)
{
    if (p != NULL) {
        ngx_debug_printf("ngx_free: freeing %p\n", p);
        free(p);
    }
}

/* 
 * ====================================================================
 * 内存池核心函数（创建、销毁、重置）
 * ====================================================================
 */

/**
 * @brief 创建内存池
 * @param size 内存池初始大小
 * @param log 日志对象
 * @return 成功返回内存池指针，失败返回 NULL
 * 
 * 执行步骤：
 * 1. 检查 size 是否小于最小值，如果是则调整为最小值
 * 2. 分配对齐的内存块
 * 3. 初始化内存池结构
 * 4. 设置各种指针和计数器
 * 
 * 内存布局：
 * +----------------------+
 * | ngx_pool_t 结构体    |
 * +----------------------+
 * | 可用内存空间         |
 * +----------------------+
 * | 对齐填充（可能）     |
 * +----------------------+
 */
ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t *p;
    
    /* 步骤1：确保内存池大小足够容纳管理结构 */
    if (size < NGX_MIN_POOL_SIZE) {
        size = NGX_MIN_POOL_SIZE;  /* 至少能放下管理结构 */
    }
    
    /* 步骤2：分配对齐的内存块 */
    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);
    if (p == NULL) {
        return NULL;  /* 分配失败 */
    }
    
    /* 步骤3：初始化内存池结构 */
    
    /* 设置数据块的起始和结束位置 */
    p->d.last = (u_char *)p + sizeof(ngx_pool_t);  /* 跳过管理结构 */
    p->d.end = (u_char *)p + size;                 /* 整个内存块的结束 */
    p->d.next = NULL;                              /* 还没有下一个块 */
    p->d.failed = 0;                               /* 失败次数初始为0 */
    
    /* 计算小块内存的最大值 */
    size = size - sizeof(ngx_pool_t);  /* 可用空间大小 */
    /* 不能超过配置的最大值 */
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;
    
    /* 步骤4：初始化其他字段 */
    p->current = p;      /* 当前指向自己 */
    p->chain = NULL;     /* 还没有链表 */
    p->large = NULL;     /* 还没有大内存块 */
    p->cleanup = NULL;   /* 还没有清理回调 */
    p->log = log;        /* 设置日志对象 */
    
    /* 调试输出 */
    ngx_debug_printf("Created pool %p with size %zu, max=%zu\n", p, size + sizeof(ngx_pool_t), p->max);
    
    return p;
}

/**
 * @brief 销毁内存池
 * @param pool 要销毁的内存池指针
 * 
 * 执行步骤：
 * 1. 执行所有注册的清理回调（逆序执行）
 * 2. 释放所有大内存块
 * 3. 释放所有内存池块
 * 
 * 注意：这个函数执行后，pool 指针不再有效！
 */
void ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t *p, *n;
    ngx_pool_large_t *l;
    ngx_pool_cleanup_t *c;
    
    if (pool == NULL) {
        return;  /* 空指针检查 */
    }
    
    /* 步骤1：执行清理回调 */
    /* 遍历清理回调链表，调用每个回调函数 */
    for (c = pool->cleanup; c != NULL; c = c->next) {
        if (c->handler != NULL) {
            ngx_debug_printf("Calling cleanup handler %p with data %p\n", c->handler, c->data);
            c->handler(c->data);  /* 执行清理函数 */
        }
    }
    
    /* 步骤2：释放大块内存 */
    /* 遍历大内存链表，释放每个大内存块 */
    for (l = pool->large; l != NULL; l = l->next) {
        if (l->alloc != NULL) {
            ngx_debug_printf("Freeing large block %p\n", l->alloc);
            ngx_free(l->alloc);  /* 释放内存 */
        }
    }
    
    /* 步骤3：释放所有内存池块 */
    /* 注意：这里使用两个指针 p 和 n 遍历链表 */
    /* p 指向当前要释放的块，n 指向下一个块 */
    for (p = pool, n = pool->d.next; ; p = n, n = n ? n->d.next : NULL) {
        ngx_debug_printf("Freeing pool block %p\n", p);
        ngx_free(p);  /* 释放当前块 */
        
        /* 如果 n 为 NULL，说明已经释放了最后一个块 */
        if (n == NULL) {
            break;
        }
    }
}

/**
 * @brief 重置内存池
 * @param pool 要重置的内存池指针
 * 
 * 执行步骤：
 * 1. 释放所有大内存块
 * 2. 重置所有内存块的 last 指针
 * 3. 重置失败计数
 * 4. 重置 current 指针
 * 5. 清空大内存链表
 * 
 * 注意：这个函数不释放内存池块，只是重置它们的状态。
 * 适用于需要重复使用内存池的场景。
 */
void ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t *p;
    ngx_pool_large_t *l;
    
    if (pool == NULL) {
        return;  /* 空指针检查 */
    }
    
    /* 步骤1：释放所有大块内存 */
    for (l = pool->large; l != NULL; l = l->next) {
        if (l->alloc != NULL) {
            ngx_free(l->alloc);  /* 释放内存 */
            l->alloc = NULL;     /* 将指针设为 NULL，避免悬垂指针 */
        }
    }
    
    /* 步骤2：重置所有内存块 */
    for (p = pool; p != NULL; p = p->d.next) {
        /* 将 last 指针重置到管理结构之后 */
        p->d.last = (u_char *)p + sizeof(ngx_pool_t);
        p->d.failed = 0;  /* 重置失败计数 */
    }
    
    /* 步骤3：重置管理指针 */
    pool->current = pool;  /* current 指向第一个块 */
    pool->large = NULL;    /* 清空大内存链表 */
    
    /* 调试输出 */
    ngx_debug_printf("Pool %p reset\n", pool);
}

/* 
 * ====================================================================
 * 内存分配函数（核心算法）
 * ====================================================================
 */

/**
 * @brief 分配小块内存（内部函数）
 * @param pool 内存池指针
 * @param size 要分配的大小
 * @param align 是否对齐（1=对齐，0=不对齐）
 * @return 分配的内存指针
 * 
 * 执行步骤：
 * 1. 从 current 指针开始遍历内存池链表
 * 2. 在每个块中尝试分配
 * 3. 如果找到足够空间，更新 last 指针并返回
 * 4. 如果所有块都没有足够空间，调用 ngx_palloc_block 创建新块
 * 
 * 注意：这个函数只处理小块内存（size <= pool->max）
 */
void *ngx_palloc_small(ngx_pool_t *pool, size_t size, ngx_uint_t align)
{
    u_char *m;
    ngx_pool_t *p;
    
    /* 步骤1：从 current 指针开始遍历 */
    p = pool->current;
    
    do {
        /* 获取当前块的可用内存起始位置 */
        m = p->d.last;
        
        /* 步骤2：如果需要对齐，调整起始位置 */
        if (align) {
            m = ngx_align_ptr(m, NGX_ALIGNMENT);
        }
        
        /* 步骤3：检查是否有足够空间 */
        /* 计算剩余空间：end - m */
        if ((size_t)(p->d.end - m) >= size) {
            /* 有足够空间，更新 last 指针并返回 */
            p->d.last = m + size;
            return m;
        }
        
        /* 空间不足，尝试下一个块 */
        p = p->d.next;
        
    } while (p);  /* 循环直到 p 为 NULL（遍历完所有块） */
    
    /* 步骤4：所有块都没有足够空间，创建新块 */
    return ngx_palloc_block(pool, size);
}

/**
 * @brief 创建新的内存块（内部函数）
 * @param pool 内存池指针
 * @param size 需要分配的大小（用于初始化新块的 last 指针）
 * @return 新内存块中的可用内存指针
 * 
 * 执行步骤：
 * 1. 计算新块的大小（与第一个块相同）
 * 2. 分配新内存块
 * 3. 初始化新块的结构
 * 4. 将新块连接到链表末尾
 * 5. 更新 current 指针（智能优化）
 * 
 * 智能优化原理：
 * 如果一个块连续多次分配失败（failed > 4），
 * 就将 current 指针跳过这个块，提高后续分配效率。
 */
void *ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char *m;
    size_t psize;
    ngx_pool_t *p, *new_pool, *current;
    
    /* 步骤1：计算新块的大小 */
    /* 新块大小与第一个块相同（包括管理结构） */
    psize = (size_t)(pool->d.end - (u_char *)pool);
    
    /* 步骤2：分配新内存块 */
    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);
    if (m == NULL) {
        return NULL;  /* 分配失败 */
    }
    
    /* 步骤3：初始化新块的结构 */
    new_pool = (ngx_pool_t *)m;            /* 将内存块转换为内存池结构 */
    new_pool->d.end = m + psize;           /* 设置结束位置 */
    new_pool->d.next = NULL;               /* 暂时没有下一个块 */
    new_pool->d.failed = 0;                /* 失败计数为0 */
    
    /* 设置可用内存起始位置 */
    m += sizeof(ngx_pool_data_t);          /* 跳过数据块结构 */
    m = ngx_align_ptr(m, NGX_ALIGNMENT);   /* 对齐 */
    new_pool->d.last = m + size;           /* 预留 size 大小的空间 */
    
    /* 步骤4：将新块连接到链表末尾 */
    
    /* 首先找到链表末尾 */
    current = pool->current;  /* 从 current 开始查找 */
    
    /* 遍历链表，同时更新失败计数和 current 指针 */
    for (p = current; p->d.next; p = p->d.next) {
        /* 如果某个块失败次数超过4，跳过它 */
        if (p->d.failed++ > 4) {
            current = p->d.next;  /* 将 current 指向下一个块 */
        }
    }
    
    /* 将新块连接到链表末尾 */
    p->d.next = new_pool;
    
    /* 步骤5：更新 current 指针 */
    /* 如果 current 被更新了，使用新的 current，否则使用新块 */
    pool->current = current ? current : new_pool;
    
    /* 调试输出 */
    ngx_debug_printf("Created new pool block %p, size=%zu\n", new_pool, psize);
    
    /* 返回新块中的可用内存（跳过 size 大小的预留空间） */
    return m;
}

/**
 * @brief 分配大块内存（内部函数）
 * @param pool 内存池指针
 * @param size 要分配的大小
 * @return 分配的内存指针
 * 
 * 执行步骤：
 * 1. 直接调用系统分配函数分配内存
 * 2. 查找大内存链表中的空槽位（复用机制）
 * 3. 如果没有空槽位，分配新的 large 结构
 * 4. 将大内存信息插入链表头部
 * 
 * 复用机制：查找前4个大内存节点，看是否有 alloc 为 NULL 的，
 * 这样可以复用 large 结构，避免频繁分配。
 */
void *ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void *p;
    ngx_uint_t n;
    ngx_pool_large_t *large;
    
    /* 步骤1：直接分配大内存 */
    p = ngx_alloc(size, pool->log);
    if (p == NULL) {
        return NULL;  /* 分配失败 */
    }
    
    n = 0;  /* 用于限制查找的节点数 */
    
    /* 步骤2：查找可用的空槽位（复用机制） */
    /* 遍历大内存链表，查找 alloc 为 NULL 的节点 */
    for (large = pool->large; large != NULL; large = large->next) {
        if (large->alloc == NULL) {
            /* 找到空槽位，复用这个节点 */
            large->alloc = p;
            ngx_debug_printf("Reused large slot for %p, size=%zu\n", p, size);
            return p;
        }
        
        /* 限制只查找前4个节点，避免遍历整个链表 */
        if (n++ > 3) {
            break;
        }
    }
    
    /* 步骤3：分配新的 large 结构 */
    /* 注意：large 结构本身是小块内存，从内存池分配 */
    large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);
    if (large == NULL) {
        /* 分配失败，需要释放刚刚分配的大内存 */
        ngx_free(p);
        return NULL;
    }
    
    /* 步骤4：将大内存信息插入链表头部 */
    large->alloc = p;          /* 记录内存地址 */
    large->next = pool->large; /* 插入链表头部 */
    pool->large = large;       /* 更新链表头指针 */
    
    /* 调试输出 */
    ngx_debug_printf("Allocated large block %p, size=%zu\n", p, size);
    
    return p;
}

/**
 * @brief 分配内存（对齐版本）
 * @param pool 内存池指针
 * @param size 要分配的大小
 * @return 分配的内存指针
 * 
 * 这个函数根据 size 决定使用哪种分配策略：
 * 1. 如果 size <= pool->max，使用小块内存分配
 * 2. 否则，使用大块内存分配
 */
void *ngx_palloc(ngx_pool_t *pool, size_t size)
{
    /* 判断大小，选择分配策略 */
    if (size <= pool->max) {
        /* 小块内存分配，需要对齐 */
        return ngx_palloc_small(pool, size, 1);
    }
    
    /* 大块内存分配 */
    return ngx_palloc_large(pool, size);
}

/**
 * @brief 分配内存（不对齐版本）
 * @param pool 内存池指针
 * @param size 要分配的大小
 * @return 分配的内存指针
 * 
 * 与 ngx_palloc 的区别是不进行内存对齐。
 */
void *ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
    if (size <= pool->max) {
        /* 小块内存分配，不需要对齐 */
        return ngx_palloc_small(pool, size, 0);
    }
    
    /* 大块内存分配 */
    return ngx_palloc_large(pool, size);
}

/**
 * @brief 分配内存并清零
 * @param pool 内存池指针
 * @param size 要分配的大小
 * @return 分配的内存指针
 * 
 * 这个函数先分配内存，然后用 0 填充所有字节。
 * 使用 memset 函数实现清零。
 */
void *ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    void *p;
    
    /* 先分配内存 */
    p = ngx_palloc(pool, size);
    
    /* 如果分配成功，清零内存 */
    if (p != NULL) {
        memset(p, 0, size);  /* 使用 memset 清零 */
    }
    
    return p;
}

/**
 * @brief 释放大块内存
 * @param pool 内存池指针
 * @param p 要释放的内存指针
 * @return 成功返回 0，失败返回 NGX_ERROR
 * 
 * 执行步骤：
 * 1. 遍历大内存链表，查找匹配的内存地址
 * 2. 如果找到，释放内存并将 alloc 设为 NULL
 * 3. 如果没找到，返回错误
 * 
 * 注意：这个函数只能释放大块内存，小块内存无法单独释放。
 */
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t *l;
    
    /* 遍历大内存链表 */
    for (l = pool->large; l != NULL; l = l->next) {
        if (p == l->alloc) {
            /* 找到匹配的内存块 */
            ngx_debug_printf("Freeing large block %p from pool\n", p);
            ngx_free(l->alloc);  /* 释放内存 */
            l->alloc = NULL;     /* 将指针设为 NULL，标记为空槽位 */
            return 0;            /* 成功 */
        }
    }
    
    /* 没有找到匹配的内存块 */
    return NGX_ERROR;  /* 失败 */
}

/* 
 * ====================================================================
 * 清理回调函数
 * ====================================================================
 */

/**
 * @brief 注册清理回调函数
 * @param pool 内存池指针
 * @param size 需要额外分配的数据大小
 * @return 成功返回清理回调结构指针，失败返回 NULL
 * 
 * 执行步骤：
 * 1. 分配清理回调结构
 * 2. 如果需要额外数据，分配数据空间
 * 3. 初始化清理回调结构
 * 4. 将结构插入链表头部
 * 
 * 链表采用头插法，新节点插入链表头部。
 */
ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *pool, size_t size)
{
    ngx_pool_cleanup_t *c;
    
    /* 步骤1：分配清理回调结构 */
    c = ngx_palloc(pool, sizeof(ngx_pool_cleanup_t));
    if (c == NULL) {
        return NULL;  /* 分配失败 */
    }
    
    /* 步骤2：如果需要额外数据，分配数据空间 */
    if (size > 0) {
        c->data = ngx_palloc(pool, size);
        if (c->data == NULL) {
            return NULL;  /* 分配失败 */
        }
    } else {
        c->data = NULL;  /* 不需要额外数据 */
    }
    
    /* 步骤3：初始化清理回调结构 */
    c->handler = NULL;  /* 用户稍后设置 */
    
    /* 步骤4：将结构插入链表头部（头插法） */
    c->next = pool->cleanup;  /* 新节点的 next 指向原来的头节点 */
    pool->cleanup = c;        /* 更新链表头指针 */
    
    /* 调试输出 */
    ngx_debug_printf("Added cleanup handler %p to pool %p\n", c, pool);
    
    return c;
}

/* 
 * ====================================================================
 * 统计和调试函数
 * ====================================================================
 */

/**
 * @brief 获取内存池统计信息
 * @param pool 内存池指针
 * @param stats 统计信息结构指针
 * 
 * 这个函数收集内存池的使用情况，但注意：
 * 1. 对于大内存块，无法获取实际分配的大小（因为我们没有记录）
 * 2. 对于小块内存，只能统计已使用的空间，无法统计分配次数
 */
void ngx_pool_get_stats(ngx_pool_t *pool, ngx_pool_stats_t *stats)
{
    ngx_pool_t *p;
    ngx_pool_large_t *l;
    
    if (stats == NULL) {
        return;  /* 参数检查 */
    }
    
    /* 清零统计结构 */
    memset(stats, 0, sizeof(ngx_pool_stats_t));
    
    /* 统计内存池块 */
    for (p = pool; p != NULL; p = p->d.next) {
        stats->pool_count++;  /* 内存池块计数 */
        /* 计算已使用的小块内存大小 */
        stats->small_count += (p->d.last - ((u_char *)p + sizeof(ngx_pool_t)));
    }
    
    /* 统计大内存块 */
    for (l = pool->large; l != NULL; l = l->next) {
        if (l->alloc != NULL) {
            stats->large_count++;  /* 大内存块计数 */
            /* 注意：这里无法获取大内存块的实际大小 */
        }
    }
}

/**
 * @brief 打印内存池信息（调试用）
 * @param pool 内存池指针
 * @param tag  调试标签，用于区分不同的调试输出
 * 
 * 这个函数打印内存池的详细信息，用于调试和学习。
 * 它会遍历所有数据结构，并打印关键信息。
 */
void ngx_pool_dump(ngx_pool_t *pool, const char *tag)
{
    ngx_pool_t *p;
    ngx_pool_large_t *l;
    ngx_pool_cleanup_t *c;
    int pool_num = 0, large_num = 0, cleanup_num = 0;
    
    /* 打印标题 */
    if (tag) {
        printf("\n=== Memory Pool Dump: %s ===\n", tag);
    } else {
        printf("\n=== Memory Pool Dump ===\n");
    }
    
    /* 1. 遍历内存池链，打印每个块的信息 */
    printf("=== Pool Blocks ===\n");
    for (p = pool; p != NULL; p = p->d.next) {
        printf("Block #%d: %p\n", ++pool_num, p);
        printf("  Range: %p - %p (size: %ld bytes)\n", 
               p, p->d.end, (long)(p->d.end - (u_char *)p));
        printf("  Used: %ld bytes\n",
               (long)(p->d.last - ((u_char *)p + sizeof(ngx_pool_t))));
        printf("  Failed allocations: %u\n", p->d.failed);
        printf("  Max small block: %zu bytes\n", p->max);
        printf("  Current pointer: %p\n", p->current);
        printf("  Next block: %p\n", p->d.next);
        printf("\n");
    }
    
    /* 2. 遍历大内存块 */
    printf("=== Large Blocks ===\n");
    for (l = pool->large; l != NULL; l = l->next) {
        printf("Large #%d: node=%p -> memory=%p\n", ++large_num, l, l->alloc);
    }
    if (large_num == 0) {
        printf("No large blocks\n");
    }
    printf("\n");
    
    /* 3. 遍历清理回调 */
    printf("=== Cleanup Handlers ===\n");
    for (c = pool->cleanup; c != NULL; c = c->next) {
        printf("Cleanup #%d: handler=%p, data=%p\n", ++cleanup_num, c->handler, c->data);
    }
    if (cleanup_num == 0) {
        printf("No cleanup handlers\n");
    }
    printf("\n");
    
    /* 4. 打印摘要 */
    printf("=== Summary ===\n");
    printf("Pool blocks: %d\n", pool_num);
    printf("Large blocks: %d\n", large_num);
    printf("Cleanup handlers: %d\n", cleanup_num);
    printf("==============================\n\n");
}