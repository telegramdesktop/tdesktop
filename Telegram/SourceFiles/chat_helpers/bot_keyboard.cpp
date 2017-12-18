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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "chat_helpers/bot_keyboard.h"

#include "styles/style_widgets.h"
#include "styles/style_history.h"

BotKeyboard::BotKeyboard(QWidget *parent) : TWidget(parent)
, _st(&st::botKbButton) {
	setGeometry(0, 0, _st->margin, st::botKbScroll.deltat);
	_height = st::botKbScroll.deltat;
	setMouseTracking(true);
}

void BotKeyboard::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();
	p.fillRect(clip, st::historyComposeAreaBg);

	if (_impl) {
		int x = rtl() ? st::botKbScroll.width : _st->margin;
		p.translate(x, st::botKbScroll.deltat);
		_impl->paint(p, width(), clip.translated(-x, -st::botKbScroll.deltat), getms());
	}
}

void BotKeyboard::Style::startPaint(Painter &p) const {
	p.setPen(st::botKbColor);
	p.setFont(st::botKbStyle.font);
}

const style::TextStyle &BotKeyboard::Style::textStyle() const {
	return st::botKbStyle;
}

void BotKeyboard::Style::repaint(not_null<const HistoryItem*> item) const {
	_parent->update();
}

int BotKeyboard::Style::buttonRadius() const {
	return st::buttonRadius;
}

void BotKeyboard::Style::paintButtonBg(Painter &p, const QRect &rect, float64 howMuchOver) const {
	App::roundRect(p, rect, st::botKbBg, BotKeyboardCorners);
}

void BotKeyboard::Style::paintButtonIcon(Painter &p, const QRect &rect, int outerWidth, HistoryMessageReplyMarkup::Button::Type type) const {
	// Buttons with icons should not appear here.
}

void BotKeyboard::Style::paintButtonLoading(Painter &p, const QRect &rect) const {
	// Buttons with loading progress should not appear here.
}

int BotKeyboard::Style::minButtonWidth(HistoryMessageReplyMarkup::Button::Type type) const {
	int result = 2 * buttonPadding();
	return result;
}

void BotKeyboard::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();

	ClickHandler::pressed();
}

void BotKeyboard::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void BotKeyboard::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();

	if (ClickHandlerPtr activated = ClickHandler::unpressed()) {
		App::activateClickHandler(activated, e->button());
	}
}

void BotKeyboard::enterEventHook(QEvent *e) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void BotKeyboard::leaveEventHook(QEvent *e) {
	clearSelection();
}

bool BotKeyboard::moderateKeyActivate(int key) {
	if (const auto item = App::histItemById(_wasForMsgId)) {
		if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (key >= Qt::Key_1 && key <= Qt::Key_9) {
				const auto index = int(key - Qt::Key_1);
				if (!markup->rows.empty()
					&& index >= 0
					&& index < int(markup->rows.front().size())) {
					App::activateBotCommand(item, 0, index);
					return true;
				}
			} else if (key == Qt::Key_Q) {
				if (const auto user = item->history()->peer->asUser()) {
					if (user->botInfo && item->from() == user) {
						App::sendBotCommand(user, user, qsl("/translate"));
						return true;
					}
				}
			}
		}
	}
	return false;
}

void BotKeyboard::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (!_impl) return;
	_impl->clickHandlerActiveChanged(p, active);
}

void BotKeyboard::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (!_impl) return;
	_impl->clickHandlerPressedChanged(p, pressed);
}

bool BotKeyboard::updateMarkup(HistoryItem *to, bool force) {
	if (!to || !to->definesReplyKeyboard()) {
		if (_wasForMsgId.msg) {
			_maximizeSize = _singleUse = _forceReply = false;
			_wasForMsgId = FullMsgId();
			_impl = nullptr;
			return true;
		}
		return false;
	}

	if (_wasForMsgId == FullMsgId(to->channelId(), to->id) && !force) {
		return false;
	}

	_wasForMsgId = FullMsgId(to->channelId(), to->id);

	auto markupFlags = to->replyKeyboardFlags();
	_forceReply = markupFlags & MTPDreplyKeyboardMarkup_ClientFlag::f_force_reply;
	_maximizeSize = !(markupFlags & MTPDreplyKeyboardMarkup::Flag::f_resize);
	_singleUse = _forceReply || (markupFlags & MTPDreplyKeyboardMarkup::Flag::f_single_use);

	_impl = nullptr;
	if (auto markup = to->Get<HistoryMessageReplyMarkup>()) {
		if (!markup->rows.empty()) {
			_impl = std::make_unique<ReplyKeyboard>(
				to,
				std::make_unique<Style>(this, *_st));
		}
	}

	resizeToWidth(width(), _maxOuterHeight);

	return true;
}

bool BotKeyboard::hasMarkup() const {
	return _impl != nullptr;
}

bool BotKeyboard::forceReply() const {
	return _forceReply;
}

int BotKeyboard::resizeGetHeight(int newWidth) {
	updateStyle(newWidth);
	_height = st::botKbScroll.deltat + st::botKbScroll.deltab + (_impl ? _impl->naturalHeight() : 0);
	if (_maximizeSize) {
		accumulate_max(_height, _maxOuterHeight);
	}
	if (_impl) {
		int implWidth = newWidth - _st->margin - st::botKbScroll.width;
		int implHeight = _height - (st::botKbScroll.deltat + st::botKbScroll.deltab);
		_impl->resize(implWidth, implHeight);
	}
	return _height;
}

bool BotKeyboard::maximizeSize() const {
	return _maximizeSize;
}

bool BotKeyboard::singleUse() const {
	return _singleUse;
}

void BotKeyboard::updateStyle(int newWidth) {
	if (!_impl) return;

	int implWidth = newWidth - st::botKbButton.margin - st::botKbScroll.width;
	_st = _impl->isEnoughSpace(implWidth, st::botKbButton) ? &st::botKbButton : &st::botKbTinyButton;

	_impl->setStyle(std::make_unique<Style>(this, *_st));
}

void BotKeyboard::clearSelection() {
	if (_impl) {
		if (ClickHandler::setActive(ClickHandlerPtr(), this)) {
			Ui::Tooltip::Hide();
			setCursor(style::cur_default);
		}
	}
}

QPoint BotKeyboard::tooltipPos() const {
	return _lastMousePos;
}

QString BotKeyboard::tooltipText() const {
	if (ClickHandlerPtr lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	}
	return QString();
}

void BotKeyboard::updateSelected() {
	Ui::Tooltip::Show(1000, this);

	if (!_impl) return;

	auto p = mapFromGlobal(_lastMousePos);
	auto x = rtl() ? st::botKbScroll.width : _st->margin;

	auto link = _impl->getState(p - QPoint(x, _st->margin));
	if (ClickHandler::setActive(link, this)) {
		Ui::Tooltip::Hide();
		setCursor(link ? style::cur_pointer : style::cur_default);
	}
}
