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

namespace Dialogs::Stories {
class Thumbnail;
} // namespace Dialogs::Stories

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
	QString button() override;
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
	using Thumbnail = Dialogs::Stories::Thumbnail;

	const not_null<Element*> _parent;
	const not_null<Data::Story*> _story;
	std::shared_ptr<Thumbnail> _thumbnail;
	bool _thumbnailFromStory = false;
	bool _subscribed = false;

};

} // namespace HistoryView
