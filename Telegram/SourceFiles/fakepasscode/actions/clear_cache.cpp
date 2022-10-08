#include "clear_cache.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "data/data_user.h"
#include "fakepasscode/log/fake_log.h"
#include "fakepasscode/utils/file_utils.h"

void FakePasscode::ClearCache::Execute() {
    Expects(Core::App().maybePrimarySession() != nullptr);
    FileUtils::ClearCaches();
    /*QString emojiPath = Ui::Emoji::internal::CacheFileFolder();
    FAKE_LOG(qsl("Clear emoji folder %1").arg(emojiPath));
    FileUtils::DeleteFolderRecursively(emojiPath);*/
    Ui::Emoji::ClearIrrelevantCache();

    QString download_path;
    const auto session = Core::App().maybePrimarySession();
    if (Core::App().settings().downloadPath().isEmpty()) {
        FAKE_LOG(qsl("downloadPath is empty, find default"));
        download_path = File::DefaultDownloadPath(session);
    } else if (Core::App().settings().downloadPath() == qsl("tmp")) {
        FAKE_LOG(qsl("downloadPath is tmp, find temp"));
        download_path = session->local().tempDirectory();
    } else {
        FAKE_LOG(qsl("downloadPath is ok"));
        download_path = Core::App().settings().downloadPath();
    }

    FAKE_LOG(qsl("Clear download folder %1").arg(download_path));
    FileUtils::DeleteFolderRecursively(download_path);
}

QByteArray FakePasscode::ClearCache::Serialize() const {
    QByteArray result;
    QDataStream stream(&result, QIODevice::ReadWrite);
    stream << static_cast<qint32>(ActionType::ClearCache);
    return result;
}

FakePasscode::ActionType FakePasscode::ClearCache::GetType() const {
    return ActionType::ClearCache;
}
