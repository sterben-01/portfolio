#include "http_connection.h"


http_connection::http_connection(){

}

http_connection::~http_connection(){
    
}


//' 网站的根目录
const char* doc_root = "/home/ziqi/Desktop/webserver/resources";
// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

//^-------------------类静态变量定义区-------------------
int http_connection::m_epollfd = -1;
int http_connection::m_client_count = 0;
//^-------------------FINISH----------------··········---

//&初始化其他参数
void http_connection::member_init(){
    m_current_CHECK_STATE = CHECK_STATE_REQUESTLINE;    //初始化 主状态机当前状态
    m_current_check_index = 0;                          //初始化 当前正在分析的字符在读缓冲区的位置。
    m_current_check_line = 0;                           //初始化 当前正在解析的行的起始位置
    m_method = GET;                                     //初始化 请求方法
    m_url = 0;                                          //初始化 目标资源名称
    m_version = 0;                                      //初始化 协议版本
    m_keeplive = false;                                 //初始化 是否keepalive
    m_host = 0;                                         //初始化 主机名
    m_content_length = 0;                               //初始化 请求体长度
    m_read_index = 0;                                   //初始化 读缓冲区中的客户端数据的最后一个字节的下一个位置
    m_write_index = 0;                                  //初始化 写缓冲区中的数据的最后一个字节
    
    bytes_already_send = 0;                             //初始化 已经发送的字节数量
    bytes_need_send = 0;                                //初始化 需要发送的字节数量
    bzero(m_readbuf, READ_BUFFER_SIZE);                 //初始化 读缓冲区
    bzero(m_writebuf, WRITE_BUFFER_SIZE);               //初始化 写缓冲区
    bzero(m_real_file, FILENAME_LEN);                   //初始化 文件路径缓冲区
}


//&初始化链接
void http_connection::init(const int& sockfd, const sockaddr_in& addr){ //使用引用避免拷贝
    this->m_accept_FD = sockfd;
    this->m_client_INFO = addr;
    //debug 这里没添加端口复用。读写描述符需要端口复用？？？
    int reuse = 1;
    setsockopt(m_accept_FD, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, EPOLLIN, true); //添加到epoll监听数组
    m_client_count++; //别忘了计数+1 
    LOG_INFO("client num:%d\n", m_client_count)
    member_init(); //初始化其余参数
}


void http_connection::close_connect(){ //关闭连接
    if(this->m_accept_FD != -1){
        removefd(m_epollfd, this->m_accept_FD); //这里涵盖了关闭文件描述符
        this->m_accept_FD = -1; //标记为-1这样标志为这个fd是没用的空置的。
        m_client_count--; //别忘了计数-1
        LOG_INFO("%s","connection closed\n");
    }
    else{
        //TODO 可能是重复关闭或者报错。以后可以加上
    }
}


bool http_connection::read(){
    if(m_read_index >= READ_BUFFER_SIZE){
        //已经超出缓冲区大小/缓冲区已满
        return false;
    }
    //读取数据
    int bytes_read = 0; //读取到的字节
    while(true){
        //%死循环读取
        bytes_read = recv(m_accept_FD, m_readbuf + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);
        /*
        %从acceptFD中读取
        读到缓存数组首地址(m_readbuf)+上次读到的位置的后一位开始(m_read_index)，
        读取数组总大小(READ_BUFFER_SIZE) - 已经读到的字节的数量(m_read_index)这么多
        */
       util_timer* timer = this->timer;
        if(bytes_read == -1){ //如果读取错误
            if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN){ //非阻塞的时候没数据会返回-1 但是错误号是EAGAIN。所以这三种情况的时候我们正常运行。
                //非阻塞的时候没数据会返回-1 但是错误号是EAGAIN。所以这三种情况的时候我们正常运行。
                break; //已经读完了，就break出去
            }
            else{
                perror("read error");
            }
            return false;
        }
        else if(bytes_read == 0){//!这里千万不要忘！等于0就是客户端断开了连接
            //printf("client disconnected...\n"); //DEBUG
            LOG_INFO("%s\n", "client disconnected...\n");//DEBUG
            return false;
        }
        m_read_index = m_read_index + bytes_read; //索引后移。
    }

    LOG_INFO("receive message from client: %s\n", m_readbuf)//打印读取到的数据//DEBUG
    return true;
}



bool http_connection::write(){
    //todo 写入数据
    int temp = 0;

    //%如果将要发送的字节小于等于0，这一次响应结束。
    if(bytes_need_send <=0){
        modfd(m_epollfd, m_accept_FD, EPOLLIN); //记得重置oneshot
        member_init(); //重置所有成员
        return true;
    }

    while(1){
        /*
        &我们这里使用了writev进行分散写。因为我们的响应头和响应体是分开的内存。我们的响应体在mmap的m_file_address
        &我们的响应头在m_write_buf。然后我们需要把这两个区域用一个结构体封装一下。
        */
       temp = writev(m_accept_FD, m_iv, m_iv_count);
       if(temp <= -1){
        // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
        // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
        if(errno == EAGAIN){
            modfd(m_epollfd, m_accept_FD, EPOLLOUT); //重新注册oneshot，不要写反了。EPOLLOUT是检测可写
            return true;
        }
        unmap(); //发生错误释放mmap分配的文件映射。
        return false;
       }
        bytes_already_send += temp;     //更新已经发送的字节数
        bytes_need_send -= temp;        //更新需要发送的字节数

        if(bytes_already_send >= m_iv[0].iov_len){
            //& 如果请求头已经发送完毕
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_already_send - m_write_index);
            m_iv[1].iov_len = bytes_need_send;
        }
        else{
            m_iv[0].iov_base = m_writebuf + bytes_already_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;

        }

        if(bytes_need_send <= 0){
            //& 如果没有数据发送了
            unmap();
            modfd(m_epollfd, m_accept_FD, EPOLLIN);
            if(m_keeplive){
                //% 如果需要keepalive
                member_init();
                return true;
            }
            else{
                return false;
            }
        }




    }




    return true;
}


//由线程池中的工作线程调用。这是处理HTTP请求的入口函数

void http_connection::process(){
    //todo 解析HTTP请求
    //% 解析请求
    HTTP_CODE read_ret = process_read();

    if(read_ret == NO_REQUEST){
        //如果请求不完整 需要继续获取数据
        modfd(m_epollfd, m_accept_FD, EPOLLIN); //修改文件描述符，重新检测。
        return;
    }
    //printf("parse http request and create response\n");
    //todo 生成响应
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_connect();
    }
    modfd(m_epollfd, m_accept_FD, EPOLLOUT); //记得要重置一下epoll事件状态以便触发oneshot下次可以正常检测
    
}


//?--------------------------------解析http请求部分------------------------------------
//主状态机，预解析请求
http_connection::HTTP_CODE http_connection::process_read(){
    //1. 定义初始状态
    LINE_STATUS line_stat = LINE_OK;    //行状态
    HTTP_CODE ret = NO_REQUEST;         //HTTP返回状态
    char* context = 0;                  //获取到的数据

    //2. while循环去逐行解析

    while(((m_current_CHECK_STATE == CHECK_STATE_CONTENT) &&(line_stat == LINE_OK))
            ||((line_stat = parse_line()) == LINE_OK)){ 
            //两种情况。第一种情况是主状态机要解析请求体，并且当前数据是OK的，可以解析
            //第二种情况是从状态机 也就是当前解析的数据是OK的，可以解析
        context = get_current_line();
        m_current_check_line = m_current_check_index; //更新当前行在缓冲区的位置。因为每行是靠\r\n来分割的。所以我们m_current_check_line是储存的这个\r\n后面第一个字符的位置。而m_current_check_index储存的是当前读到的字符。
        
        //printf("get 1 http line: %s\n", context); //DEBUG

        switch(m_current_CHECK_STATE){
            case CHECK_STATE_REQUESTLINE:
            {
                ret = process_read_line(context);
                //printf("case1\n"); //DEBUG
                if(ret == BAD_REQUEST){
                    //printf("case11\n"); //DEBUG
                    return BAD_REQUEST;
                }
                //todo 优化其他错误的处理
                
                break; //!必须加break不然会每一个都执行一次
            }
            case CHECK_STATE_HEADER:
            {
                //printf("case2\n"); //DEBUG
                ret = process_read_headers(context);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST){
                    //请求头解析完毕。进行下一步，解析具体的请求信息
                    //todo
                    return do_request();
                }
                //todo 优化其他错误的处理
                break; //!必须加break不然会每一个都执行一次
            }
            case CHECK_STATE_CONTENT:
            {
                printf("case3\n"); //DEBUG
                ret = process_read_content(context);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST){
                    //请求体解析完毕。进行下一步，解析具体的请求信息
                    //todo
                    return do_request();
                }
                line_stat = LINE_OPEN; //如果失败就是数据不完整。
                //todo 优化其他错误的处理
                break; //!必须加break不然会每一个都执行一次
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;

}   


//解析请求行，获得请求方法，目标URL，HTTP版本
//todo 此函数可以优化。整体判断逻辑不一致。
http_connection::HTTP_CODE http_connection::process_read_line(char* context){
    //& 初始数据 GET /index.html HTTP/1.1
    //* m_url = strpbrk(context, " \t"); 这加了\t的意思是有的协议可能是\t分割的。这里空格就行
    //% strpbrk 的作用是 在第一个参数中找第二个参数中任意第一次出现的字符，返回指向这个位置的指针。
    //printf("prl1\n"); //DEBUG
    m_url = strpbrk(context, " \t");

    //& 用了strpbrk之后，m_url 指向了这里 GET /index.html HTTP/1.1
    //                                      ↑
    *m_url= '\0';
    //& 替换后数据：GET\0/index.html HTTP/1.1
    m_url++;
    //& ++之后，m_url 指向了这里 GET\0/index.html HTTP/1.1
    //                                ↑

    char* method = context; //这里很牛逼。context现在是GET\0/index.html HTTP/1.1 这样。但是里面有\0。所以一会儿读到\0就停了。我们就可以提取出method部分
    if(strcasecmp(method, "GET") == 0){ //strcasecmp是比较两个字符串的时候忽略大小写
        m_method = GET;
    }
    else{
        //todo 目前只支持get请求。这里可以继续写
        return BAD_REQUEST;
    }
    //printf("prl2\n"); //DEBUG
    //& 现在的m_url是这一段 /index.html HTTP/1.1
    m_version = strpbrk(m_url, " ");
    if(!m_version){
        //如果协议版本为空
        return BAD_REQUEST;
    }
    //printf("prl3\n"); //DEBUG

    *m_version = '\0';
    //& 替换后数据：/index.html\0HTTP/1.1
    m_version++;
    //& ++之后，m_version 指向了这里 /index.html\0HTTP/1.1
    //                                            ↑
    //printf("prl4\n"); //DEBUG
    if(strcasecmp(m_version, "HTTP/1.1") != 0){ //strcasecmp是比较两个字符串的时候忽略大小写
        //printf("m_version%s\n", m_version);
        return BAD_REQUEST;
    }

    //& 现在m_url 仅仅是/index.html 这一段。因为\0后面的数据读不到
    //% 如果有的时候请求url部分长这样 http://192.168.1.1:10000/index.html 我们就要判断一下
    if(strncasecmp(m_url,"http://", 7) == 0){ //strncasecmp 是比较前n个字符并且忽略大小写
        m_url += 7; // 我们让m_url忽略掉前7个 http:// 字符。指向1这个字符。
        m_url = strchr(m_url, '/'); //% 此处可使用strpbrk。这俩一个是任意出现第一次，一个是字符出现的第一次
        //& 这里找完之后 m_url会指向这里 192.168.1.1:10000/index.html
        //                                                ↑
    }
    if(!m_url || m_url[0] != '/'){
        //如果 没找到 或 找到的不是/
        return BAD_REQUEST;
    }
    //printf("prl5\n"); //DEBUG
    m_current_CHECK_STATE = CHECK_STATE_HEADER; // 主状态机的检查状态转换为检查请求头。我们当前处理的是请求行
    return NO_REQUEST; //因为需要继续解析。return一个数据不完整


}

//解析请求头
http_connection::HTTP_CODE http_connection::process_read_headers(char* context){
    //&这个处理顺序没有关系。因为context是每一行的内容。每次读入一行进来会匹配一次。
    if(context[0] == '\0'){
        //如果第一个字符是字符串终止符，证明请求头已经读完了
        if(m_content_length != 0){
            //请求体长度不为0证明下面还有请求体需要读，则改变主状态机的状态。
            m_current_CHECK_STATE = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        //否则已经完成解析。返回完成请求
        return GET_REQUEST;
    }
    else if(strncasecmp(context, "Host:", 5) == 0){
        //处理host字段
        context = context + 5;
        context = context + strspn(context, " "); //strspn是找到第一个不出现在第二个参数里面的字符串的位置。这里的意思是找到第一个不是空格的字符
        m_host = context;
    }
    else if(strncasecmp(context, "Connection:", 11) == 0){
        //处理Connection字段
        context = context + 11;
        context = context + strspn(context, " ");
        m_keeplive = context;
    }
    else if(strncasecmp(context, "Content-Length:", 15) == 0){
        //处理Content-Length字段
        context = context + 15;
        context = context + strspn(context, " ");
        m_content_length = atoi(context);
    }
    else{
        //todo 其他字段 如 user-agent, accept language 等等
        //printf("unknown header %s\n", context);
        LOG_INFO("unknown header %s\n", context)
    }
    return NO_REQUEST; 
}


//解析请求体
//%我们没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_connection::HTTP_CODE http_connection::process_read_content(char* context){
    if((m_read_index + m_current_check_index) >= m_content_length){
        context[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//解析特定行。
//% 判断的依据就是\r\n
http_connection::LINE_STATUS http_connection::parse_line(){
    char temp;
    for( ; m_current_check_index < m_read_index; m_current_check_index++){
        //省略了第一个初始化表达式。
        temp = m_readbuf[m_current_check_index];
        if(temp == '\r'){
            if((m_current_check_index + 1) == m_read_index){
                //&如果读到了\r然后发现下一位是我们上一次读到的最后的位置证明数据不完整
                return LINE_OPEN;
            }
            else if(m_readbuf[m_current_check_index + 1] == '\n'){
                //&如果\r\n连起来了，证明本行读完。
                m_readbuf[m_current_check_index] = '\0'; //我们要把\r换成\0也就是换成字符串结束符
                m_current_check_index++; //后移一位
                m_readbuf[m_current_check_index] = '\0'; //我们要把\n换成\0也就是换成字符串结束符
                m_current_check_index++; //后移一位 这时候已经把当前独到的数据的下标移动到了第二行开头了
                return LINE_OK;
            }
            return LINE_BAD; //&读到\r但是没有数据表明数据不完整那就是坏了
        }
        else if(temp == '\n'){
            //&这种情况对应上面的LINE_OPEN也就是读到\r之后发现数据不完整，那就等数据完整继续接着读。然后这次读到了\n
            if((m_current_check_index > 1) && (m_readbuf[m_current_check_index - 1] == '\r')){
                //&如果发现上一个字符是\r 证明这是OK的。
                m_readbuf[m_current_check_index - 1] = '\0'; //我们要把\r换成\0也就是换成字符串结束符
                m_readbuf[m_current_check_index] = '\0'; //我们要把\n换成\0也就是换成字符串结束符
                m_current_check_index++; //后移一位 这时候已经把当前独到的数据的下标移动到了第二行开头了
                return LINE_OK;
            }
            return LINE_BAD;
        }
        //return LINE_OPEN
    }
    return LINE_OK;
}




//?--------------------------------FINISH----------------------------------------------






//解析出来的数据来执行具体处理
/*
&当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
&如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
&映射到内存地址m_file_address处，并告诉调用者获取文件成功
*/
http_connection::HTTP_CODE http_connection::do_request(){
    strcpy(m_real_file, doc_root); //先复制路径
    int len = strlen( doc_root ); //获取路径长度
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1 ); //m_real_file是文件路径数组基地址。从偏移len个字节开始，复制文件名过去。剩余的空间自动用\0填充

    //% 获取m_real_file文件相关的状态信息 -1失败，0成功
    if(stat(m_real_file, &m_file_stat) < 0){
        return NO_RESOURCE;
    }

    //% 判断访问权限 通过stat函数获取文件的stat.st_mode，然后和对应的权限位的掩码相与，就可以获取该权限位的值，
    //& S_IROTH 其他组读权限
    if(!(m_file_stat.st_mode & S_IROTH)){
        //如果没有权限
        return FORBIDDEN_REQUEST;
    }

    //% 判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode)){
        //如果是
        return BAD_REQUEST;
    }

    //% 只读方式打开文件
    int resource_fd = open(m_real_file, O_RDONLY);

    //%MMAP 创建内存映射。之后会从这里拿数据给write函数
    m_file_address = (char*)mmap(NULL, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, resource_fd, 0 ); //!注意这里使用了私有 MAP_PRIVATE 即 映射区文件改变不影响源文件。
    close(resource_fd); //已经映射了，即使文件关闭，映射依然存在。因为映射的是地址而不是文件本身。和文件描述符无关。所以可以直接关闭文件描述符
    //&冷知识：映射后直接关闭描述符，如果想修改文件的话可以用msync来同步至文件。
    return FILE_REQUEST;

}




//解除文件映射
void http_connection::unmap(){
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0; //文件地址置空
    }
}






//?--------------------------------生成http响应部分------------------------------------

//生成响应
bool http_connection::process_write(http_connection::HTTP_CODE http_stat){
    switch(http_stat){
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);      //写入响应行
            add_headers(strlen(error_500_form));        //写入响应头
            if(!add_content(error_500_form)){           //写入响应体
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);      //写入响应行
            add_headers(strlen(error_400_form));        //写入响应头
            if(!add_content(error_400_form)){           //写入响应体
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);      //写入响应行
            add_headers(strlen(error_404_form));        //写入响应头
            if(!add_content(error_404_form)){           //写入响应体
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(404, error_403_title);      //写入响应行
            add_headers(strlen(error_403_form));        //写入响应头
            if(!add_content(error_403_form)){           //写入响应体
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_writebuf;              //向分散写 结构体数组写入 响应头的 基地址
            m_iv[0].iov_len = m_write_index;            //向分散写 结构体数组写入 响应头的 长度。这里index有记录长度的作用
            m_iv[1].iov_base = m_file_address;          //向分散写 结构体数组写入 响应体的 基地址
            m_iv[1].iov_len = m_file_stat.st_size;      //向分散写 结构体数组写入 响应体的 长度
            m_iv_count = 2;                             //告知 分散写 结构体数组的大小

            bytes_need_send = m_write_index + m_file_stat.st_size;      //更新需要写入的数据大小 (响应头大小+响应体大小)
            return true;
        }
        default:
            return false;
        
    }

    //!如果没有响应体需要写入，也就是只返回响应头的话
    m_iv[0].iov_base    = m_writebuf;
    m_iv[0].iov_len     = m_write_index;
    m_iv_count          = 1;
    bytes_need_send     = m_write_index;
    return true;
}


//添加响应内容的主处理函数
bool http_connection::add_response(const char* format, ...){
    if(m_write_index >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list args;
    va_start(args, format);
    int len = vsnprintf(m_writebuf + m_write_index, WRITE_BUFFER_SIZE - 1 - m_write_index, format, args);
    //%将可变参数格式化输出到一个字符数组。第一个参数是位置。就是基地址+偏移量。第二个是大小。
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_index)){
        return false;
    }
    m_write_index += len; //更新最后字节位置
    va_end(args);
    return true;



}

//添加响应行
bool http_connection::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

//添加内容长度字段
bool http_connection::add_content_length(int content_length){
    return add_response("Content-Length: %d\r\n", content_length);
}

//填充数据类型字段 //todo 需要优化不同类型
bool http_connection::add_content_type(){
    return add_response("Content-Type: %s\r\n", "text/html");
}

//填充是否keeplive字段
bool http_connection::add_keeplive(){
    return add_response( "Connection: %s\r\n", (m_keeplive == true) ? "keep-alive" : "close" );
}

//填充空行
bool http_connection::add_blank_line(){
    return add_response("%s", "\r\n");
} 

//填充响应头
bool http_connection::add_headers(int content_len){
    add_content_length(content_len);
    add_content_type();
    add_keeplive();
    add_blank_line();
}

//填充响应体
bool http_connection::add_content(const char* content){
    return add_response("%s", content);
}

//?-------------------------------FINISH------------------------------------



//添加文件描述符到EPOLL监听数组中 
int addfd(int epollFd, int fd, int event_type, bool one_shot){
    //% 设置epoll事件属性
    epoll_event event;
    event.data.fd = fd; //EPOLL设置 新接受的读写文件描述符
    if(event_type == EPOLLIN | EPOLLET){ //update
        event.events = event_type;
    }
    else{
        event.events = event_type | EPOLLRDHUP; //EPOLL监听是否可读和是否异常断开
    }
    //& 链接断开的时候会触发EPOLLRDHUP事件。
    //' 设置oneshot
    //std::cout <<"one_shot state: " <<one_shot << std::endl;
    if (one_shot == true){
        //std::cout <<"one_shot true" <<one_shot << std::endl;
        event.events |= EPOLLONESHOT;
    }
    //% 使用ctl置入 epollFd
    epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event);

    set_non_blocking(fd);//设置文件描述符为非阻塞
    return 0; //TODO 此处可以优化返回值判断成功与否

}


//从EPOLL监听数组中删除文件描述符
int removefd(int epollFd, int fd){
    //% 使用ctl移除 epollFd
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, 0);
    close(fd); //关闭文件描述符
    return 0; //TODO 此处可以优化返回值判断成功与否

}



//&从EPOLL监听数组中更改文件描述符的事件类型
//! 需要重置socket上的EPOLLONESHOT 事件，确保下次可读的时候，EPOLLIN事件可以被触发。
int modfd(int epollFd, int fd, int event_type){

    epoll_event event;
    event.data.fd = fd; //EPOLL设置 新接受的读写文件描述符
    event.events = event_type | EPOLLRDHUP | EPOLLRDHUP; //修改文件描述符事件类型。并重置socket上的EPOLLONESHOT
    //% 使用ctl修改 epollFd
    epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &event);
    return 0; //TODO 此处可以优化返回值判断成功与否
}

//设置文件描述符为非阻塞
void set_non_blocking(int fd){
    int accept_FD_FLAG = fcntl(fd, F_GETFL); //获取文件描述符属性
    accept_FD_FLAG |=  O_NONBLOCK; //或操作进行取值
    fcntl(fd, F_SETFL, accept_FD_FLAG); //设置为非阻塞
}
/*
ONE SHOT
oneshot指的某socket对应的fd事件最多只能被检测一次，不论你设置的是读写还是异常。
因为可能存在这种情况：如果epoll检测到了读事件，数据读完交给一个子线程去处理，
如果该线程处理的很慢，在此期间epoll在该socket上又检测到了读事件，则又给了另一个线程去处理，
则在同一时间会存在两个工作线程操作同一个socket。

ET模式指的是：数据第一次到的时刻才通知，其余时刻不再通知。如果读完了又来了新数据，epoll继续通知。
ET模式下可以通知很多次。监听socket不用设置为oneshot是因为只存在一个主线程去操作这个监听socket

EPOLLONESHOT这种方法，可以在epoll上注册这个事件，注册这个事件后，如果在处理写成当前的SOCKET后不再重新注册相关事件，
那么这个事件就不再响应了或者说触发了。
当处理完毕想要通知epoll可以再次处理的时候就要调用epoll_ctl重新注册(重置)文件描述符上的事件。这样前面的socket就不会出现竞态

也就是说注册了 EPOLLONESHOT 事件的 socket 一旦被某个线程处理完毕， 该线程就应该立即重置这个 socket 上的 EPOLLONESHOT 事件，
以确保这个 socket 下一次可读时，其 EPOLLIN 事件能被触发，进而让其他工作线程有机会继续处理这个 socket。

*/

int http_connection::get_my_fd(){
    return m_accept_FD;
}
