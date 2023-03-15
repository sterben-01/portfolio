#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <assert.h>

#include "thread_pool.hpp"
#include "http_connection.h"
#include "lst_timer.h"

#include "log.h"

#define MAX_CLIENT_FD 65535 //最大的客户端连接数量
#define MAX_EPOLL_FD_SIZE 11000 //EPOLL监听的最大事件数量
#define TIMESLOT 5          //update


static int pipefd[2];
static sort_timer_linklist timer_lst;

static int log_close;

//添加信号捕捉
void addSig(int sig, void(*handler)(int)){
    struct sigaction signal_actoin;
    memset(&signal_actoin, '\0', sizeof(signal_actoin)); //用\0代表空 初始化信号为空
    signal_actoin.sa_handler = handler;
    sigfillset(&signal_actoin.sa_mask); 
    sigaction(sig, &signal_actoin, NULL); //接收到的信号执行sig_action 动作
}

//update---------------------------------------------------------------------------------
void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}
void addSig(int sig){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}
void timer_handler()
{
    // 定时处理任务，实际上就是调用tick()函数
    timer_lst.tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    //第一次在while外面的alarm做为引火器。让我们执行第一次处理信号，因为我们收到了是SIGALRM所以返回true调用此函数。
    //调用此函数后再次设置五秒倒计时。五秒后又会处理信号，又是SIGALRM所以再次调用此函数
    //...循环。
    alarm(TIMESLOT);
}
// void callback_process(http_connection* user_data )
// {
//     epoll_ctl( epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0 );
//     assert( user_data );
//     close( user_data->sockfd );
//     printf( "close fd %d\n", user_data->sockfd );
// }
// 定时器回调函数，它删除非活动连接socket上的注册事件，并关闭之。
//update-------------------------------FINISH----------------------------------------




int main(int argc, char* argv[]){


    if (argc <= 1){
        printf("please follow this format %s port_number\n", basename(argv[0]));
        exit(-1);
    }

    int port = atoi(argv[1]); //获取端口号

    int log_async = atoi(argv[2]); //是否同步日志

    if(log_async == 1){
        Log::get_instance()->init("./ServerLog", 0, 2000, 800000, 800);
        LOG_INFO("%s", "LOG create success with ASYNC method");
    }
    else{
        Log::get_instance()->init("./ServerLog", 0, 2000, 800000, 0);
        LOG_INFO("%s", "LOG create success with SYNC method");
    }

    

    //对SIGPIPE信号进行处理
    addSig(SIGPIPE, SIG_IGN); //发现sigpipe信号的时候忽略。


    //创建线程池，初始化线程池。
    thread_pool<http_connection>* pool = NULL; //创建线程池, 任务类型为http_connection
    try{
        pool = new thread_pool<http_connection>(); //初始化线程池
    } catch(...){
        exit(-1);
    }

    //创建数组保存所有的客户端信息
    http_connection* clients = new http_connection[MAX_CLIENT_FD];


    //^-------------------------以下为TCP部分---------------------------------------

    //1. 创建socket -- 用于监听的套接字。

    int listen_FD = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_FD == -1){
        perror("listen FD create error");
        exit(-1);
    }
    //@1.1 绑定前使用setsockopt来设置端口复用。第四个参数是void*所以需要传地址。
    
    int reuse = 1;
    setsockopt(listen_FD, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    //2. bind 将监听的文件描述符和一个ip地址、端口进行绑定
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET; //设置协议族

    //inet_pton(AF_INET, "127.0.0.60", &server_address.sin_addr.s_addr); //点分十进制转换为网络字节序的ip整数
    //上面这一行是之前地址转换函数的融合。第二个位置可以用char[] 也可以直接写一个字符串。第三个可以写一个unsigned char去接也可以直接赋值给sock_address的ip
    server_address.sin_addr.s_addr = INADDR_ANY; //指定ip地址 0.0.0.0 指的是任意地址。仅服务端可以这样使用。
    server_address.sin_port = htons(port); //指定服务器端口并转换为网络字节序 

    int ret = bind(listen_FD,(struct sockaddr *)&server_address, sizeof(server_address)); //第二个参数是sockaddr类型的指针。直接取地址之后强转即可。
    if(ret == -1){
        perror("bind error");
        exit(-1);
    }   


    //3. 监听
    ret = listen(listen_FD, 5);
    if(ret == -1){
        perror("listen error");
        exit(-1);
    }



    //^-------------------------以下包含EPOLL多路复用部分---------------------------------------
    //4. 调用epoll_create 创建EPOLL实例
    int epoll_fd = epoll_create(39);
    http_connection::m_epollfd = epoll_fd; 

    //@4.1 创建epoll_event 结构来封装信息后告知具体要监听文件的哪个操作这个步骤被置入addfd函数内部了。
    //@ 我们仅需要提供epoll文件描述符， 操作的文件描述符, 监听选项和one shot选项即可。

    //5. 将监听文件描述符添加至epoll监听的监听文件队列
    addfd(epoll_fd, listen_FD, EPOLLIN, false); //% 监听文件描述符不设置oneshot。也不设置边缘触发。使用水平触发。设置为非阻塞。所有文件描述符全部为非阻塞。

    //5.1 建立 储存epoll_wait检测到的 修改过的 文件描述符的 数组
    struct epoll_event epoll_fds[MAX_EPOLL_FD_SIZE];

    //update------------------------------------------------------------------------
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0,pipefd);
    assert( ret != -1 );
    set_non_blocking(pipefd[1] );
    addfd(epoll_fd, pipefd[0], EPOLLIN | EPOLLET , false);
    // 设置信号处理函数
    addSig(SIGALRM);
    addSig(SIGTERM);
    bool stop_server = false;
    bool timeout = false;
    alarm(TIMESLOT);  // 定时,5秒后产生SIGALARM信号
    //update----------------------FINISH----------------------------------------

    while(!stop_server){ //update
        //6. 调用epoll_wait 检测哪些文件描述符被修改了, 储存到epoll_fds数组
        int ret = epoll_wait(epoll_fd, epoll_fds, MAX_EPOLL_FD_SIZE, -1); //第四个参数设置为-1代表阻塞至至少有一个文件描述符被修改。
        //%返回值储存了修改了的文件描述符的数量。通过这个值可以决定我们需要遍历到数组的哪个下标

        if ((ret < 0) && (errno != EINTR)){ //&epoll_wait被信号中断后会返回EINTR错误。这时候我们应该判断这个错误。
            LOG_ERROR("%s","epoll detection error\n");
            break;
        }


        //7. 遍历储存了被修改的文件描述符的数组。这里我们仍旧需要将监听文件描述符和读写文件描述符分开判断
        for(int i = 0; i < ret; i++){
            int current_fd = epoll_fds[i].data.fd; //当前文件描述符
            //8. 如果是监听文件描述符对应的缓冲区被修改, 接收客户端链接
            if(current_fd == listen_FD){
                struct sockaddr_in client_INFO;
                socklen_t client_INFO_size = sizeof(client_INFO);
                int accept_FD = accept(listen_FD, (struct sockaddr *)&client_INFO, &client_INFO_size); 

                if(accept_FD == -1){
                    perror("accept connection error");
                    exit(-1);
                }

                //DEBUG--------------------将接收到的客户端信息转换为主机字节序-----------------------
                char client_ip[16];
                inet_ntop(AF_INET, &client_INFO.sin_addr.s_addr, client_ip, sizeof(client_ip)); //客户端ip端口储存在名为client_INFO的结构体里面 客户ip转换为主机字节序
                unsigned short client_port = ntohs(client_INFO.sin_port); // 客户端口转换为主机字节序
                LOG_INFO("clientIP:%s, clientPort: %d\n", client_ip, client_port);
                //DEBUG--------------------FINISH----------------------------------------------------

                //printf("branch 0\n");

                if(http_connection::m_client_count >= MAX_CLIENT_FD){
                    //如果目前连接已满
                    //TODO 给客户端写信息，如服务器已满
                    close(accept_FD);
                    continue;
                }
                //将接收到的读写文件描述符初始化后并放入客户端信息数组 这里也顺便放进epoll监听数组了。
                //& 这里直接用文件描述符号来当下标。这样操作很方便。查找是O(1)。而且文件描述符不会重复也不会超过最大值。因为超过都被拒绝连接了。
                clients[accept_FD].init(accept_FD, client_INFO);
                //printf("branch 1\n");
                //update------------------------------------------------------------------------
                util_timer* timer = new util_timer;
                timer->client_data = &clients[accept_FD];
                timer->callback = util_timer::callbackprocess;
                time_t cur = time(NULL);
                timer->expire_time = cur + 3 * TIMESLOT;
                clients[accept_FD].timer = timer;
                timer_lst.add_timer(timer);
                //update---------------------------FINISH-------------------------------
            }
            else if(epoll_fds[i].events &(EPOLLRDHUP | EPOLLHUP | EPOLLERR)){ //TODO 这行可以优化
                //如果对方异常断开/发生错误等 需要关闭连接
                clients[current_fd].close_connect();
                //printf("branch 2\n");
            }
            //update------------------------------------------------------------------------
            else if (( current_fd == pipefd[0] ) && ( epoll_fds[i].events & EPOLLIN ) ){
                // 处理信号
                int sig;
                char signals[1024];
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if( ret == -1 ) {
                    continue;
                } else if( ret == 0 ) {
                    continue;
                } else  {
                    for( int i = 0; i < ret; ++i ) {
                        switch( signals[i] )  {
                            case SIGALRM:
                            {
                                // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                                // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            //update---------------------------FINISH-------------------------------
            else if(epoll_fds[i].events & EPOLLIN){         
                //% 检测读事件
                //& 因为是模拟 Proactor 模式。所以需要一次性读完
                util_timer* timer = clients[current_fd].timer; //update
                if(clients[current_fd].read()){//TODO  读取函数
                    //读完之后交给工作线程处理
                    time_t cur = time(NULL); //update
                    timer->expire_time = cur + 3 * TIMESLOT;
                    LOG_INFO("%s", "adjuct time");//debug
                    timer_lst.adjust_timer(timer);
                    pool->append(clients + current_fd); //& 这里append形参是指针，所以需要传入一个地址或者指针。
                    /*
                    & 这里append形参是指针，所以需要传入一个地址或者指针。
                    & 我们为了简便直接把地址写成 数组基地址(clients) + 偏移量(数组第几位)的形式。看下面要点部分
                    */
                   LOG_INFO("%s", "branch 3\n");//debug
                    //printf("portcome%d\n",port);
                }
                else{
                    //如果读取失败
                    timer_lst.del_timer(timer); //update
                    clients[current_fd].close_connect();
                }
            }
            else if(epoll_fds[i].events & EPOLLOUT){
                //% 检测写事件
                //& 因为是模拟 Proactor 模式。所以需要一次性写完
                if( !clients[current_fd].write()){ //TODO 写入函数
                    //如果写入失败
                    clients[current_fd].close_connect();
                }
                
            }

        }
        if( timeout ){
            timer_handler();
            timeout = false;
        }


    }
    //10. 关闭文件描述符
    close(listen_FD); //关闭监听文件描述符
    close(epoll_fd); //关闭epoll文件描述符


    //11. 释放资源
    delete [] clients; //释放client数组
    delete pool; //释放线程池
    return 0;
}




/*
    int test[10] = {1,2,3,4,5,6,7,8,9,0};
    cout << *test+3 << endl;
    
    数组基地址是test
    +3就是偏移三个int
    所以数出来*test是1
    *test+1 是2
    *test+2 是3
    *test+3 是4
*/