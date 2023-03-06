#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <list>
#include <iostream>
#include <exception>
#include <memory>
#include <pthread.h>
#include "../lock/locker.h"

/* 
    单生产者多消费者模型：
        - 生产者：主线程
        - 消费者：线程池中的子线程
        - 互斥锁：解决互斥问题，任务请求队列是共享资源，主线程将新任务
        加入到队列中这一操作和若干个子线程从队列中取数据的操作是互斥的
        - 信号量：解决同步问题，当任务队列中没有任务是，必须主线程先放任务，
        子线程再取任务。主线程append的时候进行了post操作，即同步信号量的V
        操作，而子线程的wait执行了P操作
 */
/* 线程池类，定义为模板类方便代码复用，T是任务类 */
template<typename T>
class ThreadPool {
public:
    /* thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量 */
    ThreadPool(int thread_number=8, int max_requests=10000);
    ~ThreadPool();
    /* 往请求队列中添加任务 */
    bool append(T* request);
private:
    /* 工作线程运行的函数，它不断从工作队列中取出任务并执行之 */
    static void* worker(void* arg);
    void run();
private:
    int m_thread_number;        // 线程池中的线程数
    int m_max_requests;         // 请求队列中允许的最大请求数
    std::unique_ptr<pthread_t[]> m_threads;       // 描述线程池的数组，其大小为m_thread_number
    std::list<T*> m_workqueue;  // 请求队列
    locker m_queuelocker;       // 保护请求队列的互斥锁
    sem m_queuestat;            // 是否有任务需要处理
    bool m_stop;                // 是否结束线程
};

/* thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量 */
template<typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests) 
    : m_thread_number(thread_number), m_max_requests(max_requests),
      m_stop(false), m_threads(new pthread_t[thread_number] ) {
    if (thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }
   
    if (!m_threads) {
        throw std::exception();
    }
    /* 创建thread_number个子线程，并将它们都设置为脱离线程 */
    for (int i = 0; i < thread_number; ++i) {
        std::cout << "create the " << i << "th thread" << std::endl;
        if (pthread_create(&m_threads[i], NULL, worker, this) != 0) {
            
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
            throw std::exception();
        }
    }

}
template<typename T>
ThreadPool<T>::ThreadPool::~ThreadPool() {
    m_stop = true;
}
/* 往请求队列中添加任务 */
template<typename T>
bool ThreadPool<T>::append(T* request) {
    /* 操作工作队列时一定要加锁，因为它被所有线程共享 */
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}
/* 工作线程运行的函数，它不断从工作队列中取出任务并执行之 */
template<typename T>
void* ThreadPool<T>::worker(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    pool->run();
    return pool;
}
template<typename T>
void ThreadPool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request) {
            continue;
        }
        request->process();
    }
}
#endif