/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class PasswordInput;
class FlatLabel;
class LinkButton;
class RoundButton;
class UserpicButton;
class VerticalLayout;
} // namespace Ui

namespace Passport {

class PanelController;

class PanelAskPassword : public Ui::RpWidget {
public:
	PanelAskPassword(
		QWidget *parent,
		not_null<PanelController*> controller);

	void submit();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;

private:
	void updateControlsGeometry();
	void showError(const QString &error);
	void hideError();
	void recover();

	not_null<PanelController*> _controller;

	object_ptr<Ui::UserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _about1;
	object_ptr<Ui::FlatLabel> _about2;
	object_ptr<Ui::PasswordInput> _password;
	object_ptr<Ui::FlatLabel> _hint = { nullptr };
	object_ptr<Ui::FlatLabel> _error = { nullptr };
	object_ptr<Ui::RoundButton> _submit;
	object_ptr<Ui::LinkButton> _forgot;

};

class PanelNoPassword : public Ui::RpWidget {
public:
	PanelNoPassword(
		QWidget *parent,
		not_null<PanelController*> controller);

private:
	void setupContent();
	void refreshBottom();

	not_null<PanelController*> _controller;

	not_null<Ui::VerticalLayout*> _inner;
	base::unique_qptr<Ui::RpWidget> _about;
	base::unique_qptr<Ui::RpWidget> _button;

};

} // namespace Passport
