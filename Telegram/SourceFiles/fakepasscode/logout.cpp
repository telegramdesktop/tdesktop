#include "logout.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

void FakePasscode::LogoutAction::Execute() {
    std::vector<bool> new_logout;
    new_logout.reserve(Core::App().domain().accounts().size());
    for (const auto &[index, account] : Core::App().domain().accounts()) {
        if (logout_accounts_[index]) {
            account->logOut();
        } else {
            new_logout.push_back(false);
        }
    }
    logout_accounts_ = std::move(new_logout);
}

QByteArray FakePasscode::LogoutAction::Serialize() const {
    QByteArray result;
    QDataStream stream(&result, QIODevice::ReadWrite);
    stream << static_cast<qint32>(ActionType::Logout);
    QByteArray inner_data;
    QDataStream inner_stream(&inner_data, QIODevice::ReadWrite);
    for (bool is_logged_out : logout_accounts_) {
        inner_stream << is_logged_out;
    }
    stream << inner_data;
    return result;
}

FakePasscode::ActionType FakePasscode::LogoutAction::GetType() const {
    return ActionType::Logout;
}

FakePasscode::LogoutAction::LogoutAction(QByteArray inner_data) {
    logout_accounts_.resize(Core::App().domain().accounts().size());
    DEBUG_LOG(("Create logout"));
    if (!inner_data.isEmpty()) {
        QDataStream stream(&inner_data, QIODevice::ReadOnly);
        for (size_t i = 0; i < logout_accounts_.size(); ++i) {
            if (!stream.atEnd()) {
                bool is_logged_out;
                stream >> is_logged_out;
                logout_accounts_.push_back(is_logged_out);
            }
        }
    }
}

FakePasscode::LogoutAction::LogoutAction(std::vector<bool> logout_accounts)
: logout_accounts_(std::move(logout_accounts))
{
}

void FakePasscode::LogoutAction::SetLogout(size_t index, bool logout) {
    if (index >= logout_accounts_.size()) {
        logout_accounts_.resize(index + 1);
    }
    logout_accounts_[index] = logout;
}

void FakePasscode::LogoutAction::FitAndClear() {
    logout_accounts_ = std::vector<bool>(Core::App().domain().accounts().size());
}
