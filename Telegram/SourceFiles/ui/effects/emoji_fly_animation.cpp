/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/emoji_fly_animation.h"

#include "data/stickers/data_custom_emoji.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/animated_icon.h"
#include "styles/style_info.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

[[nodiscard]] int ComputeFlySize(Data::CustomEmojiSizeTag tag) {
	using Tag = Data::CustomEmojiSizeTag;
	if (tag == Tag::Normal) {
		return st::reactionInlineImage;
	}
	return int(base::SafeRound(
		(st::reactionInlineImage * Data::FrameSizeFromTag(tag)
			/ float64(Data::FrameSizeFromTag(Tag::Normal)))));
}

} // namespace

EmojiFlyAnimation::EmojiFlyAnimation(
	not_null<Ui::RpWidget*> body,
	not_null<Data::Reactions*> owner,
	ReactionFlyAnimationArgs &&args,
	Fn<void()> repaint,
	Fn<QColor()> textColor,
	Data::CustomEmojiSizeTag tag)
: _flySize(ComputeFlySize(tag))
, _textColor(std::move(textColor))
, _fly(
	owner,
	std::move(args),
	std::move(repaint),
	_flySize,
	tag)
, _layer(body) {
	body->sizeValue() | rpl::start_with_next([=](QSize size) {
		_layer.setGeometry(QRect(QPoint(), size));
	}, _layer.lifetime());

	_layer.paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		const auto target = _target.data();
		if (!target || !target->isVisible()) {
			return;
		}
		auto p = QPainter(&_layer);

		const auto rect = Ui::MapFrom(&_layer, target, target->rect());
		const auto skipx = (rect.width() - _flySize) / 2;
		const auto skipy = (rect.height() - _flySize) / 2;
		const auto area = _fly.paintGetArea(
			p,
			QPoint(),
			QRect(
				rect.topLeft() + QPoint(skipx, skipy),
				QSize(_flySize, _flySize)),
			(_textColor ? _textColor() : st::infoPeerBadge.premiumFg->c),
			clip,
			crl::now());
		if (_areaUpdated || _area.isEmpty()) {
			_area = area;
		} else {
			_area = _area.united(area);
		}
	}, _layer.lifetime());

	_layer.setAttribute(Qt::WA_TransparentForMouseEvents);
	_layer.show();
}

not_null<Ui::RpWidget*> EmojiFlyAnimation::layer() {
	return &_layer;
}

bool EmojiFlyAnimation::finished() const {
	if (const auto target = _target.data()) {
		return _fly.finished() || !target->isVisible();
	}
	return true;
}

void EmojiFlyAnimation::repaint() {
	if (_area.isEmpty()) {
		_layer.update();
	} else {
		_layer.update(_area);
		_areaUpdated = true;
	}
}

bool EmojiFlyAnimation::paintBadgeFrame(not_null<QWidget*> widget) {
	_target = widget;
	return !_fly.finished();
}

ReactionFlyCenter EmojiFlyAnimation::grabBadgeCenter() {
	auto result = _fly.takeCenter();
	result.size = _flySize;
	return result;
}

} // namespace Ui
