#ifndef THREADSAFEQUEUE_H
#define THREADSAFEQUEUE_H

#include "ImageFile.h"

#include <QList>
#include <QSet>
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


class ThreadSafeQueue {
public:
    void clear();
    void prepend(const QList<ImageReadRequest> &requests);
    void prependNewAndPrioritizeDuplicates(const QList<ImageReadRequest> &requests);
    ImageReadRequest dequeue();
    int size();

private:
    QList<ImageReadRequest> _queue;
    QSet<ImageReadRequest> _processedQueue;
    mutable std::mutex _mutex;
    std::condition_variable _condVar;
};

#endif // THREADSAFEQUEUE_H
