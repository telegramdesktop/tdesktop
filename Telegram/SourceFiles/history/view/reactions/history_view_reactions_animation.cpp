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
#include "base/random.h"
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kFlyDuration = crl::time(300);
constexpr auto kMiniCopies = 7;
constexpr auto kMiniCopiesDurationMax = crl::time(1400);
constexpr auto kMiniCopiesDurationMin = crl::time(700);
constexpr auto kMiniCopiesScaleInDuration = crl::time(200);
constexpr auto kMiniCopiesScaleOutDuration = crl::time(200);
constexpr auto kMiniCopiesMaxScaleMin = 0.6;
constexpr auto kMiniCopiesMaxScaleMax = 0.9;

} // namespace

AnimationArgs AnimationArgs::translated(QPoint point) const {
	return {
		.id = id,
		.flyIcon = flyIcon,
		.flyFrom = flyFrom.translated(point),
	};
}

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
	AnimationArgs &&args,
	Fn<void()> repaint,
	int size,
	Data::CustomEmojiSizeTag customSizeTag)
: _owner(owner)
, _repaint(std::move(repaint))
, _flyFrom(args.flyFrom) {
	const auto &list = owner->list(::Data::Reactions::Type::All);
	auto centerIcon = (DocumentData*)nullptr;
	auto aroundAnimation = (DocumentData*)nullptr;
	if (const auto customId = args.id.custom()) {
		const auto esize = Data::FrameSizeFromTag(customSizeTag)
			/ style::DevicePixelRatio();
		const auto data = &owner->owner();
		const auto document = data->document(customId);
		_custom = data->customEmojiManager().create(
			document,
			callback(),
			customSizeTag);
		_colored = std::make_unique<Ui::Text::CustomEmojiColored>();
		_customSize = esize;
		_centerSizeMultiplier = _customSize / float64(size);
		aroundAnimation = owner->chooseGenericAnimation(document);
	} else {
		const auto i = ranges::find(list, args.id, &::Data::Reaction::id);
		if (i == end(list) || !i->centerIcon) {
			return;
		}
		centerIcon = i->centerIcon;
		aroundAnimation = i->aroundAnimation;
		_centerSizeMultiplier = 1.;
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
	if (!_custom && !resolve(_center, centerIcon, size)) {
		return;
	}
	resolve(_effect, aroundAnimation, size * 2);
	generateMiniCopies(size + size / 2);
	if (!args.flyIcon.isNull()) {
		_flyIcon = std::move(args.flyIcon);
		_fly.start(flyCallback(), 0., 1., kFlyDuration);
	} else if (!_center && !_effect && _miniCopies.empty()) {
		return;
	} else {
		startAnimations();
	}
	_valid = true;
}

Animation::~Animation() = default;

QRect Animation::paintGetArea(
		QPainter &p,
		QPoint origin,
		QRect target,
		const QColor &colored,
		QRect clip,
		crl::time now) const {
	if (_flyIcon.isNull()) {
		const auto wide = QRect(
			target.topLeft() - QPoint(target.width(), target.height()) / 2,
			target.size() * 2);
		const auto area = _miniCopies.empty()
			? wide
			: QRect(
				target.topLeft() - QPoint(target.width(), target.height()),
				target.size() * 3);
		if (clip.isEmpty() || area.intersects(clip)) {
			paintCenterFrame(p, target, colored, now);
			if (const auto effect = _effect.get()) {
				p.drawImage(wide, effect->frame());
			}
			paintMiniCopies(p, target.center(), colored, now);
		}
		return area;
	}
	const auto from = _flyFrom.translated(origin);
	const auto lshift = target.width() / 4;
	const auto rshift = target.width() / 2 - lshift;
	const auto margins = QMargins{ lshift, lshift, rshift, rshift };
	target = target.marginsRemoved(margins);
	const auto progress = _fly.value(1.);
	const auto rect = QRect(
		anim::interpolate(from.x(), target.x(), progress),
		computeParabolicTop(
			_cached,
			from.y(),
			target.y(),
			st::reactionFlyUp,
			progress),
		anim::interpolate(from.width(), target.width(), progress),
		anim::interpolate(from.height(), target.height(), progress));
	const auto wide = rect.marginsAdded(margins);
	if (clip.isEmpty() || wide.intersects(clip)) {
		if (progress < 1.) {
			p.setOpacity(1. - progress);
			p.drawImage(rect, _flyIcon);
		}
		if (progress > 0.) {
			p.setOpacity(progress);
			paintCenterFrame(p, wide, colored, now);
		}
		p.setOpacity(1.);
	}
	return wide;
}

void Animation::paintCenterFrame(
		QPainter &p,
		QRect target,
		const QColor &colored,
		crl::time now) const {
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
		const auto scaled = (size.width() != _customSize);
		_colored->color = colored;
		_custom->paint(p, {
			.preview = QColor(0, 0, 0, 0),
			.colored = _colored.get(),
			.size = { _customSize, _customSize },
			.now = now,
			.scale = (scaled ? (size.width() / float64(_customSize)) : 1.),
			.position = QPoint(
				target.x() + (target.width() - _customSize) / 2,
				target.y() + (target.height() - _customSize) / 2),
			.scaled = scaled,
		});
	}
}

void Animation::paintMiniCopies(
		QPainter &p,
		QPoint center,
		const QColor &colored,
		crl::time now) const {
	Expects(_miniCopies.empty() || _custom != nullptr);

	if (!_minis.animating()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto size = QSize(_customSize, _customSize);
	const auto preview = QColor(0, 0, 0, 0);
	const auto progress = _minis.value(1.);
	const auto middle = center - QPoint(_customSize / 2, _customSize / 2);
	const auto scaleIn = kMiniCopiesScaleInDuration
		/ float64(kMiniCopiesDurationMax);
	const auto scaleOut = kMiniCopiesScaleOutDuration
		/ float64(kMiniCopiesDurationMax);
	_colored->color = colored;
	auto context = Ui::Text::CustomEmoji::Context{
		.preview = preview,
		.colored = _colored.get(),
		.size = size,
		.now = now,
		.scaled = true,
	};
	for (const auto &mini : _miniCopies) {
		if (progress >= mini.duration) {
			continue;
		}
		const auto value = progress / mini.duration;
		context.scale = (progress < scaleIn)
			? (mini.maxScale * progress / scaleIn)
			: (progress <= mini.duration - scaleOut)
			? mini.maxScale
			: (mini.maxScale * (mini.duration - progress) / scaleOut);
		context.position = middle + QPoint(
			anim::interpolate(0, mini.finalX, value),
			computeParabolicTop(
				mini.cached,
				0,
				mini.finalY,
				mini.flyUp,
				value));
		_custom->paint(p, context);
	}
}

void Animation::generateMiniCopies(int size) {
	if (!_custom) {
		return;
	}
	const auto random = [] {
		constexpr auto count = 16384;
		return base::RandomIndex(count) / float64(count - 1);
	};
	const auto between = [](int a, int b) {
		return (a > b)
			? (b + base::RandomIndex(a - b + 1))
			: (a + base::RandomIndex(b - a + 1));
	};
	_miniCopies.reserve(kMiniCopies);
	for (auto i = 0; i != kMiniCopies; ++i) {
		const auto maxScale = kMiniCopiesMaxScaleMin
			+ (kMiniCopiesMaxScaleMax - kMiniCopiesMaxScaleMin) * random();
		const auto duration = between(
			kMiniCopiesDurationMin,
			kMiniCopiesDurationMax);
		const auto maxSize = int(std::ceil(maxScale * _customSize));
		const auto maxHalf = (maxSize + 1) / 2;
		_miniCopies.push_back({
			.maxScale = maxScale,
			.duration = duration / float64(kMiniCopiesDurationMax),
			.flyUp = between(size / 4, size - maxHalf),
			.finalX = between(-size, size),
			.finalY = between(size - (size / 4), size),
		});
	}
}

int Animation::computeParabolicTop(
		Parabolic &cache,
		int from,
		int to,
		int top,
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
	if (cache.key != y_1) {
		const auto y_0 = std::min(0, y_1) - top;
		const auto ratio = y_1 ? (float64(y_0) / y_1) : 0.;
		const auto root = y_1 ? sqrt(ratio * (ratio - 1)) : 0.;
		const auto t_0 = !y_1
			? 0.5
			: (y_1 > 0)
			? (ratio + root)
			: (ratio - root);
		const auto a = y_1 ? (y_1 / (1 - 2 * t_0)) : (-4 * y_0);
		const auto b = y_1 - a;
		cache.key = y_1;
		cache.a = a;
		cache.b = b;
	}

	return int(base::SafeRound(cache.a * t * t + cache.b * t + from));
}

void Animation::startAnimations() {
	if (const auto center = _center.get()) {
		_center->animate(callback());
	}
	if (const auto effect = _effect.get()) {
		_effect->animate(callback());
	}
	if (!_miniCopies.empty()) {
		_minis.start(callback(), 0., 1., kMiniCopiesDurationMax);
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
			&& (!_effect || !_effect->animating())
			&& !_minis.animating());
}

} // namespace HistoryView::Reactions
