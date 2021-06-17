/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"

namespace Ui {
class UsernameInput;
class LinkButton;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

class UsernameBox : public Ui::BoxContent {
public:
	UsernameBox(QWidget*, not_null<Main::Session*> session);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateDone(const MTPUser &result);
	void updateFail(const MTP::Error &error);

	void checkDone(const MTPBool &result);
	void checkFail(const MTP::Error &error);

	void save();

	void check();
	void changed();

	void linkClick();

	QString getName() const;
	void updateLinkText();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	object_ptr<Ui::UsernameInput> _username;
	object_ptr<Ui::LinkButton> _link;

	mtpRequestId _saveRequestId = 0;
	mtpRequestId _checkRequestId = 0;
	QString _sentUsername, _checkUsername, _errorText, _goodText;

	Ui::Text::String _about;
	object_ptr<QTimer> _checkTimer;

};
