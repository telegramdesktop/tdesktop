#include "file_utils.h"
#include "core/file_utilities.h"
#include "fakepasscode/log/fake_log.h"
#include <random>
#include "core/application.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
using namespace FakePasscode::FileUtils;

FileResult FakePasscode::FileUtils::DeleteFileDoD(QString path) {
	QFile file(path);
	ushort result;
	if (Core::App().domain().local().IsDodCleaningEnabled()) {
		result |= file.open(QIODevice::OpenModeFlag::ReadWrite) ? FileResult::Success : FileResult::NotOpened;
		qint64 fileSize = file.size();

		const qint64 kBufferSize = 1024;
		std::vector<uint8_t> buffer(kBufferSize);

		for (size_t step = 1; step <= 2; step++) {
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

		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<int> byteRange(0, 255), hRange(0, 23), msRange(0, 59),
			yRange(0, 10), monthRange(1, 12), dRange(1, 28);

		file.seek(0);
		for (qint64 j = 0; j < fileSize; j += kBufferSize) {
			for (qint64 i = 0; i < kBufferSize; i++)
				buffer[i] = byteRange(gen);

			file.write(reinterpret_cast<char*>(buffer.data()), kBufferSize);
		}

		for (size_t i = 0; i < 4; i++) {
			result |= file.setFileTime(QDateTime(QDate(yRange(gen) + 2010, monthRange(gen), dRange(gen)),
				QTime(hRange(gen), msRange(gen), msRange(gen))), (QFileDevice::FileTime)i) 
				? FileResult::Success : FileResult::MetadataNotChanged;
		}
		file.close();
		result |= file.rename(QFileInfo(file).absoluteDir().absolutePath() + "/" + std::to_string(gen()).c_str()) ?
			FileResult::Success : FileResult::NotRenamed;

		FAKE_LOG(qsl("%1 file cleared Dod").arg(path));
	}
	result |= file.remove() ? FileResult::Success : FileResult::NotDeleted;

	return (FileResult)result;
}

bool FakePasscode::FileUtils::DeleteFolderRecursively(QString path, bool deleteRoot) {
	QDir dir(path);
	bool isNotOk = false;
	for (auto& entry : dir.entryList(QDir::Dirs | QDir::Filter::NoDotAndDotDot | QDir::Filter::Hidden)) {
		if (!(DeleteFolderRecursively(dir.path() + "/" + entry) && dir.rmdir(entry))) {
			isNotOk = true;
		}
	}
	for (auto& entry : dir.entryList(QDir::Filter::Files | QDir::Filter::Hidden)) {
		if (DeleteFileDoD(dir.path() + "/" + entry) != FileResult::Success) {
			isNotOk = true;
		}
	}
	if (deleteRoot) {
		isNotOk |= !dir.rmdir(path);
	}
	return !isNotOk;
}