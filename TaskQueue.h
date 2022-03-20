#include <queue>
#include "Locker.h"
using namespace std;

// 回调函数指针
using callback = void(*)(void* arg);

// 任务结构体
template <typename T>
struct Task{
	
	Task():func(nullptr),arg(nullptr){}

	Task(callback f,T* _arg):func(f),arg(_arg){}

	// 回调函数
	callback func;
	// 回调函数的参数
	T* arg;		
};

template <typename T>
class TaskQueue
{
public:
	TaskQueue() = default;

	~TaskQueue() = default;

	// 添加任务
	void addTask(Task<T> task);

	void addTask(callback f, T* _arg);
	// 取出任务
	Task<T> getTask();
	// 返回任务数量
	inline size_t taskSize()
	{
		return m_taskQueue.size();
	}

private:
	// 互斥锁
	Locker m_locker;
	// 任务队列
	queue<Task<T>> m_taskQueue;
};

template<typename T>
void TaskQueue<T>::addTask(Task<T> task)
{
	LockGuard lock(&m_locker);
	m_taskQueue.push(task);
}

template<typename T>
void TaskQueue<T>::addTask(callback f, T * _arg)
{
	LockGuard lock(&m_locker);
	m_taskQueue.push(Task<T>(f, _arg));
}

template<typename T>
Task<T> TaskQueue<T>::getTask()
{
	Task<T> t;
	LockGuard lock(&m_locker);

	if (!m_taskQueue.empty())
	{
		t = m_taskQueue.front();
		m_taskQueue.pop();
	}

	return t;
}
