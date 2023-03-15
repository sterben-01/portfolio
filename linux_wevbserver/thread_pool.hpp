#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include <list>
#include "locker.h"
#include "cond.h"
#include "sem.h"
#include "log.h"
//线程池模板类。为了代码复用

template<typename T>
class thread_pool{
    public:
        thread_pool(int thread_num = 8, int max_request = 10000);
        ~thread_pool();


        bool append(T* request);
    
    private:
        static void* worker(void* arg);
        void run();


    private:
        int m_thread_num; //线程数量
        pthread_t* m_thread_array; //线程池动态数组。 大小为m_thread_num。要用指针接因为是用的new来开辟

        int m_max_requests; //请求队列中等待处理的请求数量的最大值
        std::list<T*> m_work_queue; //请求队列

        locker m_queue_lock; //请求队列互斥锁

        sem m_queue_stat; //请求队列信号量，用来判断是否有任务需要处理

        bool m_stop; //是否结束线程



};

template<typename T>
thread_pool<T>::thread_pool(int thread_num, int max_request): 
    m_thread_num(thread_num), m_max_requests(max_request), 
    m_stop(false), m_thread_array(NULL){
        if(thread_num <= 0 || max_request <= 0){
            throw std::exception();
        }

        m_thread_array = new pthread_t[m_thread_num]; //创建线程池动态数组
        if(!m_thread_array) {
            //如果创建失败
            throw std::exception();
        }

        //创建m_thread_num个线程并设置线程脱离。因为我们不能让父线程阻塞等着回收子线程资源。所以要用线程脱离。子线程结束之后系统自动回收。
        for(int i = 0; i < m_thread_num; i++){
            /*
            注意下面创建线程第一个参数是线程ID。当子线程创建成功的时候会把ID写入到那。我们有一个数组的时候就代表我们要把这个ID塞到对应数组的对应位置。
            我们如果写 pthread_t tid1; pthread_create(&tid1, ...)这样的话第一个要取地址因为形参是指针。这样我们才可以修改。但是我们这里已经用指针声明数组了就不需要再取地址了。
            而且，对于数组，我们可以直接用指针加法来做偏移。
            第三个参数回调函数在c++类封装里面必须要做全局或者是静态函数。但是全局函数会破坏封装特性。这里必须要用静态函数。
            */
            if(pthread_create(m_thread_array + i, NULL, worker, this) != 0){ //!这里显式传入this指针。
                /* //todo 原因是我们worker回调函数必须是静态的。静态函数只能访问静态对象。我们要访问非静态对象怎么办？
                    相当于我们work的作用是给一个子线程要执行的函数包装一下。因为线程创建回调函数必须要用静态函数、但是我们需要访问类内的非静态对象。
                    那么我们就给回调函数声明成静态的。然后参数传入this（自己本身是类对象，可以访问自己的东西）。
                    然后通过this来访问自己的非静态成员。
                    我们模拟传入一个this指针。然后使用this来调用对应的非静态成员即可。
                    !但是我们的函数依旧要声明为静态
                */
                //如果创建失败
                delete [] m_thread_array; //失败了不要忘记回收内存，不然会泄漏
                LOG_ERROR("create fail :%d\n", i);
                throw std::exception();

            } 
            if(pthread_detach(m_thread_array[i])){
                //如果分离失败
                delete [] m_thread_array; //失败了不要忘记回收内存，不然会泄漏
                throw std::exception();
            }
            //printf("create thread :%d\n", i);
            LOG_INFO("create thread :%d\n", i);

        }

}

template<typename T>
thread_pool<T>::~thread_pool(){
    delete[] m_thread_array; //回收new的数组
    m_stop = true; //设置停止线程。
}

template<typename T>
bool thread_pool<T>::append(T* request){

    m_queue_lock.lock(); //上锁
    if(m_work_queue.size() > m_max_requests){
        m_queue_lock.unlock(); 
        return false;
        //如果队列满了 无法添加
        //!记得解锁
    }
    m_work_queue.push_back(request); //添加任务
    m_queue_lock.unlock(); //解锁

    m_queue_stat.post(); //增加生产者信号量表示有一个新的可用。
    return true;
}

template<typename T>
void* thread_pool<T>::worker(void* arg){
    thread_pool* pool = (thread_pool*) arg; //传入指针强转为类类型。因为传入的是this。
    pool->run(); //这里才是真正的子线程要执行的动作。从队列中取数据放入线程执行
    return pool; //这块没啥用
}

template<typename T>
void thread_pool<T>::run(){
    //循环执行直到遇到线程停止标志
    while(!m_stop){
        m_queue_stat.wait(); //如果生产者有足够的东西让我执行。这里判断是否有任务需要做 注意这里是sem_wait 会把数量-1
        m_queue_lock.lock(); //wait里面的注意这里是sem_wait是阻塞函数。执行到这里就一定表示有任务了。所以需要上锁来执行。//!先等待，有任务了再加锁！
        if(m_work_queue.empty()){
            //如果队列空了
            m_queue_lock.unlock();
            continue;
        }
        T* request = m_work_queue.front(); //获取第一个任务
        m_work_queue.pop_front(); //获取后删除
        m_queue_lock.unlock(); //获取后解锁
        if(!request){
            continue; //这判断request是否合法、假如是NULL
        }

        request->process(); //运行任务。

    }
}



#endif


/*
       创建一个线程默认的状态是joinable(可汇合的), 如果一个线程结束运行但没有被join,则它的状态类似于进程中的Zombie Process（僵尸进程）,即还有一部分资源没有被回收（退出状态码）
       所以创建线程者应该调用pthread_join来等待线程运行结束，并可得到线程的退出代码，回收其资源（类似于wait,waitpid) 

      但是调用pthread_join(pthread_id)后，如果该线程没有运行结束，调用者会被阻塞，在有些情况下我们并不希望如此，
      比如在Web服务器中当主线程为每个新来的链接创建一个子线程进行处理的时候，主线程并不希望因为调用pthread_join而阻塞（因为还要继续处理之后到来的链接），
      这时可以在子线程中加入代码 pthread_detach(pthread_self()) 或者父线程调用 pthread_detach(thread_id)（非阻塞，可立即返回） 
      将该子线程的状态设置为detached(脱离的), 则该线程运行结束后会自动释放所有资源。 
*/