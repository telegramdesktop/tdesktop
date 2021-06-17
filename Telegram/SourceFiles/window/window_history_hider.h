/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace Ui {
class RoundButton;
} // namespace Ui

namespace Window {

class HistoryHider : public Ui::RpWidget {
public:
	// Forward messages (via drag-n-drop)
	HistoryHider(QWidget *parent, MessageIdsList &&items);

	// Send path from command line argument.
	HistoryHider(QWidget *parent);

	// Share url.
	HistoryHider(QWidget *parent, const QString &url, const QString &text);

	// Inline switch button handler.
	HistoryHider(QWidget *parent, const QString &botAndQuery);

	HistoryHider(
		QWidget *parent,
		const QString &text,
		Fn<bool(PeerId)> confirm,
		rpl::producer<bool> oneColumnValue);

	void offerPeer(PeerId peer);

	void startHide();
	void confirm();
	rpl::producer<> confirmed() const;
	rpl::producer<> hidden() const;

	~HistoryHider();

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void refreshLang();
	void updateControlsGeometry();
	void animationCallback();

	QString _text;
	Fn<bool(PeerId)> _confirm;
	Ui::Animations::Simple _a_opacity;

	QRect _box;
	bool _hiding = false;
	bool _isOneColumn = false;

	int _chooseWidth = 0;

	rpl::event_stream<> _confirmed;
	rpl::event_stream<> _hidden;

};

} // namespace Window
