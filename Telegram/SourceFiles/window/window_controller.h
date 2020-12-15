/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mainwindow.h"
#include "ui/layers/layer_widget.h"

namespace Main {
class Account;
} // namespace Main

namespace Window {

class Controller final {
public:
	Controller();
	~Controller();

	Controller(const Controller &other) = delete;
	Controller &operator=(const Controller &other) = delete;

	void showAccount(not_null<Main::Account*> account);

	[[nodiscard]] not_null<::MainWindow*> widget() {
		return &_widget;
	}
	[[nodiscard]] Main::Account &account() const {
		Expects(_account != nullptr);

		return *_account;
	}
	[[nodiscard]] SessionController *sessionController() const {
		return _sessionController.get();
	}
	[[nodiscard]] bool locked() const;

	void finishFirstShow();

	void setupPasscodeLock();
	void clearPasscodeLock();
	void setupIntro();
	void setupMain();

	void showSettings();

	[[nodiscard]] int verticalShadowTop() const;

	template <typename BoxType>
	QPointer<BoxType> show(
			object_ptr<BoxType> content,
			Ui::LayerOptions options = Ui::LayerOption::KeepOther,
			anim::type animated = anim::type::normal) {
		const auto result = QPointer<BoxType>(content.data());
		showBox(std::move(content), options, animated);
		return result;
	}
	void showToast(const QString &text);

	void showRightColumn(object_ptr<TWidget> widget);
	void sideBarChanged();

	void activate();
	void reActivate();
	void updateIsActiveFocus();
	void updateIsActiveBlur();
	void updateIsActive();
	void minimize();
	void close();

	void preventOrInvoke(Fn<void()> &&callback);

	QPoint getPointForCallPanelCenter() const;

private:
	void showBox(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options,
		anim::type animated);
	void checkThemeEditor();
	void checkLockByTerms();
	void showTermsDecline();
	void showTermsDelete();

	Main::Account *_account = nullptr;
	::MainWindow _widget;
	std::unique_ptr<SessionController> _sessionController;
	base::Timer _isActiveTimer;
	QPointer<Ui::BoxContent> _termsBox;

	rpl::lifetime _accountLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Window
