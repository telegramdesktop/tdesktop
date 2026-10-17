/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
struct PreparedFile;
struct PreparedFileArchive;
} // namespace Ui

namespace Storage {

struct ArchiveEntry {
	QString absolute;
	QString name;
	QDateTime modified;
	int64 size = 0;
	bool directory = false;
};

struct ArchiveEntries {
	std::vector<ArchiveEntry> list;
	int64 total = 0;
};

struct ArchiveWriteResult {
	enum class Status {
		Done,
		TooLarge,
		Failed,
		Cancelled,
	};

	Status status = Status::Failed;
	QString path;
	int64 size = 0;
};

[[nodiscard]] QString SingleFolderPath(const QList<QUrl> &urls);
[[nodiscard]] QStringList FolderFilesForSending(const QString &folder);

[[nodiscard]] Ui::PreparedFile PrepareFolderArchive(const QString &folder);
[[nodiscard]] Ui::PreparedFile PrepareFilesArchive(const QList<QUrl> &urls);

[[nodiscard]] std::optional<ArchiveEntries> GatherArchiveEntries(
	const Ui::PreparedFileArchive &job);
[[nodiscard]] int64 ArchiveSizeEstimate(const ArchiveEntries &entries);
[[nodiscard]] ArchiveWriteResult WriteArchive(
	ArchiveEntries &&entries,
	int64 limit,
	Fn<bool(float64)> progress);

void ClearStaleArchiveFiles();

} // namespace Storage
