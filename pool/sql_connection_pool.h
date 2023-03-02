#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_
#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using std::string;

class ConnectionPool {
public:
    MYSQL* GetConnection();                 // 获取数据库连接
    bool ReleaseConnection(MYSQL* conn);    // 释放连接
    int GetFreeConn();                      // 获取空闲连接
    void DestroyPool();                     // 销毁连接池

    static ConnectionPool* GetInstance();   // 单例模式

    void init(string url, string User, string PassWord,
            string DataBaseName, int Port, int MaxConn, int close_log);
private:
    ConnectionPool();
    ~ConnectionPool();
public:
    string m_url;           // 主机地址
    int m_Port;          // 数据库端口号
    string m_User;          // 登录数据库用户名
    string m_PassWord;      // 登录数据库密码
    string m_DatabaseName;  // 使用数据库名
    int m_close_log;        // 日志开关
    
private:
    int m_MaxConn;  // 最大连接数
    int m_CurConn;  // 当前已使用的连接数
    int m_FreeConn; // 当前空闲的连接数
    locker lock;
    std::list<MYSQL*> connList; // 连接池
    sem reserve;
};


class ConnectionRAII {
public:
    ConnectionRAII(MYSQL** con, ConnectionPool* connPool);
    ~ConnectionRAII();
private:
    MYSQL* connRAII;
    ConnectionPool* poolRAII;
};

#endif