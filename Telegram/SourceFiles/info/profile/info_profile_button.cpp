/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_button.h"

#include <rpl/never.h>
#include "ui/widgets/checkbox.h"
#include "styles/style_info.h"

namespace Info {
namespace Profile {

Button::Button(
	QWidget *parent,
	rpl::producer<QString> &&text)
: Button(parent, std::move(text), st::infoProfileButton) {
}

Button::Button(
	QWidget *parent,
	rpl::producer<QString> &&text,
	const style::InfoProfileButton &st)
: RippleButton(parent, st.ripple)
, _st(st) {
	std::move(
		text
	) | rpl::start_with_next([this](QString &&value) {
		setText(std::move(value));
	}, lifetime());
}

Button *Button::toggleOn(rpl::producer<bool> &&toggled) {
	Expects(_toggle == nullptr);
	_toggle = std::make_unique<Ui::ToggleView>(
		isOver() ? _st.toggleOver : _st.toggle,
		false,
		[this] { rtlupdate(toggleRect()); });
	addClickHandler([this] {
		_toggle->setChecked(!_toggle->checked(), anim::type::normal);
	});
	std::move(
		toggled
	) | rpl::start_with_next([this](bool toggled) {
		_toggle->setChecked(toggled, anim::type::normal);
	}, lifetime());
	_toggle->finishAnimating();
	return this;
}

bool Button::toggled() const {
	return _toggle ? _toggle->checked() : false;
}

rpl::producer<bool> Button::toggledChanges() const {
	if (_toggle) {
		return _toggle->checkedChanges();
	}
	return rpl::never<bool>();
}

rpl::producer<bool> Button::toggledValue() const {
	if (_toggle) {
		return _toggle->checkedValue();
	}
	return rpl::never<bool>();
}

void Button::setColorOverride(std::optional<QColor> textColorOverride) {
	_textColorOverride = textColorOverride;
	update();
}

void Button::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto paintOver = (isOver() || isDown()) && !isDisabled();
	p.fillRect(e->rect(), paintOver ? _st.textBgOver : _st.textBg);

	paintRipple(p, 0, 0, ms);

	auto outerw = width();
	p.setFont(_st.font);
	p.setPen(_textColorOverride
		? QPen(*_textColorOverride)
		: paintOver
		? _st.textFgOver
		: _st.textFg);
	p.drawTextLeft(
		_st.padding.left(),
		_st.padding.top(),
		outerw,
		_text,
		_textWidth);

	if (_toggle) {
		auto rect = toggleRect();
		_toggle->paint(p, rect.left(), rect.top(), outerw, ms);
	}
}

QRect Button::toggleRect() const {
	Expects(_toggle != nullptr);

	auto size = _toggle->getSize();
	auto left = width() - _st.toggleSkip - size.width();
	auto top = (height() - size.height()) / 2;
	return { QPoint(left, top), size };
}

int Button::resizeGetHeight(int newWidth) {
	updateVisibleText(newWidth);
	return _st.padding.top() + _st.height + _st.padding.bottom();
}

void Button::onStateChanged(
		State was,
		StateChangeSource source) {
	if (!isDisabled() || !isDown()) {
		RippleButton::onStateChanged(was, source);
	}
	if (_toggle) {
		_toggle->setStyle(isOver() ? _st.toggleOver : _st.toggle);
	}
	setPointerCursor(!isDisabled());
}

void Button::setText(QString &&text) {
	_original = std::move(text);
	_originalWidth = _st.font->width(_original);
	updateVisibleText(width());
}

void Button::updateVisibleText(int newWidth) {
	auto availableWidth = newWidth
		- _st.padding.left()
		- _st.padding.right();
	if (_toggle) {
		availableWidth -= (width() - toggleRect().x());
	}
	accumulate_max(availableWidth, 0);
	if (availableWidth < _originalWidth) {
		_text = _st.font->elided(_original, availableWidth);
		_textWidth = _st.font->width(_text);
	} else {
		_text = _original;
		_textWidth = _originalWidth;
	}
	update();
}

} // namespace Profile
} // namespace Info
