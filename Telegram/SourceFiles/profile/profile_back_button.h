/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"

namespace Profile {

class BackButton final : public Ui::AbstractButton, private base::Subscriber {
public:
	BackButton(QWidget *parent, const QString &text);

	void setText(const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;
	void onStateChanged(State was, StateChangeSource source) override;

private:
	void updateAdaptiveLayout();

	int _unreadCounterSubscription = 0;
	QString _text;

};

} // namespace Profile
