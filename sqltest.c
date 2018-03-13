
/*
# This code is released under GNU GPL v2,v3
# Author: Tongguang Zhang
# Date: 2015-12-22
*/

// gcc -I/usr/include/mysql/ -L/usr/lib64/mysql/ -lmysqlclient sqltest.c -o sqltest

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <mysql.h>

int main()
{

	time_t start, end, now;
	int downtime;
	double cost, period;
//* 
    time(&start);  
    usleep(1590000);  
    time(&end);  
    cost=difftime(end,start);  
    printf("%f\n",cost);  
//*/
	time(&start);

	while (1) {
		break;
		time(&now);
		period = difftime(now, start);

		if (0 <= period < 8.0) {	//normal visits
			time(&start);
			printf("%f\n", period);
		}

		sleep(2);	//send heartbeat request every 5s to FPE
	}

	//mysql------------------
	MYSQL *conn_ptr;
	MYSQL_RES *res_ptr;
	MYSQL_ROW sqlrow;
	MYSQL_FIELD *fd;
	int res, i, j;
	char sql[1024];

	conn_ptr = mysql_init(NULL);
	if (!conn_ptr) {
		printf("mysql_init failed\n");
		return EXIT_FAILURE;
	}
	conn_ptr = mysql_real_connect(conn_ptr, "localhost", "root", "123456", "test", 0, NULL, 0);
	//root 为用户名 123456为密码 test为要连接的database  

	if (conn_ptr) {
		printf("Connection success\n");
	} else {
		printf("Connection failed\n");
		return EXIT_FAILURE;
	}
	//mysql_close(conn_ptr);
	//mysql------------------

	//----------------------------------
	memset(sql, 0x00, 1024);
	sprintf(sql, "select UNIX_TIMESTAMP(now())-UNIX_TIMESTAMP(selftime) from fcd_state where id=1");
	res = mysql_query(conn_ptr, sql);	//查询语句  
	if (res) {
		printf("SELECT error:%s\n", mysql_error(conn_ptr));
	} else {
		res_ptr = mysql_store_result(conn_ptr);	//取出结果集  
		if (res_ptr) {
			sqlrow = mysql_fetch_row(res_ptr);
			downtime = atoi(sqlrow[0]);
		}
		mysql_free_result(res_ptr);
	}

	printf("downtime:%d\n", downtime);
	//update fcd_state_table set selftime=currenttime where id=1;
	memset(sql, 0x00, 1024);
	sprintf(sql, "update fcd_state set selftime=now() where id=1");
	res = mysql_query(conn_ptr, sql);	//update语句  
	if (res) {
		printf("update error:%s\n", mysql_error(conn_ptr));
	}
	//----------------------------------

	return EXIT_SUCCESS;
}
