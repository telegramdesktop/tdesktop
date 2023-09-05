/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"

namespace Main {
class Session;
} // namespace Main

namespace Profile {

class BackButton final : public Ui::AbstractButton {
public:
	BackButton(
		QWidget *parent,
		not_null<Main::Session*> session,
		const QString &text,
		rpl::producer<bool> oneColumnValue);

	void setText(const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;
	void onStateChanged(State was, StateChangeSource source) override;

private:
	const not_null<Main::Session*> _session;

	rpl::lifetime _unreadBadgeLifetime;
	QString _text;

};

} // namespace Profile
