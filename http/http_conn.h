#ifndef HTTPCONN_H
#define HTTPCONN_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <map>
#include <iostream>
#include "../log/log.h"
#include "../lock/locker.h"
#include "../pool/sql_connection_pool.h"


class HttpConn {
public:
    /* 文件名的最大长度 */
    static const int FILENAME_LEN = 200;
    /* 读缓冲区大小 */
    static const int READ_BUFFER_SIZE = 2048;
    /* 写缓冲区大小 */
    static const int WRITE_BUFFER_SIZE = 1024;

    /* HTTP请求方法，目前只支持GET */
    enum METHOD {
        GET = 0, 
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATCH
    };
    
    /* 解析客户请求时，主状态机所处的状态 */
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,    // 正在分析请求行
        CHECK_STATE_HEADER,             // 正在分析请求头部
        CHECK_STATE_CONTENT             // 正在分析请求内容
    };

    /* 服务器处理HTTP请求的可能结果 */
    enum HTTP_CODE {
        NO_REQUEST,         // 请求不完整，需要继续读取客户数据
        GET_REQUEST,        // 获得了一个完整的客户请求
        BAD_REQUEST,        // 客户请求有语法错误
        NO_RESOURCE,        // 服务器没有资源
        FORBIDDEN_REQUEST,  // 客户对资源没有足够的访问权限
        FILE_REQUEST,       // 文件请求
        INTERNAL_ERROR,     // 服务器内部错误
        CLOSED_CONNECTION   // 客户端已经关闭连接
    };
    
    /* 行的读取状态 */
    enum LINE_STATUS {
        LINE_OK = 0,    // 读取到一个完整的行
        LINE_BAD,       // 行出错
        LINE_OPEN       // 行数据尚且不完整
    };
public:
    HttpConn(){}
    ~HttpConn(){}
public:
    /* 初始化新接受的连接 */
    void init(int sockfd, const sockaddr_in& addr, int close_log=0);
    /* 关闭连接 */
    void close_conn(bool real_close=true);
    /* 处理客户请求 */
    void process();
    /* 非阻塞读操作 */
    bool read();
    /* 非阻塞写操作 */
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }

    void initmysql_result(ConnectionPool* connPool);

private:
    /* 初始化连接 */
    void init();
    /* 解析HTTP请求 */
    HTTP_CODE process_read();
    /* 填充HTTP应答 */
    bool process_write(HTTP_CODE ret);

    /* 下面这组函数被process_read调用以分析HTTP请求 */
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line();

    /* 下面这一组函数被process_write调用以填充HTTP应答 */
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;       // 所有socket上的事件都被注册到同一个epoll内核事件表中，所以将epoll文件描述符设置为静态
    static int m_user_count;    // 统计用户数量
    MYSQL* mysql;
private:
    int m_sockfd;           // 该HTTP连接的socket
    sockaddr_in m_address;  // 对方的socket地址

    char m_read_buf[READ_BUFFER_SIZE];      // 读缓冲区
    int m_read_idx;                         // 标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置
    int m_checked_idx;                      // 当前正在分析的字符在读缓冲区中的位置
    int m_start_line;                       // 当前正在解析的行的起始位置
    char m_write_buf[WRITE_BUFFER_SIZE];    // 写缓冲区
    int m_write_idx;                        // 写缓冲区中待发送的字节数

    CHECK_STATE m_check_state;  // 主状态机当前所处的状态
    METHOD m_method;            // 请求方法


    char m_real_file[FILENAME_LEN]; // 客户请求的目标文件的完整路径，其内容doc_root + m_url, doc_root是网站根目录
    char* m_url;                    // 客户请求的目标文件的文件名
    char* m_version;                // HTTP协议版本号，只支持HTTP/1.1
    char* m_host;                   // 主机名
    int m_content_length;           // HTTP请求的消息体的长度
    bool m_linger;                  // HTTP请求是否要求保持连接

    char* m_file_address;       // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat;    // 目标文件的状态，判断文件是否存在、是否为目录、是否可读，并获取文件大小等
    
    /* 采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量 */
    struct iovec m_iv[2];       
    int m_iv_count;

    int bytes_have_send;    // 已发送数据
    int bytes_to_send;      // 待发送数据

    int m_close_log;

    int cgi;    // 标志是否为POST请求
    char* m_string; // 存储请求头数据
    
    // std::map<string, string> users; // 用户名和密码
    // char sql_user[100];
    // char sql_passwd[100];
    // char sql_name[100];
};

#endif