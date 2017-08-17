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
#pragma once

#include "ui/widgets/tooltip.h"

class BotKeyboard : public TWidget, public Ui::AbstractTooltipShower, public ClickHandlerHost {
	Q_OBJECT

public:
	BotKeyboard(QWidget *parent);

	bool moderateKeyActivate(int index);

	// With force=true the markup is updated even if it is
	// already shown for the passed history item.
	bool updateMarkup(HistoryItem *last, bool force = false);
	bool hasMarkup() const;
	bool forceReply() const;

	void step_selected(TimeMs ms, bool timer);
	void resizeToWidth(int newWidth, int maxOuterHeight) {
		_maxOuterHeight = maxOuterHeight;
		return TWidget::resizeToWidth(newWidth);
	}

	bool maximizeSize() const;
	bool singleUse() const;

	FullMsgId forMsgId() const {
		return _wasForMsgId;
	}

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void updateSelected();

	void updateStyle(int newWidth);
	void clearSelection();

	FullMsgId _wasForMsgId;
	int _height = 0;
	int _maxOuterHeight = 0;
	bool _maximizeSize = false;
	bool _singleUse = false;
	bool _forceReply = false;

	QPoint _lastMousePos;
	std::unique_ptr<ReplyKeyboard> _impl;

	class Style : public ReplyKeyboard::Style {
	public:
		Style(BotKeyboard *parent, const style::BotKeyboardButton &st) : ReplyKeyboard::Style(st), _parent(parent) {
		}

		int buttonRadius() const override;

		void startPaint(Painter &p) const override;
		const style::TextStyle &textStyle() const override;
		void repaint(not_null<const HistoryItem*> item) const override;

	protected:
		void paintButtonBg(Painter &p, const QRect &rect, float64 howMuchOver) const override;
		void paintButtonIcon(Painter &p, const QRect &rect, int outerWidth, HistoryMessageReplyMarkup::Button::Type type) const override;
		void paintButtonLoading(Painter &p, const QRect &rect) const override;
		int minButtonWidth(HistoryMessageReplyMarkup::Button::Type type) const override;

	private:
		BotKeyboard *_parent;

	};
	const style::BotKeyboardButton *_st = nullptr;

};
