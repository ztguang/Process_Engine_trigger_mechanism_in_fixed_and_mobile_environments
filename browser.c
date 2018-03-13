
/*
# This code is released under GNU GPL v2,v3
# Author: Tongguang Zhang
# Date: 2015-12-22
*/

// gcc browser.c -o browser
// indent -npro -kr -i8 -ts8 -sob -l280 -ss -ncs -cp1 *

//browser send " guestname=Jone, processid=2015-1119-2302-57441-1181348586, finish " to daemon WAIT

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BROWSER_PORT 22222	//监听端口
#define BROWSER_HOST "127.0.0.1"	//BROWSER本地IP地址

//两个有用的宏定义：检查和赋值并且检测
#define CHK(eval) if(eval < 0){perror("eval"); exit(-1);}
#define CHK2(res, eval) if((res = eval) < 0){perror("eval"); exit(-1);}

int main(int argc, char *argv[])
{
	int sock, pid;

	struct sockaddr_in seraddr;
	seraddr.sin_family = PF_INET;
	seraddr.sin_port = htons(BROWSER_PORT);
	seraddr.sin_addr.s_addr = inet_addr(BROWSER_HOST);

	CHK2(sock, socket(PF_INET, SOCK_STREAM, 0));
	printf("socket\n");

	CHK(connect(sock, (struct sockaddr *)&seraddr, sizeof(seraddr)) < 0);
	printf("connect\n");

	//browser send " guestname=Jone, processid=2015-1119-2302-57441-1181348586, finish " to daemon WAIT

	CHK(send(sock, "guestname=Jone", strlen("guestname=Jone"), 0));
	printf("guestname=Jone\n");

	sleep(2);
	//usleep(100000);
	CHK(send(sock, "processid=2015-1119-2302-57441-1181348586", strlen("processid=2015-1119-2302-57441-1181348586"), 0));
	printf("processid=2015-1119-2302-57441-1181348586\n");

	sleep(150);
	CHK(send(sock, "finish", strlen("finish"), 0));
	printf("finish\n");

	sleep(2);
	close(sock);
	return 0;
}
