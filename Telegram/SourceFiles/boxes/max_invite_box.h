/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

class MaxInviteBox final : public Ui::BoxContent {
public:
	MaxInviteBox(QWidget*, not_null<ChannelData*> channel);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void updateSelected(const QPoint &cursorGlobalPosition);

	not_null<ChannelData*> _channel;

	Ui::Text::String _text;
	int32 _textWidth, _textHeight;

	QRect _invitationLink;
	bool _linkOver = false;
	bool _creatingInviteLink = false;

	QPoint _lastMousePos;

};
