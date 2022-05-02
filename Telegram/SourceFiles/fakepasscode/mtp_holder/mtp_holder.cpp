#include "mtp_holder.h"
#include "instance_holder.h"

#include <mtproto/mtp_instance.h>

namespace FakePasscode {

void FakeMtpHolder::HoldMtpInstance(std::unique_ptr<MTP::Instance>&& instance) {
    if (instance) {
        instances.insert(new InstanceHolder(this, std::move(instance)));
    }
}

FakeMtpHolder::~FakeMtpHolder() {
    for (auto holder : instances) {
        delete holder;
    }
}

void FakeMtpHolder::destroy(InstanceHolder *holder) {
    instances.erase(holder);
    delete holder;
}

}
