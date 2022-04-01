#include "file_utils.h"
#include "core/file_utilities.h"
#include "fakepasscode/log/fake_log.h"

void FakePasscode::FileUtils::DeleteFileDoD(QString path) {
    QFile file(path);
    file.open(QIODevice::OpenModeFlag::ReadWrite);
    qint64 fileSize = file.size();

    const qint64 kBufferSize = 1024;
    std::vector<uint8_t> buffer(kBufferSize);

    for (size_t step = 1; step <= 2; step++){
        if (step == 1) {
            buffer.assign(kBufferSize, 0);
        }
        else {
            buffer.assign(kBufferSize, 255);
        }
        file.seek(0);
        for (qint64 i = 0; i < fileSize; i += kBufferSize) {
            file.write(reinterpret_cast<char*>(buffer.data()), kBufferSize);
        }
    }

    file.seek(0);
    for (qint64 j = 0; j < fileSize; j += kBufferSize) {
        for (qint64 i = 0; i < kBufferSize; i++)
            buffer[i] = rand() % 256;

        file.write(reinterpret_cast<char*>(buffer.data()), kBufferSize);
    }

    for (size_t i = 0; i < 4; i++){
        file.setFileTime(QDateTime(QDate(rand() % 10 + 2010, rand() % 12 + 1, rand() % 29 + 1),
            QTime(rand() % 24, rand() % 60, rand() % 60)), (QFileDevice::FileTime)i);
    }
    file.close();
    file.rename(QFileInfo(file).absoluteDir().absolutePath()+"/" + std::to_string(rand()).c_str());
    file.remove();
    FAKE_LOG(qsl("%1 file cleared").arg(path));
}

void FakePasscode::FileUtils::DeleteFolderRecursively(QString path) {
    QDir dir(path);
    srand(time(NULL));
    for (auto& entry : dir.entryList(QDir::Dirs | QDir::Filter::NoDotAndDotDot | QDir::Filter::Hidden)) {
            DeleteFolderRecursively(dir.path()+"/" + entry);
            dir.rmdir(entry);
    }
    for (auto& entry : dir.entryList(QDir::Filter::Files | QDir::Filter::Hidden)) {
        DeleteFileDoD(dir.path()+"/" + entry);
    }
}