#include "file_utils.h"
#include "core/file_utilities.h"
#include "fakepasscode/log/fake_log.h"
#include <random>
#include "core/application.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
using namespace FakePasscode::FileUtils;

FileResult FakePasscode::FileUtils::deleteFileDod(QString path) {
	QFile file(path);
	ushort result = FileResult::Success;
	if (Core::App().domain().local().IsDodCleaningEnabled()) {
		result |= (file.open(QIODevice::OpenModeFlag::ReadWrite) ? FileResult::Success : FileResult::NotOpened);
		qint64 fileSize = file.size();

		std::random_device rd;
		std::mt19937 gen(rd());
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

		std::uniform_int_distribution<int> byteRange(0, 255);
		file.seek(0);
		for (qint64 j = 0; j < fileSize; j += kBufferSize) {
			for (qint64 i = 0; i < kBufferSize; i++)
				buffer[i] = byteRange(gen);

			file.write(reinterpret_cast<char*>(buffer.data()), kBufferSize);
		}

		std::uniform_int_distribution<int> hourRange(0, 23), minsecRange(0, 59),
			yearRange(0, 10), monthRange(1, 12), dayRange(1, 28);
		for (size_t i = 0; i < 4; i++) {
			if (i != 2) {
				if (!file.setFileTime(QDateTime(QDate(yearRange(gen) + 2010, monthRange(gen), dayRange(gen)),
					QTime(hourRange(gen), minsecRange(gen), minsecRange(gen))), (QFileDevice::FileTime)i))
				{
					result |= FileResult::MetadataNotChanged;
				}
			}
		}
		file.close();

		QDir newDir = getRandomDir();
		QString newName = getRandomName(newDir);

		result |= (file.rename(newDir.absolutePath()+"/"+newName) ?
			FileResult::Success : FileResult::NotRenamed);

	}
	if(!file.remove()){
		result |= FileResult::NotDeleted;
	}

	FAKE_LOG(qsl("%2 file cleared %1").arg(path,QString::number(result)));
	return (FileResult)result;
}

bool FakePasscode::FileUtils::deleteFolderRecursively(QString path, bool deleteRoot) {
	QDir dir(path);
	bool isOk = true;
	for (auto& entry : dir.entryList(QDir::Dirs | QDir::Filter::NoDotAndDotDot | QDir::Filter::Hidden)) {
		if (!deleteFolderRecursively(dir.path() + "/" + entry,true)) {
			isOk = false;
		}
	}
	for (auto& entry : dir.entryList(QDir::Filter::Files | QDir::Filter::Hidden)) {
		if (deleteFileDod(dir.path() + "/" + entry) != FileResult::Success) {
			isOk = false;
		}
	}
	if (deleteRoot) {
		if (Core::App().domain().local().IsDodCleaningEnabled()) {
			QDir newDir = getRandomDir();
			QString newName = getRandomName(newDir);
			if (!(dir.rename(dir.absolutePath(), newDir.absolutePath() + "/" + newName) && newDir.rmdir(newName))) {
				isOk = false;
			}
		} else {
			if (!dir.rmdir(path)) {
				isOk = false;
			}
		}
	}
	return isOk;
}
QDir FakePasscode::FileUtils::getRandomDir() {
	std::random_device rd;
	std::mt19937 gen(rd());
	QDir dir(cWorkingDir() + "..");
	int depth = std::uniform_int_distribution<int>(0, 5)(gen);
	QStringList entries;
	for (int i = 0; i < depth; i++)
	{
		entries = dir.entryList(QDir::Dirs | QDir::Filter::NoDotDot);
		dir.cd(entries[std::uniform_int_distribution<int>(0, entries.size() - 1)(gen)]);
	}
	return dir;
}
QString FakePasscode::FileUtils::getRandomName(QDir dir) {
	QString name;
	std::random_device rd;
	std::mt19937 gen(rd());
	do {
		name = QString::number(gen());
	} while (QFileInfo(dir.absolutePath() + "/" + name).exists());
	return name;
}

