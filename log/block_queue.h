#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H
#include <iostream>
#include <stdlib.h>
#include <sys/time.h>
#include <queue>
#include <assert.h>
#include "../lock/locker.h"

template<typename T>
class BlockQueue {
public:
    BlockQueue(int max_size = 1000);
    ~BlockQueue();
    void clear();
    bool full();    // 判断队列是否已满
    bool empty();   // 判断队列是否为空
    bool front(T& value);      // 返回队首元素
    bool back(T& value);       // 返回队尾元素
    int size();     // 返回队列中元素个数
    int max_size(); // 返回队列最大容量
    bool push(const T& item);   // 向队列中添加元素
    bool pop(T& item);          // 元素出队
    bool pop(T& item, int ms_timeout);  // 增加了超时处理
private:
    locker m_mutex;
    cond m_cond;
    std::deque<T> bq;
    int m_max_size;     // 最大容量
};

template<typename T>
BlockQueue<T>::BlockQueue(int max_size) :m_max_size(max_size) {
    assert(max_size > 0);
}

template<typename T>
BlockQueue<T>::~BlockQueue() {
    
}

template<typename T>
void BlockQueue<T>::clear() {
    m_mutex.lock();
    bq.clear();
    m_mutex.unlock();
}

/* 判断队列是否已满 */
template<typename T>
bool BlockQueue<T>::full() {
    m_mutex.lock();
    if (bq.size() >= m_max_size) {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

/* 判断队列是否为空 */
template<typename T>
bool BlockQueue<T>::empty() {
    m_mutex.lock();
    if (0 == bq.size()) {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

/* 返回队首元素 */
template<typename T>
bool BlockQueue<T>::front(T& value) {
    m_mutex.lock();
    if (0 == bq.size()) {
        m_mutex.unlock();
        return false;
    }
    value = bq.front();
    m_mutex.unlock();
    return true;
}

/* 返回队尾元素 */
template<typename T>
bool BlockQueue<T>::back(T& value) {
    m_mutex.lock();
    if (0 == bq.size()) {
        m_mutex.unlock();
        return false;
    }
    value = bq.back();
    m_mutex.unlock();
    return true;
}

template<typename T>
int BlockQueue<T>::size() {
    int tmp = 0;
    m_mutex.lock();
    tmp = bq.size();
    m_mutex.unlock();
    return tmp;
}

template<typename T>
int BlockQueue<T>::max_size() {
    int tmp = 0;
    m_mutex.lock();
    tmp = m_max_size;
    m_mutex.unlock();
    return tmp;
}

/* 往队列添加元素，需要将所有使用队列的线程先唤醒
    当有元素push进队列，相当于生产者生产了一个元素
    若当前没有线程等待条件变量，则唤醒无意义
 */
template<typename T>
bool BlockQueue<T>::push(const T& item) {
    m_mutex.lock();
    if (bq.size() >= m_max_size) {
        m_cond.broadcast();
        m_mutex.unlock();
        return false;
    }
    bq.push_back(item);
    m_cond.broadcast();
    m_mutex.unlock();
    return true;
}

/* pop时，如果当前队列没有元素，将会等待条件变量 */
template<typename T>
bool BlockQueue<T>::pop(T& item) {
    m_mutex.lock();
    while (bq.size() <= 0) {
        if (!m_cond.wait(m_mutex.get())) {
            m_mutex.unlock();
            return false;
        }
    }
    item = bq.front();
    bq.pop_front();
    m_mutex.unlock();
    return true;
}

// 增加了超时处理
template<typename T>
bool BlockQueue<T>::pop(T& item, int ms_timeout) {
    struct timespec t = {0, 0};
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    m_mutex.lock();
    if (bq.size() <= 0) {
        t.tv_sec = now.tv_sec + ms_timeout / 1000;
        t.tv_nsec = (ms_timeout % 1000) * 1000;
        if (!m_cond.timewait(m_mutex.get(), t)) {
            m_mutex.unlock();
            return false;
        }
    }
    if (bq.size() <= 0) {
        m_mutex.unlock();
        return false;
    }
    item = bq.front();
    bq.pop_front();
    m_mutex.unlock();
    return true;
}

#endif