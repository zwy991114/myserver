#include <unistd.h>
#include "./http/http_conn.h"
#include "./pool/threadpool.h"
#include "./timer/lst_timer.h"
#include "./pool/sql_connection_pool.h"


#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define TIMESLOT 5

// 开启日志
int m_close_log = 1;
/* add timer */
/* 设置定时器相关参数 */
static int pipefd[2];
static SortTimerLST timer_lst(nullptr, nullptr, m_close_log);
static int epollfd = 0;
/* add timer */


extern void addfd(int epollfd, int fd, bool oneshot);
extern void removefd(int epollfd, int fd);
extern int setnonblocking(int fd);

void addsig(int sig, void (handler)(int), bool restart=true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

void show_error(int connfd, const char* info) {
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
}

/* add timer */

/* 信号处理函数 */
void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

/* 定时处理任务 */
void timer_handler() {
    // 实际上就是调用tick函数 
    timer_lst.tick();
    // 因为一次alarm调用只会引起一次SIGALRM信号
    // 所以要重新定时，以不断触发SIGALRM信号
    alarm(TIMESLOT);
}

/* 定时器回调函数，删除非活动连接在socket上的注册事件，并关闭 */
void cb_func(client_data* user_data) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    --HttpConn::m_user_count;
    // printf("close fd %d\n", user_data->sockfd);
    LOG_INFO("close fd %d", user_data->sockfd);
}


/* add timer */

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    // 以守护进程的方式运行
    // int ret = 0;
    // ret = daemon(1, 0);
    // if (ret == -1) {
    //     perror("daemon");
    //     exit(-1);
    // }
    // 开启日志
    
    Log::get_instance()->init("./serverLog", m_close_log, 2000, 800000, 100);
    // const char* ip = argv[1];
    int port = atoi(argv[1]);

    // 忽略SIGPIPE信号 
    addsig(SIGPIPE, SIG_IGN);

    // 创建数据库连接池
    ConnectionPool* connPool = ConnectionPool::GetInstance();
    connPool->init("localhost", "root", "123456", "server_db", 3306, 8, m_close_log);

    // 创建线程池 
    ThreadPool<HttpConn>* pool = nullptr;
    try {
        pool = new ThreadPool<HttpConn>;
    } catch (...) {
        return 1;
    }

    // 预先为每个可能的客户连接分配一个HttpConn对象 
    HttpConn* users = new HttpConn[MAX_FD];
    assert(users);
    
    users->initmysql_result(connPool);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];

    epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);
    HttpConn::m_epollfd = epollfd;

    /* add timer */
    // 创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret >= 0);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);

    // 设置信号处理函数
    addsig(SIGALRM, sig_handler);
    addsig(SIGTERM, sig_handler);
    bool stop_server = false;
    
    client_data* users_timer = new client_data[MAX_FD];
    bool timeout = false;
    // 定时
    alarm(TIMESLOT);

    /* add timer */
    
    while (!stop_server) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; ++i) {
            int sockfd = events[i].data.fd;
            // 处理新的客户连接
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                while (1) {
                    int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                
                    if (connfd < 0) {
                        // printf( "errno is: %d\n", errno );
                        LOG_ERROR("%s:errno is:%d", "accept error", errno);
                        break;
                        // continue;
                    }

                    if (HttpConn::m_user_count >= MAX_FD) {
                        show_error(connfd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;
                        // continue;
                    }

                    // 初始化客户连接 
                    users[connfd].init(connfd, client_address, m_close_log);
                    
                    /* add timer */
                    // 初始化cliend_data数据
                    users_timer[connfd].address = client_address;
                    users_timer[connfd].sockfd = connfd;
                    // 创建定时器临时变量
                    UtilTimer* timer = new UtilTimer;
                    // 设置定时器对应的连接资源
                    timer->user_data = &users_timer[connfd];
                    // 设置回调函数
                    timer->cb_func = cb_func;
                    time_t cur = time(NULL);
                    // 设置超时时间
                    timer->expire = cur + 3 * TIMESLOT;
                    // 创建该连接对应的定时器，初始化为前述临时变量
                    users_timer[connfd].timer = timer;
                    // 将定时器添加到链表中
                    timer_lst.add_timer(timer);
                    /* add timer */
                }
                continue;
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 如果有异常，直接关闭客户连接
                // users[sockfd].close_conn();
                /* add timer */
                // 服务器关闭连接，移除对应定时器
                cb_func(&users_timer[sockfd]);
                UtilTimer* timer = users_timer[sockfd].timer;
                if (timer) {
                    timer_lst.del_timer(timer);
                }
                LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
                /* add timer */
                /* add timer */
            } else if (sockfd == pipefd[0] && (events[i].events & EPOLLIN)) {
                // 处理SIGALRM信号
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) {
                    // handle the error
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    for (int i = 0; i < ret; ++i) {
                        switch (signals[i])
                        {
                            case SIGALRM:
                            {
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }                    
                        }
                    }
                }
                /* add timer */
            } else if (events[i].events & EPOLLIN) {
                /* add timer */
                UtilTimer* timer = users_timer[sockfd].timer;
                /* add timer */
                // 根据读的结果，决定是将任务添加到线程池，还是关闭连接 
                if (users[sockfd].read()) {
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    pool->append(users + sockfd);
                    
                    /* add timer */
                    // 若有数据传输，则将定时器往后延迟3个单位
                    // 并对新的定时器在链表上的位置进行调整
                    if (timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        // printf("adjust timer once\n");
                        LOG_INFO("%s", "adjust timer once");
                        timer_lst.adjust_timer(timer);
                    }
                    /* add timer */

                } else {
                    // users[sockfd].close_conn();
                    /* add timer */
                    cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_lst.del_timer(timer);
                    }
            
                    /* add timer */
                }
            } else if (events[i].events & EPOLLOUT) {
                /* add timer */
                UtilTimer* timer = users_timer[sockfd].timer;
                /* add timer */

                // 根据写的结果，决定是否关闭连接 
                if (users[sockfd].write()) {
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    /* add timer */
                    // 若有数据传输，则将定时器往后延迟3个单位
                    // 并对新的定时器在链表上的位置进行调整
                    if (timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        // printf("adjust timer once\n");
                        LOG_INFO("%s", "adjust timer once");
                        timer_lst.adjust_timer(timer);
                    }
                    /* add timer */
                    
                } else {
                    // users[sockfd].close_conn();
                    /* add timer */
                    cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_lst.del_timer(timer);
                    }
                    /* add timer */
                }
            }

        }
        /* add timer */
        // 最后处理定时事件，因为I/O事件有更高的优先级
        // 当然，这样做将导致定时任务不能精确di按照预期时间执行
        if (timeout) {
            timer_handler();
            timeout = false;
        }
        /* add timer */
    }

    close(epollfd);
    close(listenfd);
    /* add timer */
    close(pipefd[0]);
    close(pipefd[1]);
    delete [] users_timer;
    /* add timer */
    delete [] users;
    delete pool;
    return 0;
}