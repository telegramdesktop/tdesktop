/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/reactions/history_view_reactions_animation.h"

#include "history/view/history_view_element.h"
#include "history/view/history_view_bottom_info.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/animated_icon.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kFlyDuration = crl::time(300);

} // namespace

Animation::Animation(
	not_null<::Data::Reactions*> owner,
	ReactionAnimationArgs &&args,
	Fn<void()> repaint,
	int size)
: _owner(owner)
, _repaint(std::move(repaint))
, _flyFrom(args.flyFrom) {
	const auto &list = owner->list(::Data::Reactions::Type::All);
	auto centerIcon = (DocumentData*)nullptr;
	auto centerIconSize = size;
	auto aroundAnimation = (DocumentData*)nullptr;
	if (const auto customId = args.id.custom()) {
		const auto document = owner->owner().document(customId);
		if (document->sticker()) {
			centerIcon = document;
			centerIconSize = Ui::Text::AdjustCustomEmojiSize(st::emojiSize);
		}
	} else {
		const auto i = ranges::find(list, args.id, &::Data::Reaction::id);
		if (i == end(list) || !i->centerIcon) {
			return;
		}
		centerIcon = i->centerIcon;
		aroundAnimation = i->aroundAnimation;
	}
	const auto resolve = [&](
			std::unique_ptr<Ui::AnimatedIcon> &icon,
			DocumentData *document,
			int size) {
		if (!document) {
			return false;
		}
		const auto media = document->activeMediaView();
		if (!media || !media->loaded()) {
			return false;
		}
		icon = Ui::MakeAnimatedIcon({
			.generator = DocumentIconFrameGenerator(media),
			.sizeOverride = QSize(size, size),
		});
		return true;
	};
	_flyIcon = std::move(args.flyIcon);
	_centerSizeMultiplier = centerIconSize / float64(size);
	if (!resolve(_center, centerIcon, centerIconSize)) {
		return;
	}
	resolve(_effect, aroundAnimation, size * 2);
	if (!_flyIcon.isNull()) {
		_fly.start([=] { flyCallback(); }, 0., 1., kFlyDuration);
	} else {
		startAnimations();
	}
	_valid = true;
}

Animation::~Animation() = default;

QRect Animation::paintGetArea(
		QPainter &p,
		QPoint origin,
		QRect target) const {
	if (_flyIcon.isNull()) {
		paintCenterFrame(p, target);
		const auto wide = QRect(
			target.topLeft() - QPoint(target.width(), target.height()) / 2,
			target.size() * 2);
		if (const auto effect = _effect.get()) {
			p.drawImage(wide, effect->frame());
		}
		return wide;
	}
	const auto from = _flyFrom.translated(origin);
	const auto lshift = target.width() / 4;
	const auto rshift = target.width() / 2 - lshift;
	const auto margins = QMargins{ lshift, lshift, rshift, rshift };
	target = target.marginsRemoved(margins);
	const auto progress = _fly.value(1.);
	const auto rect = QRect(
		anim::interpolate(from.x(), target.x(), progress),
		computeParabolicTop(from.y(), target.y(), progress),
		anim::interpolate(from.width(), target.width(), progress),
		anim::interpolate(from.height(), target.height(), progress));
	const auto wide = rect.marginsAdded(margins);
	if (progress < 1.) {
		p.setOpacity(1. - progress);
		p.drawImage(rect, _flyIcon);
	}
	if (progress > 0.) {
		p.setOpacity(progress);
		paintCenterFrame(p, wide);
	}
	p.setOpacity(1.);
	return wide;
}

void Animation::paintCenterFrame(QPainter &p, QRect target) const {
	const auto size = QSize(
		int(base::SafeRound(target.width() * _centerSizeMultiplier)),
		int(base::SafeRound(target.height() * _centerSizeMultiplier)));
	p.drawImage(
		QRect(
			target.x() + (target.width() - size.width()) / 2,
			target.y() + (target.height() - size.height()) / 2,
			size.width(),
			size.height()),
		_center->frame());
}

int Animation::computeParabolicTop(
		int from,
		int to,
		float64 progress) const {
	const auto t = progress;

	// result = a * t * t + b * t + c

	// y = a * t * t + b * t
	// shift = y_1 = y(1) = a + b
	// y_0 = y(t_0) = a * t_0 * t_0 + b * t_0
	// 0 = 2 * a * t_0 + b
	// b = y_1 - a
	// a = y_1 / (1 - 2 * t_0)
	// b = 2 * t_0 * y_1 / (2 * t_0 - 1)
	// t_0 = (y_0 / y_1) +- sqrt((y_0 / y_1) * (y_0 / y_1 - 1))
	const auto y_1 = to - from;
	if (_cachedKey != y_1) {
		const auto y_0 = std::min(0, y_1) - st::reactionFlyUp;
		const auto ratio = y_1 ? (float64(y_0) / y_1) : 0.;
		const auto root = y_1 ? sqrt(ratio * (ratio - 1)) : 0.;
		const auto t_0 = !y_1
			? 0.5
			: (y_1 > 0)
			? (ratio + root)
			: (ratio - root);
		const auto a = y_1 ? (y_1 / (1 - 2 * t_0)) : (-4 * y_0);
		const auto b = y_1 - a;
		_cachedKey = y_1;
		_cachedA = a;
		_cachedB = b;
	}

	return int(base::SafeRound(_cachedA * t * t + _cachedB * t + from));
}

void Animation::startAnimations() {
	_center->animate([=] { callback(); });
	if (const auto effect = _effect.get()) {
		_effect->animate([=] { callback(); });
	}
}

void Animation::flyCallback() {
	if (!_fly.animating()) {
		_flyIcon = QImage();
		startAnimations();
	}
	callback();
}

void Animation::callback() {
	if (_repaint) {
		_repaint();
	}
}

void Animation::setRepaintCallback(Fn<void()> repaint) {
	_repaint = std::move(repaint);
}

bool Animation::flying() const {
	return !_flyIcon.isNull();
}

float64 Animation::flyingProgress() const {
	return _fly.value(1.);
}

bool Animation::finished() const {
	return !_valid
		|| (_flyIcon.isNull()
			&& !_center->animating()
			&& (!_effect || !_effect->animating()));
}

} // namespace HistoryView::Reactions
