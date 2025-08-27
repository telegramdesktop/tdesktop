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
#include "history/history_item.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "chat_helpers/emoji_interactions.h"
#include "chat_helpers/stickers_lottie.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_message_reactions.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "base/random.h"
#include "ui/power_saving.h"
#include "ui/ui_utility.h"
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
	not_null<QWidget*> parent,
	not_null<QWidget*> layerParent,
	not_null<Main::Session*> session,
	Fn<int(not_null<const Element*>)> itemTop)
: _parent(parent)
, _layerParent(layerParent)
, _session(session)
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

	_session->data().reactions().effectsUpdates(
	) | rpl::start_with_next([=] {
		checkPendingEffects();
	}, _lifetime);
}

EmojiInteractions::~EmojiInteractions() = default;

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
			if (already) {
				_plays.erase(i);
			} else {
				i->view = view;
			}
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
					Stickers::EffectType::PremiumSticker);
			}
		}
	}
	return true;
}

void EmojiInteractions::cancelPremiumEffect(not_null<const Element*> view) {
	_plays.erase(ranges::remove_if(_plays, [&](const Play &play) {
		if (play.view != view) {
			return false;
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
		Stickers::EffectType::EmojiInteraction);
}

void EmojiInteractions::playEffectOnRead(not_null<const Element*> view) {
	const auto flag = PowerSaving::Flag::kChatEffects;
	if (view->data()->markEffectWatched() && !PowerSaving::On(flag)) {
		playEffect(view);
	}
}

void EmojiInteractions::playEffect(not_null<const Element*> view) {
	if (const auto resolved = resolveEffect(view)) {
		playEffect(view, resolved);
	} else if (view->data()->effectId()) {
		if (resolved.document && !_downloadLifetime) {
			_downloadLifetime = _session->downloaderTaskFinished(
			) | rpl::start_with_next([=] {
				checkPendingEffects();
			});
		}
		addPendingEffect(view);
	}
}

EmojiInteractions::ResolvedEffect EmojiInteractions::resolveEffect(
		not_null<const Element*> view) {
	const auto item = view->data();
	const auto effectId = item->effectId();
	if (!effectId) {
		return {};
	}
	using Type = Data::Reactions::Type;
	const auto &effects = _session->data().reactions().list(Type::Effects);
	const auto i = ranges::find(
		effects,
		Data::ReactionId{ effectId },
		&Data::Reaction::id);
	if (i == end(effects)) {
		return {};
	}
	auto document = (DocumentData*)nullptr;
	auto content = QByteArray();
	auto filepath = QString();
	if ((document = i->aroundAnimation)) {
		content = document->createMediaView()->bytes();
		filepath = document->filepath();
	} else {
		document = i->selectAnimation;
		content = document->createMediaView()->videoThumbnailContent();
	}
	return {
		.emoticon = i->title,
		.document = document,
		.content = content,
		.filepath = filepath,
	};
}

void EmojiInteractions::playEffect(
		not_null<const Element*> view,
		const ResolvedEffect &resolved) {
	play(
		resolved.emoticon,
		view,
		resolved.document,
		resolved.content,
		resolved.filepath,
		false,
		Stickers::EffectType::MessageEffect);
}

void EmojiInteractions::addPendingEffect(not_null<const Element*> view) {
	auto found = false;
	const auto predicate = [&](base::weak_ptr<const Element> weak) {
		const auto strong = weak.get();
		if (strong == view) {
			found = true;
		}
		return !strong;
	};
	_pendingEffects.erase(
		ranges::remove_if(_pendingEffects, predicate),
		end(_pendingEffects));
	if (!found) {
		_pendingEffects.push_back(view);
	}
}

void EmojiInteractions::checkPendingEffects() {
	auto waitingDownload = false;
	const auto predicate = [&](base::weak_ptr<const Element> weak) {
		const auto strong = weak.get();
		if (!strong) {
			return true;
		}
		const auto resolved = resolveEffect(strong);
		if (resolved) {
			playEffect(strong, resolved);
			return true;
		} else if (!strong->data()->effectId()) {
			return true;
		} else if (resolved.document) {
			waitingDownload = true;
		}
		return false;
	};
	_pendingEffects.erase(
		ranges::remove_if(_pendingEffects, predicate),
		end(_pendingEffects));
	if (!waitingDownload) {
		_downloadLifetime.destroy();
	} else if (!_downloadLifetime) {
		_downloadLifetime = _session->downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			checkPendingEffects();
		});
	}
}

void EmojiInteractions::play(
		QString emoticon,
		not_null<const Element*> view,
		not_null<DocumentData*> document,
		QByteArray data,
		QString filepath,
		bool incoming,
		Stickers::EffectType type) {
	const auto top = _itemTop(view);
	const auto bottom = top + view->height();
	if (_visibleTop >= bottom
		|| _visibleBottom <= top
		|| _visibleTop == _visibleBottom
		|| (data.isEmpty() && filepath.isEmpty())) {
		return;
	}

	if (!_layer) {
		_layer = base::make_unique_q<Ui::RpWidget>(_layerParent);
		const auto raw = _layer.get();
		raw->setAttribute(Qt::WA_TransparentForMouseEvents);
		raw->show();
		raw->paintRequest() | rpl::start_with_next([=](QRect clip) {
			paint(raw, clip);
		}, raw->lifetime());
	}
	refreshLayerShift();
	_layer->setGeometry(_layerParent->rect());

	auto lottie = document->session().emojiStickersPack().effectPlayer(
		document,
		data,
		filepath,
		type);

	const auto inner = (type == Stickers::EffectType::PremiumSticker)
		? HistoryView::Sticker::Size(document)
		: HistoryView::Sticker::EmojiSize();
	const auto shift = (type == Stickers::EffectType::EmojiInteraction)
		? GenerateRandomShift(inner)
		: QPoint();
	const auto raw = lottie.get();
	lottie->updates(
	) | rpl::start_with_next([=](Lottie::Update update) {
		v::match(update.data, [&](const Lottie::Information &information) {
		}, [&](const Lottie::DisplayFrameRequest &request) {
			const auto i = ranges::find(_plays, raw, [](const Play &p) {
				return p.lottie.get();
			});
			auto update = computeRect(*i).translated(shift + _layerShift);
			if (!i->lastTarget.isEmpty()) {
				update = i->lastTarget.united(update);
			}
			_layer->update(update);
			i->lastTarget = QRect();
		});
	}, lottie->lifetime());
	_plays.push_back({
		.view = view,
		.lottie = std::move(lottie),
		.shift = shift,
		.inner = inner,
		.outer = ((type == Stickers::EffectType::PremiumSticker)
			? HistoryView::Sticker::PremiumEffectSize(document)
			: (type == Stickers::EffectType::EmojiInteraction)
			? HistoryView::Sticker::EmojiEffectSize()
			: HistoryView::Sticker::MessageEffectSize()),
		.type = type,
	});
	if (incoming) {
		_playStarted.fire(std::move(emoticon));
	}
	if (const auto media = view->media()) {
		if (type == Stickers::EffectType::EmojiInteraction) {
			media->stickerClearLoopPlayed();
		}
	}
}

void EmojiInteractions::refreshLayerShift() {
	_layerShift = Ui::MapFrom(_layerParent, _parent, QPoint(0, 0));
}

void EmojiInteractions::refreshLayerGeometryAndUpdate(QRect rect) {
	if (!rect.isEmpty()) {
		_layer->update(rect.translated(_layerShift));
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
	const auto viewTop = _itemTop(view);
	if (viewTop < 0) {
		return QRect();
	}
	if (play.type == Stickers::EffectType::MessageEffect) {
		const auto icon = view->effectIconGeometry();
		if (icon.isEmpty()) {
			return QRect();
		}
		const auto size = play.outer;
		const auto shift = view->hasRightLayout()
			? (-size.width() / 3)
			: (size.width() / 3);
		return QRect(
			shift + icon.x() + (icon.width() - size.width()) / 2,
			viewTop + icon.y() + (icon.height() - size.height()) / 2,
			size.width(),
			size.height());
	}
	const auto sticker = play.inner;
	const auto size = play.outer;
	const auto shift = (play.type == Stickers::EffectType::PremiumSticker)
		? int(sticker.width() * kPremiumShift)
		: (size.width() / 40);
	const auto inner = view->innerGeometry();
	const auto rightAligned = view->hasRightLayout();
	const auto left = rightAligned
		? (inner.x() + inner.width() + shift - size.width())
		: (inner.x() - shift);
	const auto top = viewTop
		+ inner.y()
		+ (sticker.height() - size.height()) / 2;
	return QRect(QPoint(left, top), size).translated(play.shift);
}

void EmojiInteractions::paint(not_null<QWidget*> layer, QRect clip) {
	refreshLayerShift();

	const auto factor = style::DevicePixelRatio();
	const auto whole = layer->rect();

	auto p = QPainter(layer);

	auto updated = QRect();
	const auto addRect = [&](QRect rect) {
		if (updated.isEmpty()) {
			updated = rect;
		} else {
			updated = rect.united(updated);
		}
	};
	for (auto &play : _plays) {
		if (!play.lottie->ready()) {
			continue;
		}
		const auto target = computeRect(play).translated(_layerShift);
		if (!target.intersects(whole)) {
			play.finished = true;
			addRect(target);
			continue;
		} else if (!target.intersects(clip)) {
			continue;
		}
		auto request = Lottie::FrameRequest();
		request.box = play.outer * factor;
		const auto rightAligned = play.view->hasRightLayout();
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
		if (play.started && !play.frame) {
			play.finished = true;
			addRect(target);
			continue;
		} else if (play.frame > 0) {
			play.started = true;
		}
		p.drawImage(
			QRect(target.topLeft(), frame.image.size() / factor),
			frame.image);
		play.lottie->markFrameShown();
		play.lastTarget = target.translated(_layerShift);
	}
	_plays.erase(ranges::remove_if(_plays, [](const Play &play) {
		if (!play.finished) {
			return false;
		}
		return true;
	}), end(_plays));
	checkDelayed();

	if (_plays.empty()) {
		layer->hide();
		if (_layer.get() == layer) {
			crl::on_main([moved = std::move(_layer)] {});
		}
	} else if (!updated.isEmpty()) {
		const auto translated = updated.translated(_layerShift);
		if (translated.intersects(whole)) {
			_layer->update(translated);
		}
	}
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

rpl::producer<QString> EmojiInteractions::playStarted() const {
	return _playStarted.events();
}

} // namespace HistoryView
