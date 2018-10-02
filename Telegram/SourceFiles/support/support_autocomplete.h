/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

class AuthSession;

namespace Ui {
class ScrollArea;
} // namespace Ui

namespace Support {

class Autocomplete : public Ui::RpWidget {
public:
	Autocomplete(QWidget *parent, not_null<AuthSession*> session);

	void activate();
	void deactivate();
	void setBoundings(QRect rect);

	rpl::producer<QString> insertRequests();

protected:
	void keyPressEvent(QKeyEvent *e) override;

private:
	void setupContent();

	not_null<AuthSession*> _session;
	Fn<void()> _activate;
	Fn<void()> _deactivate;
	Fn<void(int delta)> _moveSelection;

	rpl::event_stream<QString> _insertRequests;

};

} //namespace Support
