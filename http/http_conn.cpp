#include "./http_conn.h" 
using std::cout;
using std::endl;
// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file form this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

// 网站根目录
const char* doc_root = "/home/zwy/linux_project/webserver/resources";

locker m_lock;
std::map<string, string> users;

void HttpConn::initmysql_result(ConnectionPool* connPool) {
    // 先从连接池中取出一个连接
    MYSQL* mysql = nullptr;
    ConnectionRAII mysqlconn(&mysql, connPool);

    // 在user表中检索username,passwd数据，浏览器端输入
    // int utf8 = mysql_query(mysql, "set names utf8");
    // printf("utf8=%d\n", utf8);
    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_errno(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES* result = mysql_store_result(mysql);
    
    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);
    int nums = mysql_num_rows(result);
    printf("数据库有 %d 条数据\n", nums);
    // 返回所有字段结构的数组
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        
        // printf("username:%s, passwd:%s\n", temp1, temp2);
       
        
        users[temp1] = temp2;
    } 
    // 释放结果集内存
    mysql_free_result(result);
    
}

/* 设置文件描述符非阻塞 */
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/* 向内核事件表中注册文件描述符 */
void addfd(int epollfd, int fd, bool oneshot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (oneshot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/* 从内核事件表中注销文件描述符 */
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

/* 将事件重置为EPOLLSHOT */
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int HttpConn::m_user_count = 0;
int HttpConn::m_epollfd = -1;


/* 初始化新的连接 */
void HttpConn::init(int sockfd, const sockaddr_in& addr, int close_log) {
    m_sockfd = sockfd;
    m_address = addr;
    /* 以下两行为了避免TIME_WAIT状态，仅用于调试，实际使用应该去掉 */
    // int reuse = 1;
    // setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, m_sockfd, true);
    ++m_user_count;

    m_close_log = close_log;

    init();
}

/* 初始化新的连接信息 */
void HttpConn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;    // 默认为分析请求行状态
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    bytes_to_send = 0;
    bytes_have_send = 0;

    mysql = nullptr;
    cgi = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/* 关闭连接 */
void HttpConn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        --m_user_count;  // 关闭一个连接，客户总量减1
    }
}

/* 非阻塞读操作，循环读取客户数据，直到无数据可读或对方关闭连接 */
bool HttpConn::read() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        } else if (bytes_read == 0) {
            return false;
        }
        m_read_idx += bytes_read;
    }
    
    return true;
}

/* 解析出一行内容 */
HttpConn::LINE_STATUS HttpConn::parse_line() {
    char temp;
    /* 
        - m_checked_idx指向读缓冲区中正在分析的字节
        - m_read_idx指向读缓冲区中客户数据的尾部的下一个字节
        - m_读缓冲区中 0 ~ m_checked_idx字节都已分析完毕
        - 第 m_checked_idx ~ (m_read_idx - 1)字节由下面的循环挨个分析
     */
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        // 获得当前要分析的字节
        temp = m_read_buf[m_checked_idx];
        // 如果当前的字节是'\r'，即回车符，则说明可能读取到一个完整的行
        if (temp == '\r') {
            // 如果'\r'字符碰巧是目前m_read_buf中的最后一个已经被读入的客户数据，
            // 那么这次分析没有读取到一个完整的行，返回LINE_OPEN以表示还需要继续
            // 读取客户数据才能进一步分析
            if (m_checked_idx + 1 == m_read_idx) {
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_idx + 1] == '\n') {
                // 如果下一个字符是'\n'，则说明我们成功读取到一个完整的行
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 否则的话，说明客户发送的HTTP请求存在语法问题
            return LINE_BAD;
        } else if (temp == '\n') {
            // 如果当前的字节是'\n'，即换行符，则也说明可能读取到一个完整的行
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // 如果所有内容都分析完毕也没遇到'\r'字符，则返回LINE_OPEN，
    // 表示还需要继续读取客户数据才能进一步分析
    return LINE_OPEN;
}

/* 解析HTTP请求行，获得请求方法，目标URL，以及HTTP版本号 */
HttpConn::HTTP_CODE HttpConn::parse_request_line(char* text) {
    /* 
        char *strpbrk(const char *str1, const char *str2) 
        检索字符串 str1 中第一个匹配字符串 str2 中字符的字符，不包含空结束字符。
        也就是说，依次检验字符串 str1 中的字符，当被检验字符在字符串 str2 中也包含时，
        则停止检验，并返回该字符位置,如果未找到字符则返回 NULL
     */
    // example: text = GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }
    // GET /index.html HTTP/1.1 -> GET\0/index.html HTTP/1.1
    // -> text = GET; m_url = /index.html HTTP/1.1
    *m_url++ = '\0';
    
    char* method = text;
    // 检查是否为GET方法 strcasecmp比较两个字符串，忽略大小写
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else {
        return BAD_REQUEST;
    }

    /* 
         size_t strspn(const char *str1, const char *str2) 
         检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
         该函数返回 str1 中第一个不在字符串 str2 中出现的字符下标。
     */
    // 去除空格
    m_url += strspn(m_url, " \t");
    
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    
    // /index.html HTTP/1.1 -> /index.html\0HTTP/1.1
    // -> m_url = /index.html; m_version = HTTP/1.1
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    // 检查HTTP版本，仅支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // 检查URL是否合法
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        // 返回在字符串 str 中第一次出现字符 c 的位置，如果未找到该字符则返回 NULL。
        // 192.169.1.1:8000/index.html -> /index.html
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    // 当url为'/'时，显示index.html
    if (strlen(m_url) == 1) {
        strcat(m_url, "index.html");
    }

    // 请求行处理完毕，状态转移到头部字段的分析
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/* 解析HTTP请求的一个头部信息 */
HttpConn::HTTP_CODE HttpConn::parse_headers(char* text) {
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体
        // 状态机转移到CHECK_STATE_CONTENT
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 处理Connection头部字段
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        // printf("oop!unknown header %s\n", text);
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

/* 没有真正解析HTTP请求的消息体，只是判断它是否完整地读入了 */
HttpConn::HTTP_CODE HttpConn::parse_content(char* text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::process_read() {
    // 记录当前行的读取状态
    LINE_STATUS line_status = LINE_OK;  
    // 记录HTTP请求的处理结果
    HTTP_CODE ret = NO_REQUEST;

    char* text = 0;
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();
        // 记录下一行的起始位置
        m_start_line = m_checked_idx; 
        // printf("got 1 http line: %s\n", text);
        LOG_INFO("%s", text);
        // m_check_state记录主状态机当前的状态
        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:   // 第一个状态，分析请求行
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:    // 第二个状态，分析头部字段
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            } 
            case CHECK_STATE_CONTENT:   // 第三个状态，分析消息体
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    // 没有读取到一个完整的行，还需要继续读取客户数据才能进一步分析
    return NO_REQUEST;
}

/* 当得到一个完整、正确的HTTP请求时，就分析目标文件的属性。
    如果目标存在、对所有用户可读，且不是目录，则使用mmap
    将其映射到内存地址m_file_address处，并告诉调用者获取文件成功
 */
HttpConn::HTTP_CODE HttpConn::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);

    const char* p = strrchr(m_url, '/');


    // 处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        // 2:登录校验 3:注册校验

        // 根据标志判断是登录检测还是注册检测
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);
       

        // 将用户名和密码提取出来 -> user=123&&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i) {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';
        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j) {
            password[j] = m_string[i];
        }
        password[j] = '\0';
        
        
        if (*(p + 1) == '3') {
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据
            char* sql_insert = (char*)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            
            if (users.find(name) == users.end()) {
                
                m_lock.lock();
                ConnectionPool* connPool = ConnectionPool::GetInstance();
                ConnectionRAII mysqlcon(&mysql, connPool);
                
                int res = mysql_query(mysql, sql_insert);
                 
                users[name] = password;
                m_lock.unlock();

                if (!res) {
                    strcpy(m_url, "/log.html");
                } else {
                    strcpy(m_url, "/registerError.html");
                }
            } else {
                strcpy(m_url, "/registerError.html");
            }
        } else if (*(p + 1) == '2') {
            // 如果是登录，直接判断
            // 若浏览器端输入的用户名和密码在表中可以找到，返回1，否则返回0
            if (users.find(name) != users.end() && users[name] == password) {
                strcpy(m_url, "/welcome.html");
            } else {
                strcpy(m_url, "/logError.html");
            }
        }

    }

    if (*(p + 1) == '0') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (*(p + 1) == '1') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (*(p + 1) == '5') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (*(p + 1) == '6') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (*(p + 1) == '7') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

/* 对内存映射区执行munmap操作 */
void HttpConn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/* 非阻塞写操作，写HTTP响应 */
bool HttpConn::write() {
    int temp = 0;
    
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }
    while (1) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件
            // 虽然在此期间，服务器无法立即接收到同一客户的下一个请求，但这可以保证连接的完整性
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;  // temp????bytes_have_send
        }
        
        // 发生HTTP响应成功，根据Connection字段决定是否立即关闭连接
        if (bytes_to_send <= 0) {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            if (m_linger) {
                init();
                return true;
            } else {
                return false;
            }

        }
    }
}

/* 往写缓冲区写入待发送的数据 */
bool HttpConn::add_response(const char* format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }

    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}

bool HttpConn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool HttpConn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

bool HttpConn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool HttpConn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool HttpConn::add_content(const char* content) {
    return add_response("%s", content);
}

bool HttpConn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

/* 根据服务器处理HTTP请求的结果，决定返回给客户端的内容 */
bool HttpConn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;       
        }
        case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            } else {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)) {
                    return false;
                }
            }
        }
        default:
            return false;;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}


/* 由线程池中的工作线程调用，这是处理HTTP请求的入口函数 */
void HttpConn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
        return;
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}