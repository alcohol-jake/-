#include "http.h"

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

// 设置文件描述符非阻塞
int setnonblocking( int fd ) {
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

// 向epoll中添加需要监听的文件描述符
void addfd( int epollfd, int fd, bool one_shot ) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot) 
    {
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);  
}

// 从epoll中移除监听的文件描述符
void removefd( int epollfd, int fd ) {
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

// 所有的客户数
int http_conn::m_user_count = 0;
// 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int http_conn::m_epollfd = -1;

// 关闭连接
void http_conn::close_conn() {
    if(m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; // 关闭一个连接，将客户总数量-1
    }
}

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in& addr){
    m_sockfd = sockfd;
    m_address = addr;
    
    // 端口复用
    int reuse = 1;
    setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    addfd( m_epollfd, sockfd, true );

    // 用户数加1
    m_user_count++; 
    init();
}

// 初始化其他信息
void http_conn::init()
{
    m_linger = false;       // 默认不保持链接  Connection : keep-alive保持连接           
    m_content_length = 0;
    m_host = 0;
    m_read_idx = 0;
    bzero(m_read_buf, READ_BUFFER_SIZE);
    
    bzero(Method, READ_BUFFER_SIZE);
    bzero(URL, READ_BUFFER_SIZE);
    bzero(Version, READ_BUFFER_SIZE);
    bzero(sendbuf, READ_BUFFER_SIZE);
    bzero(last_path, READ_BUFFER_SIZE);
    
    bzero(username, NSIZE);
    bzero(password, NSIZE);
    bzero(submit, NSIZE);
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read() {
    if( m_read_idx >= READ_BUFFER_SIZE ) {
        return false;
    }
    int bytes_read = 0;
    while(true) {
        // 从m_read_buf + m_read_idx索引出开始保存数据，大小是READ_BUFFER_SIZE - m_read_idx
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, 
        READ_BUFFER_SIZE - m_read_idx, 0 );
        if (bytes_read == -1) {
            if( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // 没有数据
                break;
            }
            return false;   
        } else if (bytes_read == 0) {   // 对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}


// 主状态机，解析请求
void http_conn::process_read() {
    if(sscanf(m_read_buf, "%s %s %s", Method, URL, Version) != 3) { //提取"请求方法"、"URL"、"HTTP版本"三个关键要素 
	printf("Request line error!\n");
    }
    printf("%s %s %s\n",Method, URL, Version);
    if (strcmp(URL, "/") == 0) {// 如果URI是根目录，则将其重定向到 /index.html
	strcpy(URL, "/index.html");
    }
}


// 写HTTP响应
bool http_conn::write()
{ 
    const char *httpHeader = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
    send(m_sockfd, httpHeader, strlen(httpHeader), 0);
    FILE *fp=fopen(last_path,"rb");
    if(!fp){
        perror("file open ailed");
    }
    
    char buf[READ_BUFFER_SIZE]={0};
    while(!feof(fp)){
        int len=fread(buf,sizeof(char),READ_BUFFER_SIZE,fp);
        send(m_sockfd,buf,len,0);
    }
    fclose(fp);
    return 0;
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
void http_conn::process_write() {
    if(strcmp(Method,"GET")==0){
        send_file();
    }
    if(strcmp(Method,"POST")==0){
        get_last();
    }
}

void http_conn::get_last() {
    int lastidx = -1;
    for(int i=strlen(m_read_buf)-1;i>=0;i--) {
        if (m_read_buf[i] == '\n') {
             lastidx = i;
             break;
        }
    }
    int lastLineLength =strlen(m_read_buf)-lastidx-1;
    char* lastLine = (char*)malloc(lastLineLength + 1);
    for (int i = 0; i < lastLineLength; i++) {
        lastLine[i] = m_read_buf[lastidx + 1 + i];
    }
    lastLine[lastLineLength] = '\0';
    int idx=0,op=0,f=0;
    for(int i=0;i<lastLineLength;i++){
        if(lastLine[i]=='='){
            f=1;
        }
        else if(lastLine[i]=='&'){
            if(op==0&&f){
                username[idx]='\0';
            }
            else if(op==1&&f){
                password[idx]='\0';
            }
            else if(op==2&&f){
                submit[idx]='\0';
            }
            op++;
            f=0;
            idx=0;
        }
        else{
            if(op==0&&f){
                username[idx++]=lastLine[i];
            }
            else if(op==1&&f){
                password[idx++]=lastLine[i];
            }
            else if(op==2&&f){
                submit[idx++]=lastLine[i];
            }
        }
    }
    if(strcmp(submit,"Register")==0){
         mysql_register();
        char cwd[READ_BUFFER_SIZE/2];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("getcwd() error");
        }
        sprintf(last_path, "%s/resources%s", cwd,"/zc.html");
    }
    if(strcmp(submit,"Login")==0){
        int t=mysql_check();
        char cwd[READ_BUFFER_SIZE/2];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("getcwd() error");
        }
        if(t==0){
            sprintf(last_path, "%s/resources%s", cwd,"/404.html");
        }
        else{
            sprintf(last_path, "%s/resources%s", cwd,"/home.html");
        }
    }
}

void http_conn::mysql_register(){
    MYSQL mysql_conn;                   
    MYSQL *mysql = mysql_init(&mysql_conn);
    if (mysql == NULL){
        printf("mysql init err");        
        exit(1);                         
    }
    mysql = mysql_real_connect(mysql, "localhost", "root", NULL, "web", 3306, NULL, 0);  
    if (mysql == NULL){
        printf("connect err\n");        
        exit(1);                      
    }
    char sql[255]; // 分配足够的空间，这里假设 SQL 语句不超过 255 个字符
    sprintf(sql, "INSERT INTO webuser (username, password) VALUES ('%s', '%s')", username, password);
    mysql_query(mysql, sql);
     
    mysql_close(mysql);   
}
int http_conn::mysql_check(){
   MYSQL mysql_conn;                   
   MYSQL *mysql = mysql_init(&mysql_conn);
   if (mysql == NULL){
       printf("mysql init err");        
       exit(1);                         
   }
   mysql = mysql_real_connect(mysql, "localhost", "root", NULL, "web", 3306, NULL, 0);  
   if (mysql == NULL){
       printf("connect err\n");        
       exit(1);                      
   }
   const char *sql = "SELECT * FROM webuser";
   if (mysql_query(mysql, sql)!= 0){
       printf("query sql err: %s\n", mysql_error(mysql));  
   }
   MYSQL_RES *res = mysql_store_result(mysql); 
   if (res == NULL){
       printf("res err: %s\n", mysql_error(mysql));  
       exit(1);                              
   }
   int flag=0;
   int num = mysql_num_rows(res);       
   int count = mysql_field_count(mysql);  
   for (int i = 0; i < num; i++){
       MYSQL_ROW row = mysql_fetch_row(res);  
       if(strcmp(row[0],username)==0&&strcmp(row[1],password)==0){
           flag=1;
       }
   } 
   if(flag==1){
       return 1;
   }
   else{
       return 0;
   }
}
void http_conn::send_file(){
    char cwd[READ_BUFFER_SIZE];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd() error");
    }
    char abs_path[READ_BUFFER_SIZE*3];
    sprintf(abs_path, "%s/resources%s", cwd, URL);

    FILE* file;
    struct stat file_stat;
    if(stat(abs_path,&file_stat)==-1){//文件不存在
        char f[READ_BUFFER_SIZE*2]={0};
        sprintf(f, "%s/resources%s", cwd, "/404.html");
        strcpy(last_path,f);
    }
    else{//文件存在
        strcpy(last_path,abs_path);
    }
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求
    process_read();
    // 生成响应
    process_write();
    
    modfd( m_epollfd, m_sockfd, EPOLLOUT);
}
