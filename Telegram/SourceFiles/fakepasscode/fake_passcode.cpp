#include <utility>

#include "fake_passcode.h"
#include "../storage/details/storage_file_utilities.h"

MTP::AuthKeyPtr FakePasscode::FakePasscode::GetPasscode() const {
    return passcode_;
}

void FakePasscode::FakePasscode::SetPasscode(const QByteArray& passcode) {
    passcode_ = Storage::details::CreateLocalKey(passcode, salt_);
}

void FakePasscode::FakePasscode::SetSalt(QByteArray salt) {
    salt_ = std::move(salt);
}

void FakePasscode::FakePasscode::AddAction(std::unique_ptr<Action> action) {
    actions_.push_back(std::move(action));
}

void FakePasscode::FakePasscode::RemoveAction(std::unique_ptr<Action> action) {
    actions_.erase(std::find_if(actions_.begin(), actions_.end(),
                                [&action](const std::unique_ptr<Action>& lhsAction) {
        return typeid(action.get()) == typeid(lhsAction.get());
    }));
}

const std::unique_ptr<Action>& FakePasscode::FakePasscode::operator[](size_t index) const {
    return actions_.at(index);
}

const std::vector<std::unique_ptr<Action>>& FakePasscode::FakePasscode::GetActions() const {
    return actions_;
}

QByteArray FakePasscode::FakePasscode::salt_ = {};
