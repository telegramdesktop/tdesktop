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

auto Animation::flyCallback() {
	return [=] {
		if (!_fly.animating()) {
			_flyIcon = QImage();
			startAnimations();
		}
		if (_repaint) {
			_repaint();
		}
	};
}

auto Animation::callback() {
	return [=] {
		if (_repaint) {
			_repaint();
		}
	};
}

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
		centerIconSize = Ui::Text::AdjustCustomEmojiSize(st::emojiSize);
		const auto data = &owner->owner();
		const auto document = data->document(customId);
		_custom = data->customEmojiManager().create(document, callback());
		aroundAnimation = owner->chooseGenericAnimation(document);
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
	if (!_custom && !resolve(_center, centerIcon, centerIconSize)) {
		return;
	}
	resolve(_effect, aroundAnimation, size * 2);
	if (!args.flyIcon.isNull()) {
		_flyIcon = std::move(args.flyIcon);
		_fly.start(flyCallback(), 0., 1., kFlyDuration);
	} else if (!_center && !_effect) {
		return;
	} else {
		startAnimations();
	}
	_centerSizeMultiplier = centerIconSize / float64(size);
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
	Expects(_center || _custom);

	const auto size = QSize(
		int(base::SafeRound(target.width() * _centerSizeMultiplier)),
		int(base::SafeRound(target.height() * _centerSizeMultiplier)));
	if (_center) {
		const auto rect = QRect(
			target.x() + (target.width() - size.width()) / 2,
			target.y() + (target.height() - size.height()) / 2,
			size.width(),
			size.height());
		p.drawImage(rect, _center->frame());
	} else {
		const auto side = Ui::Text::AdjustCustomEmojiSize(st::emojiSize);
		const auto scaled = (size.width() != side);
		_custom->paint(p, {
			.preview = Qt::transparent,
			.size = { side, side },
			.now = crl::now(),
			.scale = (scaled ? (size.width() / float64(side)) : 1.),
			.position = QPoint(
				target.x() + (target.width() - side) / 2,
				target.y() + (target.height() - side) / 2),
			.scaled = scaled,
		});
	}
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
	if (const auto center = _center.get()) {
		_center->animate(callback());
	}
	if (const auto effect = _effect.get()) {
		_effect->animate(callback());
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
			&& (!_center || !_center->animating())
			&& (!_effect || !_effect->animating()));
}

} // namespace HistoryView::Reactions
