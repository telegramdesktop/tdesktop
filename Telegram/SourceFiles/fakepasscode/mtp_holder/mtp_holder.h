#ifndef TELEGRAM_MTP_HOLDER_H
#define TELEGRAM_MTP_HOLDER_H

#include <memory>
#include <set>
#include <map>
#include <vector>

#include <base/weak_ptr.h>
#include <mtproto/core_types.h>

namespace Core {
class Application;
}

namespace MTP{
class Instance;
}

namespace FakePasscode {

class InstanceHolder;

class FakeMtpHolder {
    friend class InstanceHolder;
public:
    ~FakeMtpHolder();
    void RegisterCriticalRequest(MTP::Instance* instance, mtpRequestId request);
    void HoldMtpInstance(std::unique_ptr<MTP::Instance>&& instance);

private:
    std::unordered_set<InstanceHolder*> instances;
    std::unordered_map<MTP::Instance*, std::vector<mtpRequestId>> requests;
    base::has_weak_ptr guard;

    std::vector<mtpRequestId> getCriticalRequests(MTP::Instance* instance) const;
    void destroy(InstanceHolder* holder);
};

}

#endif //TELEGRAM_MTP_HOLDER_H
