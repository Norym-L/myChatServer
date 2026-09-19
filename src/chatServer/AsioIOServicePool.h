#ifndef GATESERVER_ASIOIOSERVICEPOOL_H
#define GATESERVER_ASIOIOSERVICEPOOL_H

#include <Singleton.h>

class AsioIOServicePool : public TSingLeton<AsioIOServicePool>
{
    friend class TSingLeton;
public:
    void stop();
};


#endif //GATESERVER_ASIOIOSERVICEPOOL_H
