#include <pthread.h>

class Locker {
public:
	Locker() {
		pthread_mutex_init(&m_mutex, NULL);//³õÊ¼»¯»¥³âËø
	}

	~Locker() {
		pthread_mutex_destroy(&m_mutex);//ÊÍ·Å»¯»¥³âËø
	}

	void Lock() {
		pthread_mutex_lock(&m_mutex);	//¼ÓËø
	}

	void UnLock() {
		pthread_mutex_unlock(&m_mutex);	//½âËø
	}

	pthread_mutex_t* GetLock() {
		return &m_mutex;
	}

private:
	pthread_mutex_t m_mutex;
};


class LockGuard {
public:
	LockGuard(Locker* locker) :m_locker(locker) {
		m_locker->Lock();
	}

	~LockGuard() {
		m_locker->UnLock();
	}
private:
	Locker* m_locker;
};