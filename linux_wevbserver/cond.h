#ifndef COND_H
#define COND_H
#include <pthread.h>
#include <exception>
//条件变量类

class cond{
    public:
        cond();
        ~cond();

        bool wait(pthread_mutex_t* mutex); //等待
        bool timewait(pthread_mutex_t* mutex, timespec t); //等待带有时间
        bool signal(); //唤醒
        bool broadcast(); //唤醒全部


    private:

        pthread_cond_t m_cond;



};

#endif