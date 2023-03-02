#include "./lst_timer.h"

/* 链表被销毁时，删除其中所有的定时器 */
SortTimerLST::~SortTimerLST() {
    UtilTimer* tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

/* 将目标定时器timer添加到链表中 */
void SortTimerLST::add_timer(UtilTimer* timer) {
    if (!timer) {
        return;
    }
    if (!head) {
        head = tail = timer;
        return;
    }
    // 如果目标定时器的超时时间小于当前链表中所有定时器的超时时间，
    // 则把该定时器插入链表头部，作为链表的新节点，否则调用重载函数
    // add_timer(UtilTimer* timer, UtilTimer* lst_head)把它插入
    // 链表中合适的位置，以保证链表的升序特性
    if (timer->expire < head->expire) {
        timer->next = head->next;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

/* 当某个定时任务发生变化时，调整对应的定时器在链表中的位置
    这个函数只考虑被调整的定时器的超时时间延长的情况，即该
    定时器需要往链表的尾部移动    
 */
void SortTimerLST::adjust_timer(UtilTimer* timer) {
    if (!timer) {
        return;
    }
    UtilTimer* tmp = timer->next;
    // 如果被调整的定时器处在链表尾部，或者该定时器的超时值
    // 仍小于下一个定时器的超时值，则不用调整
    if (!tmp || timer->expire < tmp->expire) {
        return;
    }
    /* 如果目标定时器时链表的头节点，则将该定时器从链表中取出并重新插入链表 */
    if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        timer->next = nullptr;
        add_timer(timer, head);
    } else {
        // 如果目标定时器不是链表的头节点，则将该定时器从链表中取出，然后插入其原来所在位置之后的部分链表中
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

/* 将目标定时器从链表中删除 */
void SortTimerLST::del_timer(UtilTimer* timer) {
    if (!timer) {
        return;
    }
    // 链表中只有目标定时器 
    if (timer == head && timer == tail) {
        delete timer;
        head = nullptr;
        tail = nullptr;
        return;
    }

    // 如果链表中至少有两个定时器，且目标定时器是链表头节点，
    // 则将链表的头节点置为原头节点的下一个节点，然后删除目标定时器
    if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        delete timer;
        return;
    }

    // 如果链表中至少有两个定时器，且目标定时器是链表尾节点
    // 则将链表的尾节点置为原尾节点的前一个节点，然后删除目标定时器
    if (timer == tail) {
        tail = tail->prev;
        tail->next = nullptr;
        delete timer;
        return;
    }

    // 如果目标定时器位于链表的中间，则把它前后的定时器串联起来，然后删除目标定时器
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

/* 处理链表上的到期任务 */
void SortTimerLST::tick() {
    if (!head) {
        return;
    }
    // printf("timer tick\n");
    LOG_INFO("%s", "timer tick");
    // 获得系统当前的时间
    time_t cur = time(NULL);
    UtilTimer* tmp = head;
    // 从头节点开始依次处理每个定时器，直到遇到一个尚未到期的定时器，这就是定时器的核心逻辑
    while (tmp) {
        // 应为每个定时器都使用绝对时间作为超时值，所以可以把定时器的
        // 超时值和系统当前时间进行比较，以判断定时器是否到期
        if (cur < tmp->expire) {
            break;
        }
        // 调用定时器的回调函数，以执行定时任务
        tmp->cb_func(tmp->user_data);
        // 执行完定时器中的定时任务后，就将它从链表中删除，并重置链表头节点
        head = tmp->next;
        if (head) {
            head->prev = nullptr;
        }
        delete tmp;
        tmp = head;
    }
}

/* 
    一个重载的辅助函数，他被公有的add_timer函数和adjust_timer函数调用
    该函数表示将目标定时器timer添加到节点lst_head之后的部分链表中
 */
void SortTimerLST::add_timer(UtilTimer* timer, UtilTimer* lst_head) {
    UtilTimer* prev = lst_head;
    UtilTimer* tmp = prev->next;
    // 遍历lst_head节点之后的部分链表，直到找到一个超时时间大于目标定时器的
    // 超时时间的节点，并将目标定时器插入该节点之前
    while (tmp) {
        if (timer->expire < tmp->expire) {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    // 如果遍历完lst_head之后的部分链表，仍未找到超时时间大于目标定时器的超时时间的节点，
    // 则将目标定时器插入链表尾部，并把它设置为链表新的尾节点
    if (!tmp) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = nullptr;
        tail = timer;
    }
}