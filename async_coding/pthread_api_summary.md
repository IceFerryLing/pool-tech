# 常见 Pthreads API（pthreads-win32）

本文档总结了 Windows 上 `pthreads-win32` 常用的 pthread API，方便快速查阅。

## 1. 线程创建 / 终止 / 同步

- `pthread_create(pthread_t *tid, const pthread_attr_t *attr, void *(*start)(void *), void *arg)`
- `pthread_join(pthread_t thread, void **retval)`
- `pthread_detach(pthread_t thread)`
- `pthread_exit(void *retval)`
- `pthread_self()`
- `pthread_equal(pthread_t t1, pthread_t t2)`

## 2. 互斥量（Mutex）

- `pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)`
- `pthread_mutex_destroy(pthread_mutex_t *mutex)`
- `pthread_mutex_lock(pthread_mutex_t *mutex)`
- `pthread_mutex_trylock(pthread_mutex_t *mutex)`
- `pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime)`
- `pthread_mutex_unlock(pthread_mutex_t *mutex)`
- `pthread_mutex_consistent(pthread_mutex_t *mutex)`

### Mutex 属性

- `pthread_mutexattr_init / pthread_mutexattr_destroy`
- `pthread_mutexattr_settype / pthread_mutexattr_gettype`
- `pthread_mutexattr_setpshared / pthread_mutexattr_getpshared`
- `pthread_mutexattr_setrobust / pthread_mutexattr_getrobust`

## 3. 条件变量（Condition Variable）

- `pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)`
- `pthread_cond_destroy(pthread_cond_t *cond)`
- `pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)`
- `pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)`
- `pthread_cond_signal(pthread_cond_t *cond)`
- `pthread_cond_broadcast(pthread_cond_t *cond)`

### 条件变量属性

- `pthread_condattr_init / pthread_condattr_destroy`
- `pthread_condattr_setpshared / pthread_condattr_getpshared`

## 4. 读写锁（RWLock）

- `pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr)`
- `pthread_rwlock_destroy(pthread_rwlock_t *lock)`
- `pthread_rwlock_rdlock(pthread_rwlock_t *lock)`
- `pthread_rwlock_tryrdlock(pthread_rwlock_t *lock)`
- `pthread_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *abstime)`
- `pthread_rwlock_wrlock(pthread_rwlock_t *lock)`
- `pthread_rwlock_trywrlock(pthread_rwlock_t *lock)`
- `pthread_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *abstime)`
- `pthread_rwlock_unlock(pthread_rwlock_t *lock)`

- `pthread_rwlockattr_init / pthread_rwlockattr_destroy`
- `pthread_rwlockattr_setpshared / pthread_rwlockattr_getpshared`

## 5. 自旋锁（Spinlock）

- `pthread_spin_init(pthread_spinlock_t *lock, int pshared)`
- `pthread_spin_destroy(pthread_spinlock_t *lock)`
- `pthread_spin_lock(pthread_spinlock_t *lock)`
- `pthread_spin_trylock(pthread_spinlock_t *lock)`
- `pthread_spin_unlock(pthread_spinlock_t *lock)`

## 6. 屏障（Barrier）

- `pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned int count)`
- `pthread_barrier_wait(pthread_barrier_t *barrier)`
- `pthread_barrier_destroy(pthread_barrier_t *barrier)`

- `pthread_barrierattr_init / pthread_barrierattr_destroy`
- `pthread_barrierattr_setpshared / pthread_barrierattr_getpshared`

## 7. 线程局部存储（Thread-specific data）

- `pthread_key_create(pthread_key_t *key, void (*destructor)(void *))`
- `pthread_setspecific(pthread_key_t key, const void *value)`
- `pthread_getspecific(pthread_key_t key)`
- `pthread_key_delete(pthread_key_t key)`

## 8. 取消与一次初始化

- `pthread_cancel(pthread_t thread)`
- `pthread_setcancelstate(int state, int *oldstate)`
- `pthread_setcanceltype(int type, int *oldtype)`
- `pthread_testcancel()`
- `pthread_once(pthread_once_t *once_control, void (*init_routine)(void))`

## 9. 扩展/非标准（pthreads-win32 特有）

- `pthread_kill(pthread_t thread, int sig)`
- `pthread_getw32threadhandle_np(pthread_t thread)`
- `pthread_getw32threadid_np(pthread_t thread)`
- `pthread_win32_process_attach_np / pthread_win32_process_detach_np`
- `pthread_win32_thread_attach_np / pthread_win32_thread_detach_np`
- `pthread_timechange_handler_np(void *)`
- `pthread_delay_np(struct timespec *interval)`
- `pthread_num_processors_np(void)`
- `pthread_getunique_np(pthread_t thread)`

## 10. 典型组合

1. 共享队列线程池：`pthread_mutex + pthread_cond`
2. 读多写少：`pthread_rwlock`
3. 线程一次初始化：`pthread_once`
4. 线程私有变量：`pthread_key_create + pthread_setspecific + pthread_getspecific`