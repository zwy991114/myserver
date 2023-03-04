#include <iostream>
#include "sql_connection_pool.h"

using namespace std;
int nums;
int test_index;
locker lock;


void* work(void* args) {
    
    while (true) {
        lock.lock();
        if (test_index < nums) {
            MYSQL* conn = nullptr;
            char sql[1024] = {};
            sprintf(sql, "insert into user(id, name, age, sex) values(%d, '%s', %d, '%s')",
                test_index, "zhangsan", 20, "male");
            conn = mysql_init(conn);
            conn = mysql_real_connect(conn, "127.0.0.1", "root", "123456", "test", 3306, nullptr, 0);
            if (conn == NULL) {
                cout << "connection failture" << endl;
                break;
            }
            int res = mysql_query(conn, sql);
            
            mysql_close(conn);
            
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




// 6494108
// 7040104