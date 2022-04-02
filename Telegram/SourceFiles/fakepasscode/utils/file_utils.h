
namespace FakePasscode {
    class FileUtils  {
    public:
        static void DeleteFileDoD(QString path);
        static void DeleteFolderRecursively(QString path, bool deleteRoot = false);
    };
}
