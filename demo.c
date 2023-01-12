#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct buffer
{
	int is_empty; //0: 缓冲区空 1:缓冲区非空 -1：文件读写完毕
	char buf[1024];
}buffer_t;

buffer_t *buf_a = NULL, *buf_b = NULL;

pthread_mutex_t lock_a; //缓冲区A锁
pthread_mutex_t lock_b; //缓冲区B锁

pthread_cond_t cd_read; //读文件写入缓冲区A的等待条件
pthread_cond_t cd_copy;// 缓冲区A放入缓冲区A的等待条件
pthread_cond_t cd_write; //缓冲区B写入文件的等待条件


//线程a：读文件写入缓冲区A
//线程b：读缓冲区A，过滤， 写缓冲区B
//线程c：读缓冲区B，写文件

int read_line(int fd, char* buffer, int max_len)
{
	char tmp = 0;
	int read_count;
	int maxlen = max_len - 1; // '\0'占一个字符
	int i = 0;

	while(max_len && tmp != '\n')
	{
		read_count = read(fd, &tmp, 1);
		if(read_count < 0)
			printf("read value from file error\n");
		else if(read_count == 0) //文件末尾
			break;
		else
		{
			maxlen--;
			buffer[i] = tmp;
			i++;
		}
	}

	if(read_count == 0)
		return 0; //读到文件末尾
	else 
		return 1;
}

void* thread_a_job(void* arg)
{
	int fd_in = (int)((unsigned long)arg);
	int exit_flag = 0;

	while(1)
	{
		pthread_mutex_lock(&lock_a); //访问缓冲区A

		//如果缓冲区A非空,线程a挂起
		while(0 == buf_a->is_empty)
			pthread_cond_wait(&cd_read, &lock_a); //挂起解锁，唤醒上锁

		//缓冲区A为空,读取文件内容并写入缓冲区A
		memset(buf_a->buf, 0, sizeof(buf_a->buf));
		if(!read_line(fd_in, buf_a->buf, sizeof(buf_a->buf))) //读到文件末尾
		{
			buf_a->is_empty = -1;
			exit_flag = 1;
		}
		else
		{
			buf_a->is_empty = 0;
		}
		
		pthread_mutex_unlock(&lock_a); //释放缓冲区A

		pthread_cond_signal(&cd_copy); //唤醒b线程将数据从缓冲区A拷贝到缓存冲区B

		if(exit_flag)
			break;
	}

	return 0;
}

void* thread_b_job(void* arg)
{
	int exit_flag = 0;

	while(1)
	{
		pthread_mutex_lock(&lock_a); //访问缓冲区A

		//缓冲区A为空，线程b挂起
		while(1 == buf_a->is_empty)
			pthread_cond_wait(&cd_copy, &lock_a);

		//缓冲区A非空，访问缓冲区B
		pthread_mutex_lock(&lock_b);
			
		//缓冲区B非空，线程b挂起
		while(0 == buf_b->is_empty)
			pthread_cond_wait(&cd_copy, &lock_b);

		//缓冲区B为空，并且如果缓冲区A的数据包含“E CamX”或者“E CHIUSECASE”，则将缓冲区A的数据拷贝到缓冲区B
		if(strstr(buf_a->buf, "E CamX") != NULL || strstr(buf_a->buf, "E CHIUSECASE") != NULL)
		{
			memset(buf_b->buf, 0, sizeof(buf_b->buf));
			memcpy(buf_b->buf, buf_a->buf, sizeof(buf_b->buf));
		}

		if(-1 == buf_a->is_empty)
		{
			//线程a已经读到文件末尾，这是最后一次将数据从缓冲区A拷贝到缓冲区B
			buf_b->is_empty = -1;
			exit_flag = 1;
		}
		else
		{
			buf_a->is_empty = 1;
			buf_b->is_empty = 0;
		}

		pthread_mutex_unlock(&lock_b); //释放缓冲区B
		
		pthread_cond_signal(&cd_write); //唤醒线程c将缓冲区B的数据写入文件

		pthread_mutex_unlock(&lock_a); //释放缓冲区A

		pthread_cond_signal(&cd_read); //唤醒线程a读取文件数据到缓冲区A

		if(exit_flag)
			break;
	}

	return 0;
}

void* thread_c_job(void* arg)
{
	int fd_out = (int)((unsigned long)arg);
	int exit_flag = 0;

	while(1)
	{
		pthread_mutex_lock(&lock_b); //访问缓冲区B

		//缓冲区B为空，线程c挂起
		while(1 == buf_b->is_empty)
			pthread_cond_wait(&cd_write, &lock_b);

		//缓冲区B非空，将缓冲区B的数据写入文件
		write(fd_out, buf_b->buf, strlen(buf_b->buf));

		if(-1 == buf_b->is_empty) //线程c是最后一次将数据写入文件
			exit_flag = 1;
		else
			buf_b->is_empty = 1;

		pthread_mutex_unlock(&lock_b); //释放缓冲区B

		pthread_cond_signal(&cd_copy); //唤醒线程b将缓冲区A的数据拷贝到缓冲区B

		if(exit_flag)
			break;
	}

	return 0;
}

int main()
{
	pthread_t tid_a, tid_b, tid_c;
	int fd_in, fd_out;
	void* reval;

	fd_in = open("./ERROR.log", O_RDONLY);
	if(fd_in < 0)
	{
		printf("open ERROR.log error\n");
		return 0;
	}

	fd_out = open("./output.log", O_WRONLY);
        if(fd_out < 0)
        {
                printf("open output.log error\n");
                return 0;
        }

	buf_a = (buffer_t*)malloc(sizeof(buffer_t));
	buf_b = (buffer_t*)malloc(sizeof(buffer_t));

	pthread_mutex_init(&lock_a, NULL);
	pthread_mutex_init(&lock_b, NULL);

	pthread_create(&tid_a, NULL, thread_a_job, (void*)((unsigned long)fd_in));
	pthread_create(&tid_b, NULL, thread_b_job, NULL);
	pthread_create(&tid_c, NULL, thread_c_job, (void*)((unsigned long)fd_out));

	pthread_join(tid_a, &reval);
	pthread_join(tid_b, &reval);
	pthread_join(tid_c, &reval);

	free(buf_a);
	free(buf_b);	
	buf_a = buf_b = NULL;

	pthread_mutex_destroy(&lock_a);
	pthread_mutex_destroy(&lock_b);

	close(fd_in);
	close(fd_out);

	printf("END\n");
	return 0;
}
