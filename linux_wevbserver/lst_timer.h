#ifndef LST_TIMER_H
#define LST_TIMER_H
#include "http_connection.h"

//这个其实就是每一个节点的信息。就是双链表的node
class util_timer{
    public:
        util_timer():prev(NULL), next(NULL){}


        
    public:
        time_t              expire_time;    //超时时间
        http_connection*    client_data;    //客户端信息
        util_timer*         prev;           //前指针
        util_timer*         next;           //后指针
        void (*callback)(http_connection*); //回调函数用来处理客户数据。客户数据由定时器的执行者传递给回调函数
        static void callbackprocess (http_connection*);

};


//这个其实就是双链表本身。和leetcode的写双链表基本一样
class sort_timer_linklist{
    public:
        sort_timer_linklist():head(NULL), tail(NULL){}

        //删除链表的时候，清空每一个节点。
        ~sort_timer_linklist(){
            util_timer* tmp = head;
            while(tmp){
                head = tmp->next;
                delete tmp;
                tmp = head;
            }
        }

        locker testlock;

        void add_timer(util_timer* timer);      //添加节点 (定时器)
        void del_timer(util_timer* timer);      //删除节点 (定时器)
        void adjust_timer(util_timer* timer);   //调整节点位置 (调整定时器的时间了，就要调整节点在链表的顺序)
        void tick();                            //每次信号被触发的时候都会执行tick函数，以处理链表上到期任务。

    private:
        void add_timer(util_timer* timer, util_timer* lst_head); // 一个重载的辅助函数，它被公有的 add_timer 函数和 adjust_timer 函数调用
                                                                    //  该函数表示将目标定时器 timer 添加到节点 lst_head 之后的部分链表中





    private:
        util_timer* head;   //链表头结点
        util_timer*  tail;   //链表尾结点

};
#endif