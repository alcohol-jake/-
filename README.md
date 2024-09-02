# linux系统下使用epoll与线程池实现轻量级服务器
具体实现是用同步I/O模拟实现Proactor模式

本地压力测试能达到4500-5000这样

![FUSKAJ3537TY6MTLR_WQ0E7](https://github.com/user-attachments/assets/71a99f72-991a-458c-8763-4365066eede8)

服务器实现了解析GET与POST请求，并且可以连接本地数据库。
这里我只做了一个简单的注册与登录的页面

![_720A V28W6~YK1S2`DG%$M](https://github.com/user-attachments/assets/d46ad062-97c4-4a71-bc30-89d2ab512fbe)


数据库只有用户名和密码两个属性
