#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
/* 线程同步机制封装类 */

/* 封装互斥锁的类 */
class locker
{
public:
    /* 创建并初始化互斥锁 */
    locker();
    /* 销毁互斥锁 */
    ~locker();
    /* 获取互斥锁 */
    bool lock();
    /* 释放互斥锁 */
    bool unlock();
    /* 获取*/
    pthread_mutex_t* get();
private:
    pthread_mutex_t m_mutex;
};

/* 封装条件变量的类 */
class cond
{
public:
    /* 创建并初始化条件变量 */
    cond();
    /* 销毁条件变量 */
    ~cond();
    /* 等待条件变量 */
    bool wait(pthread_mutex_t* m_mutex);
    /* 等待条件变量一段时间 */
    bool timewait(pthread_mutex_t* m_mutex, struct timespec t);
    /* 唤醒等待条件变量的线程 */
    bool signal();
    bool broadcast();
private:
    pthread_cond_t m_cond;
    // pthread_mutex_t m_mutex;
};

/* 封装信号量的类 */
class sem {
public:
    /* 创建并初始化信号量 */
    sem();
    /* 创建并以指定值初始化信号量 */
    sem(int num);
    /* 销毁信号量 */
    ~sem();
    /* 等待信号量 */
    bool wait();
    /* 增加信号量 */
    bool post();
private:
    sem_t m_sem;
};

#endif