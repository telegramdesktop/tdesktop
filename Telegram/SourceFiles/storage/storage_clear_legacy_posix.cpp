/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_clear_legacy.h"

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

namespace Storage {
namespace details {

std::vector<QString> CollectFiles(
		const QString &base,
		size_type limit,
		const base::flat_set<QString> &skip) {
	Expects(base.endsWith('/'));
	Expects(limit > 0);

	const auto path = QFile::encodeName(base);
	const auto folder = path.mid(0, path.size() - 1);
	const auto directory = opendir(folder.constData());
	if (!directory) {
		return {};
	}
	const auto guard = gsl::finally([&] { closedir(directory); });

	auto result = std::vector<QString>();
	while (const auto entry = readdir(directory)) {
		const auto local = entry->d_name;
		if (!strcmp(local, ".") || !strcmp(local, "..")) {
			continue;
		}

		const auto full = path + QByteArray(local);
		const auto data = full.constData();
		struct stat statbuf = { 0 };
		if (stat(full.constData(), &statbuf) != 0 || S_ISDIR(statbuf.st_mode)) {
			continue;
		}

		auto name = QFile::decodeName(local);
		if (!skip.contains(name)) {
			result.push_back(std::move(name));
		}
		if (result.size() == limit) {
			break;
		}
	}
	return result;

// // It looks like POSIX solution works fine on macOS so no need for Cocoa solution.
//
//	NSString *native = [NSString stringWithUTF8String:utf8.constData()];
//	NSFileManager *manager = [NSFileManager defaultManager];
//	NSArray *properties = [NSArray arrayWithObject:NSURLIsDirectoryKey];
//	NSDirectoryEnumerator *enumerator = [manager
//		enumeratorAtURL:[NSURL fileURLWithPath:native]
//		includingPropertiesForKeys:properties
//		options:0
//		errorHandler:^(NSURL *url, NSError *error) {
//			return NO;
//		}];
//
//	auto result = std::vector<QString>();
//	for (NSURL *url in enumerator) {
//		NSNumber *isDirectory = nil;
//		NSError *error = nil;
//		if (![url getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:&error]) {
//			break;
//		} else if ([isDirectory boolValue]) {
//			continue;
//		}
//		NSString *full = [url path];
//		NSRange r = [full rangeOfString:native];
//		if (r.location != 0) {
//			break;
//		}
//		NSString *file = [full substringFromIndex:r.length + 1];
//		auto name = QString::fromUtf8([file cStringUsingEncoding:NSUTF8StringEncoding]);
//		if (!skip.contains(name)) {
//			result.push_back(std::move(name));
//		}
//		if (result.size() == limit) {
//			break;
//		}
//	}
//	return result;
}

bool RemoveLegacyFile(const QString &path) {
	const auto native = QFile::encodeName(path);
	return unlink(native.constData()) == 0;
}

} // namespace details
} // namespace Storage
