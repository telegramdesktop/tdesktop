#include "action.h"
#include "clear_proxies.h"

std::shared_ptr<FakePasscode::Action> FakePasscode::DeSerialize(QByteArray serialized) {
    QDataStream stream(&serialized, QIODevice::ReadWrite);
    qint32 passcodeTypeIndex;
    stream >> passcodeTypeIndex;
    auto passcodeType = static_cast<FakePasscode::ActionType>(passcodeTypeIndex);
    if (passcodeType == FakePasscode::ActionType::ClearProxy) {
        return std::make_unique<FakePasscode::ClearProxies>();
    }

    return nullptr;
}

std::shared_ptr<FakePasscode::Action> FakePasscode::CreateAction(FakePasscode::ActionType type) {
    if (type == ActionType::ClearProxy) {
        return std::make_shared<FakePasscode::ClearProxies>();
    }
    return nullptr;
}
