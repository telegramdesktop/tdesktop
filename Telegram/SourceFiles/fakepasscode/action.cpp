#include "action.h"
#include "clear_proxies.h"
#include "clear_cache.h"
#include "logout.h"

std::shared_ptr<FakePasscode::Action> FakePasscode::DeSerialize(QByteArray serialized) {
    QDataStream stream(&serialized, QIODevice::ReadWrite);
    qint32 passcodeTypeIndex;
    stream >> passcodeTypeIndex;
    QByteArray inner_data{};
    if (!stream.atEnd()) {
        stream >> inner_data;
    }
    auto passcodeType = static_cast<FakePasscode::ActionType>(passcodeTypeIndex);
    return CreateAction(passcodeType, inner_data);
}

std::shared_ptr<FakePasscode::Action> FakePasscode::CreateAction(FakePasscode::ActionType type,
                                                                 const QByteArray& inner_data) {
    if (type == ActionType::ClearProxy) {
        return std::make_shared<FakePasscode::ClearProxies>();
    } else if (type == ActionType::ClearCache) {
        return std::make_shared<FakePasscode::ClearCache>();
    } else if (type == ActionType::Logout) {
        return std::make_shared<FakePasscode::LogoutAction>(inner_data);
    }
    return nullptr;
}
