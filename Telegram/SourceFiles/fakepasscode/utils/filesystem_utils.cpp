#include "filesystem_utils.h"

#include "core/utils.h"
#include "storage/details/storage_file_utilities.h"

using Storage::details::ToFilePart;

namespace {

QString GetMd5Filename(const QString& path) {
	quint64 path_hash[2] = { 0 };
	const auto path_bytes = path.toUtf8();
	hashMd5(path_bytes.constData(), path_bytes.size(), path_hash);
	return ToFilePart(path_hash[0]);
}

}

void RenameAndRemoveRecursively(const QString& path) {
	auto directory = QDir(path);
	auto renamed_path = QDir::cleanPath(directory.filePath(qsl("../%1")
			.arg(GetMd5Filename(path))));
	if (QFile::rename(path, renamed_path)) {
		directory.setPath(renamed_path);
	}
	directory.removeRecursively();
}

void RenameAndRemove(const QString& path) {
	const auto renamed = GetMd5Filename(path);
	if (QFile::rename(path, renamed)) {
		QFile::remove(renamed);
	}
}
