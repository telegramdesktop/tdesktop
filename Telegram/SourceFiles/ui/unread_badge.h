/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {

class UnreadBadge : public RpWidget {
public:
	using RpWidget::RpWidget;

	void setText(const QString &text, bool active);
	int textBaseline() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _text;
	bool _active = false;

};

struct PeerBadgeStyle {
	const style::icon *verified = nullptr;
	const style::color *scam = nullptr;
};
int DrawPeerBadgeGetWidth(
	not_null<PeerData*> peer,
	Painter &p,
	QRect rectForName,
	int nameWidth,
	int outerWidth,
	const PeerBadgeStyle &st);
QSize ScamBadgeSize(bool fake);
void DrawScamBadge(
	bool fake,
	Painter &p,
	QRect rect,
	int outerWidth,
	const style::color &color);

} // namespace Ui
