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
#include "fakepasscode/utils/file_utils.h"

void FakePasscode::ClearCache::Execute() {
    Expects(Core::App().maybeActiveSession() != nullptr);
    for (const auto &[index, account] : Core::App().domain().accounts()) {
        if (account->sessionExists()) {
            FAKE_LOG(qsl("Clear cache for account %1").arg(index));

            account->session().data().cache().close([account = account.get()]{
                    account->session().data().cacheBigFile().close([=] {
                        FileUtils::deleteFolderRecursively(account->local().cachePath());
                        FileUtils::deleteFolderRecursively(account->local().cacheBigFilePath());
                        account->session().data().resetCaches();
                    });
                });
        }
    }

    /*QString emojiPath = Ui::Emoji::internal::CacheFileFolder();
    FAKE_LOG(qsl("Clear emoji folder %1").arg(emojiPath));
    FileUtils::deleteFolderRecursively(emojiPath);*/
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
    FileUtils::deleteFolderRecursively(download_path);
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
