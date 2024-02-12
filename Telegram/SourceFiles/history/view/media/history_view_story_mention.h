/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_media_unwrapped.h"
#include "history/view/media/history_view_service_box.h"

namespace Data {
class Story;
} // namespace Data

namespace Ui {
class DynamicImage;
} // namespace Ui

namespace HistoryView {

class StoryMention final
	: public ServiceBoxContent
	, public base::has_weak_ptr {
public:
	StoryMention(not_null<Element*> parent, not_null<Data::Story*> story);
	~StoryMention();

	int top() override;
	QSize size() override;
	QString title() override;
	TextWithEntities subtitle() override;
	int buttonSkip() override;
	rpl::producer<QString> button() override;
	void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) override;
	ClickHandlerPtr createViewLink() override;

	bool hideServiceText() override {
		return true;
	}

	void stickerClearLoopPlayed() override;
	std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	bool hasHeavyPart() override;
	void unloadHeavyPart() override;

private:
	bool changeSubscribedTo(uint32 value);

	const not_null<Element*> _parent;
	const not_null<Data::Story*> _story;
	std::shared_ptr<Ui::DynamicImage> _thumbnail;
	QBrush _unreadBrush;
	uint32 _paletteVersion : 29 = 0;
	uint32 _thumbnailFromStory : 1 = 0;
	uint32 _subscribed : 1 = 0;
	uint32 _unread : 1 = 0;

};

} // namespace HistoryView
