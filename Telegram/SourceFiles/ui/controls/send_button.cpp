/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/send_button.h"

#include "lang/lang_tag.h"
#include "lottie/lottie_icon.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "lang/lang_keys.h"
#include "ui/text/format_values.h"

namespace Ui {
namespace {

constexpr auto kWideScale = 5;
constexpr auto kVoiceToRoundIndex = 0;
constexpr auto kRoundToVoiceIndex = 1;
constexpr auto kForbiddenOpacity = 0.5;

} // namespace

SendButton::SendButton(QWidget *parent, const style::SendButton &st)
: RippleButton(parent, st.inner.ripple)
, _st(st)
, _lastRippleShape(currentRippleShape()) {
	updateSize();
}

SendButton::~SendButton() = default;

void SendButton::setState(State state) {
	if (_state == state) {
		return;
	}

	const auto previousType = _state.type;
	const auto newType = state.type;
	const auto voiceRoundTransition = isVoiceRoundTransition(
		previousType,
		newType);

	const auto hasSlowmode = (_state.slowmodeDelay > 0);
	const auto hasSlowmodeChanged = hasSlowmode != (state.slowmodeDelay > 0);
	auto withSameSlowmode = state;
	withSameSlowmode.slowmodeDelay = _state.slowmodeDelay;
	const auto animate = hasSlowmodeChanged
		|| (!hasSlowmode && withSameSlowmode != _state);

	if (animate && !voiceRoundTransition) {
		_contentFrom = grabContent();
	}

	if (_voiceRoundAnimating && !voiceRoundTransition) {
		_voiceRoundAnimating = false;
	}

	if (_state.slowmodeDelay != state.slowmodeDelay) {
		const auto seconds = state.slowmodeDelay;
		const auto minutes = seconds / 60;
		_slowmodeDelayText = seconds
			? u"%1:%2"_q.arg(minutes).arg(seconds % 60, 2, 10, QChar('0'))
			: QString();
	}
	if (!state.starsToSend || state.type != Type::Send) {
		_starsToSendText = Text::String();
	} else if (_starsToSendText.isEmpty()
		|| _state.starsToSend != state.starsToSend) {
		_starsToSendText.setMarkedText(
			_st.stars.style,
			Text::IconEmoji(&st::starIconEmoji).append(
				Lang::FormatCountToShort(state.starsToSend).string),
			kMarkupTextOptions);
	}
	_state = state;

	const auto newShape = currentRippleShape();
	if (_lastRippleShape != newShape) {
		_lastRippleShape = newShape;
		RippleButton::finishAnimating();
	}

	setAccessibleName([&] {
		switch (_state.type) {
		case Type::Send: return tr::lng_send_button(tr::now);
		case Type::Record:
			return tr::lng_shortcuts_record_voice_message(tr::now);
		case Type::Round:
			return tr::lng_shortcuts_record_round_message(tr::now);
		case Type::Cancel: return tr::lng_cancel(tr::now);
		case Type::Save: return tr::lng_settings_save(tr::now);
		case Type::Slowmode:
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWordsSlowmode(_state.slowmodeDelay));
		case Type::Schedule: return tr::lng_schedule_button(tr::now);
		case Type::EditPrice:
			return tr::lng_suggest_menu_edit_price(tr::now);
		}
		Unexpected("Send button type.");
	}());

	if (voiceRoundTransition) {
		_voiceRoundAnimating = true;

		const auto toRound = (newType == Type::Round);
		const auto index = toRound ? kVoiceToRoundIndex : kRoundToVoiceIndex;
		auto &icon = _voiceRoundIcons[index];
		if (!icon) {
			initVoiceRoundIcon(index);
		}
		icon->animate([=, raw = icon.get()] {
			update();
			if (!raw->animating()) {
				_voiceRoundAnimating = false;
			}
		}, 0, icon->framesCount() - 1);
		auto &after = _voiceRoundIcons[1 - index];
		if (!after) {
			initVoiceRoundIcon(1 - index);
		} else if (after->frameIndex() != 0) {
			after->jumpTo(0, nullptr);
		}
	} else if (animate) {
		_stateChangeFromWidth = width();
		_stateChangeAnimation.stop();
		updateSize();
		_contentTo = grabContent();
		_stateChangeAnimation.start(
			[=] { updateSize(); update(); },
			0.,
			1.,
			st::universalDuration);
		setPointerCursor(_state.type != Type::Slowmode);
		updateSize();
	}
	update();
}

void SendButton::finishAnimating() {
	_stateChangeAnimation.stop();
	updateSize();
	update();
}

void SendButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	auto over = (isDown() || isOver());

	if (_voiceRoundAnimating) {
		paintVoiceRoundIcon(p, over);
		return;
	}

	auto changed = _stateChangeAnimation.value(1.);
	if (changed < 1.) {
		PainterHighQualityEnabler hq(p);
		const auto ratio = style::DevicePixelRatio();

		p.setOpacity(1. - changed);
		const auto fromSize = _contentFrom.size() / (kWideScale * ratio);
		const auto fromShift = QPoint(
			(width() - fromSize.width()) / 2,
			(height() - fromSize.height()) / 2);
		auto fromRect = QRect(
			(1 - kWideScale) / 2 * fromSize.width(),
			(1 - kWideScale) / 2 * fromSize.height(),
			kWideScale * fromSize.width(),
			kWideScale * fromSize.height()
		).translated(fromShift);
		auto hiddenWidth = anim::interpolate(0, (1 - kWideScale) / 2 * fromSize.width(), changed);
		auto hiddenHeight = anim::interpolate(0, (1 - kWideScale) / 2 * fromSize.height(), changed);
		p.drawPixmap(
			fromRect.marginsAdded(
				{ hiddenWidth, hiddenHeight, hiddenWidth, hiddenHeight }),
			_contentFrom);

		p.setOpacity(changed);
		const auto toSize = _contentTo.size() / (kWideScale * ratio);
		const auto toShift = QPoint(
			(width() - toSize.width()) / 2,
			(height() - toSize.height()) / 2);
		auto toRect = QRect(
			(1 - kWideScale) / 2 * toSize.width(),
			(1 - kWideScale) / 2 * toSize.height(),
			kWideScale * toSize.width(),
			kWideScale * toSize.height()
		).translated(toShift);
		auto shownWidth = anim::interpolate((1 - kWideScale) / 2 * width(), 0, changed);
		auto shownHeight = anim::interpolate((1 - kWideScale) / 2 * toSize.height(), 0, changed);
		p.drawPixmap(
			toRect.marginsAdded(
				{ shownWidth, shownHeight, shownWidth, shownHeight }),
			_contentTo);
		return;
	}
	switch (_state.type) {
	case Type::Record: paintRecord(p, over); break;
	case Type::Round: paintRound(p, over); break;
	case Type::Save: paintSave(p, over); break;
	case Type::Cancel: paintCancel(p, over); break;
	case Type::Send:
		if (_starsToSendText.isEmpty()) {
			paintSend(p, over);
		} else {
			paintStarsToSend(p, over);
		}
		break;
	case Type::Schedule: paintSchedule(p, over); break;
	case Type::Slowmode: paintSlowmode(p); break;
	case Type::EditPrice: break;
	}
}

void SendButton::paintRecord(QPainter &p, bool over) {
	if (!isDisabled() && !_state.forbidden) {
		paintRipple(
			p,
			(width() - _st.inner.rippleAreaSize) / 2,
			_st.inner.rippleAreaPosition.y());
	}
	if (_state.forbidden) {
		p.setOpacity(kForbiddenOpacity);
	}
	paintLottieIcon(p, kVoiceToRoundIndex, over);
	if (_state.forbidden) {
		p.setOpacity(1.);
	}
}

void SendButton::paintRound(QPainter &p, bool over) {
	if (!isDisabled() && !_state.forbidden) {
		paintRipple(
			p,
			(width() - _st.inner.rippleAreaSize) / 2,
			_st.inner.rippleAreaPosition.y());
	}
	if (_state.forbidden) {
		p.setOpacity(kForbiddenOpacity);
	}
	paintLottieIcon(p, kRoundToVoiceIndex, over);
	if (_state.forbidden) {
		p.setOpacity(1.);
	}
}

void SendButton::paintLottieIcon(QPainter &p, int index, bool over) {
	auto &icon = _voiceRoundIcons[index];
	if (!icon) {
		initVoiceRoundIcon(index);
	} else if (!_voiceRoundAnimating && icon->frameIndex() != 0) {
		icon->jumpTo(0, [=] { update(); });
	}
	const auto color = (isDisabled() || !over)
		? st::historyRecordVoiceFg->c
		: st::historyRecordVoiceFgOver->c;
	icon->paintInCenter(p, rect(), color);
}

void SendButton::paintSave(QPainter &p, bool over) {
	if (!isDisabled()) {
		auto color = _st.sendIconFg->c;
		color.setAlpha(25);
		paintRipple(
			p,
			(width() - _st.inner.rippleAreaSize) / 2,
			_st.inner.rippleAreaPosition.y(),
			&color);
	}
	const auto &saveIcon = over
		? st::historyEditSaveIconOver
		: st::historyEditSaveIcon;
	saveIcon.paintInCenter(p, rect());
}

void SendButton::paintCancel(QPainter &p, bool over) {
	paintRipple(
		p,
		(width() - _st.inner.rippleAreaSize) / 2,
		_st.inner.rippleAreaPosition.y());

	const auto &cancelIcon = over
		? st::historyReplyCancelIconOver
		: st::historyReplyCancelIcon;
	cancelIcon.paintInCenter(p, rect());
}

void SendButton::paintSend(QPainter &p, bool over) {
	const auto &sendIcon = over ? _st.inner.iconOver : _st.inner.icon;
	if (const auto padding = _st.sendIconFillPadding; padding > 0) {
		const auto ellipse = sendEllipseRect();
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			if (_state.fillBgOverride.isValid()) {
				p.setBrush(_state.fillBgOverride);
			} else {
				p.setBrush(st::windowBgActive);
			}
			p.drawEllipse(ellipse);
		}
		if (!isDisabled()) {
			auto color = _st.sendIconFg->c;
			color.setAlpha(25);
			paintRipple(p, ellipse.topLeft(), &color);
		}
	} else if (!isDisabled()) {
		auto color = _st.sendIconFg->c;
		color.setAlpha(25);
		paintRipple(
			p,
			(width() - _st.inner.rippleAreaSize) / 2,
			_st.inner.rippleAreaPosition.y(),
			&color);
	}
	if (isDisabled()) {
		const auto color = st::historyRecordVoiceFg->c;
		sendIcon.paint(p, _st.sendIconPosition, width(), color);
	} else {
		sendIcon.paint(p, _st.sendIconPosition, width());
	}
}

void SendButton::paintStarsToSend(QPainter &p, bool over) {
	const auto geometry = starsGeometry();
	{
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		if (_state.fillBgOverride.isValid()) {
			p.setBrush(_state.fillBgOverride);
		} else {
			p.setBrush(over ? _st.stars.textBgOver : _st.stars.textBg);
		}
		const auto radius = geometry.rounded.height() / 2;
		p.drawRoundedRect(geometry.rounded, radius, radius);
	}
	if (!isDisabled()) {
		auto color = _st.stars.textFg->c;
		color.setAlpha(25);
		paintRipple(p, geometry.rounded.topLeft(), &color);
	}
	p.setPen(over ? _st.stars.textFgOver : _st.stars.textFg);
	_starsToSendText.draw(p, {
		.position = geometry.inner.topLeft(),
		.outerWidth = width(),
		.availableWidth = geometry.inner.width(),
	});
}

void SendButton::paintSchedule(QPainter &p, bool over) {
	const auto ellipse = scheduleEllipseRect();
	{
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(over ? st::historySendIconFgOver : st::historySendIconFg);
		p.drawEllipse(ellipse);
	}
	if (!isDisabled()) {
		auto color = st::historyComposeAreaBg->c;
		color.setAlpha(25);
		paintRipple(p, ellipse.topLeft(), &color);
	}
	st::historyScheduleIcon.paint(
		p,
		st::historyScheduleIconPosition,
		width());
}

void SendButton::paintSlowmode(QPainter &p) {
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		rect().marginsRemoved(st::historySlowmodeCounterMargins),
		_slowmodeDelayText,
		style::al_center);
}

SendButton::StarsGeometry SendButton::starsGeometry() const {
	const auto &st = _st.stars;
	const auto inner = QRect(
		0,
		0,
		_starsToSendText.maxWidth(),
		st.style.font->height);
	const auto rounded = inner.marginsAdded(QMargins(
		st.padding.left() - st.width / 2,
		st.padding.top() + st.textTop,
		st.padding.right() - st.width / 2,
		st.height - st.padding.top() - st.textTop - st.style.font->height));
	const auto add = (_st.inner.height - rounded.height()) / 2;
	const auto outer = rounded.marginsAdded(QMargins(
		add,
		add,
		add,
		_st.inner.height - add - rounded.height()));
	const auto shift = -outer.topLeft();
	return {
		.inner = inner.translated(shift),
		.rounded = rounded.translated(shift),
		.outer = outer.translated(shift),
	};
}

SendButton::RippleShape SendButton::currentRippleShape() const {
	switch (_state.type) {
	case Type::Send:
		if (!_starsToSendText.isEmpty()) {
			return RippleShape::StarsRoundRect;
		} else if (_st.sendIconFillPadding > 0) {
			return RippleShape::SendEllipse;
		}
		return RippleShape::InnerEllipse;
	case Type::Schedule:
		return RippleShape::ScheduleEllipse;
	case Type::Save:
	case Type::Record:
	case Type::Round:
	case Type::Cancel:
	case Type::Slowmode:
	case Type::EditPrice:
		return RippleShape::InnerEllipse;
	}
	Unexpected("Type in SendButton::currentRippleShape.");
}

QRect SendButton::sendEllipseRect() const {
	const auto &sendIcon = _st.inner.icon;
	const auto padding = _st.sendIconFillPadding;
	return QRect(_st.sendIconPosition, sendIcon.size()).marginsAdded(
		{ padding, padding, padding, padding });
}

QRect SendButton::scheduleEllipseRect() const {
	return QRect(
		st::historyScheduleIconPosition,
		QSize(
			st::historyScheduleIcon.width(),
			st::historyScheduleIcon.height()));
}

void SendButton::updateSize() {
	if (_state.type == Type::EditPrice) {
		resize(0, _st.inner.height);
		return;
	}
	const auto finalWidth = _starsToSendText.isEmpty()
		? _st.inner.width
		: starsGeometry().outer.width();
	const auto progress = _stateChangeAnimation.value(1.);
	resize(
		anim::interpolate(_stateChangeFromWidth, finalWidth, progress),
		_st.inner.height);
}

QPixmap SendButton::grabContent() {
	auto result = QImage(
		kWideScale * size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		p.drawPixmap(
			(kWideScale - 1) / 2 * width(),
			(kWideScale - 1) / 2 * height(),
			GrabWidget(this));
	}
	return PixmapFromImage(std::move(result));
}

QImage SendButton::prepareRippleMask() const {
	switch (_lastRippleShape) {
	case RippleShape::InnerEllipse: {
		const auto size = _st.inner.rippleAreaSize;
		return RippleAnimation::EllipseMask(QSize(size, size));
	}
	case RippleShape::SendEllipse: {
		const auto r = sendEllipseRect();
		return RippleAnimation::EllipseMask(r.size());
	}
	case RippleShape::StarsRoundRect: {
		const auto r = starsGeometry().rounded;
		const auto radius = r.height() / 2;
		return RippleAnimation::RoundRectMask(r.size(), radius);
	}
	case RippleShape::ScheduleEllipse: {
		const auto r = scheduleEllipseRect();
		return RippleAnimation::EllipseMask(r.size());
	}
	}
	Unexpected("RippleShape in SendButton::prepareRippleMask.");
}

QPoint SendButton::prepareRippleStartPosition() const {
	const auto real = mapFromGlobal(QCursor::pos());
	switch (_lastRippleShape) {
	case RippleShape::InnerEllipse: {
		const auto size = _st.inner.rippleAreaSize;
		const auto y = (height() - size) / 2;
		return real - QPoint((width() - size) / 2, y);
	}
	case RippleShape::SendEllipse:
		return real - sendEllipseRect().topLeft();
	case RippleShape::StarsRoundRect:
		return real - starsGeometry().rounded.topLeft();
	case RippleShape::ScheduleEllipse:
		return real - scheduleEllipseRect().topLeft();
	}
	Unexpected("RippleShape in SendButton::prepareRippleStartPosition.");
}

void SendButton::initVoiceRoundIcon(int index) {
	Expects(index >= 0 && index < 2);

	_voiceRoundIcons[index] = Lottie::MakeIcon({
		.path = ((index == kVoiceToRoundIndex)
			? u":/animations/chat/voice_to_video.tgs"_q
			: u":/animations/chat/video_to_voice.tgs"_q),
		.sizeOverride = _st.recordSize,
		.colorizeUsingAlpha = true,
	});
}

void SendButton::paintVoiceRoundIcon(QPainter &p, bool over) {
	if (!isDisabled() && !_state.forbidden) {
		paintRipple(
			p,
			(width() - _st.inner.rippleAreaSize) / 2,
			_st.inner.rippleAreaPosition.y());
	}

	if (_state.forbidden) {
		p.setOpacity(kForbiddenOpacity);
	}
	const auto color = (isDisabled() || !over)
		? st::historyRecordVoiceFg->c
		: st::historyRecordVoiceFgOver->c;
	const auto toVideo = (_state.type == Type::Round);
	const auto index = toVideo ? kVoiceToRoundIndex : kRoundToVoiceIndex;
	_voiceRoundIcons[index]->paintInCenter(p, rect(), color);
	if (_state.forbidden) {
		p.setOpacity(1.);
	}
}

bool SendButton::isVoiceRoundTransition(Type from, Type to) {
	return (from == Type::Record && to == Type::Round)
		|| (from == Type::Round && to == Type::Record);
}

SendStarButton::SendStarButton(
	QWidget *parent,
	const style::IconButton &st,
	const style::RoundButton &counterSt,
	rpl::producer<SendStarButtonState> state)
: RippleButton(parent, st.ripple)
, _st(st)
, _counterSt(counterSt) {
	resize(_st.width, _st.height);

	std::move(state) | rpl::on_next([=](SendStarButtonState value) {
		setCount(value.count);
		highlight(value.highlight);
	}, lifetime());
}

void SendStarButton::paintEvent(QPaintEvent *e) {
	const auto ratio = style::DevicePixelRatio();
	const auto fullSize = size() * ratio;
	if (_frame.size() != fullSize) {
		_frame = QImage(fullSize, QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
	}
	_frame.fill(Qt::transparent);

	auto p = QPainter(&_frame);

	const auto highlighted = _highlight.value(_highlighted ? 1. : 0.);
	p.setPen(Qt::NoPen);
	p.setBrush(_counterSt.textBg);
	p.drawEllipse(rect());
	paintRipple(p, QPoint());
	if (highlighted > 0.) {
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::creditsBg3);
		p.setOpacity(highlighted);
		p.drawEllipse(rect());
		p.setOpacity(1.);
	}

	st::starIconEmoji.icon.paintInCenter(p, rect(), st::premiumButtonFg->c);

	if (!_starsText.isEmpty()) {
		auto hq = PainterHighQualityEnabler(p);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		auto pen = st::transparent->p;
		pen.setWidthF(_counterSt.numbersSkip);
		p.setPen(pen);
		p.setBrush(
			anim::brush(_counterSt.textBg, st::creditsBg3, highlighted));
		const auto size = QSize(
			_starsText.maxWidth(),
			_counterSt.style.font->height);
		const auto larger = size.grownBy(_counterSt.padding);
		const auto left = (width() - larger.width());
		const auto top = 0;
		const auto r = larger.height() / 2.;
		p.drawRoundedRect(left, top, larger.width(), larger.height(), r, r);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		p.setPen(
			anim::pen(_counterSt.textFg, st::premiumButtonFg, highlighted));
		_starsText.draw(p, {
			.position = QPoint(
				left + _counterSt.padding.left(),
				top + _counterSt.padding.top()),
			.availableWidth = _starsText.maxWidth(),
		});
	}

	QPainter(this).drawImage(0, 0, _frame);
}

void SendStarButton::setCount(int count) {
	if (_count == count) {
		return;
	}
	_count = count;
	if (_count) {
		_starsText.setText(
			_counterSt.style,
			Lang::FormatCountDecimal(_count));
		const auto sub = _counterSt.padding.left()
			+ _counterSt.padding.right();
		if (_starsText.maxWidth() > width() - sub) {
			_starsText.setText(
				_counterSt.style,
				Lang::FormatCountToShort(_count).string);
		}
	} else {
		_starsText = Text::String();
	}
	update();
}

void SendStarButton::highlight(bool enabled) {
	if (_highlighted == enabled) {
		return;
	}
	_highlighted = enabled;
	_highlight.start(
		[=] { update(); },
		enabled ? 0. : 1.,
		enabled ? 1. : 0.,
		360,
		anim::easeOutCirc);
}

QImage SendStarButton::prepareRippleMask() const {
	return RippleAnimation::EllipseMask(size());
}

QPoint SendStarButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

} // namespace Ui
