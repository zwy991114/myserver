#include "./sql_connection_pool.h"

ConnectionPool::ConnectionPool() : m_CurConn(0), m_FreeConn(0) {

}

ConnectionPool::~ConnectionPool() {
    DestroyPool();
}

ConnectionPool* ConnectionPool::GetInstance() {
    static ConnectionPool connPool;
    return &connPool;
}

void ConnectionPool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log) {

    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    m_close_log = close_log;

    for (int i = 0; i < MaxConn; ++i) {
        MYSQL* conn = nullptr;
        conn = mysql_init(conn);
        if (conn == nullptr) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        conn = mysql_real_connect(conn, m_url.c_str(), m_User.c_str(), m_PassWord.c_str(),
                                    m_DatabaseName.c_str(), m_Port, NULL, 0);
        if (conn == nullptr) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        connList.push_back(conn);
        ++m_FreeConn;
    }
    reserve = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
} 

/* 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数 */
MYSQL* ConnectionPool::GetConnection() {
    MYSQL* conn = nullptr;
    if (0 == connList.size()) {
        return nullptr;
    }
    reserve.wait();

    lock.lock();
    conn = connList.front();
    connList.pop_front();
    --m_FreeConn;
    ++m_CurConn;
    lock.unlock();
    return conn;
}

/* 释放当前使用的连接 */
bool ConnectionPool::ReleaseConnection(MYSQL* conn) {
    if (nullptr == conn) {
        return false;
    }
    lock.lock();
    connList.push_back(conn);
    --m_CurConn;
    ++m_FreeConn;
    lock.unlock();
    reserve.post();
    return true;
}

// 销毁数据库连接池
void ConnectionPool::DestroyPool() {
    lock.lock();
    if (connList.size() > 0) {
        
        for (auto it = connList.begin(); it != connList.end(); ++it) {
            MYSQL* conn = *it;
            mysql_close(conn);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}

// 当前空闲的连接数
int ConnectionPool::GetFreeConn() {
    return this->m_FreeConn;
}

ConnectionRAII::ConnectionRAII(MYSQL** SQL, ConnectionPool* connPool) {
    *SQL = connPool->GetConnection();
    connRAII = *SQL;
    poolRAII = connPool;
}

ConnectionRAII::~ConnectionRAII() {
    poolRAII->ReleaseConnection(connRAII);
}