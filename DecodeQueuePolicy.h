#ifndef DECODEQUEUEPOLICY_H
#define DECODEQUEUEPOLICY_H

#include <QQueue>

class Runner;

namespace DecodeQueuePolicy {

// Inserts a visible thumbnail/probe stage into the high-priority band while
// preserving viewer precedence and stage order. Exposed from this internal
// header so the starvation policy can be regression-tested without starting
// worker threads.
void insertHighImageStageAheadOfMetadata(QQueue<Runner *> &queue,
                                         Runner *runner);

} // namespace DecodeQueuePolicy

#endif // DECODEQUEUEPOLICY_H
