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

constexpr auto kSizeMultiplier = 3;

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

EmojiInteractions::EmojiInteractions(not_null<Main::Session*> session)
: _session(session) {
	_session->data().viewRemoved(
	) | rpl::filter([=] {
		return !_plays.empty();
	}) | rpl::start_with_next([=](not_null<const Element*> view) {
		_plays.erase(ranges::remove(_plays, view, &Play::view), end(_plays));
	}, _lifetime);

	_emojiSize = Sticker::EmojiSize();
}

EmojiInteractions::~EmojiInteractions() = default;

void EmojiInteractions::play(
		ChatHelpers::EmojiInteractionPlayRequest request,
		not_null<Element*> view) {
	if (_plays.empty()) {
		play(view, std::move(request.media));
	} else {
		_delayed.push_back({ view, request.media, crl::now() });
		checkDelayed();
	}
}

void EmojiInteractions::play(
		not_null<Element*> view,
		std::shared_ptr<Data::DocumentMedia> media) {
	const auto top = view->block()->y() + view->y();
	const auto bottom = top + view->height();
	if (_visibleTop >= bottom
		|| _visibleBottom <= top
		|| _visibleTop == _visibleBottom) {
		return;
	}
	auto lottie = ChatHelpers::LottiePlayerFromDocument(
		media.get(),
		nullptr,
		ChatHelpers::StickerLottieSize::EmojiInteraction,
		_emojiSize * kSizeMultiplier * style::DevicePixelRatio(),
		Lottie::Quality::High);
	const auto shift = GenerateRandomShift(_emojiSize);
	lottie->updates(
	) | rpl::start_with_next([=](Lottie::Update update) {
		v::match(update.data, [&](const Lottie::Information &information) {
		}, [&](const Lottie::DisplayFrameRequest &request) {
			const auto rect = computeRect(view).translated(shift);
			if (rect.y() + rect.height() >= _visibleTop
				&& rect.y() <= _visibleBottom) {
				_updateRequests.fire_copy(rect);
			}
		});
	}, lottie->lifetime());
	_plays.push_back({
		.view = view,
		.lottie = std::move(lottie),
		.shift = shift,
	});
}

void EmojiInteractions::visibleAreaUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
}

QRect EmojiInteractions::computeRect(not_null<Element*> view) const {
	const auto fullWidth = view->width();
	const auto shift = (_emojiSize.width() * kSizeMultiplier) / 40;
	const auto skip = (view->hasFromPhoto() ? st::msgPhotoSkip : 0)
		+ st::msgMargin.left();
	const auto rightAligned = view->hasOutLayout()
		&& !view->delegate()->elementIsChatWide();
	const auto left = rightAligned
		? (fullWidth - skip + shift - _emojiSize.width() * kSizeMultiplier)
		: (skip - shift);
	const auto viewTop = view->block()->y() + view->y() + view->marginTop();
	const auto top = viewTop - _emojiSize.height();
	return QRect(QPoint(left, top), _emojiSize * kSizeMultiplier);
}

void EmojiInteractions::paint(QPainter &p) {
	const auto factor = style::DevicePixelRatio();
	for (auto &play : _plays) {
		if (!play.lottie->ready()) {
			continue;
		}
		auto request = Lottie::FrameRequest();
		request.box = _emojiSize * kSizeMultiplier * factor;
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
		if (play.frame + 1 == play.framesCount) {
			play.finished = true;
		}
		const auto rect = computeRect(play.view);
		p.drawImage(
			QRect(rect.topLeft() + play.shift, frame.image.size() / factor),
			frame.image);
		play.lottie->markFrameShown();
	}
	_plays.erase(ranges::remove(_plays, true, &Play::finished), end(_plays));
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
	play(good.view, std::move(good.media));
}

rpl::producer<QRect> EmojiInteractions::updateRequests() const {
	return _updateRequests.events();
}

} // namespace HistoryView
