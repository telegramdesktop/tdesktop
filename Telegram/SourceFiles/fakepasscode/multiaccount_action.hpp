#include "multiaccount_action.h"

#include "log/fake_log.h"

#include "core/application.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"

template<typename Data>
const Data FakePasscode::MultiAccountAction<Data>::kEmptyData = {};

template<typename Data>
FakePasscode::MultiAccountAction<Data>::MultiAccountAction(base::flat_map<qint32, Data> data)
    : index_actions_(std::move(data)) {}

template<typename Data>
FakePasscode::MultiAccountAction<Data>::MultiAccountAction(QByteArray inner_data) {
    if (!inner_data.isEmpty()) {
        QDataStream stream(&inner_data, QIODevice::ReadOnly);
        while (!stream.atEnd()) {
            qint32 index;
            Data action;
            stream >> index >> action;
            FAKE_LOG(qsl("Account %1 deserialized").arg(index));
            index_actions_[index] = action;
        }
    }
}

template<typename Data>
QByteArray FakePasscode::MultiAccountAction<Data>::Serialize() const {
    if (index_actions_.empty()) {
        return {};
    }

    QByteArray result;
    QDataStream stream(&result, QIODevice::ReadWrite);
    stream << static_cast<qint32>(GetType());
    QByteArray inner_data;
    QDataStream inner_stream(&inner_data, QIODevice::ReadWrite);
    for (const auto&[index, action] : index_actions_) {
        FAKE_LOG(qsl("Account %1 serialized in %2 action.").arg(index).arg(int(GetType())));
        inner_stream << index << action;
    }
    stream << inner_data;
    return result;
}

template<typename Data>
void FakePasscode::MultiAccountAction<Data>::Prepare() {
    SubscribeOnLoggingOut();
}

template<typename Data>
void FakePasscode::MultiAccountAction<Data>::OnAccountLoggedOut(qint32 index) {
    if (HasAction(index)
            && !executionInProgress_.contains(index)
            && Core::App().domain().local().IsFakeExecutionInProgress()) {
        FAKE_LOG(qsl("OUT-OF-ORDER execution of action %1. It has action for account %2, but it was logged out")
                .arg(int(GetType()))
                .arg(index));
    }
    RemoveAction(index);
}

template<typename Data>
void FakePasscode::MultiAccountAction<Data>::AddAction(qint32 index, const Data &data) {
    FAKE_LOG(qsl("Set action %1 for account %2").arg(int(GetType())).arg(index));
    index_actions_.insert_or_assign(index, data);
    SubscribeOnLoggingOut();
}

template<typename Data>
void FakePasscode::MultiAccountAction<Data>::AddAction(qint32 index, Data &&data) {
    FAKE_LOG(qsl("Set action %1 for account %2").arg(int(GetType())).arg(index));
    index_actions_.insert_or_assign(index, std::move(data));
    SubscribeOnLoggingOut();
}

template<typename Data>
void FakePasscode::MultiAccountAction<Data>::UpdateAction(qint32 index, const Data &data) {
    FAKE_LOG(qsl("Update action %1 for account %2").arg(int(GetType())).arg(index));
    index_actions_.insert_or_assign(index, data);
}

template<typename Data>
void FakePasscode::MultiAccountAction<Data>::UpdateAction(qint32 index, Data &&data) {
    FAKE_LOG(qsl("Update action %1 for account %2").arg(int(GetType())).arg(index));
    index_actions_.insert_or_assign(index, std::move(data));
}

template<typename Data>
void FakePasscode::MultiAccountAction<Data>::UpdateOrAddAction(qint32 index, const Data &data) {
    if (HasAction(index)) {
        UpdateAction(index, data);
    } else {
        AddAction(index, data);
    }
}

template<typename Data>
void FakePasscode::MultiAccountAction<Data>::UpdateOrAddAction(qint32 index, Data &&data) {
    if (HasAction(index)) {
        UpdateAction(index, std::move(data));
    } else {
        AddAction(index, std::move(data));
    }
}

template<typename Data>
bool FakePasscode::MultiAccountAction<Data>::HasAction(qint32 index) const {
    const bool has = index_actions_.contains(index);
    FAKE_LOG(qsl("Testing action %1 has account %2: %3").arg(int(GetType())).arg(index).arg(has));
    return has;
}

template <typename Data>
const Data& FakePasscode::MultiAccountAction<Data>::GetData(qint32 index) const {
    if (const auto it = index_actions_.find(index); it != index_actions_.end()) {
        FAKE_LOG(qsl("Get data for account %1 of type %2.").arg(index).arg(int(GetType())));
        return it->second;
    } else {
        return kEmptyData;
    }
}

template<typename Data>
void FakePasscode::MultiAccountAction<Data>::RemoveAction(qint32 index) {
    FAKE_LOG(qsl("Remove action %1 for account %2").arg(int(GetType())).arg(index));
    index_actions_.erase(index);
}

template<typename Data>
void FakePasscode::MultiAccountAction<Data>::Execute() {
    for (const auto &[index, account] : Core::App().domain().accounts()) {
        if (const auto it = index_actions_.find(index); it != index_actions_.end()) {
            FAKE_LOG(qsl("Account %1 performs %2 action.").arg(index).arg(int(GetType())));
            executionInProgress_.insert(index);
            ExecuteAccountAction(index, account.get(), it->second);
        }
    }
    PostponeCall([this]{
        executionInProgress_.clear();
    });
}

template<typename Data>
template<typename Fn>
void FakePasscode::MultiAccountAction<Data>::PostponeCall(Fn&& fn) {
    Core::App().postponeCall(crl::guard(&guard_, std::forward<Fn>(fn)));
}
