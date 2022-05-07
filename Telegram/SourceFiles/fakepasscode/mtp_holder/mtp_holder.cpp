#include "mtp_holder.h"
#include "instance_holder.h"

#include <mtproto/mtp_instance.h>
#include <crl/crl.h>

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

void FakeMtpHolder::RegisterCriticalRequest(MTP::Instance *instance, mtpRequestId request) {
    auto& list = requests[instance];
    if (list.empty()) {
        QObject::connect(instance, &QObject::destroyed, crl::guard(&guard, [=]{
            requests.erase(instance);
        }));
    }
    list.push_back(request);
}

std::vector<mtpRequestId> FakeMtpHolder::getCriticalRequests(MTP::Instance *instance) const {
    if (auto it = requests.find(instance); it != requests.end()) {
        return it->second;
    } else {
        return {};
    }
}

void FakeMtpHolder::destroy(InstanceHolder *holder) {
    instances.erase(holder); 
    delete holder;
}

}
