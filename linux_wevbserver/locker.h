#ifndef LOCKER_H
#define LOCKER_H
#include <pthread.h>
#include <exception>
//线程同步类

class locker{
    public:
        locker();


        ~locker();

        bool lock();
        bool unlock();
        pthread_mutex_t* get(); //获取互斥量成员本身



    private:
        pthread_mutex_t m_mutex;
};
#endif