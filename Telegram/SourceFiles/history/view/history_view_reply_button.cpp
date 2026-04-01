/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_reply_button.h"

#include "history/view/history_view_cursor_state.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/ripple_animation.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"
#include "styles/style_widgets.h"

namespace HistoryView::ReplyButton {
namespace {

constexpr auto kToggleDuration = crl::time(120);
constexpr auto kButtonShowDelay = crl::time(0);
constexpr auto kButtonHideDelay = crl::time(300);

[[nodiscard]] float64 ScaleForState(ButtonState state) {
	switch (state) {
	case ButtonState::Hidden: return 0.;
	case ButtonState::Shown:
	case ButtonState::Active:
	case ButtonState::Inside: return 1.;
	}
	Unexpected("State in ReplyButton::ScaleForState.");
}

[[nodiscard]] float64 OpacityForScale(float64 scale) {
	return scale;
}

[[nodiscard]] QSize ComputeInnerSize() {
	return QSize(ComputeInnerWidth(), st::replyCornerHeight);
}

[[nodiscard]] QSize ComputeOuterSize() {
	return QRect(
		QPoint(),
		ComputeInnerSize()
	).marginsAdded(st::replyCornerShadow).size();
}

} // namespace

int ComputeInnerWidth() {
	struct Cached {
		QString text;
		int result = 0;
	};
	static auto cached = Cached();
	const auto &text = tr::lng_fast_reply(tr::now);
	if (cached.text != text) {
		const auto &padding = st::replyCornerTextPadding;
		const auto textWidth = st::msgDateTextStyle.font->width(text);
		cached.result = padding.left() + textWidth + padding.right();
		cached.text = text;
	}
	return cached.result;
}

Button::Button(
	Fn<void(QRect)> update,
	ButtonParameters parameters,
	Fn<void()> hide,
	QSize outer)
: _update(std::move(update))
, _finalScale(ScaleForState(_state))
, _collapsed(QPoint(), outer)
, _geometry(_collapsed)
, _hideTimer(hide) {
	applyParameters(parameters, nullptr);
}

Button::~Button() = default;

bool Button::isHidden() const {
	return (_state == ButtonState::Hidden)
		&& !_opacityAnimation.animating();
}

QRect Button::geometry() const {
	return _geometry;
}

float64 Button::currentScale() const {
	return _scaleAnimation.value(_finalScale);
}

float64 Button::currentOpacity() const {
	return _opacityAnimation.value(
		OpacityForScale(ScaleForState(_state)));
}

void Button::applyParameters(ButtonParameters parameters) {
	applyParameters(std::move(parameters), _update);
}

void Button::applyParameters(
		ButtonParameters parameters,
		Fn<void(QRect)> update) {
	const auto shift = parameters.center - _collapsed.center();
	_collapsed = _collapsed.translated(shift);
	updateGeometry(update);
	applyState(ButtonState::Shown, update);
	if (parameters.outside) {
		_hideTimer.callOnce(kButtonHideDelay);
	} else {
		_hideTimer.cancel();
	}
}

void Button::updateGeometry(Fn<void(QRect)> update) {
	if (_geometry != _collapsed) {
		if (update) {
			update(_geometry);
		}
		_geometry = _collapsed;
		if (update) {
			update(_geometry);
		}
	}
}

void Button::applyState(ButtonState state) {
	applyState(state, _update);
}

void Button::applyState(ButtonState state, Fn<void(QRect)> update) {
	if (state == ButtonState::Hidden) {
		_hideTimer.cancel();
	}
	updateGeometry(update);
	if (_state == state) {
		return;
	}
	const auto finalScale = ScaleForState(state);
	_opacityAnimation.start(
		[=] { _update(_geometry); },
		OpacityForScale(ScaleForState(_state)),
		OpacityForScale(ScaleForState(state)),
		kToggleDuration,
		anim::sineInOut);
	if (state != ButtonState::Hidden && _finalScale != finalScale) {
		_scaleAnimation.start(
			[=] { _update(_geometry); },
			_finalScale,
			finalScale,
			kToggleDuration,
			anim::sineInOut);
		_finalScale = finalScale;
	}
	_state = state;
}

Manager::Manager(Fn<void(QRect)> buttonUpdate)
: _outer(ComputeOuterSize())
, _inner(QRect(QPoint(), ComputeInnerSize()))
, _cachedRound(
	ComputeInnerSize(),
	st::replyCornerShadow,
	ComputeInnerSize().height())
, _buttonShowTimer([=] { showButtonDelayed(); })
, _buttonUpdate(std::move(buttonUpdate))
, _text(st::msgDateTextStyle.font->width(tr::lng_fast_reply(tr::now))) {
	_text.setText(st::msgDateTextStyle, tr::lng_fast_reply(tr::now));
	_inner.translate(
		QRect(QPoint(), _outer).center() - _inner.center());
}

Manager::~Manager() = default;

void Manager::updateButton(ButtonParameters parameters) {
	const auto contextChanged = (_buttonContext != parameters.context);
	if (contextChanged) {
		if (_button) {
			_button->applyState(ButtonState::Hidden);
			_buttonHiding.push_back(std::move(_button));
		}
		_buttonShowTimer.cancel();
		_scheduledParameters = std::nullopt;
		_ripple = nullptr;
	}
	_buttonContext = parameters.context;
	_lastPointer = parameters.pointer;
	if (parameters.link) {
		_link = parameters.link;
	}
	if (!_buttonContext) {
		return;
	} else if (_button) {
		_button->applyParameters(parameters);
		return;
	} else if (parameters.outside) {
		_buttonShowTimer.cancel();
		_scheduledParameters = std::nullopt;
		return;
	}
	const auto globalPositionChanged = _scheduledParameters
		&& (_scheduledParameters->globalPointer
			!= parameters.globalPointer);
	const auto positionChanged = _scheduledParameters
		&& (_scheduledParameters->pointer != parameters.pointer);
	_scheduledParameters = parameters;
	if ((_buttonShowTimer.isActive() && positionChanged)
		|| globalPositionChanged) {
		_buttonShowTimer.callOnce(kButtonShowDelay);
	}
}

void Manager::showButtonDelayed() {
	clearAppearAnimations();
	_button = std::make_unique<Button>(
		_buttonUpdate,
		*_scheduledParameters,
		[=] { updateButton({}); },
		_outer);
}

void Manager::paint(QPainter &p, const PaintContext &context) {
	removeStaleButtons();
	for (const auto &button : _buttonHiding) {
		paintButton(p, context, button.get());
	}
	if (const auto current = _button.get()) {
		if (context.gestureHorizontal.ratio) {
			current->applyState(ButtonState::Hidden);
			_buttonHiding.push_back(std::move(_button));
		}
		paintButton(p, context, current);
	}
}

void Manager::paintButton(
		QPainter &p,
		const PaintContext &context,
		not_null<Button*> button) {
	const auto geometry = button->geometry();
	if (!context.clip.intersects(geometry)) {
		return;
	}
	constexpr auto kFramesCount = Ui::RoundAreaWithShadow::kFramesCount;
	const auto scale = button->currentScale();
	const auto scaleMin = ScaleForState(ButtonState::Hidden);
	const auto scaleMax = ScaleForState(ButtonState::Shown);
	const auto progress = (scale - scaleMin) / (scaleMax - scaleMin);
	const auto frame = int(
		base::SafeRound(progress * (kFramesCount - 1)));
	const auto useScale = scaleMin
		+ (frame / float64(kFramesCount - 1))
			* (scaleMax - scaleMin);
	paintButton(p, context, button, frame, useScale);
}

void Manager::paintButton(
		QPainter &p,
		const PaintContext &context,
		not_null<Button*> button,
		int frameIndex,
		float64 scale) {
	const auto opacity = button->currentOpacity();
	if (opacity == 0.) {
		return;
	}
	const auto geometry = button->geometry();
	const auto position = geometry.topLeft();
	if (opacity != 1.) {
		p.setOpacity(opacity);
	}
	const auto shadow = context.st->shadowFg()->c;
	const auto background = context.st->windowBg()->c;
	_cachedRound.setShadowColor(shadow);
	_cachedRound.setBackgroundColor(background);
	const auto radius = _inner.height() / 2.;
	const auto frame = _cachedRound.validateFrame(
		frameIndex,
		scale,
		radius);
	p.drawImage(position, *frame.image, frame.rect);

	if (_ripple && !_ripple->empty() && _button && button == _button.get()) {
		const auto color = context.st->windowBgOver()->c;
		_ripple->paint(
			p,
			position.x() + _inner.x(),
			position.y() + _inner.y(),
			_inner.width(),
			&color);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}

	const auto textLeft = position.x()
		+ _inner.x()
		+ st::replyCornerTextPadding.left();
	const auto textTop = position.y()
		+ _inner.y()
		+ (_inner.height() - st::msgDateTextStyle.font->height) / 2;
	const auto &incomingStyle = context.st->messageStyle(false, false);
	p.setPen(incomingStyle.msgDateFg);
	p.setFont(st::msgDateTextStyle.font);
	_text.draw(p, {
		.position = QPoint(textLeft, textTop),
		.availableWidth = _text.maxWidth(),
	});
	if (opacity != 1.) {
		p.setOpacity(1.);
	}
}

TextState Manager::buttonTextState(QPoint position) const {
	if (overCurrentButton(position)) {
		auto result = TextState(nullptr, _link);
		result.itemId = _buttonContext;
		return result;
	}
	return {};
}

bool Manager::overCurrentButton(QPoint position) const {
	if (!_button) {
		return false;
	}
	return buttonInner().contains(position);
}

QMargins Manager::innerMargins() const {
	return {
		_inner.x(),
		_inner.y(),
		_outer.width() - _inner.x() - _inner.width(),
		_outer.height() - _inner.y() - _inner.height(),
	};
}

QRect Manager::buttonInner() const {
	return buttonInner(_button.get());
}

QRect Manager::buttonInner(not_null<Button*> button) const {
	return button->geometry().marginsRemoved(innerMargins());
}

void Manager::remove(FullMsgId context) {
	if (_buttonContext == context) {
		_buttonContext = {};
		_button = nullptr;
		_ripple = nullptr;
	}
}

void Manager::clickHandlerPressedChanged(
		const ClickHandlerPtr &action,
		bool pressed) {
	if (action != _link || !_button) {
		return;
	}
	if (pressed) {
		const auto inner = buttonInner();
		if (!_ripple) {
			const auto mask = Ui::RippleAnimation::RoundRectMask(
				inner.size(),
				inner.height() / 2);
			_ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				mask,
				[=] { if (_button) _buttonUpdate(_button->geometry()); });
		}
		_ripple->add(_lastPointer - inner.topLeft());
	} else if (_ripple) {
		_ripple->lastStop();
	}
}

void Manager::removeStaleButtons() {
	_buttonHiding.erase(
		ranges::remove_if(_buttonHiding, &Button::isHidden),
		end(_buttonHiding));
}

void Manager::clearAppearAnimations() {
	for (const auto &button : base::take(_buttonHiding)) {
		if (!button->isHidden()) {
			button->repaint();
		}
	}
}

} // namespace HistoryView::ReplyButton
