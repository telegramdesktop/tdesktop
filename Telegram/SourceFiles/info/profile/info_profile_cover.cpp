/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_cover.h"

#include "main/main_session.h"
#include "data/data_forum_topic.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/view/media/history_view_sticker_player.h"
#include "info/profile/info_profile_values.h"
#include "chat_helpers/stickers_lottie.h"
#include "window/window_session_controller.h"
#include "ui/painter.h"
#include "styles/style_info.h"
#include "styles/style_dialogs.h"

namespace Info::Profile {

QMargins LargeCustomEmojiMargins() {
	const auto ratio = style::DevicePixelRatio();
	const auto emoji = Ui::Emoji::GetSizeLarge() / ratio;
	const auto size = Data::FrameSizeFromTag(Data::CustomEmojiSizeTag::Large)
		/ ratio;
	const auto left = (size - emoji) / 2;
	const auto right = size - emoji - left;
	return { left, left, right, right };
}

TopicIconView::TopicIconView(
	not_null<Data::ForumTopic*> topic,
	Fn<bool()> paused,
	Fn<void()> update)
: TopicIconView(
	topic,
	std::move(paused),
	std::move(update),
	st::windowSubTextFg) {
}

TopicIconView::TopicIconView(
	not_null<Data::ForumTopic*> topic,
	Fn<bool()> paused,
	Fn<void()> update,
	const style::color &generalIconFg)
: _topic(topic)
, _generalIconFg(generalIconFg)
, _paused(std::move(paused))
, _update(std::move(update)) {
	setup(topic);
}

void TopicIconView::paintInRect(QPainter &p, QRect rect, QColor textColor) {
	const auto paint = [&](const QImage &image) {
		const auto size = image.size() / style::DevicePixelRatio();
		p.drawImage(
			QRect(
				rect.x() + (rect.width() - size.width()) / 2,
				rect.y() + (rect.height() - size.height()) / 2,
				size.width(),
				size.height()),
			image);
	};
	if (_player && _player->ready()) {
		const auto colored = _playerUsesTextColor
			? (textColor.alpha() > 0)
				? textColor
				: st::windowFg->c
			: QColor(0, 0, 0, 0);
		paint(_player->frame(
			st::infoTopicCover.photo.size,
			colored,
			false,
			crl::now(),
			_paused()).image);
		_player->markFrameShown();
	} else if (!_topic->iconId() && !_image.isNull()) {
		paint(_image);
	}
}

void TopicIconView::setup(not_null<Data::ForumTopic*> topic) {
	setupPlayer(topic);
	setupImage(topic);
}

void TopicIconView::setupPlayer(not_null<Data::ForumTopic*> topic) {
	IconIdValue(
		topic
	) | rpl::map([=](DocumentId id) -> rpl::producer<DocumentData*> {
		if (!id) {
			return rpl::single((DocumentData*)nullptr);
		}
		return topic->owner().customEmojiManager().resolve(
			id
		) | rpl::map([=](not_null<DocumentData*> document) {
			return document.get();
		}) | rpl::map_error_to_done();
	}) | rpl::flatten_latest(
	) | rpl::map([=](DocumentData *document)
	-> rpl::producer<std::shared_ptr<StickerPlayer>> {
		if (!document) {
			return rpl::single(std::shared_ptr<StickerPlayer>());
		}
		const auto media = document->createMediaView();
		media->checkStickerLarge();
		media->goodThumbnailWanted();

		return rpl::single() | rpl::then(
			document->session().downloaderTaskFinished()
		) | rpl::filter([=] {
			return media->loaded();
		}) | rpl::take(1) | rpl::map([=] {
			auto result = std::shared_ptr<StickerPlayer>();
			const auto sticker = document->sticker();
			if (sticker->isLottie()) {
				result = std::make_shared<HistoryView::LottiePlayer>(
					ChatHelpers::LottiePlayerFromDocument(
						media.get(),
						ChatHelpers::StickerLottieSize::StickerSet,
						st::infoTopicCover.photo.size,
						Lottie::Quality::High));
			} else if (sticker->isWebm()) {
				result = std::make_shared<HistoryView::WebmPlayer>(
					media->owner()->location(),
					media->bytes(),
					st::infoTopicCover.photo.size);
			} else {
				result = std::make_shared<HistoryView::StaticStickerPlayer>(
					media->owner()->location(),
					media->bytes(),
					st::infoTopicCover.photo.size);
			}
			result->setRepaintCallback(_update);
			_playerUsesTextColor = media->owner()->emojiUsesTextColor();
			return result;
		});
	}) | rpl::flatten_latest(
	) | rpl::on_next([=](std::shared_ptr<StickerPlayer> player) {
		_player = std::move(player);
		if (!_player) {
			_update();
		}
	}, _lifetime);
}

void TopicIconView::setupImage(not_null<Data::ForumTopic*> topic) {
	using namespace Data;
	if (topic->isGeneral()) {
		rpl::single(rpl::empty) | rpl::then(
			style::PaletteChanged()
		) | rpl::on_next([=] {
			_image = ForumTopicGeneralIconFrame(
				st::infoForumTopicIcon.size,
				_generalIconFg->c);
			_update();
		}, _lifetime);
		return;
	}
	rpl::combine(
		TitleValue(topic),
		ColorIdValue(topic)
	) | rpl::map([=](const QString &title, int32 colorId) {
		return ForumTopicIconFrame(colorId, title, st::infoForumTopicIcon);
	}) | rpl::on_next([=](QImage &&image) {
		_image = std::move(image);
		_update();
	}, _lifetime);
}

TopicIconButton::TopicIconButton(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Data::ForumTopic*> topic)
: TopicIconButton(parent, topic, [=] {
	return controller->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
}) {
}

TopicIconButton::TopicIconButton(
	QWidget *parent,
	not_null<Data::ForumTopic*> topic,
	Fn<bool()> paused)
: AbstractButton(parent)
, _view(topic, paused, [=] { update(); }) {
	resize(st::infoTopicCover.photo.size);
	paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(this);
		_view.paintInRect(p, rect());
	}, lifetime());
}

} // namespace Info::Profile
