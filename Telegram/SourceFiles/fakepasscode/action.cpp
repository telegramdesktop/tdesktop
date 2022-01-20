#include "action.h"
#include "fakepasscode/actions/clear_proxies.h"
#include "fakepasscode/actions/clear_cache.h"
#include "fakepasscode/actions/logout.h"

std::unique_ptr<FakePasscode::Action> FakePasscode::DeSerialize(QByteArray serialized) {
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

std::unique_ptr<FakePasscode::Action> FakePasscode::CreateAction(FakePasscode::ActionType type,
                                                                 const QByteArray& inner_data) {
    if (type == ActionType::ClearProxy) {
        return std::make_unique<FakePasscode::ClearProxies>();
    } else if (type == ActionType::ClearCache) {
        return std::make_unique<FakePasscode::ClearCache>();
    } else if (type == ActionType::Logout) {
        return std::make_unique<FakePasscode::LogoutAction>(inner_data);
    }
    return nullptr;
}
