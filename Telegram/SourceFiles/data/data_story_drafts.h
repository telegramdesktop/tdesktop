/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QJsonArray>

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct StoryDraftEdits {
	int angle = 0;
	bool flipped = false;
	crl::time from = 0;
	crl::time till = 0;
	crl::time cover = 0;
	QRect canvasMediaRect;
};

struct StoryDraftVideo {
	QRectF rect;
	float64 scale = 1.;
	float64 rotation = 0.;
	float64 volume = 1.;
};

struct StoryDraft {
	uint64 id = 0;
	TimeId date = 0;
	PeerId peer;
	QString mediaPath;
	QSize dimensions;
	crl::time duration = 0;
	bool video = false;
	TextWithTags caption;
	int period = 0;
	StoryDraftEdits edits;
	StoryDraftVideo videoPlacement;
	QString stillPath;
	QJsonArray widgets;
	QImage thumbnail;
};

struct StoryDraftMedia {
	QImage photo;
	QString videoPath;
	QByteArray videoContent;
};

class StoryDrafts final {
public:
	explicit StoryDrafts(not_null<Main::Session*> session);

	[[nodiscard]] const std::vector<StoryDraft> &list();
	[[nodiscard]] int count();
	[[nodiscard]] const StoryDraft *find(uint64 id);
	[[nodiscard]] rpl::producer<> changes() const;

	uint64 save(StoryDraft draft, const StoryDraftMedia &media);
	void remove(uint64 id);

private:
	void load();
	void write();
	[[nodiscard]] QString folder() const;
	[[nodiscard]] QString storeMedia(
		uint64 id,
		const StoryDraftMedia &media) const;
	[[nodiscard]] QString storeStill(uint64 id, const QImage &still) const;

	const not_null<Main::Session*> _session;
	std::vector<StoryDraft> _list;
	rpl::event_stream<> _changes;
	bool _loaded = false;

};

} // namespace Data
