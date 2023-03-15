#include "lst_timer.h"

//定时器添加到链表中
void sort_timer_linklist::add_timer(util_timer* timer){
    if(!timer){
        return;
    }

    if(!head){
        //如果没有头，代表现在添加的将成为链表头。
        head = tail = timer;
        return;
    }
    /* 如果目标定时器的超时时间小于当前链表中所有定时器的超时时间，则把该定时器插入链表头部,作为链表新的头节点，
           否则就需要调用重载函数 add_timer(),把它插入链表中合适的位置，以保证链表的升序特性 */
    if(timer->expire_time < head->expire_time){
        //如果当前添加节点超时时间小于头部，则这个节点成为头部
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    //如果不小于头部，调用重载的add_timer进行对应位置插入
    add_timer(timer, head);
}
void sort_timer_linklist::add_timer(util_timer* timer, util_timer* lst_head){
    util_timer* pre = lst_head;
    util_timer* tmp = lst_head->next;
    while(tmp){
        if(timer->expire_time < tmp->expire_time){
            pre->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = pre;
            break;
        }
        pre = tmp;
        tmp = tmp->next;
    }
}


void sort_timer_linklist::del_timer(util_timer* timer){
    testlock.lock();
    if(!timer){
        return;
    }

    //如果只有一个节点
    if((timer == head) && (timer == tail)){
        delete timer;
        head = NULL;    //指针置空
        tail = NULL;    //指针置空
        return;
    }

    //如果链表内至少有两个节点而且目标定时器是链表的头结点
    if(timer == head){
        head = head->next;
        head->prev = NULL;  //记得这是双向链表
        delete timer;
        return;
    }

    //如果链表内至少有两个节点而且目标定时器是链表的尾节点
    if(timer == tail){
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    
    //如果目标定时器在链表的中间
    if(timer->prev == NULL || timer->next == NULL){
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;

    testlock.unlock();


}
void sort_timer_linklist::adjust_timer(util_timer* timer){
    if(!timer){
        return;
    }
    util_timer* temp = timer->next;
    // 如果被调整的目标定时器处在链表的尾部，或者该定时器新的超时时间值仍然小于其下一个定时器的超时时间则不用调整
    if(!temp|| timer->expire_time < temp->expire_time){
        return;
    }

    // 如果目标定时器是链表的头节点，则将该定时器从链表中取出并重新插入链表
    if(timer == head){
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else{
        // 如果目标定时器不是链表的头节点，则将该定时器从链表中取出，然后插入其原来所在位置后的部分链表中
        timer->next->prev = timer->prev;
        timer->prev->next = timer->next;
        add_timer(timer);
    }






}


/* SIGALARM 信号每次被触发就在其信号处理函数中执行一次 tick() 函数，以处理链表上到期任务。*/
void sort_timer_linklist::tick(){
    if(!head){
        return;
    }
    printf("---------------timer tick-------------------\n"); //DEBUG
    time_t cur = time(NULL); //获取系统当前时间
    util_timer* tmp = head;
    while(tmp){
        printf("!\n"); 
        /* 因为每个定时器都使用绝对时间作为超时值，所以可以把定时器的超时值和系统当前时间，
        比较以判断定时器是否到期*/
        if(cur < tmp->expire_time){
            //如果没到过期时间
            break;
        }
        // 调用定时器的回调函数，以执行定时任务
        tmp->callback( tmp->client_data);
        //执行完定时器中的定时任务之后，就将它从链表中删除，并重置链表头节点
        head = tmp->next;
        if(head){
            //如果是头结点
            head->prev = NULL;
        }
        printf("----------tick DELETED-----------\n");
        delete tmp;
        tmp = head;

    }

}



void util_timer::callbackprocess (http_connection* clientinfo){
    epoll_ctl(clientinfo->m_epollfd, EPOLL_CTL_DEL, clientinfo->get_my_fd(), 0 );
    printf( "close fd %d\n", clientinfo->get_my_fd());
    close(clientinfo->get_my_fd());
}