/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/message_bar.h"

#include "ui/text/text_options.h"
#include "styles/style_chat.h"

namespace Ui {

MessageBar::MessageBar(not_null<QWidget*> parent, const style::MessageBar &st)
: _st(st)
, _widget(parent) {
	setup();
}

void MessageBar::setup() {
	_widget.resize(0, st::historyReplyHeight);
	_widget.paintRequest(
	) | rpl::start_with_next([=](QRect rect) {
		auto p = Painter(&_widget);
		paint(p);
	}, _widget.lifetime());
}

void MessageBar::set(MessageBarContent &&content) {
	_contentLifetime.destroy();
	tweenTo(std::move(content));
}

void MessageBar::set(rpl::producer<MessageBarContent> content) {
	_contentLifetime.destroy();
	std::move(
		content
	) | rpl::start_with_next([=](MessageBarContent &&content) {
		tweenTo(std::move(content));
	}, _contentLifetime);
}

MessageBar::BodyAnimation MessageBar::DetectBodyAnimationType(
		Animation *currentAnimation,
		const MessageBarContent &currentContent,
		const MessageBarContent &nextContent) {
	const auto now = currentAnimation
		? currentAnimation->bodyAnimation
		: BodyAnimation::None;
	return (now == BodyAnimation::Full
		|| currentContent.title != nextContent.title)
		? BodyAnimation::Full
		: (now == BodyAnimation::Text
			|| currentContent.text != nextContent.text
			|| currentContent.id != nextContent.id)
		? BodyAnimation::Text
		: BodyAnimation::None;
}

void MessageBar::tweenTo(MessageBarContent &&content) {
	_widget.update();
	if (!_st.duration || anim::Disabled() || _widget.size().isEmpty()) {
		updateFromContent(std::move(content));
		return;
	}
	const auto hasImageChanged = (_content.preview.isNull()
		!= content.preview.isNull());
	const auto bodyChanged = (_content.id != content.id
		|| _content.title != content.title
		|| _content.text != content.text
		|| _content.preview.constBits() != content.preview.constBits());
	auto animation = Animation();
	animation.bodyAnimation = DetectBodyAnimationType(
		_animation.get(),
		_content,
		content);
	animation.movingTo = (content.id > _content.id)
		? RectPart::Top
		: (content.id < _content.id)
		? RectPart::Bottom
		: RectPart::None;
	animation.imageFrom = grabImagePart();
	animation.bodyOrTextFrom = grabBodyOrTextPart(animation.bodyAnimation);
	auto was = std::move(_animation);
	updateFromContent(std::move(content));
	animation.imageTo = grabImagePart();
	animation.bodyOrTextTo = grabBodyOrTextPart(animation.bodyAnimation);
	if (was) {
		_animation = std::move(was);
		std::swap(*_animation, animation);
		_animation->imageShown = std::move(animation.imageShown);
	} else {
		_animation = std::make_unique<Animation>(std::move(animation));
	}

	if (hasImageChanged) {
		_animation->imageShown.start(
			[=] { _widget.update(); },
			_image.isNull() ? 1. : 0.,
			_image.isNull() ? 0. : 1.,
			_st.duration);
	}
	if (bodyChanged) {
		_animation->bodyMoved.start(
			[=] { _widget.update(); },
			0.,
			1.,
			_st.duration);
	}
}

void MessageBar::updateFromContent(MessageBarContent &&content) {
	_content = std::move(content);
	_title.setText(_st.title, _content.title);
	_text.setMarkedText(_st.text, _content.text, Ui::DialogTextOptions());
	_image = prepareImage(_content.preview);
}

QRect MessageBar::imageRect() const {
	const auto left = st::msgReplyBarSkip + st::msgReplyBarSkip;
	const auto top = st::msgReplyPadding.top();
	const auto size = st::msgReplyBarSize.height();
	return QRect(left, top, size, size);
}

QRect MessageBar::bodyRect(bool withImage) const {
	const auto innerLeft = st::msgReplyBarSkip + st::msgReplyBarSkip;
	const auto imageSkip = st::msgReplyBarSize.height()
		+ st::msgReplyBarSkip
		- st::msgReplyBarSize.width()
		- st::msgReplyBarPos.x();
	const auto left = innerLeft + (withImage ? imageSkip : 0);
	const auto top = st::msgReplyPadding.top();
	const auto width = _widget.width() - left - st::msgReplyPadding.right();
	const auto height = st::msgReplyBarSize.height();
	return QRect(left, top, width, height);
}

QRect MessageBar::bodyRect() const {
	return bodyRect(!_image.isNull());
}

QRect MessageBar::textRect() const {
	auto result = bodyRect();
	result.setTop(result.top() + st::msgServiceNameFont->height);
	return result;
}

auto MessageBar::makeGrabGuard() {
	auto imageShown = _animation
		? std::move(_animation->imageShown)
		: Ui::Animations::Simple();
	return gsl::finally([&, shown = std::move(imageShown)]() mutable {
		if (_animation) {
			_animation->imageShown = std::move(shown);
		}
	});
}

QPixmap MessageBar::grabBodyOrTextPart(BodyAnimation type) {
	return (type == BodyAnimation::Full)
		? grabBodyPart()
		: (type == BodyAnimation::Text)
		? grabTextPart()
		: QPixmap();
}

QPixmap MessageBar::grabBodyPart() {
	const auto guard = makeGrabGuard();
	return GrabWidget(widget(), bodyRect());
}

QPixmap MessageBar::grabTextPart() {
	const auto guard = makeGrabGuard();
	return GrabWidget(widget(), textRect());
}

QPixmap MessageBar::grabImagePart() {
	if (!_animation) {
		return _image;
	}
	const auto guard = makeGrabGuard();
	return (_animation->bodyMoved.animating()
		&& !_animation->imageFrom.isNull()
		&& !_animation->imageTo.isNull())
		? GrabWidget(widget(), imageRect())
		: _animation->imageFrom;
}

void MessageBar::finishAnimating() {
	if (_animation) {
		_animation = nullptr;
		_widget.update();
	}
}

QPixmap MessageBar::prepareImage(const QImage &preview) {
	return QPixmap::fromImage(preview, Qt::ColorOnly);
}

void MessageBar::paint(Painter &p) {
	const auto progress = _animation ? _animation->bodyMoved.value(1.) : 1.;
	const auto imageFinal = _image.isNull() ? 0. : 1.;
	const auto imageShown = _animation
		? _animation->imageShown.value(imageFinal)
		: imageFinal;
	if (progress == 1. && imageShown == 1. && _animation) {
		_animation = nullptr;
	}
	const auto body = [&] {
		if (!_animation || !_animation->imageShown.animating()) {
			return bodyRect();
		}
		const auto noImage = bodyRect(false);
		const auto withImage = bodyRect(true);
		return QRect(
			anim::interpolate(noImage.x(), withImage.x(), imageShown),
			noImage.y(),
			anim::interpolate(noImage.width(), withImage.width(), imageShown),
			noImage.height());
	}();
	const auto text = textRect();
	const auto image = imageRect();
	const auto width = _widget.width();
	const auto noShift = !_animation
		|| (_animation->movingTo == RectPart::None);
	const auto shiftFull = st::msgReplyBarSkip;
	const auto shiftTo = noShift
		? 0
		: (_animation->movingTo == RectPart::Top)
		? anim::interpolate(shiftFull, 0, progress)
		: anim::interpolate(-shiftFull, 0, progress);
	const auto shiftFrom = noShift
		? 0
		: (_animation->movingTo == RectPart::Top)
		? (shiftTo - shiftFull)
		: (shiftTo + shiftFull);

	if (!_animation) {
		if (!_image.isNull()) {
			p.drawPixmap(image, _image);
		}
	} else if (!_animation->imageFrom.isNull()
		|| !_animation->imageTo.isNull()) {
		const auto rect = [&] {
			if (!_animation->imageShown.animating()) {
				return image;
			}
			const auto size = anim::interpolate(0, image.width(), imageShown);
			return QRect(
				image.x(),
				image.y() + (image.height() - size) / 2,
				size,
				size);
		}();
		if (_animation->bodyMoved.animating()) {
			p.setOpacity(1. - progress);
			p.drawPixmap(
				rect.translated(0, shiftFrom),
				_animation->imageFrom);
			p.setOpacity(progress);
			p.drawPixmap(rect.translated(0, shiftTo), _animation->imageTo);
		} else {
			p.drawPixmap(rect, _image);
		}
	}
	if (!_animation || _animation->bodyAnimation == BodyAnimation::None) {
		p.setPen(_st.textFg);
		p.setTextPalette(_st.textPalette);
		_text.drawLeftElided(p, body.x(), text.y(), body.width(), width);
	} else if (_animation->bodyAnimation == BodyAnimation::Text) {
		p.setOpacity(1. - progress);
		p.drawPixmap(
			body.x(),
			text.y() + shiftFrom,
			_animation->bodyOrTextFrom);
		p.setOpacity(progress);
		p.drawPixmap(body.x(), text.y() + shiftTo, _animation->bodyOrTextTo);
	}
	if (!_animation || _animation->bodyAnimation != BodyAnimation::Full) {
		p.setPen(_st.titleFg);
		_title.drawLeftElided(p, body.x(), body.y(), body.width(), width);
	} else {
		p.setOpacity(1. - progress);
		p.drawPixmap(
			body.x(),
			body.y() + shiftFrom,
			_animation->bodyOrTextFrom);
		p.setOpacity(progress);
		p.drawPixmap(body.x(), body.y() + shiftTo, _animation->bodyOrTextTo);
	}
}

} // namespace Ui
