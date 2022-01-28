/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_react_animation.h"

#include "history/view/history_view_element.h"
#include "lottie/lottie_icon.h"
#include "data/data_message_reactions.h"
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
	const auto i = ranges::find(list, args.emoji, &::Data::Reaction::emoji);
	if (i == end(list) || !i->centerIcon) {
		return;
	}
	const auto resolve = [&](
			std::unique_ptr<Lottie::Icon> &icon,
			DocumentData *document,
			int size) {
		if (!document) {
			return false;
		}
		const auto media = document->activeMediaView();
		if (!media || !media->loaded()) {
			return false;
		}
		icon = std::make_unique<Lottie::Icon>(Lottie::IconDescriptor{
			.path = document->filepath(true),
			.json = media->bytes(),
			.sizeOverride = QSize(size, size),
		});
		return true;
	};
	_flyIcon = std::move(args.flyIcon);
	if (!resolve(_center, i->centerIcon, size)
		|| !resolve(_effect, i->aroundAnimation, size * 2)) {
		return;
	}
	if (_flyIcon) {
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
	if (!_flyIcon) {
		p.drawImage(target, _center->frame());
		const auto wide = QRect(
			target.topLeft() - QPoint(target.width(), target.height()) / 2,
			target.size() * 2);
		p.drawImage(wide, _effect->frame());
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
	auto hq = PainterHighQualityEnabler(p);
	if (progress < 1.) {
		p.setOpacity(1. - progress);
		p.drawImage(rect, _flyIcon->frame());
	}
	if (progress > 0.) {
		p.setOpacity(progress);
		p.drawImage(wide, _center->frame());
	}
	p.setOpacity(1.);
	return wide;
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
	_center->animate([=] { callback(); }, 0, _center->framesCount() - 1);
	_effect->animate([=] { callback(); }, 0, _effect->framesCount() - 1);
}

void Animation::flyCallback() {
	if (!_fly.animating()) {
		_flyIcon = nullptr;
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
	return (_flyIcon != nullptr);
}

float64 Animation::flyingProgress() const {
	return _fly.value(1.);
}

bool Animation::finished() const {
	return !_valid
		|| (!_flyIcon && !_center->animating() && !_effect->animating());
}

} // namespace HistoryView::Reactions
