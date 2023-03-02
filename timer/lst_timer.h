#ifndef LST_TIMER
#define LST_TIMER

#include <ctime>
#include <arpa/inet.h>
#include <stdio.h>
#include "../log/log.h"

#define BUFFER_SIZE 64

class UtilTimer;  // 前向声明

/* 用户数据结构 */
struct client_data
{
    sockaddr_in address;    // 客户端socket地址
    int sockfd;             // socket文件描述符
    char buf[BUFFER_SIZE];  // 读缓存
    UtilTimer* timer;      // 定时器
};

/* 定时器类 */
class UtilTimer {
public:
    UtilTimer() : prev(nullptr), next(nullptr) {}
public:
    time_t expire;                  // 任务的超时时间
    void (*cb_func) (client_data*); // 任务回调函数
    client_data* user_data;         // 回调函数处理的客户数据，由定时器的执行者传递给回调函数
    UtilTimer* prev;                // 指向前一个定时器
    UtilTimer* next;                // 指向下一个定时器
};

/* 定时器链表，升序、双向链表，带有头节点和尾节点 */
class SortTimerLST
{
public:
    SortTimerLST() : head(nullptr), tail(nullptr), m_close_log(0) {};
    SortTimerLST(UtilTimer* h, UtilTimer* n, int close_log) : head(h), tail(n), m_close_log(close_log) {};
    ~SortTimerLST();
    void add_timer(UtilTimer* timer);       // 将目标定时器timer添加到链表中
    void adjust_timer(UtilTimer* timer);    // 当某个定时任务发生变化时，调整对应的定时器在链表中的位置
    void del_timer(UtilTimer* timer);       // 将目标定时器从链表中删除
    void tick();    // SIGALRM信号每次出发就在其信号处理函数（如果使用统一事件源，则是主函数）中执行一次tick函数，以处理链表上到期的任务

private:
    void add_timer(UtilTimer* timer, UtilTimer* lst_head);  // 将目标定时器timer添加到节点lst_head之后的部分链表中
private:
    UtilTimer* head;
    UtilTimer* tail;
    int m_close_log;
};

#endif