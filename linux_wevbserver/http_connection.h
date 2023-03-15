#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <sys/epoll.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <iostream>
#include <string.h>
#include "locker.h"
#include "log.h"
#include <assert.h>
class util_timer;

class http_connection{
    public:

        static int m_epollfd;                       //所有socket上的事件都被注册到同一个epollfd里面。所有对象共享这个数据所以是静态
        static int m_client_count;                  //统计客户端数量。所有对象共享这个数据所以是静态
        static const int FILENAME_LEN = 200;        //文件名的最大长度
        static const int READ_BUFFER_SIZE = 2048;   //读缓冲区的大小
        static const int WRITE_BUFFER_SIZE = 1024;  //写缓冲区大小
        // static int pipefd[2];                       //管道数组//update
        // static sort_timer_linklist timer_lst;       //update

        util_timer* timer;                          //定时器

        /*
            METHOD HTTP请求方法，这里只支持GET
        */
        enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
        

        /*
            CHECK_STATE                 :   解析客户端请求时，主状态机的状态
            CHECK_STATE_REQUESTLINE     :   当前正在分析请求行
            CHECK_STATE_HEADER          :   当前正在分析头部字段
            CHECK_STATE_CONTENT         :   当前正在解析请求体
        */
        enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
        
        /*
            HTTP_CODE 服务器处理HTTP请求的可能结果，报文解析的结果
            NO_REQUEST          :   请求不完整，需要继续读取客户数据
            GET_REQUEST         :   表示获得了一个完成的客户请求
            BAD_REQUEST         :   表示客户请求语法错误
            NO_RESOURCE         :   表示服务器没有资源
            FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
            FILE_REQUEST        :   文件请求,获取文件成功
            INTERNAL_ERROR      :   表示服务器内部错误
            CLOSED_CONNECTION   :   表示客户端已经关闭连接了
        */
        enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
        
        /*
            LINE_STATUS 从状态机的三种可能状态，即行的读取状态，分别表示
            LINE_OK             :   读取到一个完整的行 
            LINE_BAD            :   行出错
            LINE_OPEN           :   行数据尚且不完整
        */
        enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};

    public:

        http_connection();
        ~http_connection();

        void init(const int& sockfd, const sockaddr_in& addr); //初始化新接收的链接。

        void close_connect();                                   //关闭连接

        void process();                                         //处理客户端请求。对应线程池文件里的T* request->process

        bool read();                                            //非阻塞读取
        bool write();                                           //非阻塞写入

        void unmap();                                           //解除文件映射

        HTTP_CODE process_read();                               //预解析http请求
        HTTP_CODE process_read_line(char* context);             //解析请求行
        HTTP_CODE process_read_headers(char* context);          //解析请求头
        HTTP_CODE process_read_content(char* context);          //解析请求体
        
        HTTP_CODE do_request();                                 //解析出来的数据来执行具体处理

        LINE_STATUS parse_line();                               //解析特定行。


        bool process_write(HTTP_CODE http_stat);                //生成响应
        


        bool add_response(const char* format, ...);             //填充响应内容的主处理函数
        bool add_status_line(int status, const char* title) ;   //填充响应行
        bool add_headers(int content_len);                      //填充响应头
        bool add_content(const char* content);                  //填充响应体

        bool add_content_length(int content_length);            //填充数据长度字段
        bool add_content_type();                                //填充数据类型字段 //todo 需要优化不同类型
        bool add_keeplive();                                    //填充是否keeplive字段
        bool add_blank_line();                                  //填充空行

        int get_my_fd();                                        //update
    private:
        void member_init();                         //初始化其余的数据


        char* get_current_line(){                   //获取当前行的数据
            return m_readbuf + m_current_check_line;
        }














    private:

        int m_accept_FD;                            //接受连接后的读写文件描述符
        sockaddr_in m_client_INFO;                  //客户端信息

        char m_readbuf[READ_BUFFER_SIZE];           //读缓冲
        char m_writebuf[WRITE_BUFFER_SIZE];         //写缓冲
        int m_read_index;                           //记录了读缓冲区中的客户端数据的最后一个字节的下一个位置。因为有可能一次读不完，需要把第二次读的内容在这个位置接上。
        int m_write_index;                          //写缓冲区中已发送的数据的最后一个字节的下标。也同时承担着写缓冲区中待发送的字节数

        int m_current_check_index;                  //当前正在分析的字符在读缓冲区的位置。
        int m_current_check_line;                   //当前正在解析的行的起始位置
        CHECK_STATE m_current_CHECK_STATE;          //当前主状态机的状态


        char*   m_url;                                  //请求目标资源（名称）
        char*   m_version;                              //协议版本
        char*   m_host;                                 //主机名
        METHOD  m_method;                               //请求方法
        bool    m_keeplive;                             //HTTP是否keepalive
        int     m_content_length;                       //HTTP请求体的长度
        char    m_real_file[ FILENAME_LEN ];            // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录

        struct stat     m_file_stat;                    // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
        char*           m_file_address;                 // 客户请求的目标文件被mmap到内存中的起始位置


        int bytes_need_send;                                // 将要发送的数据的字节数
        int bytes_already_send;                             // 已经发送的字节数


        struct iovec m_iv[2];                               // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
        int m_iv_count;                                     //上面m_iv数组元素的数量
};






//&设置文件描述符为非阻塞

void set_non_blocking(int fd);

//&添加文件描述符到EPOLL监听数组中 
int addfd(int epollFd, int fd, int event_type, bool one_shot);

//&从EPOLL监听数组中删除文件描述符
int removefd(int epollFd, int fd);

//&从EPOLL监听数组中更改文件描述符的事件类型
int modfd(int epollFd, int fd, int event_type);

#endif