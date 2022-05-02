#ifndef TELEGRAM_MTP_HOLDER_H
#define TELEGRAM_MTP_HOLDER_H

#include <memory>
#include <set>

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
    void HoldMtpInstance(std::unique_ptr<MTP::Instance>&& instance);

private:
    std::unordered_set<InstanceHolder*> instances;

    void destroy(InstanceHolder* holder);
};

}

#endif //TELEGRAM_MTP_HOLDER_H
