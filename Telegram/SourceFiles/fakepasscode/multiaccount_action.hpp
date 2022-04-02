#include "multiaccount_action.h"

#include "log/fake_log.h"

#include "core/application.h"
#include "main/main_domain.h"

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
bool FakePasscode::MultiAccountAction<Data>::HasAction(qint32 index) const {
    const bool has = index_actions_.contains(index);
    FAKE_LOG(qsl("Testing action %1 has account %2: %3").arg(int(GetType())).arg(index).arg(has));
    return has;
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
            ExecuteAccountAction(index, account.get(), it->second);
        }
    }
}
