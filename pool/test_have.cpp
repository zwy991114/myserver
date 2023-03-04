#include <iostream>
#include "sql_connection_pool.h"

using namespace std;
int nums;
int test_index;
locker lock;
ConnectionPool* test_pool;


void* work(void* args) {
    
    while (true) {
        lock.lock();
        if (test_index < nums) {
            MYSQL *mysql = NULL;
            ConnectionRAII mysqlcon(&mysql, test_pool);
            char sql[1024] = {};
            sprintf(sql, "insert into user(id, name, age, sex) values(%d, '%s', %d, '%s')",
                test_index, "zhangsan", 20, "male");
            
            int res = mysql_query(mysql, sql);
        
            ++test_index;   
        } else {
            
            lock.unlock();
            break;
        }
        lock.unlock();
    } 
    return nullptr;
}

int main() {
   
    nums = 1000;
    test_index = 0;
    clock_t begin = clock();
    
    test_pool = ConnectionPool::GetInstance();
    test_pool->init("localhost", "root", "123456", "test", 3306, 8, 1);
    
    int thread_number = 8;
    pthread_t* m_threads = new pthread_t[thread_number];
    for (int i = 0; i < thread_number; ++i) {
        pthread_create(m_threads + i, nullptr, work, nullptr);       
    }
    while (test_index < nums);
    
    clock_t end = clock();
    cout << (end - begin) << "ms" << endl;
    cout << "insert items: " << test_index << endl;
    
    for (int i = 0; i < thread_number; ++i) {
        pthread_join(m_threads[i], nullptr);
    }

    return 0;
}



// 1702538
// 1771483