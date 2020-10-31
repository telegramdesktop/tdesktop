/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/file_bookmark_mac.h"

namespace Platform {
namespace {

#ifdef OS_MAC_STORE
QMutex BookmarksMutex;
#endif // OS_MAC_STORE

} // namespace

#ifdef OS_MAC_STORE
struct FileBookmark::Data {
	~Data() {
		if (url) [url release];
	}
	NSURL *url = nil;
	QString name;
	QByteArray bookmark;
	int counter = 0;
};
#endif // OS_MAC_STORE

FileBookmark::FileBookmark(const QByteArray &bookmark) {
#ifdef OS_MAC_STORE
	if (bookmark.isEmpty()) return;

	BOOL isStale = NO;
	NSError *error = nil;
	NSURL *url = [NSURL URLByResolvingBookmarkData:bookmark.toNSData() options:NSURLBookmarkResolutionWithSecurityScope relativeToURL:nil bookmarkDataIsStale:&isStale error:&error];
	if (!url) return;

	if ([url startAccessingSecurityScopedResource]) {
		data = new Data();
		data->url = [url retain];
		data->name = NS2QString([url path]);
		data->bookmark = bookmark;
		[url stopAccessingSecurityScopedResource];
	}
#endif // OS_MAC_STORE
}

bool FileBookmark::check() const {
	if (enable()) {
		disable();
		return true;
	}
	return false;
}

bool FileBookmark::enable() const {
#ifndef OS_MAC_STORE
	return true;
#else // OS_MAC_STORE
	if (!data) return false;

	QMutexLocker lock(&_bookmarksMutex);
	if (data->counter > 0 || [data->url startAccessingSecurityScopedResource] == YES) {
		++data->counter;
		return true;
	}
	return false;
#endif // OS_MAC_STORE
}

void FileBookmark::disable() const {
#ifdef OS_MAC_STORE
	if (!data) return;

	QMutexLocker lock(&_bookmarksMutex);
	if (data->counter > 0) {
		--data->counter;
		if (!data->counter) {
			[data->url stopAccessingSecurityScopedResource];
		}
	}
#endif // OS_MAC_STORE
}

const QString &FileBookmark::name(const QString &original) const {
#ifndef OS_MAC_STORE
	return original;
#else // OS_MAC_STORE
	return (data && !data->name.isEmpty()) ? data->name : original;
#endif // OS_MAC_STORE
}

QByteArray FileBookmark::bookmark() const {
#ifndef OS_MAC_STORE
	return QByteArray();
#else // OS_MAC_STORE
	return data ? data->bookmark : QByteArray();
#endif // OS_MAC_STORE
}

FileBookmark::~FileBookmark() {
#ifdef OS_MAC_STORE
	if (data && data->counter > 0) {
		LOG(("Did not disable() bookmark, counter: %1").arg(data->counter));
		[data->url stopAccessingSecurityScopedResource];
	}
#endif // OS_MAC_STORE
}

QByteArray PathBookmark(const QString &path) {
#ifndef OS_MAC_STORE
	return QByteArray();
#else // OS_MAC_STORE
	NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.toUtf8().constData()]];
	if (!url) return QByteArray();

	NSError *error = nil;
	NSData *data = [url bookmarkDataWithOptions:(NSURLBookmarkCreationWithSecurityScope | NSURLBookmarkCreationSecurityScopeAllowOnlyReadAccess) includingResourceValuesForKeys:nil relativeToURL:nil error:&error];
	return data ? QByteArray::fromNSData(data) : QByteArray();
#endif // OS_MAC_STORE
}

} // namespace Platform
