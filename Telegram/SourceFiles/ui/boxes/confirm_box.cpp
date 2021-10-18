/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/confirm_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Ui {
namespace {

TextParseOptions kInformBoxTextOptions = {
	(TextParseLinks
		| TextParseMultiline
		| TextParseMarkdown
		| TextParseRichText), // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TextParseOptions kMarkedTextBoxOptions = {
	(TextParseLinks
		| TextParseMultiline
		| TextParseMarkdown
		| TextParseRichText
		| TextParseMentions
		| TextParseHashtags), // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

} // namespace

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(tr::lng_box_ok(tr::now))
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth
	- st::boxPadding.left()
	- st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth
	- st::boxPadding.left()
	- st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const TextWithEntities &text,
	const QString &confirmText,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth
	- st::boxPadding.left()
	- st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	const style::RoundButton &confirmStyle,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(confirmStyle)
, _text(st::boxWidth
	- st::boxPadding.left()
	- st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	const QString &cancelText,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(cancelText)
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth
	- st::boxPadding.left()
	- st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	const style::RoundButton &confirmStyle,
	const QString &cancelText,
	ConfirmBox::ConfirmedCallback confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(cancelText)
, _confirmStyle(st::defaultBoxButton)
, _text(st::boxWidth
	- st::boxPadding.left()
	- st::defaultBox.buttonPadding.right())
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	const InformBoxTag &,
	const QString &text,
	const QString &doneText,
	Fn<void()> closedCallback)
: _confirmText(doneText)
, _confirmStyle(st::defaultBoxButton)
, _informative(true)
, _text(st::boxWidth
	- st::boxPadding.left()
	- st::defaultBox.buttonPadding.right())
, _confirmedCallback(generateInformCallback(closedCallback))
, _cancelledCallback(generateInformCallback(closedCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	const InformBoxTag &,
	const TextWithEntities &text,
	const QString &doneText,
	Fn<void()> closedCallback)
: _confirmText(doneText)
, _confirmStyle(st::defaultBoxButton)
, _informative(true)
, _text(st::boxWidth
	- st::boxPadding.left()
	- st::defaultBox.buttonPadding.right())
, _confirmedCallback(generateInformCallback(closedCallback))
, _cancelledCallback(generateInformCallback(closedCallback)) {
	init(text);
}

FnMut<void()> ConfirmBox::generateInformCallback(
		Fn<void()> closedCallback) {
	return crl::guard(this, [=] {
		closeBox();
		if (closedCallback) {
			closedCallback();
		}
	});
}

void ConfirmBox::init(const QString &text) {
	_text.setText(
		st::boxLabelStyle,
		text,
		_informative ? kInformBoxTextOptions : _textPlainOptions);
}

void ConfirmBox::init(const TextWithEntities &text) {
	_text.setMarkedText(st::boxLabelStyle, text, kMarkedTextBoxOptions);
}

void ConfirmBox::prepare() {
	addButton(
		rpl::single(_confirmText),
		[=] { confirmed(); },
		_confirmStyle);
	if (!_informative) {
		addButton(
			rpl::single(_cancelText),
			[=] { _cancelled = true; closeBox(); });
	}

	boxClosing() | rpl::start_with_next([=] {
		if (!_confirmed && (!_strictCancel || _cancelled)) {
			if (auto callback = std::move(_cancelledCallback)) {
				callback();
			}
		}
	}, lifetime());

	textUpdated();
}

void ConfirmBox::setMaxLineCount(int count) {
	if (_maxLineCount != count) {
		_maxLineCount = count;
		textUpdated();
	}
}

void ConfirmBox::textUpdated() {
	_textWidth = st::boxWidth
		- st::boxPadding.left()
		- st::defaultBox.buttonPadding.right();
	_textHeight = _text.countHeight(_textWidth);
	if (_maxLineCount > 0) {
		accumulate_min(
			_textHeight,
			_maxLineCount * st::boxLabelStyle.lineHeight);
	}
	setDimensions(
		st::boxWidth,
		st::boxPadding.top() + _textHeight + st::boxPadding.bottom());

	setMouseTracking(_text.hasLinks());
}

void ConfirmBox::confirmed() {
	if (!_confirmed) {
		_confirmed = true;

		const auto confirmed = &_confirmedCallback;
		if (const auto callbackPtr = std::get_if<1>(confirmed)) {
			if (auto callback = base::take(*callbackPtr)) {
				callback();
			}
		} else if (const auto callbackPtr = std::get_if<2>(confirmed)) {
			if (auto callback = base::take(*callbackPtr)) {
				const auto weak = Ui::MakeWeak(this);
				callback(crl::guard(weak, [=] { closeBox(); }));
			}
		}
	}
}

void ConfirmBox::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
}

void ConfirmBox::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	ClickHandler::pressed();
	return BoxContent::mousePressEvent(e);
}

void ConfirmBox::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	if (const auto activated = ClickHandler::unpressed()) {
		ActivateClickHandler(window(), activated, e->button());
		crl::on_main(this, [=] {
			closeBox();
		});
		return;
	}
	BoxContent::mouseReleaseEvent(e);
}

void ConfirmBox::leaveEventHook(QEvent *e) {
	ClickHandler::clearActive(this);
}

void ConfirmBox::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
	setCursor(active ? style::cur_pointer : style::cur_default);
	update();
}

void ConfirmBox::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	update();
}

void ConfirmBox::updateLink() {
	_lastMousePos = QCursor::pos();
	updateHover();
}

void ConfirmBox::updateHover() {
	const auto m = mapFromGlobal(_lastMousePos);
	const auto state = _text.getStateLeft(
		m - QPoint(st::boxPadding.left(), st::boxPadding.top()),
		_textWidth,
		width());

	ClickHandler::setActive(state.link, this);
}

void ConfirmBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		confirmed();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void ConfirmBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	// draw box title / text
	p.setPen(st::boxTextFg);
	if (_maxLineCount > 0) {
		_text.drawLeftElided(
			p,
			st::boxPadding.left(),
			st::boxPadding.top(),
			_textWidth,
			width(),
			_maxLineCount,
			style::al_left);
	} else {
		_text.drawLeft(
			p,
			st::boxPadding.left(),
			st::boxPadding.top(),
			_textWidth,
			width(),
			style::al_left);
	}
}

InformBox::InformBox(
	QWidget*,
	const QString &text, Fn<void()> closedCallback)
: ConfirmBox(
	ConfirmBox::InformBoxTag(),
	text,
	tr::lng_box_ok(tr::now),
	std::move(closedCallback)) {
}

InformBox::InformBox(
	QWidget*,
	const QString &text,
	const QString &doneText,
	Fn<void()> closedCallback)
: ConfirmBox(
	ConfirmBox::InformBoxTag(),
	text,
	doneText,
	std::move(closedCallback)) {
}

InformBox::InformBox(
	QWidget*,
	const TextWithEntities &text,
	Fn<void()> closedCallback)
: ConfirmBox(
	ConfirmBox::InformBoxTag(),
	text,
	tr::lng_box_ok(tr::now),
	std::move(closedCallback)) {
}

InformBox::InformBox(
	QWidget*,
	const TextWithEntities &text,
	const QString &doneText,
	Fn<void()> closedCallback)
: ConfirmBox(
	ConfirmBox::InformBoxTag(),
	text,
	doneText,
	std::move(closedCallback)) {
}

ConfirmDontWarnBox::ConfirmDontWarnBox(
	QWidget*,
	rpl::producer<TextWithEntities> text,
	const QString &checkbox,
	rpl::producer<QString> confirm,
	FnMut<void(bool)> callback)
: _confirm(std::move(confirm))
, _content(setupContent(std::move(text), checkbox, std::move(callback))) {
}

void ConfirmDontWarnBox::prepare() {
	setDimensionsToContent(st::boxWidth, _content);
	addButton(std::move(_confirm), [=] { _callback(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });
}

not_null<Ui::RpWidget*> ConfirmDontWarnBox::setupContent(
		rpl::producer<TextWithEntities> text,
		const QString &checkbox,
		FnMut<void(bool)> callback) {
	const auto result = Ui::CreateChild<Ui::VerticalLayout>(this);
	result->add(
		object_ptr<Ui::FlatLabel>(
			result,
			std::move(text),
			st::boxLabel),
		st::boxPadding);
	const auto control = result->add(
		object_ptr<Ui::Checkbox>(
			result,
			checkbox,
			false,
			st::defaultBoxCheckbox),
		style::margins(
			st::boxPadding.left(),
			st::boxPadding.bottom(),
			st::boxPadding.right(),
			st::boxPadding.bottom()));
	_callback = [=, callback = std::move(callback)]() mutable {
		const auto checked = control->checked();
		auto local = std::move(callback);
		closeBox();
		local(checked);
	};
	return result;
}

} // namespace Ui
