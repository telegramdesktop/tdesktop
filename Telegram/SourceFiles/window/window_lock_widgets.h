/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "boxes/abstract_box.h"
#include "base/bytes.h"

namespace Ui {
class PasswordInput;
class LinkButton;
class RoundButton;
class CheckView;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Window {

class Controller;

class LockWidget : public Ui::RpWidget {
public:
	LockWidget(QWidget *parent, not_null<Controller*> window);

	not_null<Controller*> window() const;

	virtual void setInnerFocus();

	void showAnimated(const QPixmap &bgAnimCache, bool back = false);
	void showFinished();

protected:
	void paintEvent(QPaintEvent *e) override;
	virtual void paintContent(QPainter &p);

private:
	void animationCallback();

	const not_null<Controller*> _window;
	Ui::Animations::Simple _a_show;
	bool _showBack = false;
	QPixmap _cacheUnder, _cacheOver;

};

class PasscodeLockWidget : public LockWidget {
public:
	PasscodeLockWidget(QWidget *parent, not_null<Controller*> window);

	void setInnerFocus() override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void paintContent(QPainter &p) override;
	void changed();
	void submit();
	void error();

	object_ptr<Ui::PasswordInput> _passcode;
	object_ptr<Ui::RoundButton> _submit;
	object_ptr<Ui::LinkButton> _logout;
	QString _error;

};

struct TermsLock {
	bytes::vector id;
	TextWithEntities text;
	std::optional<int> minAge;
	bool popup = false;

	inline bool operator==(const TermsLock &other) const {
		return (id == other.id);
	}
	inline bool operator!=(const TermsLock &other) const {
		return !(*this == other);
	}

	static TermsLock FromMTP(
		Main::Session *session,
		const MTPDhelp_termsOfService &data);

};

class TermsBox : public Ui::BoxContent {
public:
	TermsBox(
		QWidget*,
		const TermsLock &data,
		rpl::producer<QString> agree,
		rpl::producer<QString> cancel);
	TermsBox(
		QWidget*,
		const TextWithEntities &text,
		rpl::producer<QString> agree,
		rpl::producer<QString> cancel,
		bool attentionAgree = false);

	rpl::producer<> agreeClicks() const;
	rpl::producer<> cancelClicks() const;
	QString lastClickedMention() const;

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;

private:
	TermsLock _data;
	rpl::producer<QString> _agree;
	rpl::producer<QString> _cancel;
	rpl::event_stream<> _agreeClicks;
	rpl::event_stream<> _cancelClicks;
	QString _lastClickedMention;
	bool _attentionAgree = false;

	bool _ageErrorShown = false;
	Ui::Animations::Simple _ageErrorAnimation;

};

} // namespace Window
