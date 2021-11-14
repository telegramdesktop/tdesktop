#include <utility>
#include <memory>

#include "fake_passcode.h"
#include "../storage/details/storage_file_utilities.h"
#include "../storage/serialize_common.h"

MTP::AuthKeyPtr FakePasscode::FakePasscode::GetEncryptedPasscode() const {
    return passcode_;
}

void FakePasscode::FakePasscode::SetPasscode(const QByteArray& passcode) {
    fake_passcode_ = passcode;
    passcode_ = Storage::details::CreateLocalKey(passcode, salt_);
}

void FakePasscode::FakePasscode::SetSalt(QByteArray salt) {
    salt_ = std::move(salt);
    passcode_ = Storage::details::CreateLocalKey(fake_passcode_, salt_);
}

void FakePasscode::FakePasscode::AddAction(std::shared_ptr<Action> action) {
    actions_.push_back(std::move(action));
}

void FakePasscode::FakePasscode::RemoveAction(std::shared_ptr<Action> action) {
    actions_.erase(std::find_if(actions_.begin(), actions_.end(),
                                [&action](const std::shared_ptr<Action>& lhsAction) {
        return typeid(action.get()) == typeid(lhsAction.get());
    }));
}

const std::shared_ptr<FakePasscode::Action>& FakePasscode::FakePasscode::operator[](size_t index) const {
    return actions_.at(index);
}

const std::vector<std::shared_ptr<FakePasscode::Action>>& FakePasscode::FakePasscode::GetActions() const {
    return actions_;
}

void FakePasscode::FakePasscode::Execute() const {
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
    return checkKey->equals(passcode_);
}

const QByteArray &FakePasscode::FakePasscode::getSalt() const {
    return salt_;
}

void FakePasscode::FakePasscode::setSalt(const QByteArray &salt) {
    salt_ = salt;
}

const QByteArray &FakePasscode::FakePasscode::getRealPasscode() const {
    return real_passcode_;
}

void FakePasscode::FakePasscode::setRealPasscode(const QByteArray &realPasscode) {
    real_passcode_ = realPasscode;
}

void FakePasscode::FakePasscode::SetPasscode(MTP::AuthKeyPtr passcode) {
    passcode_ = std::move(passcode);
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
    if (fake_passcode_ != other.fake_passcode_) {
        return false;
    } else if (real_passcode_ != other.real_passcode_) {
        return false;
    } else if (actions_.size() != other.actions_.size()) {
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
