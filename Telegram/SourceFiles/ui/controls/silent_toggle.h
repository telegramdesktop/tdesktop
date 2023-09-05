/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"
#include "ui/widgets/tooltip.h"

namespace Ui {

class SilentToggle final
	: public RippleButton
	, public AbstractTooltipShower {
public:
	SilentToggle(QWidget *parent, not_null<ChannelData*> channel);

	void setChecked(bool checked);
	bool checked() const {
		return _checked;
	}

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::IconButton &_st;

	not_null<ChannelData*> _channel;
	bool _checked = false;

	// Animations::Simple _crossLineAnimation;

};

} // namespace Ui
