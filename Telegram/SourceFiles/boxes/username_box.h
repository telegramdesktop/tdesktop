/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class UsernameInput;
class LinkButton;
} // namespace Ui

class UsernameBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	UsernameBox(QWidget*);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onSave();

	void onCheck();
	void onChanged();

	void onLinkClick();

private:
	void onUpdateDone(const MTPUser &result);
	bool onUpdateFail(const RPCError &error);

	void onCheckDone(const MTPBool &result);
	bool onCheckFail(const RPCError &error);

	QString getName() const;
	void updateLinkText();

	object_ptr<Ui::UsernameInput> _username;
	object_ptr<Ui::LinkButton> _link;

	mtpRequestId _saveRequestId = 0;
	mtpRequestId _checkRequestId = 0;
	QString _sentUsername, _checkUsername, _errorText, _goodText;

	Text _about;
	object_ptr<QTimer> _checkTimer;

};
