#include "action.h"
#include "clear_proxies.h"
#include "clear_cache.h"

std::shared_ptr<FakePasscode::Action> FakePasscode::DeSerialize(QByteArray serialized) {
    QDataStream stream(&serialized, QIODevice::ReadWrite);
    qint32 passcodeTypeIndex;
    stream >> passcodeTypeIndex;
    auto passcodeType = static_cast<FakePasscode::ActionType>(passcodeTypeIndex);
    return CreateAction(passcodeType);
}

std::shared_ptr<FakePasscode::Action> FakePasscode::CreateAction(FakePasscode::ActionType type) {
    if (type == ActionType::ClearProxy) {
        return std::make_shared<FakePasscode::ClearProxies>();
    } else if (type == ActionType::ClearCache) {
        return std::make_shared<FakePasscode::ClearCache>();
    }
    return nullptr;
}
