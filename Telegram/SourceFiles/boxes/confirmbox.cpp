/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "confirmbox.h"
#include "mainwidget.h"
#include "window.h"

#include "application.h"

TextParseOptions _confirmBoxTextOptions = {
	TextParseLinks | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

ConfirmBox::ConfirmBox(const QString &text, const QString &doneText, const style::BoxButton &doneStyle, const QString &cancelText, const style::BoxButton &cancelStyle) : AbstractBox(st::boxWidth), 
_informative(false),
_text(100),
_confirm(this, doneText.isEmpty() ? lang(lng_box_ok) : doneText, doneStyle),
_cancel(this, cancelText.isEmpty() ? lang(lng_cancel) : cancelText, cancelStyle) {
	init(text);
}

ConfirmBox::ConfirmBox(const QString &text, const QString &doneText, const style::BoxButton &doneStyle, bool informative) : AbstractBox(st::boxWidth),
_informative(true),
_text(100),
_confirm(this, doneText.isEmpty() ? lang(lng_box_ok) : doneText, doneStyle),
_cancel(this, QString(), st::cancelBoxButton) {
	init(text);
}

void ConfirmBox::init(const QString &text) {
	_text.setText(st::boxTextFont, text, _informative ? _confirmBoxTextOptions : _textPlainOptions);

	textstyleSet(&st::boxTextStyle);
	_textWidth = st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right();
	_textHeight = qMin(_text.countHeight(_textWidth), 16 * int(st::boxTextStyle.lineHeight));
	setMaxHeight(st::boxPadding.top() + _textHeight + st::boxPadding.bottom() + st::boxButtonPadding.top() + _confirm.height() + st::boxButtonPadding.bottom());
	textstyleRestore();

	connect(&_confirm, SIGNAL(clicked()), this, SIGNAL(confirmed()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onCancel()));
	if (_informative) {
		_cancel.hide();
		connect(this, SIGNAL(confirmed()), this, SLOT(onCancel()));
	}
	setMouseTracking(_text.hasLinks());

	prepare();
}

void ConfirmBox::onCancel() {
	emit cancelPressed();
	onClose();
}

void ConfirmBox::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
}

void ConfirmBox::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	if (textlnkOver()) {
		textlnkDown(textlnkOver());
		update();
	}
	return LayeredWidget::mousePressEvent(e);
}

void ConfirmBox::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	if (textlnkOver() && textlnkOver() == textlnkDown()) {
		App::wnd()->hideLayer();
		textlnkOver()->onClick(e->button());
	}
	textlnkDown(TextLinkPtr());
}

void ConfirmBox::leaveEvent(QEvent *e) {
	if (_myLink) {
		if (textlnkOver() == _myLink) {
			textlnkOver(TextLinkPtr());
			update();
		}
		_myLink = TextLinkPtr();
		setCursor(style::cur_default);
		update();
	}
}

void ConfirmBox::updateLink() {
	_lastMousePos = QCursor::pos();
	updateHover();
}

void ConfirmBox::updateHover() {
	QPoint m(mapFromGlobal(_lastMousePos));
	bool wasMy = (_myLink == textlnkOver());
	textstyleSet(&st::boxTextStyle);
	_myLink = _text.linkLeft(m.x() - st::boxPadding.left(), m.y() - st::boxPadding.top(), _textWidth, width(), (_text.maxWidth() < width()) ? style::al_center : style::al_left);
	textstyleRestore();
	if (_myLink != textlnkOver()) {
		if (wasMy || _myLink || rect().contains(m)) {
			textlnkOver(_myLink);
		}
		setCursor(_myLink ? style::cur_pointer : style::cur_default);
		update();
	}
}

void ConfirmBox::closePressed() {
	emit cancelled();
}

void ConfirmBox::hideAll() {
	_confirm.hide();
	_cancel.hide();
}

void ConfirmBox::showAll() {
	if (_informative) {
		_confirm.show();
	} else {
		_confirm.show();
		_cancel.show();
	}
}

void ConfirmBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		emit confirmed();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void ConfirmBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (paint(p)) return;

	// draw box title / text
	p.setPen(st::black->p);
	textstyleSet(&st::boxTextStyle);
	_text.drawLeftElided(p, st::boxPadding.left(), st::boxPadding.top(), _textWidth, width(), 16, style::al_left);
	textstyleRestore();
}

void ConfirmBox::resizeEvent(QResizeEvent *e) {
	_confirm.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _confirm.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _confirm.width() + st::boxButtonPadding.left(), _confirm.y());
}

ConfirmLinkBox::ConfirmLinkBox(const QString &url) : ConfirmBox(lang(lng_open_this_link) + qsl("\n\n") + url, lang(lng_open_link)), _url(url) {
	connect(this, SIGNAL(confirmed()), this, SLOT(onOpenLink()));
}

void ConfirmLinkBox::onOpenLink() {
	if (reMailStart().match(_url).hasMatch()) {
		EmailLink(_url).onClick(Qt::LeftButton);
	} else {
		TextLink(_url).onClick(Qt::LeftButton);
	}
	App::wnd()->hideLayer();
}

MaxInviteBox::MaxInviteBox(const QString &link) : AbstractBox(st::boxWidth),
_close(this, lang(lng_box_ok), st::defaultBoxButton),
_text(st::boxTextFont, lng_participant_invite_sorry(lt_count, cMaxGroupCount()), _confirmBoxTextOptions, st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right()),
_link(link), _linkOver(false),
a_goodOpacity(0, 0), a_good(animFunc(this, &MaxInviteBox::goodAnimStep)) {
	setMouseTracking(true);

	_textWidth = st::boxWidth - st::boxPadding.left() - st::boxButtonPadding.right();
	_textHeight = qMin(_text.countHeight(_textWidth), 16 * int(st::boxTextStyle.lineHeight));
	setMaxHeight(st::boxPadding.top() + _textHeight + st::boxTextFont->height + st::boxTextFont->height * 2 + st::newGroupLinkPadding.bottom() + st::boxButtonPadding.top() + _close.height() + st::boxButtonPadding.bottom());

	connect(&_close, SIGNAL(clicked()), this, SLOT(onClose()));

	prepare();
}

void MaxInviteBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void MaxInviteBox::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_linkOver) {
		App::app()->clipboard()->setText(_link);
		_goodTextLink = lang(lng_create_channel_link_copied);
		a_goodOpacity = anim::fvalue(1, 0);
		a_good.start();
	}
}

void MaxInviteBox::leaveEvent(QEvent *e) {
	updateSelected(QCursor::pos());
}

void MaxInviteBox::updateSelected(const QPoint &cursorGlobalPosition) {
	QPoint p(mapFromGlobal(cursorGlobalPosition));

	bool linkOver = _invitationLink.contains(p);
	if (linkOver != _linkOver) {
		_linkOver = linkOver;
		update();
		setCursor(_linkOver ? style::cur_pointer : style::cur_default);
	}
}

bool MaxInviteBox::goodAnimStep(float64 ms) {
	float dt = ms / st::newGroupLinkFadeDuration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_goodOpacity.finish();
	} else {
		a_goodOpacity.update(dt, anim::linear);
	}
	update();
	return res;
}

void MaxInviteBox::hideAll() {
	_close.hide();
}

void MaxInviteBox::showAll() {
	_close.show();
}

void MaxInviteBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	// draw box title / text
	p.setPen(st::black->p);
	_text.drawLeftElided(p, st::boxPadding.left(), st::boxPadding.top(), _textWidth, width(), 16, style::al_left);

	QTextOption option(style::al_left);
	option.setWrapMode(QTextOption::WrapAnywhere);
	p.setFont(_linkOver ? st::defaultInputField.font->underline() : st::defaultInputField.font);
	p.setPen(st::btnDefLink.color);
	p.drawText(_invitationLink, _link, option);
	if (!_goodTextLink.isEmpty() && a_goodOpacity.current() > 0) {
		p.setOpacity(a_goodOpacity.current());
		p.setPen(st::setGoodColor->p);
		p.setFont(st::boxTextFont->f);
		p.drawTextLeft(st::boxPadding.left(), height() - st::boxButtonPadding.bottom() - _close.height() + st::defaultBoxButton.textTop + st::defaultBoxButton.font->ascent - st::boxTextFont->ascent, width(), _goodTextLink);
		p.setOpacity(1);
	}
}

void MaxInviteBox::resizeEvent(QResizeEvent *e) {
	_close.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _close.height());
	_invitationLink = myrtlrect(st::boxPadding.left(), st::boxPadding.top() + _textHeight + st::boxTextFont->height, width() - st::boxPadding.left() - st::boxPadding.right(), 2 * st::boxTextFont->height);
}
