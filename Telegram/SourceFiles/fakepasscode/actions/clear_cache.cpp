#include "clear_cache.h"

#include "core/application.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "data/data_session.h"
#include "storage/storage_facade.h"
#include "storage/storage_account.h"
#include "core/file_utilities.h"
#include "data/data_user.h"
#include "fakepasscode/log/fake_log.h"

void FakePasscode::ClearCache::Execute() {
    Expects(Core::App().maybeActiveSession() != nullptr);
    for (const auto &[index, account] : Core::App().domain().accounts()) {
        if (account->sessionExists()) {
            FAKE_LOG(qsl("Clear cache for account %1").arg(index));

            account->session().data().cache().clear();
            account->session().data().cacheBigFile().clear();
        }
    }
    Ui::Emoji::ClearIrrelevantCache();
    QString download_path;
    const auto session = Core::App().maybeActiveSession();
    if (Core::App().settings().downloadPath().isEmpty()) {
        download_path = File::DefaultDownloadPath(session);
    } else if (Core::App().settings().downloadPath() == qsl("tmp")) {
        download_path = session->local().tempDirectory();
    } else {
        download_path = Core::App().settings().downloadPath();
    }
    FAKE_LOG(qsl("Clear download folder %1").arg(download_path));
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
