#ifndef HTTP_CONN_H__
#define HTTP_conn_H__

#include <sys/epoll.h>
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
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>
#include <mysql/mysql.h>

#include "locker.h"


class http_conn{
public:

    static int m_epollfd;//所有socket上的事件都被注册到同一个epoll中
    static int m_user_count;//统计用户的数量
    
    static const int READ_BUFFER_SIZE=2048;//读缓冲区的大小
    static const int WRITE_BUFFER_SIZE=2048;//写缓冲区的大小 
    static const int NSIZE=100;//读缓冲区的大小

    http_conn(){}
    ~http_conn(){}
    
    void process();//处理客户端请求
    void init(int sockfd,const sockaddr_in &addr);//初始化新接收的连接
    void close_conn();//关闭连接
    bool read();//非阻塞读
    bool write();//非阻塞写
    

private:
    int m_sockfd;//该http连接的socket
    struct sockaddr_in m_address;//socket地址
    char m_read_buf[READ_BUFFER_SIZE];//读缓冲区
    
    char Method[READ_BUFFER_SIZE];
    
    char URL[READ_BUFFER_SIZE];
    
    char Version[READ_BUFFER_SIZE];
    
    char sendbuf[READ_BUFFER_SIZE];
    
    char last_path[READ_BUFFER_SIZE];
    
    char username[NSIZE];
    
    char password[NSIZE];
    
    char submit[NSIZE];
    
    void mysql_register();
    
    int mysql_check();
    
    char *m_host;//主机名暂时没用上
    bool m_linger;//http请求是否要保持连接暂时每用上
    int m_content_length;//这也暂时没用上
    
    int m_read_idx; 
    
    struct stat m_file_stat; 
    
    char* m_file_address;  
    
    void process_write(); 
    void init();//初始化连接其余的信息
    void send_file();
    void process_read();//解析http请求
    void get_last();//获得post请求最后一行的用户名和密码信息
};


#endif
