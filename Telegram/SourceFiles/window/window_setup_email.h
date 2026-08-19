/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "window/window_lock_widgets.h"

namespace Ui {
class InputField;
class CodeInput;
class RoundButton;
class IconButton;
class FlatLabel;
class VerticalLayout;
} // namespace Ui

namespace Window {

class SlideAnimation;

class SetupEmailConfirmWidget : public Ui::RpWidget {

public:
	SetupEmailConfirmWidget(
		QWidget *parent,
		not_null<Controller*> window,
		const QString &email,
		int codeLength,
		const QString &emailPattern);

	rpl::producer<> backRequests() const;
	rpl::producer<> confirmations() const;

	void showAnimated(QPixmap oldContentCache);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void verifyCode(const QString &code);

	not_null<Controller*> _window;
	QString _email;
	QString _emailPattern;
	QString _error;
	rpl::lifetime _requestLifetime;
	std::optional<::MTP::Sender> _api;
	object_ptr<Ui::IconButton> _backButton;
	object_ptr<Ui::VerticalLayout> _layout;
	Ui::CodeInput *_codeInput = nullptr;
	Ui::FlatLabel *_errorLabel = nullptr;

	rpl::event_stream<> _backRequests;
	rpl::event_stream<> _confirmations;

	std::unique_ptr<SlideAnimation> _showAnimation;
	Fn<void(anim::repeat)> _iconAnimate;

};

class SetupEmailLockWidget : public LockWidget {
public:
	SetupEmailLockWidget(QWidget *parent, not_null<Controller*> window);

	void setInnerFocus() override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void paintContent(QPainter &p) override;
	void submit();
	void showConfirmWidget(
		const QString &email,
		int codeLength,
		const QString &emailPattern);
	void showEmailInput();
	void showAccountsMenu();
	QPixmap grabContent();

	object_ptr<Ui::VerticalLayout> _layout;
	SetupEmailConfirmWidget *_confirmWidget;
	rpl::lifetime _requestLifetime;
	std::optional<::MTP::Sender> _api;
	object_ptr<Ui::IconButton> _backButton = { nullptr };
	object_ptr<Ui::RoundButton> _logoutButton = { nullptr };
	object_ptr<Ui::RoundButton> _debugButton = { nullptr };
	base::unique_qptr<Ui::PopupMenu> _accountsMenu;
	Ui::InputField *_emailInput = nullptr;
	Ui::RoundButton *_submit = nullptr;
	Ui::FlatLabel *_errorLabel = nullptr;
	QString _error;

	std::unique_ptr<SlideAnimation> _backAnimation;
	Fn<void(anim::repeat)> _iconAnimate;

};

} // namespace Window
