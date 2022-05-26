#include "logout.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "data/data_session.h"
#include "fakepasscode/log/fake_log.h"

void FakePasscode::LogoutAction::Execute() {
    for (const auto &[index, account] : Core::App().domain().accounts()) {
        if (index_to_logout_[index]) {
            FAKE_LOG(qsl("Account %1 setup to logout, perform.").arg(index));
            Core::App().logoutWithChecksAndClear(account.get());
            index_to_logout_.remove(index);
        }
    }
}

QByteArray FakePasscode::LogoutAction::Serialize() const {
    if (index_to_logout_.empty()) {
        return {};
    }

    QByteArray result;
    QDataStream stream(&result, QIODevice::ReadWrite);
    stream << static_cast<qint32>(ActionType::Logout);
    QByteArray inner_data;
    QDataStream inner_stream(&inner_data, QIODevice::ReadWrite);
    for (const auto&[index, is_logged_out] : index_to_logout_) {
        if (is_logged_out) {
            FAKE_LOG(qsl("Account %1 serialized in logout action, because it will be logout from it.").arg(index));
            inner_stream << index;
        }
    }
    stream << inner_data;
    return result;
}

FakePasscode::ActionType FakePasscode::LogoutAction::GetType() const {
    return ActionType::Logout;
}

FakePasscode::LogoutAction::LogoutAction(QByteArray inner_data) {
    FAKE_LOG(qsl("Create logout from QByteArray of size: %1").arg(inner_data.size()));
    if (!inner_data.isEmpty()) {
        QDataStream stream(&inner_data, QIODevice::ReadOnly);
        while (!stream.atEnd()) {
            qint32 index;
            stream >> index;
            FAKE_LOG(qsl("Account %1 deserialized in logout action.").arg(index));
            index_to_logout_[index] = true;
        }
    }
    SubscribeOnAccountsChanges();
}

FakePasscode::LogoutAction::LogoutAction(base::flat_map<qint32, bool> index_to_logout)
: index_to_logout_(std::move(index_to_logout))
{
    SubscribeOnAccountsChanges();
}

void FakePasscode::LogoutAction::SetLogout(qint32 index, bool logout) {
    FAKE_LOG(qsl("Set logout %1 for account %2").arg(logout).arg(index));
    index_to_logout_[index] = logout;
}

bool FakePasscode::LogoutAction::IsLogout(qint32 index) const {
    if (auto pos = index_to_logout_.find(index); pos != index_to_logout_.end()) {
        FAKE_LOG(qsl("Found logout for %1. Send %2").arg(index).arg(pos->second));
        return pos->second;
    }
	FAKE_LOG(qsl("Not found logout for %1. Send false").arg(index));
	return false;
}

const base::flat_map<qint32, bool>& FakePasscode::LogoutAction::GetLogout() const {
    return index_to_logout_;
}

void FakePasscode::LogoutAction::SubscribeOnAccountsChanges() {
    Core::App().domain().accountsChanges() | rpl::start_with_next([this] {
        SubscribeOnLoggingOut();
    }, lifetime_);
}

void FakePasscode::LogoutAction::Prepare() {
    SubscribeOnLoggingOut();
}

void FakePasscode::LogoutAction::SubscribeOnLoggingOut() {
    sub_lifetime_.destroy();
    for (const auto&[index, account] : Core::App().domain().accounts()) {
        FAKE_LOG(qsl("Subscribe on logout for account %1").arg(index));
        account->sessionChanges() | rpl::start_with_next([index = index, this] (const Main::Session* session) {
            if (session == nullptr) {
                FAKE_LOG(qsl("Account %1 logged out, remove from us.").arg(index));
                index_to_logout_.remove(index);
            }
        }, sub_lifetime_);
    }
}
