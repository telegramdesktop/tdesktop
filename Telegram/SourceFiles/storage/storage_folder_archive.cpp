/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_folder_archive.h"

#include "platform/platform_file_utilities.h"
#include "ui/chat/attach/attach_prepare.h"

#include <minizip/zip.h>
#include <QtCore/QDirIterator>
#include <QtCore/QTemporaryFile>

namespace Storage {
namespace {

constexpr auto kArchiveChunkSize = int64(1024 * 1024);
constexpr auto kArchiveMemLevel = 8;
constexpr auto kArchiveEntryOverhead = int64(128);
constexpr auto kStaleArchiveTimeout = 24 * 60 * 60;
constexpr auto kUtf8NamesFlag = uLong(1) << 11;
constexpr auto kDirectoryAttribute = uLong(0x10);

using Status = ArchiveWriteResult::Status;

struct WriteState {
	Fn<bool(float64)> progress;
	int64 processed = 0;
	int64 total = 0;
	bool cancelled = false;
};

[[nodiscard]] bool StepProgress(WriteState &state) {
	if (state.cancelled) {
		return false;
	} else if (state.progress) {
		const auto value = (state.total > 0)
			? std::clamp(state.processed / float64(state.total), 0., 1.)
			: 0.;
		if (!state.progress(value)) {
			state.cancelled = true;
		}
	}
	return !state.cancelled;
}

class ZipToFile {
public:
	explicit ZipToFile(const QString &path) : _file(path) {
	}

	[[nodiscard]] zlib_filefunc64_def funcs() {
		auto result = zlib_filefunc64_def();
		result.opaque = this;
		result.zopen64_file = &ZipToFile::Open;
		result.zread_file = &ZipToFile::Read;
		result.zwrite_file = &ZipToFile::Write;
		result.ztell64_file = &ZipToFile::Tell;
		result.zseek64_file = &ZipToFile::Seek;
		result.zclose_file = &ZipToFile::Close;
		result.zerror_file = &ZipToFile::Error;
		return result;
	}

	[[nodiscard]] int64 writtenSize() {
		return _file.size();
	}

private:
	voidpf open() {
		return _file.open(QIODevice::ReadWrite | QIODevice::Truncate)
			? this
			: nullptr;
	}

	uLong read(void *buf, uLong size) {
		const auto result = _file.read(static_cast<char*>(buf), size);
		return (result > 0) ? uLong(result) : 0;
	}

	uLong write(const void *buf, uLong size) {
		const auto result = _file.write(
			static_cast<const char*>(buf),
			size);
		return (result >= 0) ? uLong(result) : 0;
	}

	[[nodiscard]] ZPOS64_T tell() {
		return ZPOS64_T(_file.pos());
	}

	long seek(ZPOS64_T offset, int origin) {
		const auto position = (origin == ZLIB_FILEFUNC_SEEK_SET)
			? int64(offset)
			: (origin == ZLIB_FILEFUNC_SEEK_CUR)
			? (_file.pos() + int64(offset))
			: (_file.size() + int64(offset));
		return _file.seek(position) ? 0 : -1;
	}

	int close() {
		_file.close();
		return 0;
	}

	[[nodiscard]] int error() {
		return (_file.error() == QFileDevice::NoError) ? 0 : -1;
	}

	static voidpf Open(voidpf opaque, const void *filename, int mode) {
		return static_cast<ZipToFile*>(opaque)->open();
	}

	static uLong Read(voidpf opaque, voidpf stream, void *buf, uLong size) {
		return static_cast<ZipToFile*>(opaque)->read(buf, size);
	}

	static uLong Write(
			voidpf opaque,
			voidpf stream,
			const void *buf,
			uLong size) {
		return static_cast<ZipToFile*>(opaque)->write(buf, size);
	}

	static ZPOS64_T Tell(voidpf opaque, voidpf stream) {
		return static_cast<ZipToFile*>(opaque)->tell();
	}

	static long Seek(
			voidpf opaque,
			voidpf stream,
			ZPOS64_T offset,
			int origin) {
		return static_cast<ZipToFile*>(opaque)->seek(offset, origin);
	}

	static int Close(voidpf opaque, voidpf stream) {
		return static_cast<ZipToFile*>(opaque)->close();
	}

	static int Error(voidpf opaque, voidpf stream) {
		return static_cast<ZipToFile*>(opaque)->error();
	}

	QFile _file;

};

[[nodiscard]] QString ArchiveDirectory() {
	return QDir::tempPath() + u"/tdarchive"_q;
}

[[nodiscard]] QString ArchiveFileTemplate() {
	const auto directory = ArchiveDirectory();
	QDir().mkpath(directory);
	return directory + u"/XXXXXX.zip"_q;
}

[[nodiscard]] QString ArchiveRootName(const QString &folder) {
	const auto name = QDir(folder).dirName();
	return name.isEmpty() ? u"Archive"_q : name;
}

[[nodiscard]] QString CommonParentName(const QStringList &paths) {
	auto parent = QString();
	for (const auto &path : paths) {
		const auto folder = QFileInfo(path).absolutePath();
		if (folder.isEmpty()) {
			return QString();
		} else if (parent.isEmpty()) {
			parent = folder;
		} else if (parent != folder) {
			return QString();
		}
	}
	return QDir(parent).dirName();
}

[[nodiscard]] bool AlreadyCompressedName(const QString &name) {
	const auto kStoredExtensions = {
		"3gp", "7z", "aac", "apk", "avi", "avif", "bz2", "cab", "docx",
		"epub", "flac", "flv", "gif", "gz", "heic", "heif", "jar", "jpeg",
		"jpg", "jxl", "m4a", "m4v", "mkv", "mov", "mp3", "mp4", "odp",
		"ods", "odt", "oga", "ogg", "opus", "png", "pptx", "rar", "tgz",
		"webm", "webp", "wma", "wmv", "xlsx", "xz", "zip", "zst",
	};
	const auto slash = name.lastIndexOf('/');
	const auto dot = name.lastIndexOf('.');
	if (dot <= slash + 1) {
		return false;
	}
	const auto extension = name.mid(dot + 1).toLower();
	return ranges::find(kStoredExtensions, extension)
		!= end(kStoredExtensions);
}

void FillEntryDate(const QDateTime &modified, zip_fileinfo *result) {
	const auto date = modified.date();
	const auto time = modified.time();
	result->tmz_date.tm_sec = time.second();
	result->tmz_date.tm_min = time.minute();
	result->tmz_date.tm_hour = time.hour();
	result->tmz_date.tm_mday = date.day();
	result->tmz_date.tm_mon = date.month() - 1;
	result->tmz_date.tm_year = std::max(date.year(), 1980);
}

[[nodiscard]] std::optional<ArchiveEntries> WalkFolder(
		const QString &folder,
		const QString &root) {
	auto result = ArchiveEntries();
	auto it = QDirIterator(
		folder,
		QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
		QDirIterator::Subdirectories);
	while (it.hasNext()) {
		it.next();
		const auto info = it.fileInfo();
		const auto absolute = info.filePath();
		auto relative = absolute.mid(folder.size());
		while (relative.startsWith('/')) {
			relative = relative.mid(1);
		}
		if (relative.isEmpty() || info.isSymLink()) {
			continue;
		} else if (info.isDir()) {
			result.list.push_back({
				absolute,
				root + '/' + relative + '/',
				info.lastModified(),
				0,
				true,
			});
		} else if (!info.isFile()) {
			continue;
		} else if (!info.isReadable()) {
			return std::nullopt;
		} else {
			const auto size = info.size();
			result.total += size;
			result.list.push_back({
				absolute,
				root + '/' + relative,
				info.lastModified(),
				size,
			});
		}
	}
	return result;
}

[[nodiscard]] QString DedupedName(
		base::flat_map<QString, int> &counts,
		const QString &name) {
	if (++counts[name.toLower()] == 1) {
		return name;
	}
	const auto dot = name.lastIndexOf('.');
	while (true) {
		const auto suffix = u" (%1)"_q.arg(counts[name.toLower()]);
		const auto result = (dot > 0)
			? (name.left(dot) + suffix + name.mid(dot))
			: (name + suffix);
		if (++counts[result.toLower()] == 1) {
			return result;
		}
		++counts[name.toLower()];
	}
}

[[nodiscard]] std::optional<ArchiveEntries> GatherFiles(
		const QStringList &paths) {
	auto result = ArchiveEntries();
	auto counts = base::flat_map<QString, int>();
	for (const auto &path : paths) {
		const auto info = QFileInfo(path);
		if (info.isDir()) {
			continue;
		} else if (!info.exists() || !info.isReadable()) {
			return std::nullopt;
		} else if (!info.isFile()) {
			continue;
		}
		const auto size = info.size();
		result.total += size;
		result.list.push_back({
			info.absoluteFilePath(),
			DedupedName(counts, info.fileName()),
			info.lastModified(),
			size,
		});
	}
	if (result.list.empty()) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] ArchiveWriteResult WriteArchiveFile(
		const std::vector<ArchiveEntry> &entries,
		WriteState &state,
		int64 limit) {
	auto temp = QTemporaryFile(ArchiveFileTemplate());
	if (!temp.open()) {
		return { Status::Failed };
	}
	const auto path = temp.fileName();
	temp.close();

	auto io = ZipToFile(path);
	auto funcs = io.funcs();
	const auto zip = zipOpen2_64("", APPEND_STATUS_CREATE, nullptr, &funcs);
	if (!zip) {
		return { Status::Failed };
	}
	auto buffer = QByteArray(kArchiveChunkSize, 0);
	auto finish = Status::Done;
	for (const auto &entry : entries) {
		if (!StepProgress(state)) {
			finish = Status::Cancelled;
			break;
		}
		if (!entry.directory && !QFile::exists(entry.absolute)) {
			state.processed += entry.size;
			continue;
		}
		auto zipfi = zip_fileinfo();
		FillEntryDate(entry.modified, &zipfi);
		zipfi.external_fa = entry.directory ? kDirectoryAttribute : 0;
		const auto name = entry.name.toUtf8();
		const auto zip64 = (entry.size >= int64(0xFFFFFFFFULL)) ? 1 : 0;
		const auto stored = entry.directory
			|| AlreadyCompressedName(entry.name);
		if (zipOpenNewFileInZip4_64(
				zip,
				name.constData(),
				&zipfi,
				nullptr,
				0,
				nullptr,
				0,
				nullptr,
				stored ? 0 : Z_DEFLATED,
				stored ? 0 : Z_DEFAULT_COMPRESSION,
				0,
				-MAX_WBITS,
				kArchiveMemLevel,
				Z_DEFAULT_STRATEGY,
				nullptr,
				0,
				0,
				kUtf8NamesFlag,
				zip64) != ZIP_OK) {
			finish = Status::Failed;
			break;
		}
		if (!entry.directory) {
			auto file = QFile(entry.absolute);
			if (!file.open(QIODevice::ReadOnly)) {
				finish = Status::Failed;
				break;
			}
			while (true) {
				const auto read = file.read(
					buffer.data(),
					kArchiveChunkSize);
				if (read < 0) {
					finish = Status::Failed;
					break;
				} else if (!read) {
					break;
				}
				if (zipWriteInFileInZip(
						zip,
						buffer.constData(),
						uInt(read)) != ZIP_OK) {
					finish = Status::Failed;
					break;
				}
				state.processed += read;
				if (!StepProgress(state)) {
					finish = Status::Cancelled;
					break;
				}
				if (io.writtenSize() > limit) {
					finish = Status::TooLarge;
					break;
				}
			}
			if (finish != Status::Done) {
				break;
			}
		}
		if (zipCloseFileInZip(zip) != ZIP_OK) {
			finish = Status::Failed;
			break;
		}
	}
	if (zipClose(zip, nullptr) != ZIP_OK
		&& finish == Status::Done) {
		finish = Status::Failed;
	}
	if (finish != Status::Done) {
		return { finish, QString(), io.writtenSize() };
	}
	temp.setAutoRemove(false);
	return { finish, path, QFileInfo(path).size() };
}

[[nodiscard]] Ui::PreparedFile ArchiveFile(
		const QString &zipName,
		Ui::PreparedFileArchive &&job) {
	auto result = Ui::PreparedFile(QString());
	result.displayName = zipName;
	result.type = Ui::PreparedFile::Type::File;
	result.information = std::make_unique<Ui::PreparedFileInformation>();
	result.archive = std::make_shared<Ui::PreparedFileArchive>(
		std::move(job));
	return result;
}

} // namespace

QString SingleFolderPath(const QList<QUrl> &urls) {
	if (urls.size() != 1 || !urls.front().isLocalFile()) {
		return QString();
	}
	const auto path = Platform::File::UrlToLocal(urls.front());
	return QFileInfo(path).isDir() ? path : QString();
}

QStringList FolderFilesForSending(const QString &folder) {
	auto result = QStringList();
	const auto dir = QDir(folder);
	for (const auto &info : dir.entryInfoList(QDir::Files, QDir::Name)) {
		result.push_back(info.absoluteFilePath());
	}
	const auto subdirs = dir.entryInfoList(
		QDir::Dirs | QDir::NoDotAndDotDot,
		QDir::Name);
	for (const auto &subdir : subdirs) {
		const auto inner = QDir(subdir.absoluteFilePath()).entryInfoList(
			QDir::Files,
			QDir::Name);
		for (const auto &info : inner) {
			result.push_back(info.absoluteFilePath());
		}
	}
	return result;
}

Ui::PreparedFile PrepareFolderArchive(const QString &folder) {
	const auto path = QFileInfo(folder).absoluteFilePath();
	const auto root = ArchiveRootName(path);
	return ArchiveFile(root + u".zip"_q, {
		.folder = path,
		.root = root,
	});
}

Ui::PreparedFile PrepareFilesArchive(const QList<QUrl> &urls) {
	auto paths = QStringList();
	for (const auto &url : urls) {
		if (url.isLocalFile()) {
			paths.push_back(Platform::File::UrlToLocal(url));
		}
	}
	const auto parent = CommonParentName(paths);
	const auto name = parent.isEmpty() ? u"Archive"_q : parent;
	return ArchiveFile(name + u".zip"_q, {
		.paths = paths,
	});
}

std::optional<ArchiveEntries> GatherArchiveEntries(
		const Ui::PreparedFileArchive &job) {
	return job.folder.isEmpty()
		? GatherFiles(job.paths)
		: WalkFolder(job.folder, job.root);
}

int64 ArchiveSizeEstimate(const ArchiveEntries &entries) {
	auto result = kArchiveEntryOverhead;
	for (const auto &entry : entries.list) {
		result += entry.size + kArchiveEntryOverhead;
	}
	return result;
}

ArchiveWriteResult WriteArchive(
		ArchiveEntries &&entries,
		int64 limit,
		Fn<bool(float64)> progress) {
	auto state = WriteState{
		.progress = std::move(progress),
		.total = entries.total,
	};
	return WriteArchiveFile(entries.list, state, limit);
}

void ClearStaleArchiveFiles() {
	crl::async([] {
		const auto stale = QDateTime::currentDateTime().addSecs(
			-kStaleArchiveTimeout);
		const auto entries = QDir(ArchiveDirectory()).entryInfoList(
			QDir::Files | QDir::NoDotAndDotDot);
		for (const auto &entry : entries) {
			if (entry.lastModified() < stale) {
				QFile::remove(entry.absoluteFilePath());
			}
		}
	});
}

} // namespace Storage
