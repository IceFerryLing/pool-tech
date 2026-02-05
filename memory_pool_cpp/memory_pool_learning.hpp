/**
 * @file memory_pool_learning.hpp
 * @brief Nginx风格内存池学习版 (C++11)
 * @date 2025-12-07
 * 
 * 设计目标：帮助大一学生理解内存池的核心概念
 * 
 * 内存池是什么？
 *   内存池是一种"批量批发，零售使用"的内存管理技术。
 *   想象一下：
 *     - 普通malloc：每次买一瓶水都去超市排队结账
 *     - 内存池：一次性批发一箱水，随用随取
 * 
 * 为什么需要内存池？
 *   1. 减少malloc/free调用次数（提高效率）
 *   2. 减少内存碎片（像整理书桌一样整理内存）
 *   3. 自动清理（不需要手动管理每个分配）
 *   4. 适合特定场景（如网络服务器处理请求）
 * 
 * 内存池的核心设计思想：
 *   小块内存池   大块内存池   清理回调
 *   (零售区)    (批发区)    (收银台)
 * 
 * 学习要点（请对照代码中的标记）：
 *   1. 内存对齐原理与实现
 *   2. 大小块分离管理策略
 *   3. 链表数据结构在内存池中的应用
 *   4. RAII（资源获取即初始化）原则
 *   5. 智能指针简化资源管理
 *   6. 函数对象作为清理回调
 */

#ifndef MEMORY_POOL_LEARNING_HPP
#define MEMORY_POOL_LEARNING_HPP

#include <cstddef>      // size_t, ptrdiff_t
#include <cstdint>      // uintptr_t (用于地址计算)
#include <cstdlib>      // malloc, free, aligned_alloc
#include <cstring>      // memset (内存清零)
#include <functional>   // std::function (函数对象)
#include <memory>       // std::unique_ptr (智能指针)
#include <iostream>     // 输出调试信息
#include <stdexcept>    // std::bad_alloc (异常处理)

// 配置常量
// 这些常量控制内存池的行为，可以调整它们来观察效果

// 内存对齐要求（字节）
// 为什么需要对齐？
//     CPU从内存读取数据时，如果数据没有对齐到合适的边界，
//     可能需要两次读取操作，降低效率。
//     对齐就像把书整齐地放在书架上，一次可以拿一本，而不是半本。
static constexpr size_t MEMORY_ALIGNMENT = 16;  // 常用值：4, 8, 16, 32

// 小块内存的最大值（4KB-1）
// 为什么是4095而不是4096？
//     因为内存池需要一点额外空间存储管理信息。
//     就像你买一箱24瓶水，实际上只能喝23瓶，留一瓶给箱子本身。
static constexpr size_t MAX_SMALL_BLOCK = 4095;  // 4KB - 1字节

// 默认内存池大小（16KB）
// 这个值影响什么？
//     如果太小：需要频繁创建新内存块（像频繁去超市）
//     如果太大：可能浪费内存（像买太多水喝不完）
static constexpr size_t DEFAULT_POOL_SIZE = 16 * 1024;  // 16KB

// 内存池类定义
class LearningMemoryPool {
public:
    // 内存块类（小块内存管理）
    // 内存块：内存池的基本组成单元
    // 想象一下：内存池像一本笔记本，内存块就是其中的一页纸
    // 当你写满一页，就翻到下一页继续写
    class MemoryBlock {
    public:
        char* page_start;   // 这一页纸的开始位置（固定不变）
        char* writing_pos;  // 当前写入位置（随着写字向后移动）
        char* page_end;     // 这一页纸的结束位置（固定不变）
        MemoryBlock* next_page;  // 指向下一页纸的指针
        size_t failed_tries;     // 这一页纸空间不足的次数
        
        // 构造函数：分配一页"纸"（内存块）
        // @param page_size 这一页纸的大小
        MemoryBlock(size_t page_size) : failed_tries(0), next_page(nullptr) {
            // 对齐分配内存
            // aligned_alloc是C++11的对齐分配函数
            // 要求：alignment必须是2的幂，size必须是alignment的倍数
            page_start = static_cast<char*>(_aligned_malloc(MEMORY_ALIGNMENT, page_size));
            
            if (!page_start) {
                // 分配失败，抛出异常
                // 在真实项目中，这里应该有更复杂的错误处理
                throw std::bad_alloc();
            }
            
            // 初始时，从开头开始写
            writing_pos = page_start;
            // 这一页的结束位置 = 开始位置 + 页大小
            page_end = page_start + page_size;
            
            // 调试信息：可以看到内存块的实际地址
            std::cout << "[BLOCK] 创建新页 地址:" << (void*)page_start << " 大小:" << page_size << "字节" << std::endl;
        }
        
        // 析构函数：归还这一页纸（释放内存）
        // RAII原则：资源获取即初始化
        //     在构造函数中获取资源，在析构函数中释放资源
        //     确保资源不会泄漏，就像借书一定要还
        ~MemoryBlock() {
            if (page_start) {
                std::free(page_start);
                std::cout << "[BLOCK] 释放页面 地址:" << (void*)page_start << std::endl;
            }
        }
        
        // 禁止拷贝（内存块应该唯一）
        MemoryBlock(const MemoryBlock&) = delete;
        MemoryBlock& operator=(const MemoryBlock&) = delete;
        
        // 移动构造函数（C++11特性）
        // 为什么需要移动？
        //     有时我们需要"转移"所有权而不是拷贝
        //     就像把书递给同学，而不是复印一本
        MemoryBlock(MemoryBlock&& other) noexcept
            : page_start(other.page_start),
            writing_pos(other.writing_pos),
            page_end(other.page_end),
            next_page(other.next_page),
            failed_tries(other.failed_tries) {
            // 转移所有权后，原对象不再拥有资源
            other.page_start = nullptr;
            other.writing_pos = nullptr;
            other.page_end = nullptr;
            other.next_page = nullptr;
        }
        
        // 计算这一页纸还剩多少空白
        size_t remaining_space() const {
            return page_end - writing_pos;
        }
        
        // 在这一页纸上"写字"（分配内存）
        // @param size 要写的字节数
        // @param align 是否需要对齐
        // @return 可以开始写字的位置（指针）
        void* write_on_page(size_t size, bool align = true) {
            char* write_at = writing_pos;
            
            // 内存对齐处理
            if (align) {
                // 对齐计算原理：
                // 1. 把指针转换为整数（地址值）
                uintptr_t address = reinterpret_cast<uintptr_t>(write_at);
                // 2. 计算需要向上对齐到alignment的倍数
                //    公式：(address + alignment - 1) & ~(alignment - 1)
                //    这就像把书放到书架上，必须从某个格子开始
                address = (address + MEMORY_ALIGNMENT - 1) & ~(MEMORY_ALIGNMENT - 1);
                // 3. 转换回指针
                write_at = reinterpret_cast<char*>(address);
            }
            
            // 检查剩余空间是否足够
            if (write_at + size <= page_end) {
                // 空间足够，更新写入位置
                writing_pos = write_at + size;
                return write_at;
            }
            
            // 空间不足
            failed_tries++;
            return nullptr;
        }
    };
    
    // 大内存块类（单独管理大内存）
    // 大内存块：单独管理的"大件物品"
    // 为什么大内存要单独管理？
    //     1. 大内存使用频率低，单独管理更灵活
    //     2. 避免大内存占用小块内存池的空间
    //     3. 可以单独释放大内存
    //     想象：图书馆里的大部头书放在专门的书架，不跟普通书混放
    struct BigMemoryItem {
        void* memory;          // 实际分配的大内存
        size_t size;           // 内存大小
        BigMemoryItem* next;   // 下一个大内存块
        
        // 构造函数：直接分配大内存
        BigMemoryItem(size_t s) : size(s), next(nullptr) {
            memory = std::malloc(s);
            if (!memory) throw std::bad_alloc();
            
            std::cout << "[BIG] 分配大内存 地址:" << memory << " 大小:" << s << "字节" << std::endl;
        }
        
        // 析构函数：释放大内存
        ~BigMemoryItem() {
            if (memory) {
                std::free(memory);
                std::cout << "[BIG] 释放大内存 地址:" << memory << std::endl;
            }
        }
    };
    
    // 清理回调类（自动清理资源）
    // 清理回调：内存池销毁时的"自动清洁工"
    // 用途：释放文件、网络连接、锁等外部资源
    //     就像离开教室前，值日生负责关灯、关门
    class CleanupHelper {
    public:
        using CleanupFunction = std::function<void(void*)>;
        
        CleanupFunction cleaner;  // 清理函数
        void* data;               // 清理时需要的数据
        CleanupHelper* next;      // 下一个清理任务
        
        // 构造函数
        CleanupHelper(CleanupFunction func, void* d) 
            : cleaner(std::move(func)), data(d), next(nullptr) {
            std::cout << "[CLEANUP] 注册清理任务" << std::endl;
        }
        
        // 执行清理工作
        void do_cleanup() {
            if (cleaner) {
                cleaner(data);
            }
        }
    };

private:
    // 内存池的核心数据成员
    
    // 第一页纸（第一个内存块）
    // 使用unique_ptr智能指针管理：
    //     1. 自动释放内存（不需要手动delete）
    //     2. 明确所有权（这个内存块属于内存池）
    //     3. 防止内存泄漏
    std::unique_ptr<MemoryBlock> first_page_;
    
    // 当前正在使用的页面
    // 为什么需要current_page_？
    //     优化：跳过那些经常空间不足的页面
    //     就像读书时，遇到难懂的章节先跳过，回头再看
    MemoryBlock* current_page_;
    
    // 大内存物品的链表头
    // 链表结构：适合频繁插入删除的场景
    //     BigMemoryItem1 -> BigMemoryItem2 -> BigMemoryItem3 -> nullptr
    BigMemoryItem* big_items_head_;
    
    // 清理回调的链表头
    CleanupHelper* cleanup_head_;
    
    // 小块内存的最大值
    // 这个值动态计算：取配置值和实际页面大小的较小值
    size_t max_small_size_;

public:
    // 公有接口
    
    // 构造函数：创建内存池
    // @param initial_size 初始内存池大小
    // 
    // 构造过程：
    //     1. 检查并调整大小
    //     2. 创建第一个内存块
    //     3. 初始化各个指针
    //     4. 计算小块内存最大值
    explicit LearningMemoryPool(size_t initial_size = DEFAULT_POOL_SIZE) 
        : current_page_(nullptr), 
        big_items_head_(nullptr),
        cleanup_head_(nullptr),
        max_small_size_(MAX_SMALL_BLOCK) {
        
        std::cout << "\n开始创建内存池..." << std::endl;
        std::cout << "   初始大小要求: " << initial_size << "字节" << std::endl;
        
        // 步骤1：确保内存池足够大
        // 最小需要能放下MemoryBlock对象本身
        size_t min_required = sizeof(MemoryBlock) + MEMORY_ALIGNMENT;
        if (initial_size < min_required) {
            initial_size = min_required;
            std::cout << "   调整大小到: " << initial_size << "字节" << std::endl;
        }
        
        // 步骤2：创建第一个内存块
        try {
            first_page_ = std::make_unique<MemoryBlock>(initial_size);
            current_page_ = first_page_.get();
            
            std::cout << "   创建第一个内存块成功" << std::endl;
            std::cout << "      地址: " << (void*)first_page_->page_start << std::endl;
            std::cout << "      大小: " << initial_size << "字节" << std::endl;
        } catch (const std::bad_alloc& e) {
            std::cerr << "   内存分配失败: " << e.what() << std::endl;
            throw;  // 向上传播异常
        }
        
        // 步骤3：计算实际可用的小块内存最大值
        // 可用空间 = 页面总大小 - MemoryBlock对象本身占用的空间
        size_t available_space = initial_size - sizeof(MemoryBlock);
        max_small_size_ = std::min(max_small_size_, available_space);
        
        std::cout << "   小块内存最大值: " << max_small_size_ << "字节" << std::endl;
        std::cout << "   内存池创建完成！" << std::endl;
    }
    
    // 析构函数：自动清理所有资源
    // 析构顺序：
    //     1. 执行所有清理回调（关闭文件等）
    //     2. 释放所有大内存
    //     3. 智能指针自动释放内存块
    ~LearningMemoryPool() {
        std::cout << "\n开始清理内存池..." << std::endl;
        
        // 步骤1：执行清理回调
        std::cout << "   1. 执行清理回调..." << std::endl;
        while (cleanup_head_) {
            CleanupHelper* current = cleanup_head_;
            cleanup_head_ = cleanup_head_->next;
            
            current->do_cleanup();
            delete current;  // 注意：这里delete是因为我们用了new创建
        }
        
        // 步骤2：释放大内存
        std::cout << "   2. 释放大内存..." << std::endl;
        while (big_items_head_) {
            BigMemoryItem* current = big_items_head_;
            big_items_head_ = big_items_head_->next;
            
            delete current;  // BigMemoryItem的析构函数会释放内存
        }
        
        // 步骤3：智能指针自动释放内存块
        std::cout << "   3. 释放内存块（智能指针自动处理）" << std::endl;
        std::cout << "   内存池清理完成！" << std::endl;
    }
    
    // 主分配函数（自动处理对齐）
    // @param size 需要的内存大小
    // @return 分配的内存指针，失败返回nullptr
    // 
    // 分配策略：
    //     if (size <= max_small_size_)
    //         // 小块：从现有页面分配
    //     else
    //         // 大块：单独分配
    void* allocate(size_t size) {
        if (size == 0) {
            return nullptr;
        }
        
        std::cout << "\n申请分配: " << size << "字节" << std::endl;
        
        // 步骤1：判断大小，选择分配策略
        if (size <= max_small_size_) {
            std::cout << "   属于小块内存，尝试从页面分配..." << std::endl;
            return allocate_small(size, true);  // 需要对齐
        } else {
            std::cout << "   属于大块内存，单独分配..." << std::endl;
            return allocate_big(size);
        }
    }
    
    // 分配内存但不保证对齐（用于学习对齐的重要性）
    void* allocate_unaligned(size_t size) {
        if (size <= max_small_size_) {
            return allocate_small(size, false);  // 不需要对齐
        } else {
            return allocate_big(size);
        }
    }
    
    // 分配内存并清零（calloc的替代）
    // 清零的作用：
    //     1. 防止使用未初始化的内存
    //     2. 安全考虑（避免泄露之前的数据）
    void* allocate_zeroed(size_t size) {
        void* memory = allocate(size);
        if (memory) {
            std::memset(memory, 0, size);
            std::cout << "   内存已清零" << std::endl;
        }
        return memory;
    }
    
    // 注册清理回调
    // 用途示例：
    //     pool.add_cleanup([](void* file) {
    //         fclose(static_cast<FILE*>(file));
    //     }, my_file);
    void add_cleanup(CleanupHelper::CleanupFunction cleaner, void* data = nullptr) {
        // 分配CleanupHelper对象（使用new，因为需要手动管理生命周期）
        CleanupHelper* new_cleanup = new CleanupHelper(std::move(cleaner), data);
        
        // 插入链表头部（头插法，最简单）
        new_cleanup->next = cleanup_head_;
        cleanup_head_ = new_cleanup;
        
        std::cout << "   清理回调注册成功" << std::endl;
    }
    
    // 重置内存池（清空内容但不释放内存）
    // 适用场景：
    //     处理完一个请求后，重置内存池处理下一个请求
    //     避免反复创建销毁内存池的开销
    void reset() {
        std::cout << "\n重置内存池..." << std::endl;
        
        // 步骤1：执行清理回调
        std::cout << "   1. 执行清理回调..." << std::endl;
        while (cleanup_head_) {
            CleanupHelper* current = cleanup_head_;
            cleanup_head_ = cleanup_head_->next;
            
            current->do_cleanup();
            delete current;
        }
        
        // 步骤2：释放大内存（大内存不保留）
        std::cout << "   2. 释放大内存..." << std::endl;
        while (big_items_head_) {
            BigMemoryItem* current = big_items_head_;
            big_items_head_ = big_items_head_->next;
            delete current;
        }
        
        // 步骤3：重置所有内存块的写入位置
        std::cout << "   3. 重置内存块..." << std::endl;
        MemoryBlock* block = first_page_.get();
        while (block) {
            // 把写入位置重置到页面开头
            block->writing_pos = block->page_start;
            // 重置失败计数
            block->failed_tries = 0;
            
            std::cout << "      重置块: " << (void*)block->page_start << " 失败计数清零" << std::endl;
            
            block = block->next_page;
        }
        
        // 步骤4：重置当前页面指针
        current_page_ = first_page_.get();
        
        std::cout << "   内存池重置完成！" << std::endl;
    }
    
    // 统计和调试功能
    
    // 内存池统计信息
    struct Statistics {
        size_t block_count = 0;       // 内存块数量
        size_t big_item_count = 0;    // 大内存块数量
        size_t small_used = 0;        // 已使用的小块内存字节数
        size_t big_used = 0;          // 已使用的大块内存字节数
        
        // 打印统计信息
        void print() const {
            std::cout << "\n内存池统计信息：" << std::endl;
            std::cout << "   内存块数量: " << block_count << std::endl;
            std::cout << "   大内存块数量: " << big_item_count << std::endl;
            std::cout << "   小块内存使用量: " << small_used << "字节" << std::endl;
            std::cout << "   大块内存使用量: " << big_used << "字节" << std::endl;
            std::cout << "   总内存使用量: " << (small_used + big_used) << "字节" << std::endl;
        }
    };
    
    // 获取当前统计信息
    Statistics get_statistics() const {
        Statistics stats;
        
        // 统计内存块
        MemoryBlock* block = first_page_.get();
        while (block) {
            stats.block_count++;
            // 已使用的小块内存 = 当前写入位置 - 页面开始位置
            stats.small_used += (block->writing_pos - block->page_start);
            block = block->next_page;
        }
        
        // 统计大内存
        BigMemoryItem* big = big_items_head_;
        while (big) {
            stats.big_item_count++;
            stats.big_used += big->size;
            big = big->next;
        }
        
        return stats;
    }
    
    // 调试功能：打印内存池详细信息
    // 学习建议：运行后仔细查看输出，理解内存池的结构
    void debug_print(const std::string& title = "") const {
        std::cout << "\n==== 内存池调试信息 ";
        if (!title.empty()) {
            std::cout << "[" << title << "] ";
        }
        std::cout << "====" << std::endl;
        
        // 打印所有内存块
        std::cout << "内存块列表：" << std::endl;
        int block_num = 1;
        MemoryBlock* block = first_page_.get();
        
        while (block) {
            std::cout << "   " << block_num << ". 地址: " << (void*)block->page_start << std::endl;
            std::cout << "      范围: " << (void*)block->page_start 
                    << " ~ " << (void*)block->page_end 
                    << " (大小: " << (block->page_end - block->page_start) << "字节)" << std::endl;
            std::cout << "      已使用: " << (block->writing_pos - block->page_start) << "字节" << std::endl;
            std::cout << "      剩余: " << block->remaining_space() << "字节" << std::endl;
            std::cout << "      失败次数: " << block->failed_tries << std::endl;
            std::cout << "      下一页: " << (void*)block->next_page << std::endl;
            
            block = block->next_page;
            block_num++;
        }
        
        // 打印大内存
        std::cout << "\n大内存列表：" << std::endl;
        int big_num = 1;
        BigMemoryItem* big = big_items_head_;
        
        while (big) {
            std::cout << "   " << big_num << ". 地址: " << big->memory << std::endl;
            std::cout << "      大小: " << big->size << "字节" << std::endl;
            
            big = big->next;
            big_num++;
        }
        
        if (big_num == 1) {
            std::cout << "   (无大内存)" << std::endl;
        }
        
        // 打印清理回调
        std::cout << "\n清理回调列表：" << std::endl;
        int cleanup_num = 1;
        CleanupHelper* cleanup = cleanup_head_;
        
        while (cleanup) {
            std::cout << "   " << cleanup_num << ". 清理函数: " << (void*)&cleanup->cleaner << std::endl;
            std::cout << "      数据: " << cleanup->data << std::endl;
            
            cleanup = cleanup->next;
            cleanup_num++;
        }
        
        if (cleanup_num == 1) {
            std::cout << "   (无清理回调)" << std::endl;
        }
        
        // 打印统计信息
        get_statistics().print();
        
        std::cout << "==============================" << std::endl;
    }

private:
    // 私有辅助函数
    
    // 分配小块内存
    // @param size 需要的大小
    // @param align 是否需要对齐
    // @return 分配的内存指针
    // 
    // 算法步骤：
    //     1. 从当前页面开始尝试
    //     2. 如果当前页面空间不足，尝试下一个页面
    //     3. 如果所有页面都空间不足，创建新页面
    void* allocate_small(size_t size, bool align) {
        MemoryBlock* block = current_page_;
        
        // 步骤1：遍历现有页面
        while (block) {
            // 在当前页面尝试分配
            void* memory = block->write_on_page(size, align);
            
            if (memory) {
                // 分配成功
                std::cout << "   分配成功 地址:" << memory << " 大小:" << size << "字节" << std::endl;
                std::cout << "      来自页面:" << (void*)block->page_start << std::endl;
                
                // 如果这个页面失败次数太多，跳过它（优化）
                if (block->failed_tries > 4) {
                    current_page_ = block->next_page;
                    std::cout << "      页面失败次数过多，跳过此页面" << std::endl;
                }
                
                return memory;
            }
            
            // 当前页面空间不足，尝试下一个
            std::cout << "   页面" << (void*)block->page_start << "空间不足，尝试下一个..." << std::endl;
            block = block->next_page;
        }
        
        // 步骤2：所有页面都满了，创建新页面
        std::cout << "   所有页面都满了，创建新页面..." << std::endl;
        return create_new_page(size);
    }
    
    // 创建新内存页
    // @param size 需要的最小空间（用于预留）
    // @return 新页面中的内存指针
    // 
    // 创建过程：
    //     1. 计算新页面大小（与第一个页面相同）
    //     2. 分配新页面
    //     3. 将新页面连接到链表
    //     4. 预留请求的空间
    void* create_new_page(size_t size) {
        // 步骤1：确定新页面大小
        // 通常与第一个页面大小相同，保持一致性
        size_t page_size = first_page_->page_end - first_page_->page_start;
        
        // 步骤2：创建新页面
        std::unique_ptr<MemoryBlock> new_page;
        try {
            new_page = std::make_unique<MemoryBlock>(page_size);
        } catch (const std::bad_alloc&) {
            std::cerr << "   创建新页面失败：内存不足" << std::endl;
            return nullptr;
        }
        
        // 步骤3：连接到链表
        // 找到链表末尾
        MemoryBlock* last_page = first_page_.get();
        while (last_page->next_page) {
            last_page = last_page->next_page;
        }
        
        // 连接到末尾
        last_page->next_page = new_page.get();
        
        // 步骤4：预留空间
        // 在新页面中立即分配请求的内存
        void* memory = new_page->write_on_page(size, true);
        
        // 步骤5：更新当前页面指针
        current_page_ = new_page.get();
        
        // 步骤6：转移所有权（重要！）
        // 现在new_page归链表管理，释放unique_ptr的控制权
        new_page.release();  // 不释放内存，只是放弃管理权
        
        std::cout << "   新页面创建成功，已分配内存" << std::endl;
        return memory;
    }
    
    // 分配大块内存
    // @param size 需要的大小
    // @return 分配的内存指针
    // 
    // 与小块内存的区别：
    //     1. 直接使用malloc分配
    //     2. 单独管理，不占用页面空间
    //     3. 可以单独释放
    void* allocate_big(size_t size) {
        try {
            // 创建大内存项
            BigMemoryItem* new_big = new BigMemoryItem(size);
            
            // 插入链表头部（头插法）
            new_big->next = big_items_head_;
            big_items_head_ = new_big;
            
            std::cout << "   大内存分配成功" << std::endl;
            return new_big->memory;
        } catch (const std::bad_alloc&) {
            std::cerr << "   大内存分配失败" << std::endl;
            return nullptr;
        }
    }
    
    // 禁止拷贝（内存池应该唯一）
    LearningMemoryPool(const LearningMemoryPool&) = delete;
    LearningMemoryPool& operator=(const LearningMemoryPool&) = delete;
};

#endif // MEMORY_POOL_LEARNING_HPP