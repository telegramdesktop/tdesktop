/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_emoji_interactions.h"

#include "history/view/history_view_element.h"
#include "history/view/media/history_view_sticker.h"
#include "history/history.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "chat_helpers/emoji_interactions.h"
#include "chat_helpers/stickers_lottie.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "base/random.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kPremiumShift = 21. / 240;
constexpr auto kMaxPlays = 5;
constexpr auto kMaxPlaysWithSmallDelay = 3;
constexpr auto kSmallDelay = crl::time(200);
constexpr auto kDropDelayedAfterDelay = crl::time(2000);

[[nodiscard]] QPoint GenerateRandomShift(QSize emoji) {
	// Random shift in [-0.08 ... 0.08] of animated emoji size.
	const auto maxShift = emoji * 2 / 25;
	return {
		base::RandomIndex(maxShift.width() * 2 + 1) - maxShift.width(),
		base::RandomIndex(maxShift.height() * 2 + 1) - maxShift.height(),
	};
}

} // namespace

EmojiInteractions::EmojiInteractions(
	not_null<Main::Session*> session,
	Fn<int(not_null<const Element*>)> itemTop)
: _session(session)
, _itemTop(std::move(itemTop)) {
	_session->data().viewRemoved(
	) | rpl::filter([=] {
		return !_plays.empty() || !_delayed.empty();
	}) | rpl::start_with_next([=](not_null<const Element*> view) {
		_plays.erase(ranges::remove(_plays, view, &Play::view), end(_plays));
		_delayed.erase(
			ranges::remove(_delayed, view, &Delayed::view),
			end(_delayed));
	}, _lifetime);
}

EmojiInteractions::~EmojiInteractions() {
	//for (const auto &play : _plays) {
	//	if (play.premium) {
	//		play.view->externalLottieProgressing(false);
	//	}
	//}
}

void EmojiInteractions::play(
		ChatHelpers::EmojiInteractionPlayRequest request,
		not_null<Element*> view) {
	if (!view->media()) {
		// Large emoji may be disabled.
		return;
	} else if (_plays.empty()) {
		play(
			std::move(request.emoticon),
			view,
			std::move(request.media),
			request.incoming);
	} else {
		const auto now = crl::now();
		_delayed.push_back({
			request.emoticon,
			view,
			std::move(request.media),
			now,
			request.incoming,
		});
		checkDelayed();
	}
}

bool EmojiInteractions::playPremiumEffect(
		not_null<const Element*> view,
		Element *replacing) {
	const auto already = ranges::contains(_plays, view, &Play::view);
	if (replacing) {
		const auto i = ranges::find(_plays, replacing, &Play::view);
		if (i != end(_plays)) {
			//if (i->premium) {
			//	replacing->externalLottieProgressing(false);
			//}
			if (already) {
				_plays.erase(i);
			} else {
				i->view = view;
			}
			//if (i->premium) {
			//	view->externalLottieProgressing(true);
			//}
			return true;
		}
	} else if (already) {
		return false;
	}
	if (const auto media = view->media()) {
		if (const auto document = media->getDocument()) {
			if (document->isPremiumSticker()) {
				play(
					QString(),
					view,
					document,
					document->createMediaView()->videoThumbnailContent(),
					QString(),
					false,
					true);
			}
		}
	}
	return true;
}

void EmojiInteractions::cancelPremiumEffect(not_null<const Element*> view) {
	_plays.erase(ranges::remove_if(_plays, [&](const Play &play) {
		if (play.view != view) {
			return false;
		//} else if (play.premium) {
		//	play.view->externalLottieProgressing(false);
		}
		return true;
	}), end(_plays));
}

void EmojiInteractions::play(
		QString emoticon,
		not_null<const Element*> view,
		std::shared_ptr<Data::DocumentMedia> media,
		bool incoming) {
	play(
		std::move(emoticon),
		view,
		media->owner(),
		media->bytes(),
		media->owner()->filepath(),
		incoming,
		false);
}

void EmojiInteractions::play(
		QString emoticon,
		not_null<const Element*> view,
		not_null<DocumentData*> document,
		QByteArray data,
		QString filepath,
		bool incoming,
		bool premium) {
	const auto top = _itemTop(view);
	const auto bottom = top + view->height();
	if (_visibleTop >= bottom
		|| _visibleBottom <= top
		|| _visibleTop == _visibleBottom
		|| (data.isEmpty() && filepath.isEmpty())) {
		return;
	}

	auto lottie = document->session().emojiStickersPack().effectPlayer(
		document,
		data,
		filepath,
		premium);

	const auto inner = premium
		? HistoryView::Sticker::Size(document)
		: HistoryView::Sticker::EmojiSize();
	const auto shift = premium ? QPoint() : GenerateRandomShift(inner);
	const auto raw = lottie.get();
	lottie->updates(
	) | rpl::start_with_next([=](Lottie::Update update) {
		v::match(update.data, [&](const Lottie::Information &information) {
		}, [&](const Lottie::DisplayFrameRequest &request) {
			const auto i = ranges::find(_plays, raw, [](const Play &p) {
				return p.lottie.get();
			});
			const auto rect = computeRect(*i).translated(shift);
			if (rect.y() + rect.height() >= _visibleTop
				&& rect.y() <= _visibleBottom) {
				_updateRequests.fire_copy(rect);
			}
		});
	}, lottie->lifetime());
	//if (premium) {
	//	view->externalLottieProgressing(true);
	//}
	_plays.push_back({
		.view = view,
		.lottie = std::move(lottie),
		.shift = shift,
		.inner = inner,
		.outer = (premium
			? HistoryView::Sticker::PremiumEffectSize(document)
			: HistoryView::Sticker::EmojiEffectSize()),
		.premium = premium,
	});
	if (incoming) {
		_playStarted.fire(std::move(emoticon));
	}
	if (const auto media = view->media()) {
		if (!premium) {
			media->stickerClearLoopPlayed();
		}
	}
}

void EmojiInteractions::visibleAreaUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
}

QRect EmojiInteractions::computeRect(const Play &play) const {
	const auto view = play.view;
	const auto sticker = play.inner;
	const auto size = play.outer;
	const auto shift = play.premium
		? int(sticker.width() * kPremiumShift)
		: (size.width() / 40);
	const auto inner = view->innerGeometry();
	const auto rightAligned = view->hasOutLayout()
		&& !view->delegate()->elementIsChatWide();
	const auto left = rightAligned
		? (inner.x() + inner.width() + shift - size.width())
		: (inner.x() - shift);
	const auto viewTop = _itemTop(view) + inner.y();
	if (viewTop < 0) {
		return QRect();
	}
	const auto top = viewTop + (sticker.height() - size.height()) / 2;
	return QRect(QPoint(left, top), size).translated(play.shift);
}

void EmojiInteractions::paint(QPainter &p) {
	const auto factor = style::DevicePixelRatio();
	for (auto &play : _plays) {
		if (!play.lottie->ready()) {
			continue;
		}
		auto request = Lottie::FrameRequest();
		request.box = play.outer * factor;
		const auto rightAligned = play.view->hasOutLayout()
			&& !play.view->delegate()->elementIsChatWide();
		if (!rightAligned) {
			request.mirrorHorizontal = true;
		}
		const auto frame = play.lottie->frameInfo(request);
		play.frame = frame.index;
		if (!play.framesCount) {
			const auto &information = play.lottie->information();
			play.framesCount = information.framesCount;
			play.frameRate = information.frameRate;
		}
		const auto rect = computeRect(play);
		if (play.started && !play.frame) {
			play.finished = true;
			_updateRequests.fire_copy(rect);
			continue;
		} else if (play.frame > 0) {
			play.started = true;
		}
		p.drawImage(
			QRect(rect.topLeft(), frame.image.size() / factor),
			frame.image);
		//const auto info = HistoryView::ExternalLottieInfo{
		//	.frame = frame.index,
		//	.count = play.framesCount,
		//};
		//if (!play.premium || play.view->externalLottieTill(info)) {
			play.lottie->markFrameShown();
		//}
	}
	_plays.erase(ranges::remove_if(_plays, [](const Play &play) {
		if (!play.finished) {
			return false;
		//} else if (play.premium) {
		//	play.view->externalLottieProgressing(false);
		}
		return true;
	}), end(_plays));
	checkDelayed();
}

void EmojiInteractions::checkDelayed() {
	if (_delayed.empty() || _plays.size() >= kMaxPlays) {
		return;
	}
	auto withTooLittleDelay = false;
	auto withHalfPlayed = false;
	for (const auto &play : _plays) {
		if (!play.framesCount
			|| !play.frameRate
			|| !play.frame
			|| (play.frame * crl::time(1000)
				< kSmallDelay * play.frameRate)) {
			withTooLittleDelay = true;
			break;
		} else if (play.frame * 2 > play.framesCount) {
			withHalfPlayed = true;
		}
	}
	if (withTooLittleDelay) {
		return;
	} else if (_plays.size() >= kMaxPlaysWithSmallDelay && !withHalfPlayed) {
		return;
	}
	const auto now = crl::now();
	const auto i = ranges::find_if(_delayed, [&](const Delayed &delayed) {
		return (delayed.shouldHaveStartedAt + kDropDelayedAfterDelay > now);
	});
	if (i == end(_delayed)) {
		_delayed.clear();
		return;
	}
	auto good = std::move(*i);
	_delayed.erase(begin(_delayed), i + 1);
	play(
		std::move(good.emoticon),
		good.view,
		std::move(good.media),
		good.incoming);
}

rpl::producer<QRect> EmojiInteractions::updateRequests() const {
	return _updateRequests.events();
}

rpl::producer<QString> EmojiInteractions::playStarted() const {
	return _playStarted.events();
}

} // namespace HistoryView
