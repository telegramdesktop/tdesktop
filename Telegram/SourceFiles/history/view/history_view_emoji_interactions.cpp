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

constexpr auto kEmojiMultiplier = 3;
constexpr auto kPremiumMultiplier = 2.25;
constexpr auto kEmojiCachesCount = 4;
constexpr auto kPremiumCachesCount = 8;
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

	_emojiSize = Sticker::EmojiSize();
	_premiumSize = Sticker::Size();
}

EmojiInteractions::~EmojiInteractions() {
	for (const auto &play : _plays) {
		if (play.premium) {
			play.view->externalLottieProgressing(false);
		}
	}
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

void EmojiInteractions::playPremiumEffect(
		not_null<const Element*> view,
		Element *replacing) {
	if (replacing) {
		const auto i = ranges::find(_plays, replacing, &Play::view);
		if (i != end(_plays)) {
			if (i->premium) {
				replacing->externalLottieProgressing(false);
			}
			i->view = view;
			if (i->premium) {
				view->externalLottieProgressing(true);
			}
			return;
		}
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
}

void EmojiInteractions::cancelPremiumEffect(not_null<const Element*> view) {
	_plays.erase(ranges::remove_if(_plays, [&](const Play &play) {
		if (play.view != view) {
			return false;
		} else if (play.premium) {
			play.view->externalLottieProgressing(false);
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

	auto lottie = preparePlayer(document, data, filepath, premium);

	const auto shift = premium ? QPoint() : GenerateRandomShift(_emojiSize);
	const auto raw = lottie.get();
	lottie->updates(
	) | rpl::start_with_next([=](Lottie::Update update) {
		v::match(update.data, [&](const Lottie::Information &information) {
		}, [&](const Lottie::DisplayFrameRequest &request) {
			const auto i = ranges::find(_plays, raw, [](const Play &p) {
				return p.lottie.get();
			});
			const auto rect = computeRect(
				i->view,
				i->premium).translated(shift);
			if (rect.y() + rect.height() >= _visibleTop
				&& rect.y() <= _visibleBottom) {
				_updateRequests.fire_copy(rect);
			}
		});
	}, lottie->lifetime());
	if (premium) {
		view->externalLottieProgressing(true);
	}
	_plays.push_back({
		.view = view,
		.lottie = std::move(lottie),
		.shift = shift,
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

QSize EmojiInteractions::sizeFor(bool premium) const {
	return premium
		? (_premiumSize * kPremiumMultiplier)
		: (_emojiSize * kEmojiMultiplier);
}

std::unique_ptr<Lottie::SinglePlayer> EmojiInteractions::preparePlayer(
		not_null<DocumentData*> document,
		QByteArray data,
		QString filepath,
		bool premium) {
	// Shortened copy from stickers_lottie module.
	const auto baseKey = document->bigFileBaseCacheKey();
	const auto tag = uint8(0);
	const auto keyShift = ((tag << 4) & 0xF0)
		| (uint8(ChatHelpers::StickerLottieSize::EmojiInteraction) & 0x0F);
	const auto key = Storage::Cache::Key{
		baseKey.high,
		baseKey.low + keyShift
	};
	const auto get = [=](int i, FnMut<void(QByteArray &&cached)> handler) {
		document->owner().cacheBigFile().get(
			{ key.high, key.low + i },
			std::move(handler));
	};
	const auto weak = base::make_weak(&document->session());
	const auto put = [=](int i, QByteArray &&cached) {
		crl::on_main(weak, [=, data = std::move(cached)]() mutable {
			weak->data().cacheBigFile().put(
				{ key.high, key.low + i },
				std::move(data));
		});
	};
	const auto request = Lottie::FrameRequest{
		sizeFor(premium) * style::DevicePixelRatio(),
	};
	auto &weakProvider = _sharedProviders[document];
	auto shared = [&] {
		if (const auto result = weakProvider.lock()) {
			return result;
		}
		const auto result = Lottie::SinglePlayer::SharedProvider(
			premium ? kPremiumCachesCount : kEmojiCachesCount,
			get,
			put,
			Lottie::ReadContent(data, filepath),
			request,
			Lottie::Quality::High);
		weakProvider = result;
		return result;
	}();
	return std::make_unique<Lottie::SinglePlayer>(std::move(shared), request);
}

void EmojiInteractions::visibleAreaUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
}

QRect EmojiInteractions::computeRect(
		not_null<const Element*> view,
		bool premium) const {
	const auto fullWidth = view->width();
	const auto sticker = premium ? _premiumSize : _emojiSize;
	const auto size = sizeFor(premium);
	const auto shift = size.width() / 40;
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
	return QRect(QPoint(left, top), size);
}

void EmojiInteractions::paint(QPainter &p) {
	const auto factor = style::DevicePixelRatio();
	for (auto &play : _plays) {
		if (!play.lottie->ready()) {
			continue;
		}
		auto request = Lottie::FrameRequest();
		request.box = sizeFor(play.premium) * factor;
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
		const auto rect = computeRect(
			play.view,
			play.premium).translated(play.shift);
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
		if (!play.premium || play.view->externalLottieTill(frame.index)) {
			play.lottie->markFrameShown();
		}
	}
	_plays.erase(ranges::remove_if(_plays, [](const Play &play) {
		if (!play.finished) {
			return false;
		} else if (play.premium) {
			play.view->externalLottieProgressing(false);
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
