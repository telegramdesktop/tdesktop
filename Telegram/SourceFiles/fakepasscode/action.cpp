#include "action.h"
#include "fakepasscode/actions/clear_proxies.h"
#include "fakepasscode/actions/clear_cache.h"
#include "fakepasscode/actions/logout.h"
#include "fakepasscode/log/fake_log.h"

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
    FAKE_LOG(qsl("Create action of type %1 with %2 size of inner_data").arg(static_cast<int>(type)).arg(inner_data.size()));
    if (type == ActionType::ClearProxy) {
        return std::make_shared<FakePasscode::ClearProxies>();
    } else if (type == ActionType::ClearCache) {
        return std::make_shared<FakePasscode::ClearCache>();
    } else if (type == ActionType::Logout) {
        return std::make_shared<FakePasscode::LogoutAction>(inner_data);
    }
    FAKE_LOG(qsl("No realization found for type %1").arg(static_cast<int>(type)));
    return nullptr;
}
