/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class PasswordInput;
class LinkButton;
class RoundButton;
} // namespace Ui

class PasscodeWidget : public TWidget {
	Q_OBJECT

public:
	PasscodeWidget(QWidget *parent);

	void setInnerFocus();

	void showAnimated(const QPixmap &bgAnimCache, bool back = false);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

public slots:
	void onError();
	void onChanged();
	void onSubmit();

private:
	void animationCallback();

	void showAll();
	void hideAll();

	Animation _a_show;
	bool _showBack = false;
	QPixmap _cacheUnder, _cacheOver;

	object_ptr<Ui::PasswordInput> _passcode;
	object_ptr<Ui::RoundButton> _submit;
	object_ptr<Ui::LinkButton> _logout;
	QString _error;

};
