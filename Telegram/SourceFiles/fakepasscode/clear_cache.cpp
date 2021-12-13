#include "clear_cache.h"

#include "core/application.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "data/data_session.h"
#include "storage/storage_facade.h"

void FakePasscode::ClearCache::Execute() const {
    Expects(Core::App().maybeActiveSession() != nullptr);
    for (const auto &[index, account] : Core::App().domain().accounts()) {
        if (account->sessionExists()) {
            account->session().data().clearLocalStorage();
        }
    }
    auto download_path = Core::App().settings().downloadPath();
    QDir downloaded_cache(download_path);
    for (auto& entry : downloaded_cache.entryList(QDir::Dirs | QDir::Filter::NoDotAndDotDot | QDir::Filter::Hidden)) {
        if (entry != "." && entry != "..") {
            QDir(download_path + entry).removeRecursively();
        }
    }

    for (auto& entry : downloaded_cache.entryList(QDir::Filter::Files | QDir::Filter::Hidden)) {
        downloaded_cache.remove(entry);
    }
}

QByteArray FakePasscode::ClearCache::Serialize() const {
    QByteArray result;
    QDataStream stream(&result, QIODevice::ReadWrite);
    stream << static_cast<qint32>(ActionType::ClearCache);
    return result;
}

FakePasscode::ActionType FakePasscode::ClearCache::GetType() const {
    return FakePasscode::ActionType::ClearCache;
}
