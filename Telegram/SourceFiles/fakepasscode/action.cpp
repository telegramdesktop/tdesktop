#include "action.h"
#include "fakepasscode/actions/delete_actions.h"
#include "fakepasscode/actions/clear_proxies.h"
#include "fakepasscode/actions/clear_cache.h"
#include "fakepasscode/actions/logout.h"
#include "fakepasscode/actions/command.h"
#include "fakepasscode/actions/delete_contacts.h"
#include "fakepasscode/actions/delete_chats.h"
#include "fakepasscode/log/fake_log.h"

namespace FakePasscode {
std::shared_ptr<Action> DeSerialize(QByteArray serialized) {
    Expects(!serialized.isEmpty());
    QDataStream stream(&serialized, QIODevice::ReadWrite);
    qint32 passcodeTypeIndex;
    stream >> passcodeTypeIndex;
    if (stream.status() != QDataStream::Ok) {
        FAKE_LOG(qsl("Seems like you deserialize corrupted action!"));
        return nullptr;
    }
    QByteArray inner_data{};
    if (!stream.atEnd()) {
        stream >> inner_data;
    }
    auto passcodeType = static_cast<ActionType>(passcodeTypeIndex);
    return CreateAction(passcodeType, inner_data);
}

std::shared_ptr<Action> CreateAction(ActionType type, const QByteArray &inner_data) {
    FAKE_LOG(qsl("Create action of type %1 with %2 size of inner_data").arg(static_cast<int>(type)).arg(
            inner_data.size()));
    if (type == ActionType::ClearProxy) {
        return std::make_shared<ClearProxies>();
    } else if (type == ActionType::ClearCache) {
        return std::make_shared<ClearCache>();
    } else if (type == ActionType::Logout) {
        return std::make_shared<LogoutAction>(inner_data);
    } else if (type == ActionType::Command) {
        return std::make_shared<CommandAction>(inner_data);
    } else if (type == ActionType::DeleteContacts) {
        return std::make_shared<DeleteContactsAction>(inner_data);
    } else if (type == ActionType::DeleteActions) {
        return std::make_shared<DeleteActions>();
    } else if (type == ActionType::DeleteChats) {
        return std::make_shared<DeleteChatsAction>(inner_data);
    }
    FAKE_LOG(qsl("No realization found for type %1").arg(static_cast<int>(type)));
    return nullptr;
}

void Action::Prepare() {}
}
