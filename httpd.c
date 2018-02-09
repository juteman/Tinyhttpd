/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void *accept_request(void *);

void bad_request(int);

void cat(int, FILE *);

void cannot_execute(int);

void error_die(const char *);

void execute_cgi(int, const char *, const char *, const char *);

int get_line(int, char *, int);

void headers(int, const char *);

void not_found(int);

void serve_file(int, const char *);

int startup(u_short *);

void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
// 线程处理函数
void *accept_request(void *_client) {
  int client = *(int *)_client;
  char buf[1024];   // 读取行数据时的缓冲区
  int numchars;     // 读取了多少字符
  char method[255]; // 存储HTTP请求名称（字符串）
  char url[255];
  char path[512];
  size_t i, j;
  struct stat st;
  int cgi = 0; /* becomes true if server decides this is a CGI
             * program */
  char *query_string = NULL;

  // 读取HTTP头第一行：GET /index.php HTTP1.1
  numchars = get_line(client, buf, sizeof(buf));
  i = 0;
  j = 0;

  // 先看是什么方法，把方法名复制出来
  while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
    method[i] = buf[j];
    i++;
    j++;
  }
  method[i] = '\0';

  if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
    unimplemented(client);
    return NULL;
  }

  // 如果是POST请求，就认为需要调用CGI脚本来处理
  if (strcasecmp(method, "POST") == 0)
    cgi = 1;

  i = 0;
  // 跳过空白符
  while (ISspace(buf[j]) && (j < sizeof(buf)))
    j++;

  // 从缓冲区中把URL读取出来
  while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) {
    url[i] = buf[j];
    i++;
    j++;
  }
  url[i] = '\0';

  // 先处理如果是GET请求的情况
  if (strcasecmp(method, "GET") == 0) {
    query_string = url;
    // 移动指针，去找GET参数，也就是?后面的部分
    while ((*query_string != '?') && (*query_string != '\0'))
      query_string++;
    // 如果找到了的话，说明这个请求也需要调用脚本来处理
    // 此时就把请求字符串单独抽取出来
    if (*query_string == '?') {
      cgi = 1;
      // 这里是直接截断字符串，然后保留指针位置
      // 也就是query_string指针指向的是真正的请求参数
      *query_string = '\0';
      query_string++;
    }
  }

  // 这里是做一下路径拼接，因为url字符串以`/`开头，所以不用拼接新的分隔符
  sprintf(path, "htdocs%s", url);
  // 如果访问路径的最后一个字符是`/`，就为其补全，也就是默认访问index.html
  if (path[strlen(path) - 1] == '/')
    strcat(path, "index.html");
  // 检查请求所对应的文件是否存在
  if (stat(path, &st) == -1) {
    // 如果不存在，就把剩下的请求头都从缓冲区中读出去
    while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
      numchars = get_line(client, buf, sizeof(buf));
    // 然后返回一个404
    not_found(client);
  } else {
    // 如果文件存在但却是个目录，则继续拼接路径，默认访问这个目录下的index.html
    if ((st.st_mode & S_IFMT) == S_IFDIR)
      strcat(path, "/index.html");
    // 如果文件具有可执行权限，就执行它
    if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) ||
        (st.st_mode & S_IXOTH))
      cgi = 1;
    // 最后根据cgi参数，来决定是要返回静态文件，还是要调用脚本
    if (!cgi)
      serve_file(client, path);
    else
      execute_cgi(client, path, method, query_string);
  }

  close(client);
  return NULL;
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
// 返回一个400错误
void bad_request(int client) {
  char buf[1024];

  sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "Content-type: text/html\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "<P>Your browser sent a bad request, ");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "such as a POST without a Content-Length.\r\n");
  send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
// 这个函数就是读取文件的所有内容并发送给客户端
// 其实可以用mmap系统调用，可能效率更高
void cat(int client, FILE *resource) {
  char buf[1024];

  fgets(buf, sizeof(buf), resource);
  while (!feof(resource)) {
    send(client, buf, strlen(buf), 0);
    fgets(buf, sizeof(buf), resource);
  }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
// 返回一个500错误
void cannot_execute(int client) {
  char buf[1024];

  sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
// 报错并退出
void error_die(const char *sc) {
  // 这是stdio里面的函数，用于根据errno，打印错误信息到stderr并退出
  perror(sc);
  exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path, const char *method,
                 const char *query_string) {
  char buf[1024];
  int cgi_output[2];
  int cgi_input[2];
  pid_t pid;
  int status;
  int i;
  char c;
  int numchars = 1;
  int content_length = -1;

  // 首先需要根据请求是GET还是POST，来分别进行处理
  buf[0] = 'A';
  buf[1] = '\0';
  // 如果是GET，那么就要忽略剩余的请求头
  if (strcasecmp(method, "GET") == 0)
    while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
      numchars = get_line(client, buf, sizeof(buf));
  else /* POST */
  {
    // 如果是POST的话，就需要读出请求长度也就是Content-Length
    numchars = get_line(client, buf, sizeof(buf));
    while ((numchars > 0) && strcmp("\n", buf)) {
      buf[15] = '\0';
      if (strcasecmp(buf, "Content-Length:") == 0)
        content_length = atoi(&(buf[16]));
      numchars = get_line(client, buf, sizeof(buf));
    }
    // 如果请求长度不合法（比如根本就不是数字），那么就报错
    // 错误处理还挺完善
    if (content_length == -1) {
      bad_request(client);
      return;
    }
  }

  // 上述检查都没问题以后，这里就直接给客户端返回200状态了
  // 但其实下面开始可能会引发错误的，所以这里是一个不严谨的细节
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  send(client, buf, strlen(buf), 0);

  // 首先为输入和输出分配读写管道
  // 如果报错的话就退出
  if (pipe(cgi_output) < 0) {
    cannot_execute(client);
    return;
  }
  if (pipe(cgi_input) < 0) {
    cannot_execute(client);
    return;
  }

  // 然后fork自身，生成两个进程
  if ((pid = fork()) < 0) {
    cannot_execute(client);
    return;
  }
  // 子进程要调用CGI脚本
  if (pid == 0) /* child: CGI script */
  {
    // 环境变量缓冲区，当然这样会有溢出的风险
    char meth_env[255];
    char query_env[255];
    char length_env[255];

    // 这里是在重定向管道
    // 把父进程读写管道的描述符分别绑定到子进程的标准输入和输出
    // dup2功能和freopen()类似
    dup2(cgi_output[1], 1);
    dup2(cgi_input[0], 0);
    // 然后关掉不必要的描述符
    // 这里可能不是很好理解，画个管道的示意图就好了
    close(cgi_output[0]);
    close(cgi_input[1]);
    // 这里开始设置基本的CGI环境变量
    // 请求类型、参数、长度之类
    sprintf(meth_env, "REQUEST_METHOD=%s", method);
    putenv(meth_env);
    if (strcasecmp(method, "GET") == 0) {
      sprintf(query_env, "QUERY_STRING=%s", query_string);
      putenv(query_env);
    } else { /* POST */
      sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
      putenv(length_env);
    }

    // 最后，子进程使用exec函数簇，调用外部脚本来执行
    execl(path, path, NULL);
    exit(0);
  } else { /* parent */
    // 在父进程这一端，也要关闭不必要的描述符
    close(cgi_output[1]);
    close(cgi_input[0]);
    // 对于POST请求，要直接write()给子进程
    // 这样子进程所调用的脚本就可以从标准输入取得POST数据
    if (strcasecmp(method, "POST") == 0)
      for (i = 0; i < content_length; i++) {
        recv(client, &c, 1, 0);
        write(cgi_input[1], &c, 1);
      }
    // 然后父进程再从输出管道里面读出所有结果，返回给客户端
    while (read(cgi_output[0], &c, 1) > 0)
      send(client, &c, 1, 0);

    close(cgi_output[0]);
    close(cgi_input[1]);
    // 最后等待子进程结束
    waitpid(pid, &status, 0);
  }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
// 从socket中读取一行，感觉性能会比较差，因为是一个一个字符读取的
int get_line(int sock, char *buf, int size) {
  int i = 0;
  char c = '\0';
  int n;

  while ((i < size - 1) && (c != '\n')) {
    // 每次只读取一个字符（这样性能会很差）
    n = recv(sock, &c, 1, 0);
    /* DEBUG printf("%02X\n", c); */
    if (n > 0) {
      // 这里是检测是否读取到了回车换行符
      if (c == '\r') {
        /*
         * 注意MSG_PEEK参数，表示不清除TCP Buffer中被读出的数据
         * 也就意味着再次读取的话，还是读取到和先前一样的数据
         * 只不过你可以先检查先前读取到的数据，然后第二次读取自己需要的数量的数据
         * 相当于预先检查一下数据流的下面一段
         */
        n = recv(sock, &c, 1, MSG_PEEK);
        /* DEBUG printf("%02X\n", c); */
        // 如果下面一个字符是\n，说明一切正常，此时就把它读出来并加入缓冲区
        if ((n > 0) && (c == '\n'))
          recv(sock, &c, 1, 0);
        else
          // 如果下面一个不是，说明出了问题，只有\r而没有\n，此时就手动添加一个\n
          // 因为先前那个字符并没有被从TCP
          // Buffer中删除，因此下次再recv时，还是读出这个字符
          c = '\n';
      }
      buf[i] = c;
      i++;
    } else
      // 没有读到数据的话，设置条件直接退出
      // 这里就相当于直接一个break
      c = '\n';
  }
  // 最后结束字符串
  buf[i] = '\0';
  return (i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
// 这个函数就是单纯地返回HTTP头信息
void headers(int client, const char *filename) {
  char buf[1024];
  // 这句没有意义
  (void)filename; /* could use filename to determine file type */

  strcpy(buf, "HTTP/1.0 200 OK\r\n");
  send(client, buf, strlen(buf), 0);
  strcpy(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  strcpy(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
// 返回一个404错误
// 这么写看起来真是纠结……
void not_found(int client) {
  char buf[1024];

  sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "your request because the resource specified\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "is unavailable or nonexistent.\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</BODY></HTML>\r\n");
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
// 这个函数很简单，就是读取静态文件并返回
void serve_file(int client, const char *filename) {
  FILE *resource = NULL;
  int numchars = 1;
  char buf[1024];

  buf[0] = 'A';
  buf[1] = '\0';
  // 首先把请求头都给读掉
  while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
    numchars = get_line(client, buf, sizeof(buf));

  // 然后打开文件
  resource = fopen(filename, "r");

  // 这里还是，再判断文件是否存在，但其实已经没有意义了
  if (resource == NULL)
    not_found(client);
  else {
    headers(client, filename);
    cat(client, resource);
  }
  fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port) {
  int httpd = 0;
  struct sockaddr_in name;
  // 创建一个socket
  httpd = socket(PF_INET, SOCK_STREAM, 0);
  if (httpd == -1)
    error_die("socket");
  // 初始化sockaddr_in 结构体
  memset(&name, 0, sizeof(name));
  name.sin_family = AF_INET;
  name.sin_port = htons(*port);
  name.sin_addr.s_addr = htonl(INADDR_ANY);
  // 将socket绑定到对应端口上
  if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
    error_die("bind");
  if (*port == 0) /* if dynamically allocating a port */
  {
    int namelen = sizeof(name);
    /*
     *  1. getsockname()可以获得一个与socket相关的地址
     *    1) 服务器端可以通过它得到相关客户端地址
     *    2) 客户端可以得到当前已连接成功的socket的IP和端口
     *  2.
     * 在客户端不进行bind而直接连接服务器时，而且客户端需要知道当前使用哪个IP进行通信时比较有用（如多网卡的情况）。
     */
    if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
      error_die("getsockname");
    *port = ntohs(name.sin_port);
  }
  // 最后开始监听
  if (listen(httpd, 5) < 0)
    error_die("listen");
  return (httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client) {
  char buf[1024];

  sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</TITLE></HEAD>\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</BODY></HTML>\r\n");
  send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void) {
  int server_sock = -1;
  u_short port = 0;
  int client_sock = -1;
  struct sockaddr_in client_name;
  socklen_t client_name_len = sizeof(client_name);
  pthread_t newthread;

  // 如果port = 0，则端口随机
  server_sock = startup(&port);
  printf("httpd running on port %d\n", port);

  // 无限循环，一个请求创建一个循环
  // 这里为它设置一个退出条件，这样避免语法检查的问题
  while (1) {
    client_sock =
        accept(server_sock, (struct sockaddr *)&client_name, &client_name_len);
    if (client_sock == -1)
      error_die("accept");
    /* accept_request(client_sock); */
    // 创建线程，accept_request是线程处理函数，client_sock是参数，注意这里的参数类型转换
    if (pthread_create(&newthread, NULL, accept_request,
                       (void *)&client_sock) != 0) {
      perror("pthread_create");
      break;
    }
  }

  // 出现意外退出的时候，注意关闭socket
  close(server_sock);

  return (0);
}
