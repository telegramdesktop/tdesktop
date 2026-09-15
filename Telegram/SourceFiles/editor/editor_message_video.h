/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class DocumentData;
class FileLoader;
class HistoryItem;

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Editor {

struct VideoClipSource;

class MessageVideo final : public base::has_weak_ptr {
public:
	[[nodiscard]] static DocumentData *Find(not_null<HistoryItem*> item);

	explicit MessageVideo(not_null<HistoryItem*> item);
	~MessageVideo();

	[[nodiscard]] not_null<DocumentData*> document() const;
	void load();
	[[nodiscard]] bool loading() const;
	[[nodiscard]] float64 progress() const;
	[[nodiscard]] std::shared_ptr<VideoClipSource> source() const;
	[[nodiscard]] rpl::producer<> changes() const;

private:
	void waitForLoader();
	void readSource(const QString &path, const QByteArray &content);
	void fail();

	const not_null<DocumentData*> _document;
	const FullMsgId _itemId;
	std::shared_ptr<Data::DocumentMedia> _media;
	std::unique_ptr<FileLoader> _loader;
	std::shared_ptr<VideoClipSource> _source;
	QString _tempPath;
	bool _waiting = false;
	bool _reading = false;
	bool _failed = false;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;

};

} // namespace Editor
