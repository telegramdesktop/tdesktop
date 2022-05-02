#include "instance_holder.h"

#include <mtproto/mtp_instance.h>

namespace FakePasscode {

InstanceHolder::InstanceHolder(FakePasscode::FakeMtpHolder *parent, std::unique_ptr<MTP::Instance> &&instance)
    : _parent(parent)
    , _instance(std::move(instance))
{
    auto inst = _instance.get();
    Expects(parent != nullptr);
    Expects(inst != nullptr);

    inst->clearGlobalHandlers();
    inst->clearCallbacks();
    inst->lifetime().destroy();
//    inst->setGlobalFailHandler([](const MTP::Error&, const MTP::Response&){});
//    inst->setSessionResetHandler([](MTP::ShiftedDcId shiftedDcId){});
//    inst->setStateChangedHandler([](MTP::ShiftedDcId shiftedDcId, int32 state){});
//    inst->setUpdatesHandler([](const MTP::Response&){});
}

}
