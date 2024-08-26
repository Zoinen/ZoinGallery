#ifndef LENSDATABASE_H
#define LENSDATABASE_H

#include <QString>

class LensDatabase {
public:
    static QString lensNameForId(uint64_t lensId);
};

#endif // LENSDATABASE_H
