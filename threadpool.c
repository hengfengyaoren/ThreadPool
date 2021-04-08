
//线程池实现
/*
任务队列：任务的集合，先描述任务，在描述集合  (日志任务，crud任务等)
执行队列：
管理组件:
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

//===========111111111
//不同的类型： c++可以用模板实现
//宏定义：NJOB NWORKER 不同的类型进行同类型操作可采用宏
//宏与内联区别：一个是预编译，一个是编译的时候

//往队列添加结点，头插法
#define LL_ADD(item, list) do { 	\
	item->prev = NULL;				\
	item->next = list;				\
	list = item;					\
} while(0)

//往队列中移除结点 
#define LL_REMOVE(item, list) do {						\
	if (item->prev != NULL) item->prev->next = item->next;	\
	if (item->next != NULL) item->next->prev = item->prev;	\
	if (list == item) list = item->next;					\
	item->prev = item->next = NULL;							\
} while(0)

//===========222
//任务描述: 任务的回调函数自己实现，带上自己的参数
//任务队列
struct NJOB
{
	//任务基本操作 不同的任务实现不同的回调函数即可
	void (*func)(struct NJOB* arg);	//回调函数
	void* user_data;		//自己的参数

	//将任务变成集合（加上指针) 也可以用单向链表,双向链表可操作空间多一点
	struct NJOB* prev;
	struct NJOB* next;
};


/*
最开始任务不足时候，初始执行队列可能用不到这么多，线程怎么退出?
1 pthread_exit(threadid) 优雅退出
pthread_cancel(threadid)  粗暴退出  不知道线程的状态

2 phtread_detach(); 父子线程分离

  pthread_join(threadid)
*/
//===========3333
//执行队列
struct NWORKER
{

	pthread_t threadid;	//每个线程都有一个id
	int terminate;		//线程退出  比较友好退出??为何？
	struct NMANAGER* pool;

	struct NWORKER* prev;
	struct NWORKER* next;

};


//===========4444
//管理组件   其实就是线程池
typedef struct NMANAGER
{
	//任务队列
	struct NJOB* jobs;
	//执行队列
	struct NWORKER* workers;
	//互斥锁
	pthread_mutex_t mtx;
	//条件变量
	pthread_cond_t cond;
} nThreadPool;




//===========5555  
//一个线程按照8M空间计算 设置线程数量
//线程的入口函数(线程的回调函数)与任务的回调函数关系:没有关系
//线程的回调函数：是不断（永真）去任务队列取任务(取完任务后，执行的)
void* thread_callback(void* arg)  //线程的入口函数
{

	struct NWORKER* worker = (struct NWORKER*)arg;

	while(1)
	{
		//判断任务队列是否有任务
		pthread_mutex_lock(&worker->pool->mtx);
		while(worker->pool->jobs == NULL)
		{
			//pthread_mutex_lock(&worker->pool->mtx);为何不放在这里是因为对整个队列枷锁???
			if (worker->terminate)
			{
				break;    //终止线程
			}
			pthread_cond_wait(&worker->pool->cond, &worker->pool->mtx);
		}

		if (worker->terminate)
		{
			pthread_mutex_unlock(&worker->pool->mtx);
			break;    //终止线程
		}

		//jobs--->
		struct NJOB* job = worker->pool->jobs;
		if (job != NULL)
		{
			//参数1：头结点 参数2：列表
			LL_REMOVE(job, worker->pool->jobs);
		}

		pthread_mutex_unlock(&worker->pool->mtx);

		if (job == NULL)
		{
			continue;
		}

		job->func(job); //真正执行的任务

	}

	free(worker);

	pthread_exit(NULL);

}

//api
//1 创建线程池
//参数：线程池对象，线程数量
int nThreadPoolCreate(nThreadPool* pool, int numWorkers)
{
	//a 第一步检测参数
	if (numWorkers < 0)
	{
		numWorkers = 1;
	}
	memset(pool, 0, sizeof(nThreadPool));

	// 第二步初始化一堆参数 NMANAGER结构体里面变量
	//b mutex
	pthread_mutex_t blank_mutex = PTHREAD_MUTEX_INITIALIZER;
	memcpy(&pool->mtx, &blank_mutex, sizeof(pthread_mutex_t));//结构体赋值直接采用memcpy比较好
	//c cond
	pthread_cond_t blank_cond = PTHREAD_COND_INITIALIZER;
	memcpy(&pool->cond, &blank_cond, sizeof(pthread_cond_t));//结构体赋值直接采用memcpy比较好
	//d 初始化执行队列 线程
	int i = 0;
	for (; i < numWorkers; i++)
	{
		struct NWORKER* worker = (struct NWORKER*)malloc(sizeof(struct NWORKER));
		if (worker == NULL)
		{
			perror("malloc");
			return -1;
		}

		memset(worker, 0, sizeof(struct NWORKER));
		worker->pool = pool;

		//一个worker对应一个线程  参数：线程id，  怎么绑定起来的？
		int ret = pthread_create(&worker->threadid, NULL, thread_callback, (void*)worker);
		if (ret)
		{
			perror("pthread_create");
			free(worker);
			return -2;
		}

		LL_ADD(worker, worker->pool->workers);
	}
}

//2 销毁线程池
int nThreadPoolDestroy(nThreadPool* pool)
{
	struct NWORKER* worker = NULL;

	for (worker = pool->workers; worker != NULL; worker = worker->next)
	{
		worker->terminate = 1;
	}

	pthread_mutex_lock(&pool->mtx);

	pool->workers = NULL;
	pool->jobs = NULL;

	pthread_cond_broadcast(&pool->cond);

	pthread_mutex_unlock(&pool->mtx);
}

//3 往线程池抛任务(将Task加入任务队列)
int nThreadPoolPushJob(nThreadPool* pool, struct NJOB* job)
{
	//a 往任务队列添加任务
	pthread_mutex_lock(&pool->mtx);

	LL_ADD(job, pool->jobs);
	//b 条件变量唤醒
	pthread_cond_signal(&pool->cond); //线程条件等待是否会有静群现象？为何？  此函数：在当前进程中的所有线程中找到等待线程，然后激活一个等待该条件的线程(存在多个等待线程时按入队顺序激活其中一个)
	//pthread_cond_signal惊群的问题， 是一个内核老版本的问题， 新版本的条件等待是队列， signal是从队列里面取出一个节点。 是激活的一个线程。
	pthread_mutex_unlock(&pool->mtx);

}



///////////////////////////////////////
//sdk 提供开发者使用

#if 1 //debug

#define KING_MAX_THREAD			80
#define KING_COUNTER_SIZE		1000

void king_counter(struct NJOB* job)
{

	int index = *(int*)job->user_data;

	printf("index : %d, selfid : %lu\n", index, pthread_self());

	free(job->user_data);
	free(job);
}



int main(int argc, char* argv[])
{
	//1定义线程池
	nThreadPool pool;
	//2创建线程池（线程数量80个)
	nThreadPoolCreate(&pool, KING_MAX_THREAD);

	//3 往线程池中抛1000个任务
	int i = 0;
	for (i = 0; i < KING_COUNTER_SIZE; i++)
	{
		struct NJOB* job = (struct NJOB*)malloc(sizeof(struct NJOB));
		if (job == NULL)
		{
			perror("malloc");
			exit(1);
		}

		job->func = king_counter;
		job->user_data = malloc(sizeof(int));
		*(int*)job->user_data = i;

		nThreadPoolPushJob(&pool, job);

	}

	getchar();
	printf("\n");


}


#endif


