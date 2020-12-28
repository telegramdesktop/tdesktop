/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/message_bar.h"

#include "ui/text/text_options.h"
#include "ui/image/image_prepare.h"
#include "styles/style_chat.h"
#include "styles/palette.h"

namespace Ui {
namespace {

[[nodiscard]] int SameFirstPartLength(const QString &a, const QString &b) {
	const auto [i, j] = ranges::mismatch(a, b);
	return (i - a.begin());
}

[[nodiscard]] bool MuchDifferent(
		int same,
		const QString &a,
		const QString &b) {
	return (same * 2 < a.size()) || (same * 2 < b.size());
}

[[nodiscard]] bool MuchDifferent(const QString &a, const QString &b) {
	return MuchDifferent(SameFirstPartLength(a, b), a, b);
}

[[nodiscard]] bool ComplexTitleAnimation(
		int same,
		const QString &a,
		const QString &b) {
	return !MuchDifferent(same, a, b)
		&& (same != a.size() || same != b.size());
}

} // namespace

MessageBar::MessageBar(not_null<QWidget*> parent, const style::MessageBar &st)
: _st(st)
, _widget(parent) {
	setup();

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_topBarGradient = _bottomBarGradient = QPixmap();
	}, _widget.lifetime());
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
	const auto somethingChanged = (currentContent.text != nextContent.text)
		|| (currentContent.title != nextContent.title)
		|| (currentContent.index != nextContent.index)
		|| (currentContent.count != nextContent.count);
	return (now == BodyAnimation::Full
		|| MuchDifferent(currentContent.title, nextContent.title)
		|| (currentContent.title.isEmpty() && somethingChanged))
		? BodyAnimation::Full
		: (now == BodyAnimation::Text || somethingChanged)
		? BodyAnimation::Text
		: BodyAnimation::None;
}

void MessageBar::tweenTo(MessageBarContent &&content) {
	Expects(content.count > 0);
	Expects(content.index >= 0 && content.index < content.count);

	_widget.update();
	if (!_st.duration || anim::Disabled() || _widget.size().isEmpty()) {
		updateFromContent(std::move(content));
		return;
	}
	const auto hasImageChanged = (_content.preview.isNull()
		!= content.preview.isNull());
	const auto bodyChanged = (_content.index != content.index
		|| _content.count != content.count
		|| _content.title != content.title
		|| _content.text != content.text
		|| _content.preview.constBits() != content.preview.constBits());
	const auto barCountChanged = (_content.count != content.count);
	const auto barFrom = _content.index;
	const auto barTo = content.index;
	auto animation = Animation();
	animation.bodyAnimation = DetectBodyAnimationType(
		_animation.get(),
		_content,
		content);
	animation.movingTo = (content.index > _content.index)
		? RectPart::Top
		: (content.index < _content.index)
		? RectPart::Bottom
		: RectPart::None;
	animation.imageFrom = grabImagePart();
	animation.bodyOrTextFrom = grabBodyOrTextPart(animation.bodyAnimation);
	const auto sameLength = SameFirstPartLength(
		_content.title,
		content.title);
	if (animation.bodyAnimation == BodyAnimation::Text
		&& ComplexTitleAnimation(sameLength, _content.title, content.title)) {
		animation.titleSame = grabTitleBase(sameLength);
		animation.titleFrom = grabTitlePart(sameLength);
	}
	auto was = std::move(_animation);
	updateFromContent(std::move(content));
	animation.imageTo = grabImagePart();
	animation.bodyOrTextTo = grabBodyOrTextPart(animation.bodyAnimation);
	if (!animation.titleSame.isNull()) {
		animation.titleTo = grabTitlePart(sameLength);
	}
	if (was) {
		_animation = std::move(was);
		std::swap(*_animation, animation);
		_animation->imageShown = std::move(animation.imageShown);
		_animation->barScroll = std::move(animation.barScroll);
		_animation->barTop = std::move(animation.barTop);
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
	if (barCountChanged) {
		_animation->barScroll.stop();
		_animation->barTop.stop();
	} else if (barFrom != barTo) {
		const auto wasState = countBarState(barFrom);
		const auto nowState = countBarState(barTo);
		_animation->barScroll.start(
			[=] { _widget.update(); },
			wasState.scroll,
			nowState.scroll,
			_st.duration);
		_animation->barTop.start(
			[] {},
			wasState.offset,
			nowState.offset,
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

QRect MessageBar::titleRangeRect(int from, int till) const {
	auto result = bodyRect();
	result.setHeight(st::msgServiceNameFont->height);
	const auto left = from
		? st::msgServiceNameFont->width(_content.title.mid(0, from))
		: 0;
	const auto right = (till <= _content.title.size())
		? st::msgServiceNameFont->width(_content.title.mid(0, till))
		: result.width();
	result.setLeft(result.left() + left);
	result.setWidth(right - left);
	return result;
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

QPixmap MessageBar::grabTitleBase(int till) {
	return grabTitleRange(0, till);
}

QPixmap MessageBar::grabTitlePart(int from) {
	Expects(from <= _content.title.size());

	return grabTitleRange(from, _content.title.size());
}

QPixmap MessageBar::grabTitleRange(int from, int till) {
	const auto guard = makeGrabGuard();
	return GrabWidget(widget(), titleRangeRect(from, till));
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
	if (progress == 1. && imageShown == imageFinal && _animation) {
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

	paintLeftBar(p);

	if (!_animation) {
		if (!_image.isNull()) {
			p.drawPixmap(image, _image);
		}
	} else if (!_animation->imageTo.isNull()
		|| (!_animation->imageFrom.isNull()
			&& _animation->imageShown.animating())) {
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
			p.setOpacity(1.);
		} else {
			p.drawPixmap(rect, _image);
		}
	}
	if (!_animation || _animation->bodyAnimation == BodyAnimation::None) {
		if (_title.isEmpty()) {
			// "Loading..." state.
			p.setPen(st::historyComposeAreaFgService);
			_text.drawLeftElided(
				p,
				body.x(),
				body.y() + (body.height() - st::normalFont->height) / 2,
				body.width(),
				width);
		} else {
			p.setPen(_st.textFg);
			p.setTextPalette(_st.textPalette);
			_text.drawLeftElided(p, body.x(), text.y(), body.width(), width);
		}
	} else if (_animation->bodyAnimation == BodyAnimation::Text) {
		p.setOpacity(1. - progress);
		p.drawPixmap(
			body.x(),
			text.y() + shiftFrom,
			_animation->bodyOrTextFrom);
		p.setOpacity(progress);
		p.drawPixmap(body.x(), text.y() + shiftTo, _animation->bodyOrTextTo);
		p.setOpacity(1.);
	}
	if (!_animation || _animation->bodyAnimation != BodyAnimation::Full) {
		if (_animation && !_animation->titleSame.isNull()) {
			const auto factor = style::DevicePixelRatio();
			p.drawPixmap(body.x(), body.y(), _animation->titleSame);
			p.setOpacity(1. - progress);
			p.drawPixmap(
				body.x() + (_animation->titleSame.width() / factor),
				body.y() + shiftFrom,
				_animation->titleFrom);
			p.setOpacity(progress);
			p.drawPixmap(
				body.x() + (_animation->titleSame.width() / factor),
				body.y() + shiftTo,
				_animation->titleTo);
			p.setOpacity(1.);
		} else {
			p.setPen(_st.titleFg);
			_title.drawLeftElided(p, body.x(), body.y(), body.width(), width);
		}
	} else {
		p.setOpacity(1. - progress);
		p.drawPixmap(
			body.x(),
			body.y() + shiftFrom,
			_animation->bodyOrTextFrom);
		p.setOpacity(progress);
		p.drawPixmap(body.x(), body.y() + shiftTo, _animation->bodyOrTextTo);
		p.setOpacity(1.);
	}
}

auto MessageBar::countBarState(int index) const -> BarState {
	Expects(index >= 0 && index < _content.count);

	auto result = BarState();
	const auto line = st::msgReplyBarSize.width();
	const auto height = st::msgReplyBarSize.height();
	const auto count = _content.count;
	const auto shownCount = std::min(count, 4);
	const auto dividers = (shownCount - 1) * line;
	const auto size = float64(st::msgReplyBarSize.height() - dividers)
		/ shownCount;
	const auto fullHeight = count * size + (count - 1) * line;
	const auto topByIndex = [&](int index) {
		return index * (size + line);
	};
	result.scroll = (count < 5 || index < 2)
		? 0
		: (index >= count - 2)
		? (fullHeight - height)
		: (topByIndex(index) - (height - size) / 2);
	result.size = size;
	result.skip = line;
	result.offset = topByIndex(index);
	return result;
}

auto MessageBar::countBarState() const -> BarState {
	return countBarState(_content.index);
}

void MessageBar::ensureGradientsCreated(int size) {
	if (!_topBarGradient.isNull()) {
		return;
	}
	const auto rows = size * style::DevicePixelRatio() - 2;
	auto bottomMask = QImage(
		QSize(1, size) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	const auto step = ((1ULL << 24) - 1) / rows;
	const auto limit = step * rows;
	auto bits = bottomMask.bits();
	const auto perLine = bottomMask.bytesPerLine();
	for (auto counter = uint32(0); counter != limit; counter += step) {
		const auto value = (counter >> 16);
		memset(bits, int(value), perLine);
		bits += perLine;
	}
	memset(bits, 255, perLine * 2);
	auto bottom = style::colorizeImage(bottomMask, st::historyPinnedBg);
	bottom.setDevicePixelRatio(style::DevicePixelRatio());
	auto top = bottom.mirrored();
	_bottomBarGradient = Images::PixmapFast(std::move(bottom));
	_topBarGradient = Images::PixmapFast(std::move(top));
}

void MessageBar::paintLeftBar(Painter &p) {
	const auto state = countBarState();
	const auto gradientSize = int(std::ceil(state.size * 2.5));
	if (_content.count > 4) {
		ensureGradientsCreated(gradientSize);
	}

	const auto scroll = _animation
		? _animation->barScroll.value(state.scroll)
		: state.scroll;
	const auto offset = _animation
		? _animation->barTop.value(state.offset)
		: state.offset;
	const auto line = st::msgReplyBarSize.width();
	const auto height = st::msgReplyBarSize.height();
	const auto activeFrom = offset - scroll;
	const auto activeTill = activeFrom + state.size;
	const auto single = state.size + state.skip;

	const auto barSkip = st::msgReplyPadding.top() + st::msgReplyBarPos.y();
	const auto fullHeight = barSkip + height + barSkip;
	const auto bar = QRect(
		st::msgReplyBarSkip + st::msgReplyBarPos.x(),
		barSkip,
		line,
		state.size);
	const auto paintFromScroll = std::max(scroll - barSkip, 0.);
	const auto paintFrom = int(std::floor(paintFromScroll / single));
	const auto paintTillScroll = (scroll + height + barSkip);
	const auto paintTill = std::min(
		int(std::floor(paintTillScroll / single)) + 1,
		_content.count);

	p.setPen(Qt::NoPen);
	const auto activeBrush = QBrush(st::msgInReplyBarColor);
	const auto inactiveBrush = QBrush(QColor(
		st::msgInReplyBarColor->c.red(),
		st::msgInReplyBarColor->c.green(),
		st::msgInReplyBarColor->c.blue(),
		st::msgInReplyBarColor->c.alpha() / 3));
	const auto radius = line / 2.;
	auto hq = PainterHighQualityEnabler(p);
	p.setClipRect(bar.x(), 0, bar.width(), fullHeight);
	for (auto i = paintFrom; i != paintTill; ++i) {
		const auto top = i * single - scroll;
		const auto bottom = top + state.size;
		const auto active = (top == activeFrom);
		p.setBrush(active ? activeBrush : inactiveBrush);
		p.drawRoundedRect(bar.translated(0, top), radius, radius);
		if (active
			|| bottom - line <= activeFrom
			|| top + line >= activeTill) {
			continue;
		}
		const auto partFrom = std::max(top, activeFrom);
		const auto partTill = std::min(bottom, activeTill);
		p.setBrush(activeBrush);
		p.drawRoundedRect(
			QRect(bar.x(), bar.y() + partFrom, line, partTill - partFrom),
			radius,
			radius);
	}
	p.setClipping(false);
	if (_content.count > 4) {
		const auto firstScroll = countBarState(2).scroll;
		const auto gradientTop = (scroll >= firstScroll)
			? 0
			: anim::interpolate(-gradientSize, 0, scroll / firstScroll);
		const auto lastScroll = countBarState(_content.count - 3).scroll;
		const auto largestScroll = countBarState(_content.count - 1).scroll;
		const auto gradientBottom = (scroll <= lastScroll)
			? fullHeight
			: anim::interpolate(
				fullHeight,
				fullHeight + gradientSize,
				(scroll - lastScroll) / (largestScroll - lastScroll));
		if (gradientTop > -gradientSize) {
			p.drawPixmap(
				QRect(bar.x(), gradientTop, bar.width(), gradientSize),
				_topBarGradient);
		}
		if (gradientBottom < fullHeight + gradientSize) {
			p.drawPixmap(
				QRect(
					bar.x(),
					gradientBottom - gradientSize,
					bar.width(),
					gradientSize),
				_bottomBarGradient);
		}
	}
}

} // namespace Ui
