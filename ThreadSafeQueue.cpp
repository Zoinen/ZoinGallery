#include "ThreadSafeQueue.h"

void ThreadSafeQueue::clear() {
    std::lock_guard<std::mutex> lock(_mutex);
    _queue.clear();
    _processedQueue.clear();
}

void ThreadSafeQueue::prepend(const QList<ImageReadRequest> &requests) {
    std::lock_guard<std::mutex> lock(_mutex);
    prependList(_queue, requests);
    _condVar.notify_one();
}

void ThreadSafeQueue::prependNewAndPrioritizeDuplicates(const QList<ImageReadRequest> &requests) {
    std::lock_guard<std::mutex> lock(_mutex);

    QList<ImageReadRequest> elementsUnique;
    for (int i = 0; i < requests.size(); i++) {
        if (!_processedQueue.contains(requests[i])) {
            elementsUnique.append(requests[i]);
            if (_queue.contains(requests[i])) {
                _queue.removeOne(requests[i]);
            }
        }
    }
    prependList(_queue, elementsUnique);

    _condVar.notify_one();
}

ImageReadRequest ThreadSafeQueue::dequeue() {
    std::unique_lock<std::mutex> lock(_mutex);
    while (_queue.isEmpty()) {
        _condVar.wait(lock);
    }
    ImageReadRequest var = _queue.takeFirst();
    _processedQueue.insert(var);
    return var;
}

int ThreadSafeQueue::size() {
    std::unique_lock<std::mutex> lock(_mutex);
    return _queue.size();
}
