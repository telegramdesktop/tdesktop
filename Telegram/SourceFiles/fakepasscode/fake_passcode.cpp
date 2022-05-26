#include <utility>
#include <memory>

#include "fake_passcode.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "fakepasscode/log/fake_log.h"
#include "actions/action_executor.h"

#include "core/application.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"

MTP::AuthKeyPtr FakePasscode::FakePasscode::GetEncryptedPasscode() const {
    if (!encrypted_passcode_) {
        encrypted_passcode_ = EncryptPasscode(fake_passcode_.current());
    }
    return encrypted_passcode_;
}

void FakePasscode::FakePasscode::SetPasscode(QByteArray passcode) {
    fake_passcode_ = std::move(passcode);
}

void FakePasscode::FakePasscode::AddAction(std::shared_ptr<Action> action) {
    FAKE_LOG(qsl("Add action of type %1 for passcode %2").arg(static_cast<int>(action->GetType())).arg(name_));
    ActionType type = action->GetType();
    actions_[type] = std::move(action);
    actions_[type]->Prepare();
    state_changed_.fire({});
}

void FakePasscode::FakePasscode::RemoveAction(ActionType type) {
    FAKE_LOG(qsl("Remove action of type %1 for passcode %2").arg(static_cast<int>(type)).arg(name_));
    actions_.erase(type);
    state_changed_.fire({});
}
void FakePasscode::FakePasscode::ClearActions(){
    FAKE_LOG(qsl("Clear actions for passcode %1").arg(name_));
    actions_.clear();
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
    ExecuteActions(actions_ | ranges::view::values | ranges::to_vector, name_);
}

FakePasscode::FakePasscode::FakePasscode(
        base::flat_map<ActionType, std::shared_ptr<Action>> actions)
        : actions_(std::move(actions)) {
    SetEncryptedChangeOnPasscode();
}

FakePasscode::FakePasscode::FakePasscode() {
    SetEncryptedChangeOnPasscode();
}

bool FakePasscode::FakePasscode::CheckPasscode(const QByteArray &passcode) const {
    const auto checkKey = EncryptPasscode(passcode);
    MTP::AuthKeyPtr fake_passcode = GetEncryptedPasscode();
    return checkKey->equals(fake_passcode);
}

QByteArray FakePasscode::FakePasscode::SerializeActions() const {
    QByteArray result;
    QDataStream stream(&result, QIODevice::ReadWrite);
    std::vector<QByteArray> serialized_actions;
    serialized_actions.reserve(actions_.size());
    for (const auto &[type, action] : actions_) {
        FAKE_LOG(qsl("Serialize action of type %1 for passcode %2").arg(static_cast<int>(type)).arg(name_));
        auto serialized_data = action->Serialize();
        if (!serialized_data.isEmpty()) {
            serialized_actions.push_back(std::move(serialized_data));
        } else {
            FAKE_LOG(qsl("Serialization failed for action of type %1 for passcode %2, "
                         "because we have no data for it").arg(static_cast<int>(type)).arg(name_));
        }
    }

    stream << qint32(serialized_actions.size());
    for (auto&& data : serialized_actions) {
        stream << data;
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
        // Ignore corrupted actions. Possibly we write amount greater than real amount
        if (actionSerialized.isEmpty()) {
            continue;
        }
        auto action = ::FakePasscode::DeSerialize(actionSerialized);
        FAKE_LOG(qsl("Find action of type %1 for passcode %2").arg(static_cast<int>(action->GetType())).arg(name_));
        actions_[action->GetType()] = std::move(action);
    }
}

QByteArray FakePasscode::FakePasscode::GetPasscode() const {
    return fake_passcode_.current();
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
        : fake_passcode_(std::move(passcode.fake_passcode_)),
          actions_(std::move(passcode.actions_)), name_(std::move(passcode.name_)),
          state_changed_(std::move(passcode.state_changed_)) {
    SetEncryptedChangeOnPasscode();
}

FakePasscode::FakePasscode &FakePasscode::FakePasscode::operator=(FakePasscode &&passcode) noexcept {
    if (this == &passcode) {
        return *this;
    }

    fake_passcode_ = std::move(passcode.fake_passcode_);
    actions_ = std::move(passcode.actions_);
    name_ = std::move(passcode.name_);
    state_changed_ = std::move(passcode.state_changed_);

    return *this;
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

MTP::AuthKeyPtr FakePasscode::FakePasscode::EncryptPasscode(const QByteArray& passcode) {
	return Storage::details::CreateLocalKey(
			passcode,
			Core::App().domain().local().GetPasscodeSalt());
}

void FakePasscode::FakePasscode::SetEncryptedChangeOnPasscode() {
    fake_passcode_.changes() | rpl::start_with_next([=](QByteArray passcode) {
        FAKE_LOG(qsl("Change and encrypt pass to %1").arg(QString::fromUtf8(passcode)));
        encrypted_passcode_ = EncryptPasscode(passcode);
    }, lifetime_);
}

void FakePasscode::FakePasscode::ReEncryptPasscode() {
    encrypted_passcode_ = EncryptPasscode(fake_passcode_.current());
}

void FakePasscode::FakePasscode::Prepare() {
    for (const auto&[_, action] : actions_) {
        action->Prepare();
    }
}
