/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/tooltip.h"

class ReplyKeyboard;

namespace style {
struct BotKeyboardButton;
} // namespace style

namespace Main {
class Session;
} // namespace Main

class BotKeyboard
	: public TWidget
	, public Ui::AbstractTooltipShower
	, public ClickHandlerHost {
public:
	BotKeyboard(not_null<Main::Session*> session, QWidget *parent);

	bool moderateKeyActivate(int index);

	// With force=true the markup is updated even if it is
	// already shown for the passed history item.
	bool updateMarkup(HistoryItem *last, bool force = false);
	[[nodiscard]] bool hasMarkup() const;
	[[nodiscard]] bool forceReply() const;

	[[nodiscard]] QString placeholder() const {
		return _placeholder;
	}

	void step_selected(crl::time ms, bool timer);
	void resizeToWidth(int newWidth, int maxOuterHeight) {
		_maxOuterHeight = maxOuterHeight;
		return TWidget::resizeToWidth(newWidth);
	}

	[[nodiscard]] bool maximizeSize() const;
	[[nodiscard]] bool singleUse() const;

	[[nodiscard]] FullMsgId forMsgId() const {
		return _wasForMsgId;
	}

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	~BotKeyboard();

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

	const not_null<Main::Session*> _session;
	FullMsgId _wasForMsgId;
	QString _placeholder;
	int _height = 0;
	int _maxOuterHeight = 0;
	bool _maximizeSize = false;
	bool _singleUse = false;
	bool _forceReply = false;

	QPoint _lastMousePos;
	std::unique_ptr<ReplyKeyboard> _impl;

	const style::BotKeyboardButton *_st = nullptr;

};
