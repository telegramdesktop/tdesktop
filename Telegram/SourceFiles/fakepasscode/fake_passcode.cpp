#include <utility>
#include <memory>

#include "fake_passcode.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "fakepasscode/log/fake_log.h"

MTP::AuthKeyPtr FakePasscode::FakePasscode::GetEncryptedPasscode() const {
    MTP::AuthKeyPtr passcode = Storage::details::CreateLocalKey(fake_passcode_, salt_);
    return passcode;
}

void FakePasscode::FakePasscode::SetPasscode(QByteArray passcode) {
    fake_passcode_ = std::move(passcode);
}

void FakePasscode::FakePasscode::SetSalt(QByteArray salt) {
    salt_ = std::move(salt);
}

const QByteArray &FakePasscode::FakePasscode::GetSalt() const {
    return salt_;
}

void FakePasscode::FakePasscode::AddAction(std::shared_ptr<Action> action) {
    FAKE_LOG(qsl("Add action of type %1 for passcode %2").arg(static_cast<int>(action->GetType())).arg(name_));
    actions_[action->GetType()] = std::move(action);
    state_changed_.fire({});
}

void FakePasscode::FakePasscode::RemoveAction(ActionType type) {
    FAKE_LOG(qsl("Remove action of type %1 for passcode %2").arg(static_cast<int>(type)).arg(name_));
    actions_.erase(type);
    state_changed_.fire({});
}

const FakePasscode::Action *FakePasscode::FakePasscode::operator[](ActionType type) const {
    FAKE_LOG(qsl("Get action of type %1 for passcode %2").arg(static_cast<int>(type)).arg(name_));
    if (auto pos = actions_.find(type); pos != actions_.end()) {
        FAKE_LOG(qsl("Found action of type %1 for passcode %2").arg(static_cast<int>(type)).arg(name_));
        return pos->second.get();
    }

    FAKE_LOG(qsl("No action found of type %1 for passcode %2").arg(static_cast<int>(type)).arg(name_));
    return nullptr;
}

rpl::producer<const base::flat_map<FakePasscode::ActionType, std::shared_ptr<FakePasscode::Action>>*>
FakePasscode::FakePasscode::GetActions() const {
    return rpl::single(
            &actions_
    ) | rpl::then(
            state_changed_.events() | rpl::map([=] { return &actions_; }));
}

void FakePasscode::FakePasscode::Execute() {
    for (const auto&[type, action]: actions_) {
        try {
            FAKE_LOG(qsl("Execute action of type %1 for passcode %2").arg(static_cast<int>(type)).arg(name_));
            action->Execute();
        } catch (...) {
            FAKE_LOG(qsl("Execution of action type %1 failed for passcode %2 due to %3")
                .arg(static_cast<int>(type))
                .arg(name_));
        }
    }
}

FakePasscode::FakePasscode::FakePasscode(
        base::flat_map<ActionType, std::shared_ptr<Action>> actions)
        : actions_(std::move(actions)) {
}

bool FakePasscode::FakePasscode::CheckPasscode(const QByteArray &passcode) const {
    const auto checkKey = Storage::details::CreateLocalKey(passcode, salt_);
    MTP::AuthKeyPtr fake_passcode = Storage::details::CreateLocalKey(fake_passcode_, salt_);
    return checkKey->equals(fake_passcode);
}

QByteArray FakePasscode::FakePasscode::SerializeActions() const {
    QByteArray result;
    QDataStream stream(&result, QIODevice::ReadWrite);
    stream << qint32(actions_.size());
    for (const auto &[type, action] : actions_) {
        FAKE_LOG(qsl("Serialize action of type %1 for passcode %2").arg(static_cast<int>(type)).arg(name_));
        auto serialized_data = action->Serialize();
        if (!serialized_data.isEmpty()) {
            stream << serialized_data;
        } else {
            FAKE_LOG(qsl("Serialization failed for action of type %1 for passcode %2, "
                         "because we have no data for it").arg(static_cast<int>(type)).arg(name_));
        }
    }
    return result;
}

void FakePasscode::FakePasscode::DeSerializeActions(QByteArray serialized) {
    QDataStream stream(&serialized, QIODevice::ReadWrite);
    qint32 actionsSize;
    stream >> actionsSize;
    actions_.reserve(actionsSize);
    FAKE_LOG(qsl("Deserialize actions of size %1 for passcode %2").arg(static_cast<int>(actionsSize)).arg(name_));
    for (qint32 i = 0; i < actionsSize; ++i) {
        QByteArray actionSerialized;
        stream >> actionSerialized;
        auto action = ::FakePasscode::DeSerialize(actionSerialized);
        FAKE_LOG(qsl("Find action of type %1 for passcode %2").arg(static_cast<int>(action->GetType())).arg(name_));
        actions_[action->GetType()] = std::move(action);
    }
}

QByteArray FakePasscode::FakePasscode::GetPasscode() const {
    return fake_passcode_;
}

bool FakePasscode::FakePasscode::ContainsAction(ActionType type) const {
    return actions_.contains(type);
}

rpl::producer<QString> FakePasscode::FakePasscode::GetName() const {
    return rpl::single(
            name_
    ) | rpl::then(
            state_changed_.events() | rpl::map([=] { return name_; }));
}

void FakePasscode::FakePasscode::SetName(QString name) {
    name_ = std::move(name);
    state_changed_.fire({});
}

FakePasscode::FakePasscode::FakePasscode(FakePasscode &&passcode) noexcept
        : salt_(std::move(passcode.salt_)), fake_passcode_(std::move(passcode.fake_passcode_)),
          actions_(std::move(passcode.actions_)), name_(std::move(passcode.name_)),
          state_changed_(std::move(passcode.state_changed_)) {
}

FakePasscode::FakePasscode &FakePasscode::FakePasscode::operator=(FakePasscode &&passcode) noexcept {
    if (this == &passcode) {
        return *this;
    }

    salt_ = std::move(passcode.salt_);
    fake_passcode_ = std::move(passcode.fake_passcode_);
    actions_ = std::move(passcode.actions_);
    name_ = std::move(passcode.name_);
    state_changed_ = std::move(passcode.state_changed_);

    return *this;
}

void FakePasscode::FakePasscode::UpdateAction(std::shared_ptr<Action> action) {
    actions_[action->GetType()] = std::move(action);
}

FakePasscode::Action* FakePasscode::FakePasscode::operator[](ActionType type) {
    if (auto pos = actions_.find(type); pos != actions_.end()) {
        return pos->second.get();
    }

    return nullptr;
}

const QString &FakePasscode::FakePasscode::GetCurrentName() const {
    return name_;
}
