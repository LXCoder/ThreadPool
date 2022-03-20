main: main.cpp ThreadPool.h TaskQueue.h Locker.h 
	g++ -std=c++11 -lpthread -o main main.cpp ThreadPool.h TaskQueue.h Locker.h
