
namespace FakePasscode {
    namespace FileUtils  {
        enum FileResult : unsigned short {
            Success = 0,
            NotRenamed = 1,
            NotDeleted = 2,
            MetadataNotChanged = 4,
            NotOpened = 8
        };
        FileResult deleteFileDod(QString path);
        QDir getRandomDir();
        QString getRandomName(QDir);
        bool deleteFolderRecursively(QString path, bool deleteRoot = false);
    };
}
