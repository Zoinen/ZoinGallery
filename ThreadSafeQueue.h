#ifndef THREADSAFEQUEUE_H
#define THREADSAFEQUEUE_H

#include <QList>
#include <queue>
#include <mutex>
#include <condition_variable>

template <class T>
void insertList(QList<T> &listDst, int pos, const QList<T> &listSrc) {
    listDst.insert(pos, listSrc.size(), T());
    for (int i = 0; i < listSrc.size(); i++) {
        listDst[pos + i] = listSrc[i];
    }
}

template <class T>
void prependList(QList<T> &listDst, const QList<T> &listSrc) {
    insertList(listDst, 0, listSrc);
}


template <class T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue()
        : _queue(), _mutex(), _condVar() {}

    void clear() {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.clear();
    }

    void prepend(QList<T> elements) {
        std::lock_guard<std::mutex> lock(_mutex);
        prependList(_queue, elements);
        _condVar.notify_one();
    }

    T dequeue() {
        std::unique_lock<std::mutex> lock(_mutex);
        while (_queue.isEmpty()) {
            _condVar.wait(lock);
        }
        return _queue.takeFirst();
    }

private:
    QList<T> _queue;
    mutable std::mutex _mutex;
    std::condition_variable _condVar;
};

#endif // THREADSAFEQUEUE_H
