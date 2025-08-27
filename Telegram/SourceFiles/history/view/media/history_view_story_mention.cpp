/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_story_mention.h"

#include "core/click_handler_types.h" // ClickHandlerContext
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_user.h"
#include "data/data_photo_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "editor/photo_editor_common.h"
#include "editor/photo_editor_layer_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/media/history_view_sticker_player_abstract.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/outline_segments.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/painter.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "settings/settings_information.h" // UpdatePhotoLocally
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kReadOutlineAlpha = 0.5;

} // namespace

StoryMention::StoryMention(
	not_null<Element*> parent,
	not_null<Data::Story*> story)
: _parent(parent)
, _story(story)
, _unread(story->owner().stories().isUnread(story) ? 1 : 0) {
}

StoryMention::~StoryMention() {
	if (_subscribed) {
		changeSubscribedTo(0);
		_parent->checkHeavyPart();
	}
}

int StoryMention::top() {
	return st::msgServiceGiftBoxButtonMargins.top();
}

QSize StoryMention::size() {
	return { st::msgServicePhotoWidth, st::msgServicePhotoWidth };
}

TextWithEntities StoryMention::title() {
	return {};
}

int StoryMention::buttonSkip() {
	return st::storyMentionButtonSkip;
}

rpl::producer<QString> StoryMention::button() {
	return tr::lng_action_story_mention_button();
}

TextWithEntities StoryMention::subtitle() {
	return _parent->data()->notificationText();
}

ClickHandlerPtr StoryMention::createViewLink() {
	const auto itemId = _parent->data()->fullId();
	return std::make_shared<LambdaClickHandler>(crl::guard(this, [=](
			ClickContext) {
		if (const auto photo = _story->photo()) {
			_parent->delegate()->elementOpenPhoto(photo, itemId);
		} else if (const auto video = _story->document()) {
			_parent->delegate()->elementOpenDocument(video, itemId);
		}
	}));
}

void StoryMention::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) {
	const auto showStory = _story->forbidsForward() ? 0 : 1;
	if (!_thumbnail || _thumbnailFromStory != showStory) {
		const auto item = _parent->data();
		const auto history = item->history();
		_thumbnail = showStory
			? Ui::MakeStoryThumbnail(_story)
			: Ui::MakeUserpicThumbnail(item->out()
				? history->session().user()
				: history->peer);
		_thumbnailFromStory = showStory;
		changeSubscribedTo(0);
	}
	if (changeSubscribedTo(1)) {
		_thumbnail->subscribeToUpdates([=] {
			_parent->data()->history()->owner().requestViewRepaint(_parent);
		});
	}

	const auto padding = (geometry.width() - st::storyMentionSize) / 2;
	const auto size = geometry.width() - 2 * padding;
	p.drawImage(
		geometry.topLeft() + QPoint(padding, padding),
		_thumbnail->image(size));

	const auto thumbnail = QRectF(geometry.marginsRemoved(
		QMargins(padding, padding, padding, padding)));
	const auto added = 0.5 * (_unread
		? st::storyMentionUnreadSkipTwice
		: st::storyMentionReadSkipTwice);
	const auto outline = thumbnail.marginsAdded(
		QMarginsF(added, added, added, added));
	if (_unread && _paletteVersion != style::PaletteVersion()) {
		_paletteVersion = style::PaletteVersion();
		_unreadBrush = QBrush(Ui::UnreadStoryOutlineGradient(outline));
	}
	auto readColor = context.st->msgServiceFg()->c;
	readColor.setAlphaF(std::min(1. * readColor.alphaF(), kReadOutlineAlpha));
	p.setPen(QPen(
		_unread ? _unreadBrush : QBrush(readColor),
		0.5 * (_unread
			? st::storyMentionUnreadStrokeTwice
			: st::storyMentionReadStrokeTwice)));
	p.setBrush(Qt::NoBrush);
	auto hq = PainterHighQualityEnabler(p);
	p.drawEllipse(outline);
}

void StoryMention::stickerClearLoopPlayed() {
}

std::unique_ptr<StickerPlayer> StoryMention::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return nullptr;
}

bool StoryMention::hasHeavyPart() {
	return _subscribed != 0;
}

void StoryMention::unloadHeavyPart() {
	if (changeSubscribedTo(0)) {
		_thumbnail->subscribeToUpdates(nullptr);
	}
}

bool StoryMention::changeSubscribedTo(uint32 value) {
	Expects(value == 0 || value == 1);

	if (_subscribed == value) {
		return false;
	}
	_subscribed = value;
	const auto stories = &_parent->history()->owner().stories();
	if (value) {
		_parent->history()->owner().registerHeavyViewPart(_parent);
		stories->registerPolling(_story, Data::Stories::Polling::Chat);
	} else {
		stories->unregisterPolling(_story, Data::Stories::Polling::Chat);
	}
	return true;
}

} // namespace HistoryView
