#ifndef TELEGRAM_INSTANCE_HOLDER_H
#define TELEGRAM_INSTANCE_HOLDER_H

#include <memory>

namespace MTP {
class Instance;
}

namespace FakePasscode {

class FakeMtpHolder;

class InstanceHolder {
public:
    InstanceHolder(FakeMtpHolder* parent, std::unique_ptr<MTP::Instance>&& instance);

private:
    FakeMtpHolder* _parent;
    std::unique_ptr<MTP::Instance> _instance;
};

}

#endif //TELEGRAM_INSTANCE_HOLDER_H
