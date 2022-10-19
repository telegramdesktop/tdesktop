#include <QDir>
#include <QString>

namespace FakePasscode::FileUtils {
    enum FileResult : unsigned short {
        Success = 0,
        NotRenamed = 1,
        NotDeleted = 2,
        MetadataNotChanged = 4,
        NotOpened = 8
    };
    FileResult DeleteFile(QString path);
    QDir GetRandomDir();
    QString GetRandomName(QDir);
    bool DeleteFolderRecursively(QString path, bool deleteRoot = false);
    void ClearCaches(bool restore = true);
}  // namespace FakePasscode::FileUtils
