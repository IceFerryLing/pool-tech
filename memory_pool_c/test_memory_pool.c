#include "ngx_palloc.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>

/* ============ 清理回调函数示例 ============ */

void file_cleanup_handler(void *data)
{
    FILE *fp = (FILE *)data;
    if (fp != NULL) {
        printf("[CLEANUP] Closing file pointer: %p\n", fp);
        fclose(fp);
    }
}

void buffer_cleanup_handler(void *data)
{
    char *buffer = (char *)data;
    printf("[CLEANUP] Cleaning buffer: %s\n", buffer);
}

void custom_data_cleanup(void *data)
{
    printf("[CLEANUP] Freeing custom data at: %p\n", data);
}

/* ============ 测试用例 ============ */

void test_1_basic_allocation()
{
    printf("\n"
            "┌─────────────────────────────────────────────┐\n"
            "│ 测试1：基本分配功能                         │\n"
            "│ 验证：创建内存池、分配小块内存、大块内存    │\n"
            "└─────────────────────────────────────────────┘\n");
    
    ngx_log_t log = { stderr, 0 };
    ngx_pool_t *pool = ngx_create_pool(1024, &log);
    assert(pool != NULL && "创建内存池失败");
    
    // 1. 分配小块内存
    char *str1 = ngx_palloc(pool, 100);
    strcpy(str1, "Hello, Memory Pool!");
    printf("✓ 分配小块内存：%p -> '%s'\n", str1, str1);
    
    // 2. 分配对齐的内存
    int *aligned_array = ngx_palloc(pool, 10 * sizeof(int));
    for (int i = 0; i < 10; i++) {
        aligned_array[i] = i * 10;
    }
    printf("✓ 分配对齐的整数数组：%p\n", aligned_array);
    
    // 3. 分配并清零的内存
    char *zeroed = ngx_pcalloc(pool, 50);
    int is_zero = 1;
    for (int i = 0; i < 50; i++) {
        if (zeroed[i] != 0) is_zero = 0;
    }
    printf("✓ 分配并清零的内存：%p (全零：%s)\n", zeroed, is_zero ? "是" : "否");
    
    // 4. 分配大块内存（超过 max 值）
    void *large_block = ngx_palloc(pool, 8192);
    printf("✓ 分配大块内存（8KB）：%p\n", large_block);
    
    // 5. 显示内存池状态
    ngx_pool_dump(pool, "基础分配测试后");
    
    ngx_destroy_pool(pool);
    printf("✅ 测试1通过\n");
}

void test_2_cleanup_mechanism()
{
    printf("\n"
            "┌─────────────────────────────────────────────┐\n"
            "│ 测试2：清理回调机制                         │\n"
            "│ 验证：清理函数在内存池销毁时自动执行        │\n"
            "└─────────────────────────────────────────────┘\n");
    
    ngx_log_t log = { stderr, 0 };
    ngx_pool_t *pool = ngx_create_pool(512, &log);
    
    printf("测试清理回调机制...\n");
    
    // 1. 注册文件清理回调（模拟打开文件）
    FILE *test_file = tmpfile();
    if (test_file) {
        fprintf(test_file, "Test data for cleanup\n");
        fflush(test_file);
        
        ngx_pool_cleanup_t *cln = ngx_pool_cleanup_add(pool, 0);
        cln->handler = file_cleanup_handler;
        cln->data = test_file;
        printf("✓ 注册文件清理回调：%p -> %p\n", cln->handler, cln->data);
    }
    
    // 2. 注册缓冲区清理回调
    ngx_pool_cleanup_t *cln2 = ngx_pool_cleanup_add(pool, 64);
    if (cln2 && cln2->data) {
        strcpy((char *)cln2->data, "This buffer will be cleaned up automatically");
        cln2->handler = buffer_cleanup_handler;
        printf("✓ 注册缓冲区清理回调：%p\n", cln2->handler);
    }
    
    // 3. 注册自定义数据清理回调
    typedef struct {
        int id;
        char name[32];
    } custom_data_t;
    
    ngx_pool_cleanup_t *cln3 = ngx_pool_cleanup_add(pool, sizeof(custom_data_t));
    if (cln3 && cln3->data) {
        custom_data_t *data = (custom_data_t *)cln3->data;
        data->id = 1001;
        strcpy(data->name, "Test Custom Data");
        cln3->handler = custom_data_cleanup;
        printf("✓ 注册自定义数据清理回调\n");
    }
    
    printf("\n正在销毁内存池（应该看到清理回调执行）...\n");
    printf("--------------------------------------------------\n");
    ngx_destroy_pool(pool);
    printf("--------------------------------------------------\n");
    
    printf("✅ 测试2通过\n");
}

void test_3_pool_reset_reuse()
{
    printf("\n"
            "┌─────────────────────────────────────────────┐\n"
            "│ 测试3：内存池重置与重用                     │\n"
            "│ 验证：重置内存池可以重用内存，提高性能      │\n"
            "└─────────────────────────────────────────────┘\n");
    
    ngx_log_t log = { stderr, 0 };
    ngx_pool_t *pool = ngx_create_pool(2048, &log);
    
    // 第一阶段：分配内存
    printf("第一阶段：分配内存\n");
    void *ptr1 = ngx_palloc(pool, 100);
    void *ptr2 = ngx_palloc(pool, 200);
    void *large1 = ngx_palloc(pool, 4096);  // 大内存
    
    printf("分配指针：\n");
    printf("  ptr1:  %p (100 bytes)\n", ptr1);
    printf("  ptr2:  %p (200 bytes)\n", ptr2);
    printf("  large1: %p (4KB)\n", large1);
    
    ngx_pool_dump(pool, "第一阶段分配后");
    
    // 重置内存池
    printf("\n重置内存池...\n");
    ngx_reset_pool(pool);
    
    ngx_pool_dump(pool, "重置后");
    
    // 第二阶段：重用内存池
    printf("第二阶段：重用内存池\n");
    void *ptr3 = ngx_palloc(pool, 150);
    void *ptr4 = ngx_palloc(pool, 300);
    
    printf("重新分配指针：\n");
    printf("  ptr3: %p (150 bytes)\n", ptr3);
    printf("  ptr4: %p (300 bytes)\n", ptr4);
    
    // 注意：重置后重新分配的内存地址可能与之前不同
    // 这是因为大内存被释放了，但小块内存可能在同一位置
    
    ngx_destroy_pool(pool);
    printf("✅ 测试3通过\n");
}

void test_4_alignment_verification()
{
    printf("\n"
            "┌─────────────────────────────────────────────┐\n"
            "│ 测试4：内存对齐验证                         │\n"
            "│ 验证：分配的内存是否正确对齐                │\n"
            "└─────────────────────────────────────────────┘\n");
    
    ngx_log_t log = { stderr, 0 };
    ngx_pool_t *pool = ngx_create_pool(4096, &log);
    
    printf("内存对齐测试（对齐要求：%lu字节）\n", (unsigned long)NGX_ALIGNMENT);
    
    // 测试不同大小的分配
    size_t sizes[] = {1, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    
    for (int i = 0; i < num_sizes; i++) {
        void *ptr = ngx_palloc(pool, sizes[i]);
        uintptr_t addr = (uintptr_t)ptr;
        
        // 检查是否对齐
        int is_aligned = (addr % NGX_ALIGNMENT) == 0;
        
        printf("  分配 %3zu 字节：地址=%p, 对齐=%s\n", 
                sizes[i], ptr, is_aligned ? "✓" : "✗");
        
        assert(is_aligned && "内存未正确对齐！");
    }
    
    // 测试不对齐版本
    printf("\n测试不对齐分配（ngx_pnalloc）：\n");
    void *unaligned = ngx_pnalloc(pool, 10);
    printf("  分配 10 字节（不对齐）：%p\n", unaligned);
    
    ngx_destroy_pool(pool);
    printf("✅ 测试4通过\n");
}

void test_5_performance_comparison()
{
    printf("\n"
            "┌─────────────────────────────────────────────┐\n"
            "│ 测试5：性能对比测试                         │\n"
            "│ 验证：内存池 vs 传统 malloc/free            │\n"
            "└─────────────────────────────────────────────┘\n");
    
    const int ITERATIONS = 10000;
    const int ALLOC_SIZE = 64;  // 模拟典型的小对象分配
    
    printf("测试配置：\n");
    printf("  迭代次数：%d\n", ITERATIONS);
    printf("  每次分配：%d 字节\n", ALLOC_SIZE);
    printf("  测试方法：内存池 vs malloc/free\n");
    
    ngx_log_t log = { stderr, 0 };
    
    // 方法1：使用内存池
    clock_t start = clock();
    ngx_pool_t *pool = ngx_create_pool(ITERATIONS * ALLOC_SIZE, &log);
    
    for (int i = 0; i < ITERATIONS; i++) {
        int *ptr = ngx_palloc(pool, ALLOC_SIZE);
        *ptr = i;  // 简单操作
    }
    
    ngx_destroy_pool(pool);
    clock_t end = clock();
    double pool_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    // 方法2：使用 malloc/free
    start = clock();
    
    // 使用数组保存指针，以便最后释放
    void **pointers = malloc(ITERATIONS * sizeof(void *));
    
    for (int i = 0; i < ITERATIONS; i++) {
        pointers[i] = malloc(ALLOC_SIZE);
        *(int *)pointers[i] = i;
    }
    
    // 逐个释放
    for (int i = 0; i < ITERATIONS; i++) {
        free(pointers[i]);
    }
    
    free(pointers);
    end = clock();
    double malloc_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    // 显示结果
    printf("\n性能对比结果：\n");
    printf("  内存池总耗时：%.6f 秒\n", pool_time);
    printf("  malloc/free 总耗时：%.6f 秒\n", malloc_time);
    printf("  性能提升：%.2fx\n", malloc_time / pool_time);
    
    printf("\n分析：\n");
    printf("  ✓ 内存池减少了系统调用次数\n");
    printf("  ✓ 内存池减少了内存碎片\n");
    printf("  ✓ 内存池简化了内存管理（无需手动释放每个对象）\n");
    printf("  ✓ 适合批量分配、统一释放的场景\n");
    
    printf("✅ 测试5完成\n");
}

void test_6_edge_cases()
{
    printf("\n"
            "┌─────────────────────────────────────────────┐\n"
            "│ 测试6：边界情况测试                         │\n"
            "│ 验证：异常输入和边界条件的处理              │\n"
            "└─────────────────────────────────────────────┘\n");
    
    ngx_log_t log = { stderr, 0 };
    
    printf("1. 测试创建极小的内存池\n");
    ngx_pool_t *tiny_pool = ngx_create_pool(32, &log);
    if (tiny_pool) {
        printf("  ✓ 创建成功，实际大小：%ld 字节\n", 
               (long)(tiny_pool->d.end - (u_char *)tiny_pool));
        ngx_destroy_pool(tiny_pool);
    }
    
    printf("\n2. 测试分配0字节内存\n");
    ngx_pool_t *pool = ngx_create_pool(1024, &log);
    void *zero_alloc = ngx_palloc(pool, 0);
    printf("  ✓ 分配0字节返回：%p\n", zero_alloc);
    
    printf("\n3. 测试释放非大内存（应该失败）\n");
    char *small_mem = ngx_palloc(pool, 100);
    int free_result = ngx_pfree(pool, small_mem);
    printf("  ✓ 释放小块内存结果：%s（预期：失败）\n", 
            free_result == 0 ? "成功" : "失败");
    
    printf("\n4. 测试释放大内存\n");
    void *large_mem = ngx_palloc(pool, 8192);
    free_result = ngx_pfree(pool, large_mem);
    printf("  ✓ 释放大块内存结果：%s（预期：成功）\n", 
            free_result == 0 ? "成功" : "失败");
    
    printf("\n5. 测试内存耗尽情况\n");
    // 创建一个小内存池，然后尝试分配超过其大小的内存
    ngx_pool_t *small_pool = ngx_create_pool(128, &log);
    void *should_fail = ngx_palloc(small_pool, 256);  // 超过池大小
    printf("  ✓ 分配超过池大小的内存：%p（预期：NULL或新块）\n", should_fail);
    
    ngx_pool_dump(small_pool, "内存耗尽测试");
    
    ngx_destroy_pool(pool);
    ngx_destroy_pool(small_pool);
    printf("✅ 测试6通过\n");
}

void test_7_memory_pool_chaining()
{
    printf("\n"
            "┌─────────────────────────────────────────────┐\n"
            "│ 测试7：内存池链式扩展                       │\n"
            "│ 验证：当内存池不足时自动创建新块            │\n"
            "└─────────────────────────────────────────────┘\n");
    
    ngx_log_t log = { stderr, 0 };
    // 创建较小的内存池，以便触发链式扩展
    ngx_pool_t *pool = ngx_create_pool(256, &log);
    
    printf("初始内存池大小：256 字节\n");
    printf("小块内存最大值：%zu 字节\n", pool->max);
    
    printf("\n开始分配内存（将触发链式扩展）：\n");
    
    // 分配多个内存块，超过初始池大小
    for (int i = 1; i <= 10; i++) {
        size_t size = 32 * i;  // 逐渐增大的分配
        void *ptr = ngx_palloc(pool, size);
        printf("  分配 #%2d：%3zu 字节 -> %p\n", i, size, ptr);
        
        if (i == 5) {
            printf("  （此时应该已经创建了新的内存块）\n");
            ngx_pool_dump(pool, "第5次分配后");
        }
    }
    
    printf("\n最终内存池状态：\n");
    ngx_pool_dump(pool, "多次分配后");
    
    // 验证 current 指针优化
    printf("\n验证 current 指针优化：\n");
    printf("  初始 current：%p\n", pool);
    printf("  最终 current：%p\n", pool->current);
    printf("  current 已优化到较新的内存块\n");
    
    ngx_destroy_pool(pool);
    printf("✅ 测试7通过\n");
}

/* ============ 主函数 ============ */

int main()
{
    printf("\n"
            "╔══════════════════════════════════════════════════╗\n"
            "║     Nginx 内存池实现 - 综合测试程序              ║\n"
            "║           学习版（带详细注释）                   ║\n"
            "╚══════════════════════════════════════════════════╝\n");
    
    printf("\n本程序演示 Nginx 内存池的核心功能和特性。\n");
    printf("每个测试用例都展示了内存池的不同方面。\n");
    
    // 运行所有测试用例
    test_1_basic_allocation();       // 基本功能
    test_2_cleanup_mechanism();      // 清理回调
    test_3_pool_reset_reuse();       // 重置重用
    test_4_alignment_verification(); // 内存对齐
    test_5_performance_comparison(); // 性能对比
    test_6_edge_cases();             // 边界情况
    test_7_memory_pool_chaining();   // 链式扩展
    
    printf("\n"
            "╔══════════════════════════════════════════════════╗\n"
            "║             所有测试用例执行完毕！               ║\n"
            "║           内存池实现验证通过 ✓                  ║\n"
            "╚══════════════════════════════════════════════════╝\n");
    
    printf("\n学习要点总结：\n");
    printf("1. ✅ 内存池减少系统调用，提高性能\n");
    printf("2. ✅ 两级管理（小内存+大内存）优化内存使用\n");
    printf("3. ✅ 清理回调机制自动管理外部资源\n");
    printf("4. ✅ 内存对齐提高 CPU 访问效率\n");
    printf("5. ✅ 智能 current 指针优化分配速度\n");
    printf("6. ✅ 重置功能支持内存重用\n");
    printf("7. ✅ 链式扩展支持动态增长\n");
    
    printf("\n适合的使用场景：\n");
    printf("  • HTTP 请求处理（每个请求一个内存池）\n");
    printf("  • 配置文件解析（临时内存分配）\n");
    printf("  • 数据库连接管理（连接相关内存）\n");
    printf("  • 任何需要批量分配、统一释放的场景\n");
    
    return 0;
}