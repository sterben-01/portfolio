#ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H
#include<condition_variable>
#include<deque>
#include<mutex>
#include<sys/time.h>
#include<assert.h>

/*
关于这里的front, back, 为什么要返回T而不是T&, 是因为标准STL都是非侵入式。也就是元素传入的是副本，也就是拷贝。
返回也是返回元素的副本 也就是拷贝。
所以传入的也是const&.因为需要拷贝一份

&注意这里的pop。我们设计成写回传入对象的方式。而不是返回对象。因为我们要把从deque里面找到的对象赋值回给传入元素。
&我们用写回的这个方式提取。所以我们采用引用的方式。
*/


template<class T>
class BlockDeque{
    public:
        explicit BlockDeque(size_t Max_Capacity = 1000);
        ~BlockDeque();


        void clear();

        bool empty();

        bool full();
        
        void close();

        size_t size();

        size_t capacity();
        
        T& front();             //反正都是读操作。返回T&和T区别不大。

        T& back();              

        void push_back(const T& item); //STL是非侵入式。所以传入副本

        void push_front(const T& item); //STL是值语义。我们这使用&防止拷贝是防止传入时拷贝，而放入到容器内的时候依旧是一份拷贝。

        bool pop(T& item); //用写回的方式提取数据。也就是队列中提取的数据会协会到item里。所以用引用传参。

        bool pop(T& item, int timeout); //用写回的方式提取数据。也就是队列中提取的数据会协会到item里。所以用引用传参。

        void flush();

    private:
        std::deque<T> _deq;

        size_t _capacity;

        std::mutex _mtx;

        bool _isClose;

        std::condition_variable _condConsumer;

        std::condition_variable _condProducer;

};




template<typename T>
BlockDeque<T>::BlockDeque(size_t Maxcapacity):_capacity(Maxcapacity){
    assert(Maxcapacity > 0);
    _isClose = false;
}

template<typename T>
BlockDeque<T>::~BlockDeque(){
    close();
}

template<typename T>
void BlockDeque<T>::close(){
    //加括号是为了让lock guard离开作用域后自动释放
    {
        std::lock_guard<std::mutex> locker(_mtx);
        _deq.clear();
        _isClose = true;
    }
    _condProducer.notify_all(); //通知所有生产者线程
    _condConsumer.notify_all(); //通知所有消费者线程
}

template<typename T>
void BlockDeque<T>::flush(){
    _condConsumer.notify_one(); //通知一个消费者线程。
}

template<typename T>
void BlockDeque<T>::clear(){
    std::lock_guard<std::mutex> locker(_mtx);
    _deq.clear();
}

template<typename T>
T& BlockDeque<T>::front(){
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.front();
}

template<typename T>
T& BlockDeque<T>::back(){
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.back();
}

template<typename T>
size_t BlockDeque<T>::size(){
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.size();
}


template<typename T>
size_t BlockDeque<T>::capacity(){
    std::lock_guard<std::mutex> locker(_mtx);
    return _capacity;
}

/*
& 生产者push，消费者pop。所以push之后生产者就要通知消费者。生产者不能消费了，就需要消费者通知生产者。
*/

template<typename T>
void BlockDeque<T>::push_back(const T& item){
    std::unique_lock<std::mutex> locker(_mtx); //!这里必须要用uniquelock 因为lockguard不支持解锁。因为下面的wait函数会先解锁，再睡眠。
    //!不然一直拿着锁别的线程也无法使用。所以先解锁，让消费者也可以抢锁消费。
    while(_deq.size() >= _capacity){
        //&如果队列满了，就在这里等着。直到消费者消费了，通知了生产者，生产者再继续生产。
        //%注意这里必须要while。因为可能会被伪唤醒。
        _condProducer.wait(locker);
        //假如被伪唤醒了，也就是现在队列依旧是满的。137行执行完毕会继续执行134来再次判断是否满足条件。这时候依旧是满的所以还会等待。

    }
    _deq.push_back(item);
    _condConsumer.notify_one(); //生产出了一个，就通知消费者。

}

template<typename T>
void BlockDeque<T>::push_front(const T& item){
    std::unique_lock<std::mutex> locker(_mtx);
    while(_deq.size() >= _capacity){
        _condProducer.wait(locker);
    }

    _deq.push_front(item);
    _condConsumer.notify_one();
}


template<typename T>
bool BlockDeque<T>::empty(){
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.empty();
}

template<typename T>
bool BlockDeque<T>::full(){
    std::lock_guard<std::mutex> locker(_mtx);
    return _deq.size() >= _capacity;
}

//&注意我们pop的原理不是返回对象，而是写入参数
template<typename T>
bool BlockDeque<T>::pop(T& item){
    std::unique_lock<std::mutex> locker(_mtx);
    while(_deq.empty()){
        //如果队列是空的，消费者就要等待被生产者通知。
        _condConsumer.wait(locker);
        if(_isClose){
            //如果关闭了
            return false;
        }
    }
    item = _deq.front();
    _deq.pop_front();
    _condProducer.notify_one(); //消费者消费了一个，就通知生产者可以继续生产了。
    return true;
}

template<typename T>
bool BlockDeque<T>::pop(T& item, int timeout){
    std::unique_lock<std::mutex> locker(_mtx);
    while(_deq.empty()){
        //等待至输入的时间为止。如果到了时间，依旧没有被唤醒，则继续执行。这里就是如果==timeout为true就是超时了。就返回false
        if(_condConsumer.wait_for(locker, std::chrono::seconds(timeout)) == std::cv_status::timeout){
            return false;
        }
        if(_isClose){
            return false;
        }
    }
    item = _deq.front();
    _deq.pop_front();
    _condProducer.notify_one();
    return true;}










#endif // BLOCKQUEUE_H