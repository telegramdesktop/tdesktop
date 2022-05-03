#ifndef TELEGRAM_INSTANCE_HOLDER_H
#define TELEGRAM_INSTANCE_HOLDER_H

#include <memory>
#include <base/timer.h>
#include <base/weak_ptr.h>

namespace MTP {
class Instance;
}

namespace FakePasscode {

class FakeMtpHolder;

class InstanceHolder : public base::has_weak_ptr {
public:
    InstanceHolder(FakeMtpHolder* parent, std::unique_ptr<MTP::Instance>&& instance);

private:
    FakeMtpHolder* _parent;
    std::unique_ptr<MTP::Instance> _instance;
    base::Timer _checkTimer;
    base::Timer _requestTimer;
    base::Timer _logoutTimer;

    bool completed() const;
    void check();
    void logout();
    void die();
};

}

#endif //TELEGRAM_INSTANCE_HOLDER_H
