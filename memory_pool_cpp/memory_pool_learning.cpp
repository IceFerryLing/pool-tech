/**
 * @file memory_pool_demo.cpp
 * @brief 内存池学习示例程序
 * @date 2025-12-07
 * 
 * 学习目标：通过实际代码理解内存池的工作原理
 * 
 * 实验建议：
 *   1. 先通读代码，理解每个函数的作用
 *   2. 运行程序，观察输出
 *   3. 修改配置常量（如MEMORY_ALIGNMENT），观察变化
 *   4. 尝试自己添加功能（如内存泄漏检测）
 * 
 * 运行步骤：
 *   g++ -std=c++11 -o memory_pool_demo memory_pool_demo.cpp
 *   ./memory_pool_demo
 */

#include "memory_pool_learning.hpp"
#include <string>

// 示例1：基础数据类型分配
void demo_basic_types(LearningMemoryPool& pool) {
    std::cout << "\n示例1：基础数据类型分配" << std::endl;
    
    // 分配一个整数
    int* number = static_cast<int*>(pool.allocate(sizeof(int)));
    if (number) {
        *number = 42;
        std::cout << "   分配的整数: " << *number 
                << " 地址: " << (void*)number << std::endl;
    }
    
    // 分配一个浮点数
    double* pi = static_cast<double*>(pool.allocate(sizeof(double)));
    if (pi) {
        *pi = 3.141592653589793;
        std::cout << "   分配的浮点数: " << *pi 
                << " 地址: " << (void*)pi << std::endl;
    }
    
    // 分配一个字符数组
    char* text = static_cast<char*>(pool.allocate(20));
    if (text) {
        std::strcpy(text, "Hello, Memory Pool!");
        std::cout << "   分配的字符串: " << text 
                << " 地址: " << (void*)text << std::endl;
    }
}

// 示例2：对齐与不对齐的对比
void demo_alignment_compare(LearningMemoryPool& pool) {
    std::cout << "\n示例2：对齐对比" << std::endl;
    
    // 分配不对齐的内存
    void* unaligned = pool.allocate_unaligned(7);
    std::cout << "   不对齐分配 (7字节): " << unaligned << std::endl;
    std::cout << "     地址值: " << reinterpret_cast<uintptr_t>(unaligned) << std::endl;
    std::cout << "     对齐检查: " << (reinterpret_cast<uintptr_t>(unaligned) % MEMORY_ALIGNMENT) 
            << " (0表示对齐)" << std::endl;
    
    // 分配对齐的内存
    void* aligned = pool.allocate(7);
    std::cout << "   对齐分配 (7字节): " << aligned << std::endl;
    std::cout << "     地址值: " << reinterpret_cast<uintptr_t>(aligned) << std::endl;
    std::cout << "     对齐检查: " << (reinterpret_cast<uintptr_t>(aligned) % MEMORY_ALIGNMENT) 
            << " (0表示对齐)" << std::endl;
}

// 示例3：大内存分配
void demo_big_allocation(LearningMemoryPool& pool) {
    std::cout << "\n示例3：大内存分配" << std::endl;
    
    // 分配大内存（超过小块阈值）
    size_t big_size = 8192;  // 8KB
    std::cout << "   申请大内存: " << big_size << "字节" << std::endl;
    
    void* big_memory = pool.allocate(big_size);
    if (big_memory) {
        std::cout << "   大内存分配成功: " << big_memory << std::endl;
        
        // 使用大内存
        char* buffer = static_cast<char*>(big_memory);
        for (size_t i = 0; i < 10; ++i) {
            buffer[i] = 'A' + i;
        }
        buffer[10] = '\0';
        std::cout << "   写入数据: " << buffer << std::endl;
    }
}

// 示例4：清零内存分配
void demo_zeroed_allocation(LearningMemoryPool& pool) {
    std::cout << "\n示例4：清零内存" << std::endl;
    
    // 分配并清零
    int* zeroed_array = static_cast<int*>(pool.allocate_zeroed(10 * sizeof(int)));
    
    if (zeroed_array) {
        std::cout << "   分配的数组地址: " << zeroed_array << std::endl;
        
        // 检查是否真的清零
        bool all_zero = true;
        for (int i = 0; i < 10; ++i) {
            if (zeroed_array[i] != 0) {
                all_zero = false;
                break;
            }
        }
        
        if (all_zero) {
            std::cout << "   数组已正确清零" << std::endl;
        } else {
            std::cout << "   数组未清零" << std::endl;
        }
    }
}

// 示例5：自定义数据结构
struct Student {
    std::string name;
    int age;
    double gpa;
    
    void print() const {
        std::cout << "     姓名: " << name << ", 年龄: " << age << ", GPA: " << gpa << std::endl;
    }
};

void demo_custom_struct(LearningMemoryPool& pool) {
    std::cout << "\n示例5：自定义数据结构" << std::endl;
    
    // 分配Student对象
    Student* student = static_cast<Student*>(pool.allocate(sizeof(Student)));
    
    if (student) {
        // 使用"定位new"在分配的内存上构造对象
        new (student) Student();  // 调用构造函数
        
        student->name = "张三";
        student->age = 20;
        student->gpa = 3.8;
        
        std::cout << "   学生对象分配成功" << std::endl;
        student->print();
        
        // 注意：需要手动调用析构函数（如果对象有非平凡析构）
        student->~Student();
    }
}

// 示例6：清理回调演示
#include <cstdio>

void demo_cleanup_callbacks(LearningMemoryPool& pool) {
    std::cout << "\n示例6：清理回调" << std::endl;
    
    // 模拟打开文件
    FILE* fake_file = reinterpret_cast<FILE*>(0x12345678);
    std::cout << "   模拟打开文件: " << fake_file << std::endl;
    
    // 注册清理回调
    pool.add_cleanup([](void* file_ptr) {
        FILE* f = reinterpret_cast<FILE*>(file_ptr);
        std::cout << "   清理回调执行: 关闭文件 " << f << std::endl;
        // 实际项目中这里应该是 fclose(f);
    }, fake_file);
    
    // 模拟其他需要清理的资源
    int* resource = static_cast<int*>(pool.allocate(sizeof(int)));
    if (resource) {
        *resource = 100;
        
        pool.add_cleanup([](void* res) {
            int* r = static_cast<int*>(res);
            std::cout << "   清理回调执行: 释放资源，值为 " << *r << std::endl;
        }, resource);
    }
    
    std::cout << "   清理回调已注册，内存池销毁时会自动执行" << std::endl;
}

// 示例7：重置内存池
void demo_reset_pool(LearningMemoryPool& pool) {
    std::cout << "\n示例7：重置内存池" << std::endl;
    
    // 先分配一些内存
    int* before_reset = static_cast<int*>(pool.allocate(sizeof(int)));
    if (before_reset) {
        *before_reset = 999;
        std::cout << "   重置前分配的值: " << *before_reset << std::endl;
    }
    
    // 查看当前状态
    auto stats_before = pool.get_statistics();
    std::cout << "   重置前统计:" << std::endl;
    std::cout << "     小块使用: " << stats_before.small_used << "字节" << std::endl;
    std::cout << "     大块数量: " << stats_before.big_item_count << std::endl;
    
    // 重置内存池
    pool.reset();
    
    // 查看重置后状态
    auto stats_after = pool.get_statistics();
    std::cout << "   重置后统计:" << std::endl;
    std::cout << "     小块使用: " << stats_after.small_used << "字节" << std::endl;
    std::cout << "     大块数量: " << stats_after.big_item_count << std::endl;
    
    // 重置后再次分配
    int* after_reset = static_cast<int*>(pool.allocate(sizeof(int)));
    if (after_reset) {
        *after_reset = 123;
        std::cout << "   重置后分配的值: " << *after_reset << std::endl;
    }
}

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << "      Nginx风格内存池学习程序" << std::endl;
    std::cout << "===========================================" << std::endl;
    
    try {
        // 创建内存池
        std::cout << "\n步骤1：创建内存池" << std::endl;
        LearningMemoryPool pool(1024 * 8);  // 8KB初始大小
        
        // 运行各个示例
        demo_basic_types(pool);
        demo_alignment_compare(pool);
        demo_big_allocation(pool);
        demo_zeroed_allocation(pool);
        demo_custom_struct(pool);
        demo_cleanup_callbacks(pool);
        
        // 查看内存池当前状态
        pool.debug_print("运行示例后");
        
        // 测试重置功能
        demo_reset_pool(pool);
        
        // 再次查看状态
        pool.debug_print("重置后");
        
        std::cout << "\n所有示例运行完成！" << std::endl;
        std::cout << "内存池将在main函数结束时自动销毁" << std::endl;
        std::cout << "观察析构函数的输出，了解自动清理过程" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "\n程序出错: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n===========================================" << std::endl;
    std::cout << "       学习完成，期待你的改进和优化！" << std::endl;
    std::cout << "===========================================" << std::endl;
    
    return 0;
}