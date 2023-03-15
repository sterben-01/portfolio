#ifndef SEM_H
#define SEM_H
#include <exception>
#include <semaphore.h>

//信号量类

class sem{
    public:
        sem();
        sem(int num); //num是设置信号量的数量
        ~sem();
        bool wait(); //等待信号量
        bool post(); //增加信号量


    private:
        sem_t m_sem;

};


#endif