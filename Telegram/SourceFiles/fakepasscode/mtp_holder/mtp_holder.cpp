#include "mtp_holder.h"
#include "instance_holder.h"

#include <mtproto/mtp_instance.h>
#include <crl/crl.h>

#include "fakepasscode/log/fake_log.h"

namespace FakePasscode {

void FakeMtpHolder::HoldMtpInstance(std::unique_ptr<MTP::Instance>&& instance) {
    FAKE_LOG(qsl("Try to hold instance"));
    if (instance) {
        FAKE_LOG(qsl("Insert instance"));
        instances.insert(new InstanceHolder(this, std::move(instance)));
    }
}

FakeMtpHolder::~FakeMtpHolder() {
    FAKE_LOG(qsl("Delete instances"));
    for (auto holder : instances) {
        delete holder;
    }
}

void FakeMtpHolder::RegisterCriticalRequest(MTP::Instance *instance, mtpRequestId request) {
    FAKE_LOG(qsl("Register crit request %1 for instance %2").arg(request).arg((uintptr_t)instance));
    auto& list = requests[instance];
    if (list.empty()) {
        FAKE_LOG(qsl("Connect to destroy"));
        QObject::connect(instance, &QObject::destroyed, crl::guard(&guard, [=]{
            requests.erase(instance);
        }));
    }
    FAKE_LOG(qsl("Push request to list"));
    list.push_back(request);
}

std::vector<mtpRequestId> FakeMtpHolder::getCriticalRequests(MTP::Instance *instance) const {
    FAKE_LOG(qsl("Try to get requests for %1").arg((uintptr_t)instance));
    if (auto it = requests.find(instance); it != requests.end()) {
        FAKE_LOG(qsl("Found crit requests, return!"));
        return it->second;
    } else {
        FAKE_LOG(qsl("No crit requests!"));
        return {};
    }
}

void FakeMtpHolder::destroy(InstanceHolder *holder) {
    FAKE_LOG(qsl("Destroy holder %1").arg((uintptr_t)holder));
    instances.erase(holder); 
    delete holder;
}

}
