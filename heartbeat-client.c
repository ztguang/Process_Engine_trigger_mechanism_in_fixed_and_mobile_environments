// gcc -I/usr/include/mysql/ -L/usr/lib64/mysql/ -lmysqlclient -lpthread heartbeat-client.c -o heartbeat-client
// gcc heartbeat-client.c -o heartbeat-client
// indent -npro -kr -i8 -ts8 -sob -l280 -ss -ncs -cp1 *
/* heartbeat-client.c
 *
 * Copyright (c) 2015 zhang tong guang. ztguang.blog.chinaunix.net
 * Use may be in whole or in part in accordance to
 * the General Public License (GPL).
*/
/*****************************************************************************/
/*** heartbeat-client.c                                                    ***/
/*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <resolv.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <mysql.h>

#define BUF_SIZE 1024				//默认缓冲区
#define NAME_SIZE 40				//客户端用户名
#define PROCESSID_SIZE 52			//
#define BROWSERMSG_SIZE 62

#define SERVER_PORT 11111			//监听端口
#define SERVER_HOST "127.0.0.1"		//服务器IP地址
#define BROWSER_PORT 22222			//监听端口
#define BROWSER_HOST "127.0.0.1"	//BROWSER本地IP地址
#define EPOLL_RUN_TIMEOUT -1		//epoll的超时时间
#define EPOLL_SIZE 10				//epoll监听的客户端的最大数目, that is pipe_reader
#define LISTEN_SIZE 3				//监听队列长度

#define DELAY 2						//seconds

//两个有用的宏定义：检查和赋值并且检测
#define CHK(eval) if(eval < 0){perror("eval"); printf("(%d)-<%s>\n", __LINE__, __FUNCTION__); exit(-1);}
#define CHK2(res, eval) if((res = eval) < 0){perror("eval"); printf("(%d)-<%s>\n", __LINE__, __FUNCTION__); exit(-1);}

struct sockaddr_in seraddr;
int serverfd, got_reply = 1;
int startfcd = 0;		//SMD 和 FCD 通信的启动开关
char message[BUF_SIZE];
double period;			//used to determine whether SMD access FCD continuously
time_t start, now;
char processid[PROCESSID_SIZE];

/*---------------------------------------------------------------------
	sig_handler - if the single is OOB, set flag.  If ALARM, send heartbeat.
	heartbeat checking
 ---------------------------------------------------------------------*/
void sig_handler(int signum)
{
	int bytes, len;
	char buf[BUF_SIZE];
	//char processid[PROCESSID_SIZE] = { 0, };

	//mysql------------------
	MYSQL *conn_ptr;
	MYSQL_RES *res_ptr;
	MYSQL_ROW sqlrow;
	MYSQL_FIELD *fd;
	int res, i, j;
	char sql[1024];
	char c;

	struct tcp_info info;
	int leng = sizeof(info);

	if (signum == SIGURG) {

		//读写之前，先判断socket connection是否正常
		getsockopt(serverfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
		if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
			CHK2(bytes, recv(serverfd, &c, sizeof(c), MSG_OOB));
			if (bytes == 0) return;

			got_reply = (c == 'Y');	/* Got reply */
			fprintf(stderr, "[server is alive]\n");
			time(&start);

		} else {		// socket disconnected 
			printf("(%d)-<%s> SIGURG: socket disconnected FCD\n", __LINE__, __FUNCTION__);
			return;
		}

	} else if (signum == SIGALRM) {
		//printf("(%d)-<%s> got_reply = %d\n", __LINE__, __FUNCTION__, got_reply);

		if (got_reply) {
			alarm(DELAY);	// Wait a while

			//读写之前，先判断socket connection是否正常
			getsockopt(serverfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
			if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
				CHK2(len, send(serverfd, "?", 1, MSG_OOB));		// Alive??
				if (len == 0) return;
				got_reply = 0;
				//printf("(%d)-<%s> send [MSG_OOB] to FCD\n", __LINE__, __FUNCTION__);
				printf("(%d)-<%s> send [heartbeat] to FCD\n", __LINE__, __FUNCTION__);
			} else {		// socket disconnected 
				CHK(close(serverfd));
				CHK2(serverfd, socket(PF_INET, SOCK_STREAM, 0));
				if (fcntl(serverfd, F_SETOWN, getpid()) != 0) {		// claim SIGIO/SIGURG signals
					perror("Can't claim SIGURG and SIGIO");
					return;
				}
				if (connect(serverfd, (struct sockaddr *)&seraddr, sizeof(seraddr)) == 0)
					printf("(%d)-<%s> re-connect FCD [OK]\n", __LINE__, __FUNCTION__);
				else
					printf("(%d)-<%s> re-connect FCD [KO]\n", __LINE__, __FUNCTION__);
			} //end if

		} else {
			//printf("(%d)-<%s> Lost connection to server!\n", __LINE__, __FUNCTION__);
			printf("(%d)-<%s> not recv [heartbeat] from FCD!\n", __LINE__, __FUNCTION__);
			alarm(DELAY);	/* Wait a while */
		} //end if (got_reply)

	} //end if
} //end sig_handler

//daemon WAIT on SMD that waiting data sent by browser
//browser send " guestname=Jone, processid=2015-1119-2302-57441-1181348586, finish " to daemon WAIT
//arg refer to pipefd
void *handle_browser(void *arg)
{
	//socket to listen
	int listener;		//监听socket
	struct sockaddr_in addr, peer;
	addr.sin_family = PF_INET;
	addr.sin_port = htons(BROWSER_PORT);
	addr.sin_addr.s_addr = inet_addr(BROWSER_HOST);
	socklen_t socklen;
	socklen = sizeof(struct sockaddr_in);
	int len;
	int client, starting = 1;	//starting: grest start to run tasks on browser

	struct tcp_info info;
	int leng = sizeof(info);

	CHK2(listener, socket(PF_INET, SOCK_STREAM, 0));	//初始化监听socket

	// 设置套接字选项避免地址使用错误
	int on = 1;
	if ((setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0) {
		perror("browser: setsockopt failed\n");
		exit(EXIT_FAILURE);
	}

	CHK(bind(listener, (struct sockaddr *)&addr, sizeof(addr)));	//绑定监听socket

	//printf("listen\n");
	CHK(listen(listener, LISTEN_SIZE));	//设置监听
	printf("(%d)-<%s> browser: listening\n", __LINE__, __FUNCTION__);
	//socket to listen

	int pipe_write = *((int *)arg);
	char browsermsg[BROWSERMSG_SIZE];

	while (1) {
		CHK2(client, accept(listener, (struct sockaddr *)&peer, &socklen));
		printf("(%d)-<%s> browser: accepted\n", __LINE__, __FUNCTION__);

		while (1) {
			//------------------read * from browser

			//读写之前，先判断socket connection是否正常
			getsockopt(client, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
			if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
				bzero(browsermsg, BROWSERMSG_SIZE);
				//接受guestname=Jone, processid=2015-1119-2302-57441-1181348586, finish
				CHK2(len, recv(client, browsermsg, BROWSERMSG_SIZE, MSG_NOSIGNAL));
			} else {		// socket disconnected 
				printf("(%d)-<%s> FCD disconnected browser\n", __LINE__, __FUNCTION__);
				startfcd = 0;		//very improtant
				alarm(0);			//very improtant
				break;
			}

			if (starting) {
				time(&start);
				starting = 0;
				//printf("(%d)-<%s> start time: %f\n", __LINE__, __FUNCTION__, start);
			}

			CHK(write(pipe_write, browsermsg, strlen(browsermsg)));	//send guestname, processid, finish

			//printf("(%d)-<%s> browser: send msg to handle_fcd: %s\n", __LINE__, __FUNCTION__, browsermsg);
			//printf("(%d)-<%s> browser client: %d\n", __LINE__, __FUNCTION__, client);
			//CHK(close(client));
		} //end while
	} //end while

	return NULL; // not reach there
} //end handle_browser thread

//communication between SMD & FCD, SMD 和 FCD 通信
//arg is NULL
void *handle_fcd(void *arg)
{
	//mysql------------------
	MYSQL *conn_ptr;
	MYSQL_RES *res_ptr;
	MYSQL_ROW sqlrow;
	MYSQL_FIELD *fd;
	int res, i, j;
	char sql[1024];

	conn_ptr = mysql_init(NULL);
	if (!conn_ptr) {
		printf("(%d)-<%s> mysql_init failed\n", __LINE__, __FUNCTION__);
		return EXIT_FAILURE;
	}
	conn_ptr = mysql_real_connect(conn_ptr, "localhost", "root", "123456", "test", 0, NULL, 0);
	//root 为用户名 123456为密码 test为要连接的database  

	if (conn_ptr) {
		printf("(%d)-<%s> MYSQL Connection success\n", __LINE__, __FUNCTION__);
	} else {
		printf("(%d)-<%s> MYSQL Connection failed\n", __LINE__, __FUNCTION__);
		return EXIT_FAILURE;
	}
	//mysql_close(conn_ptr);
	//mysql------------------

	int bytes;
	//char line[100];
	struct tcp_info info;
	int leng = sizeof(info);

	int len;
	char buf[BUF_SIZE];
	//char processid[PROCESSID_SIZE] = { 0, };

	sleep(1);	//等待main线程就绪

	while (1)
	{
		got_reply = 1;

		//printf("client-fcd: startfcd: %d\n", startfcd);
		if (startfcd)	//wait main() to set startfcd=1, SMD 和 FCD 通信的启动开关
		{
			alarm(DELAY);		// very important

			//读写之前，先判断socket connection是否正常
			getsockopt(serverfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
			if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
				//printf("main TCP_ESTABLISHED\n");
				;
			} else {
				CHK(close(serverfd));
				CHK2(serverfd, socket(PF_INET, SOCK_STREAM, 0));
				if (fcntl(serverfd, F_SETOWN, getpid()) != 0) {		// claim SIGIO/SIGURG signals
					perror("Can't claim SIGURG and SIGIO");
					continue;
				}
				if (connect(serverfd, (struct sockaddr *)&seraddr, sizeof(seraddr)) == 0)
					printf("(%d)-<%s> re-connect FCD [OK]\n", __LINE__, __FUNCTION__);
				else {
					printf("(%d)-<%s> re-connect FCD [KO]\n", __LINE__, __FUNCTION__);
					//continue;
				}
			}

			//if (connect(serverfd, (struct sockaddr *)&seraddr, sizeof(seraddr)) == 0) {
			//printf("[OK] communication between SMD & FCD\n");

			//while (1) {
			while (startfcd) {
//*
				//读写之前，先判断socket connection是否正常
				getsockopt(serverfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
				if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
					printf("(%d)-<%s> TCP_ESTABLISHED [OK]\n", __LINE__, __FUNCTION__);
				} else {		// socket disconnected 
					printf("(%d)-<%s> TCP_ESTABLISHED [KO]\n", __LINE__, __FUNCTION__);
					//continue;
				}
//*/
				//---------------trigger between SMD & FCD
				time(&now);
				period = difftime(now, start);
				printf("(%d)-<%s> period: %f\n", __LINE__, __FUNCTION__, period);

				if (0 <= period && period < 8.0) {	//normal visits
					//time(&start);
					//printf("0 <= period < 8.0, [trigger FCD], redirect to FCD, [access FCD]\n");
					printf("(%d)-<%s> 0 <= period < 8.0, access FCD\n", __LINE__, __FUNCTION__);
					printf("(%d)-<%s> -----------------------[trigger FCD]\n", __LINE__, __FUNCTION__);
				}

				if (period > 30.0) {		//server shutdown or connectiong to FCD is abnormal
					//trigger SMD;
					//redirect to localhost and invoke task in SMD
					//printf("[trigger SMD], redirect to SMD, [access SMD]\n");
					printf("(%d)-<%s> period > 30.0\n", __LINE__, __FUNCTION__);
					printf("(%d)-<%s> -----------------------[trigger SMD]\n", __LINE__, __FUNCTION__);
				}
				//---------------trigger between SMD & FCD

				//memset(line, 0x00, 100);
				//gets(line);
				//printf("send [%s]\n", line);
				//CHK2(len, send(serverfd, "adsf", 4, 0)); sleep(1);

				//CHK2(len, send(serverfd, line, strlen(line), 0));		// send
				//if (len <= 0) break;

				//CHK2(bytes, recv(serverfd, line, sizeof(line), 0));		// recieve
				//if (bytes <= 0) break;

				sleep(2);
			} //end while (startfcd)
			//printf("(%d)-<%s> end while\n", __LINE__, __FUNCTION__);
		} //end if (startfcd)
		sleep(5);	//send heartbeat request every 5s to FPE
	} //end while (1)
} //end handle_fcd thread

/*---------------------------------------------------------------------
	main - set up client and begin the heartbeat.

    create two threads：
	handle_browser：等待browser，将browser的信息通过管道写给 main thread
	handle_fcd：接受 FCD 的信息，将从 handle_browser 接受到的信息发送给 FCD
 ---------------------------------------------------------------------*/
int main(int count, char *strings[])
{
	struct sigaction act;
	bzero(&act, sizeof(act));
	act.sa_handler = sig_handler;
	act.sa_flags = SA_RESTART;
	sigaction(SIGURG, &act, 0);
	sigaction(SIGALRM, &act, 0);

	struct tcp_info info;
	int leng = sizeof(info);

	int pid, pipe_fd[2], epfd;
	int res, len;
	char buf[BUF_SIZE];
	//struct sockaddr_in seraddr;
	seraddr.sin_family = PF_INET;
	seraddr.sin_port = htons(SERVER_PORT);
	seraddr.sin_addr.s_addr = inet_addr(SERVER_HOST);

	static struct epoll_event ev, events[1];
	ev.events = EPOLLIN | EPOLLET;
	CHK(pipe(pipe_fd));
	CHK2(epfd, epoll_create(EPOLL_SIZE));
	ev.data.fd = pipe_fd[0];	// reader
	CHK(epoll_ctl(epfd, EPOLL_CTL_ADD, pipe_fd[0], &ev));

	//------- create thread: handle_browser
	pthread_t writer;
	int rt = pthread_create(&writer, NULL, handle_browser, (void *)&pipe_fd[1]);
	if (-1 == rt) {
		printf("(%d)-<%s> thread creation error\n", __LINE__, __FUNCTION__);
		return -1;
	}
	//------- create thread: handle_browser

	//------- create thread: handle_fcd
	pthread_t fcd;
	//int rtfcd = pthread_create(&fcd, NULL, handle_fcd, (void *)&serverfd);
	int rtfcd = pthread_create(&fcd, NULL, handle_fcd, NULL);
	if (-1 == rtfcd) {
		printf("(%d)-<%s> thread creation error\n", __LINE__, __FUNCTION__);
		return -1;
	}
	//------- create thread: handle_fcd

	//------- the following is main thread, daemon MAIN on SMD

	char username[NAME_SIZE];
	//char processid[PROCESSID_SIZE];
	char browsermsg[BROWSERMSG_SIZE];
	int epoll_events_count;
	char *prt_c;

	CHK2(serverfd, socket(PF_INET, SOCK_STREAM, 0));
	if (fcntl(serverfd, F_SETOWN, getpid()) != 0)	// claim SIGIO/SIGURG signals
		perror("Can't claim SIGURG and SIGIO");

	//该系统使用的前提条件，SMD先连接FCD，然后SMD可以外出移动办公
	if (connect(serverfd, (struct sockaddr *)&seraddr, sizeof(seraddr)) == 0)
		printf("(%d)-<%s> connect FCD [OK]\n", __LINE__, __FUNCTION__);
	else
		printf("(%d)-<%s> connect FCD [KO]\n", __LINE__, __FUNCTION__);

	while (1) {
		//read * from browser, event_driven
		//guestname=Jone, processid=2015-1119-2302-57441-1181348586, finish
		//CHK2(epoll_events_count, epoll_wait(epfd, events, 2, EPOLL_RUN_TIMEOUT));

		//epoll_wait(int epfd, struct epoll_event * events, int maxevents, int timeout)
		//参数events用来从内核得到事件的集合，maxevents告之内核这个events有多大，
		//这个 maxevents的值不能大于创建epoll_create()时的size，
		//参数timeout是超时时间 , 毫秒，0会立即返回，-1将不确定，也有说法说是永久阻塞
		if((epoll_events_count = epoll_wait(epfd, events, 1, EPOLL_RUN_TIMEOUT)) < 0){
			//perror("evall");
			//printf("EINTR [%d] [%d]\n", EINTR, epoll_events_count);
			//exit(-1);
			sleep(1);
			continue;
		}

		for (int i = 0; i < epoll_events_count; i++) {
//*
			//读写之前，先判断socket connection是否正常
			getsockopt(serverfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
			if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
				//printf("main TCP_ESTABLISHED\n");
				;
			} else {
				//printf("main re - TCP_ESTABLISH\n");
				CHK(close(serverfd));
				CHK2(serverfd, socket(PF_INET, SOCK_STREAM, 0));
				if (fcntl(serverfd, F_SETOWN, getpid()) != 0) {		// claim SIGIO/SIGURG signals
					perror("Can't claim SIGURG and SIGIO");
					//continue;
				}
				if (connect(serverfd, (struct sockaddr *)&seraddr, sizeof(seraddr)) == 0)
					printf("(%d)-<%s> re-connect FCD [OK]\n", __LINE__, __FUNCTION__);
				else {
					printf("(%d)-<%s> re-connect FCD [KO]\n", __LINE__, __FUNCTION__);
					continue;
				}
			}
//*/
			if (events[i].data.fd == pipe_fd[0])	//管道读端，从browser接受信息
			{
				bzero(&browsermsg, BROWSERMSG_SIZE);
				CHK2(res, read(pipe_fd[0], browsermsg, BROWSERMSG_SIZE));
				//printf("(%d)-<%s> browsermsg: %s\n", __LINE__, __FUNCTION__, browsermsg);

				if (!strncmp(browsermsg, "finish", 6))	//send finish to FCD
				{
					//读写之前，先判断socket connection是否正常
					getsockopt(serverfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
					if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
						CHK(send(serverfd, "finish", strlen("finish"), MSG_NOSIGNAL));
						printf("(%d)-<%s> send to FCD: finish\n", __LINE__, __FUNCTION__);
					} else {
						printf("(%d)-<%s> send to local: finish\n", __LINE__, __FUNCTION__);
						continue;
					}

					startfcd = 0;		// very important

					//send information for current process to FCD;
					//sprintf(processid, "%s", "current process information"); //read from database actually
					//CHK(send(serverfd, processid, strlen(processid), 0)); //need to modify
					//close(pipe_fd[0]);
					//close(pipe_fd[1]);
/*
					sleep(2);
					pthread_cancel(fcd);

					CHK2(serverfd, socket(PF_INET, SOCK_STREAM, 0));
					CHK(connect(serverfd, (struct sockaddr *)&seraddr, sizeof(seraddr)) < 0);
					printf("finish, then re - connect\n");

					//------- create thread: handle_fcd
					//rtfcd = pthread_create(&fcd, NULL, handle_fcd, (void *)&serverfd);
					rtfcd = pthread_create(&fcd, NULL, handle_fcd, NULL);
					if (-1 == rtfcd) {
						printf("client-main: thread creation error\n");
						return -1;
					}
					//------- create thread: handle_fcd

					//CHK(close(serverfd));
					//return 0;
//*/
				} else if (!strncmp(browsermsg, "guestname", 9)) {	//send username to FCD
					prt_c = &browsermsg[10];

					//读写之前，先判断socket connection是否正常
					getsockopt(serverfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
					if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
						//CHK(send(serverfd, prt_c, strlen(prt_c), 0));     //send username to FCD
						CHK(send(serverfd, prt_c, strlen(prt_c), MSG_NOSIGNAL));	//send username to FCD
						printf("(%d)-<%s> send to FCD: username: %s\n", __LINE__, __FUNCTION__, prt_c);
					} else {
						printf("(%d)-<%s> send to local: username: %s\n", __LINE__, __FUNCTION__, prt_c);
						continue;
					}
					bzero(username, NAME_SIZE);
					strncpy(username, prt_c, NAME_SIZE);

				} else if (!strncmp(browsermsg, "processid", 9)) {	//send processid to FCD
					prt_c = &browsermsg[10];

					//读写之前，先判断socket connection是否正常
					getsockopt(serverfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *) &leng);
					if ((info.tcpi_state == TCP_ESTABLISHED)) {		// socket connected 
						//CHK(send(serverfd, prt_c, strlen(prt_c), 0));     //send processid to FCD
						CHK(send(serverfd, prt_c, strlen(prt_c), MSG_NOSIGNAL));	//send processid to FCD
						printf("(%d)-<%s> send to FCD: processid: %s\n", __LINE__, __FUNCTION__, prt_c);
					} else {
						printf("(%d)-<%s> send to local: processid: %s\n", __LINE__, __FUNCTION__, prt_c);
						continue;
					}
					bzero(processid, PROCESSID_SIZE);
					strncpy(processid, prt_c, PROCESSID_SIZE);
					startfcd = 1;	//SMD 和 FCD 通信的启动开关
				} // end if
			} // end if
		} // end for
	} //end while
} // end main
