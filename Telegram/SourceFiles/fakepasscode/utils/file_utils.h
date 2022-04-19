
namespace FakePasscode {
    namespace FileUtils  {
        enum FileResult : unsigned short {
            Success = 0,
            NotRenamed = 1,
            NotDeleted = 2,
            MetadataNotChanged = 4,
            NotOpened = 8
        };
        FileResult DeleteFileDod(QString path);
        QDir GetRandomDir();
        QString GetRandomName(QDir);
        bool DeleteFolderRecursively(QString path, bool deleteRoot = false);
    };
}
