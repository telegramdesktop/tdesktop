#include <utility>
#include <memory>

#include "fake_passcode.h"
#include "../storage/details/storage_file_utilities.h"
#include "../storage/serialize_common.h"

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

const QByteArray& FakePasscode::FakePasscode::GetSalt() const {
    return salt_;
}

void FakePasscode::FakePasscode::AddAction(std::shared_ptr<Action> action) {
    actions_.push_back(std::move(action));
    actions_changed_.fire({});
}

void FakePasscode::FakePasscode::RemoveAction(std::shared_ptr<Action> action) {
    actions_.erase(std::find_if(actions_.begin(), actions_.end(),
                                [&](const std::shared_ptr<Action>& lhsAction) {
        return typeid(action.get()) == typeid(lhsAction.get());
    }));
    actions_changed_.fire({});
}

const std::shared_ptr<FakePasscode::Action>& FakePasscode::FakePasscode::operator[](size_t index) const {
    return actions_.at(index);
}

rpl::producer<std::vector<std::shared_ptr<FakePasscode::Action>>> FakePasscode::FakePasscode::GetActions() const {
    return rpl::single(
            actions_
    ) | rpl::then(
            actions_changed_.events() | rpl::map([=] { return actions_; }));
}

void FakePasscode::FakePasscode::Execute() {
    for (const auto& action : actions_) {
        try {
            action->Execute();
        } catch (...) {}
    }
}

FakePasscode::FakePasscode::FakePasscode(
        std::vector<std::shared_ptr<Action>> actions)
: actions_(std::move(actions))
{
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
    for (const auto& action : actions_) {
        stream << action->Serialize();
    }
    return result;
}

void FakePasscode::FakePasscode::DeSerializeActions(QByteArray serialized) {
    QDataStream stream(&serialized, QIODevice::ReadWrite);
    qint32 actionsSize;
    stream >> actionsSize;
    actions_.reserve(actionsSize);
    for (qint32 i = 0; i < actionsSize; ++i) {
        QByteArray actionSerialized;
        stream >> actionSerialized;
        actions_.emplace_back(::FakePasscode::DeSerialize(actionSerialized));
    }
}

QByteArray FakePasscode::FakePasscode::GetPasscode() const {
    return fake_passcode_;
}

bool FakePasscode::FakePasscode::ContainsAction(ActionType type) const {
    return std::any_of(actions_.begin(), actions_.end(), [=] (const auto& action) {
        return action->GetType() == type;
    });
}

bool FakePasscode::FakePasscode::operator==(const FakePasscode &other) const {
    if (this == &other) {
        return true;
    }

    // No need to check for salt, because it can change, but passcode is the same
    if (fake_passcode_ != other.fake_passcode_ || actions_.size() != other.actions_.size()) {
        return false;
    }

    for (size_t i = 0; i < actions_.size(); ++i) {
        if (actions_[i]->GetType() != other.actions_[i]->GetType()) {
            return false;
        }
    }
    return true;
}

bool FakePasscode::FakePasscode::operator!=(const FakePasscode &other) const {
    return !(*this == other);
}

const QString &FakePasscode::FakePasscode::GetName() const {
    return name_;
}

void FakePasscode::FakePasscode::SetName(QString name) {
    name_ = std::move(name);
}

FakePasscode::FakePasscode::FakePasscode(FakePasscode &&passcode) noexcept
: salt_(std::move(passcode.salt_))
, fake_passcode_(std::move(passcode.fake_passcode_))
, actions_(std::move(passcode.actions_))
, name_(std::move(passcode.name_))
, actions_changed_(std::move(passcode.actions_changed_)) {
}

FakePasscode::FakePasscode& FakePasscode::FakePasscode::operator=(FakePasscode&& passcode) noexcept {
    if (this == &passcode) {
        return *this;
    }

    salt_ = std::move(passcode.salt_);
    fake_passcode_ = std::move(passcode.fake_passcode_);
    actions_ = std::move(passcode.actions_);
    name_ = std::move(passcode.name_);
    actions_changed_ = std::move(passcode.actions_changed_);

    return *this;
}

void FakePasscode::FakePasscode::UpdateAction(std::shared_ptr<Action> action) {
    for (auto& passcode_action : actions_) {
        if (passcode_action->GetType() == action->GetType()) {
            passcode_action = std::move(action);
            break;
        }
    }
}

std::shared_ptr<FakePasscode::Action> FakePasscode::FakePasscode::GetAction(ActionType type) const {
    for (const auto & action : actions_) {
        if (action->GetType() == type) {
            return action;
        }
    }

    return nullptr;
}
