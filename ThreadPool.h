#include "TaskQueue.h"
#include <iostream>
#include <string.h>
#include <string>
#include <unistd.h>

template <typename T>
class ThreadPool
{
public:

	ThreadPool(const ThreadPool&) = delete;

	ThreadPool& operator= (const ThreadPool&) = delete;

	// 创建线程池
	ThreadPool(int min, int max);

	// 销毁线程池
	~ThreadPool();

	// 给线程池添加任务
	void addTask(Task<T> task);

	// 获取线程池中工作的线程的个数
	int getBusyNum();

	// 获取线程池中活着的线程的个数
	int getAliveNum();

private:
	// 工作的线程(消费者线程)任务函数
	static void* worker(void* arg);

	// 管理者线程任务函数
	static void* manager(void* arg);

	// 单个线程退出
	void threadExit();

private:
	TaskQueue<T>* m_taskQ;						// 任务队列
	pthread_t *m_threadIDs;						// 工作的线程ID
	pthread_t m_managerID;						// 管理者线程ID
	int m_minNum;								// 最小线程数量
	int m_maxNum;								// 最大线程数量
	int m_busyNum;								// 忙的线程的个数
	int m_liveNum;								// 存活的线程的个数
	int m_exitNum;								// 要销毁的线程个数
	Locker m_locker;							// 锁
	pthread_cond_t m_notEmpty;					// 任务队列是不是空了
	bool m_shutdown;							// 是不是要销毁线程池, 销毁为 true, 不销毁为 false
	static const int ADD_THREAD_PER_NUM = 2;	// 每次添加线程的数量
};

template<typename T>
ThreadPool<T>::ThreadPool(int min, int max)
{
	do {
		m_minNum = min;
		m_maxNum = max;
		m_liveNum = 0;
		m_busyNum = 0;
		m_exitNum = 0;
		m_shutdown = false;

		// 创建任务队列
		m_taskQ = new TaskQueue<T>();
		if (m_taskQ == nullptr)
		{
			cout << "创建任务队列失败..." << endl;
			break;
		}

		// 创建线程池
		m_threadIDs = new pthread_t[m_maxNum];
		if (m_threadIDs == nullptr)
		{
			cout << "创建线程池失败..." << endl;
			break;
		}

		// 把线程池数组置 0
		memset(m_threadIDs, 0, sizeof(m_threadIDs)*max);

		// 初始化条件变量
		if (pthread_cond_init(&m_notEmpty, NULL) != 0)
		{
			cout << "初始化条件变量失败..." << endl;
			break;
		}

		// 创建管理者线程
		pthread_create(&m_managerID, NULL, manager, this);

		// 往线程池添加线程
		for (int i = 0;i < m_minNum;++i)
		{
			pthread_create(&m_threadIDs[i], NULL, worker, this);
			//pthread_detach()
			cout << "创建子线程, ID: " << to_string(m_threadIDs[i]) << endl;
		}

		return;
	} while (0);

	// 创建过程中失败的话，需要释放已经创建成功的资源
	if (m_taskQ)
		delete m_taskQ;
	if (m_threadIDs)
		delete[] m_threadIDs;
}

template<typename T>
ThreadPool<T>::~ThreadPool()
{
	m_shutdown = true;//关闭线程池

	// 阻塞回收管理者线程
	pthread_join(m_managerID, NULL);

	// 唤醒阻塞的工作线程
	for (int i = 0;i < m_liveNum;++i)
		pthread_cond_signal(&m_notEmpty);

	// 销毁条件变量
	pthread_cond_destroy(&m_notEmpty);

	// 释放资源
	if (m_taskQ)
		delete m_taskQ;
	if (m_threadIDs)
		delete[] m_threadIDs;
}

template<typename T>
void ThreadPool<T>::addTask(Task<T> task)
{
	if (m_shutdown)
		return;

	// 这里不用加锁，是因为 m_taskQ->addTask() 函数里面已经加锁了
	m_taskQ->addTask(task);
	// 发送信号通知消费者
	pthread_cond_signal(&m_notEmpty);
}

template<typename T>
int ThreadPool<T>::getBusyNum()
{
	int busyCnt = 0;
	LockGuard lock(&m_locker);
	busyCnt = m_busyNum;
	return busyCnt;
}

template<typename T>
int ThreadPool<T>::getAliveNum()
{
	int aliveCnt = 0;
	LockGuard lock(&m_locker);
	aliveCnt = m_liveNum;
	return aliveCnt;
}

template<typename T>
void * ThreadPool<T>::worker(void * arg)
{
	// 静态函数中不能使用非静态变量，所以把线程池实例传进来
	ThreadPool* pool = static_cast<ThreadPool*>(arg);

	while (true)
	{
		pool->m_locker.Lock();//加锁
		// 当前任务队列为空
		while (pool->m_taskQ->taskSize() == 0 && !pool->m_shutdown)
		{
			cout << "thread " << to_string(pthread_self()) << " waiting..." << endl;
			// 阻塞线程
			pthread_cond_wait(&pool->m_notEmpty, pool->m_locker.GetLock());

			// 判断是不是需要销毁线程
			if (pool->m_exitNum > 0)
			{
				--pool->m_exitNum;
				if (pool->m_liveNum > pool->m_minNum)
				{
					--pool->m_liveNum;
					pool->m_locker.UnLock();//解锁
					pool->threadExit();
				}
			}
		}

		// 判断线程池是否被关闭了
		if (pool->m_shutdown)
		{
			pool->m_locker.UnLock();//解锁
			pool->threadExit();
		}

		// 从任务队列取出一个任务
		Task<T> task = pool->m_taskQ->getTask();
		++pool->m_busyNum;

		pool->m_locker.UnLock();//解锁

		++pool->m_busyNum;	// 忙线程数 +1

		// 子线程执行工作函数
		cout << "thread " << pthread_self() << " start working..." << endl;
		task.func(task.arg);

		delete task.arg;
		task.arg = nullptr;

		cout << "thread " << to_string(pthread_self()) << " finish work..." << endl;


		pool->m_locker.Lock();		//加锁
		--pool->m_busyNum;			//忙线程数 -1
		pool->m_locker.UnLock();	//解锁
	}
	return nullptr;
}

template<typename T>
void * ThreadPool<T>::manager(void * arg)
{
	ThreadPool* pool = static_cast<ThreadPool*>(arg);

	while (!pool->m_shutdown)
	{
		sleep(3);//管理者线程每隔 3 秒执行一次

		pool->m_locker.Lock();//加锁

		int busyCnt = pool->m_busyNum;
		int aliveCnt = pool->m_liveNum;
		int taskCnt = (int)pool->m_taskQ->taskSize();
		
		pool->m_locker.UnLock();//解锁

		// 添加线程
		// 任务的个数>存活的线程个数 && 存活的线程数<最大线程数
		if (taskCnt > aliveCnt && aliveCnt < pool->m_maxNum)
		{
			LockGuard lock(&pool->m_locker);
			int cnt = 0;
			// 逐步增加，不是一次性增加
			for (int i = 0;i < pool->m_maxNum && cnt < ADD_THREAD_PER_NUM && pool->m_liveNum < pool->m_maxNum;++i)
			{
				if (pool->m_threadIDs[i] == 0)
				{
					pthread_create(&pool->m_threadIDs[i], NULL, worker, pool);
					++cnt;
					++pool->m_liveNum;
				}
			}
		}

		// 销毁线程
		// 忙的线程*2 < 存活的线程数 && 存活的线程>最小线程数
		if ((busyCnt << 1) < aliveCnt && aliveCnt > pool->m_minNum)
		{
			pool->m_locker.Lock();//加锁
			pool->m_exitNum = ADD_THREAD_PER_NUM;
			pool->m_locker.UnLock();//解锁

			// 发送信号唤醒阻塞在L180行的线程
			for (int i = 0;i < ADD_THREAD_PER_NUM;++i)
				pthread_cond_signal(&pool->m_notEmpty);
		}
	}
	return nullptr;
}

template<typename T>
void ThreadPool<T>::threadExit()
{
	pthread_t pid = pthread_self();//获得本线程的 id
	for (int i = 0;i < m_maxNum;++i)
	{
		// 找到该线程
		if (m_threadIDs[i] == pid)
		{
			cout << "threadExit() called, " << to_string(pid) << " exiting..." << endl;
			m_threadIDs[i] = 0;//把线程 id 置 0
			break;
		}
	}
	pthread_exit(NULL);
}

