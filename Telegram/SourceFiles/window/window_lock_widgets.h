/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "boxes/abstract_box.h"
#include "base/bytes.h"

namespace Ui {
class PasswordInput;
class LinkButton;
class RoundButton;
class CheckView;
} // namespace Ui

namespace Window {

class LockWidget : public Ui::RpWidget {
public:
	LockWidget(QWidget *parent);

	virtual void setInnerFocus();

	void showAnimated(const QPixmap &bgAnimCache, bool back = false);

protected:
	void paintEvent(QPaintEvent *e) override;
	virtual void paintContent(Painter &p);

private:
	void animationCallback();

	Animation _a_show;
	bool _showBack = false;
	QPixmap _cacheUnder, _cacheOver;

};

class PasscodeLockWidget : public LockWidget {
public:
	PasscodeLockWidget(QWidget *parent);

	void setInnerFocus() override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void paintContent(Painter &p) override;
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

	static TermsLock FromMTP(const MTPDhelp_termsOfService &data);

};

class TermsBox : public BoxContent {
public:
	TermsBox(
		QWidget*,
		const TermsLock &data,
		Fn<QString()> agree,
		Fn<QString()> cancel);
	TermsBox(
		QWidget*,
		const TextWithEntities &text,
		Fn<QString()> agree,
		Fn<QString()> cancel,
		bool attentionAgree = false);

	rpl::producer<> agreeClicks() const;
	rpl::producer<> cancelClicks() const;
	QString lastClickedMention() const;

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;

private:
	TermsLock _data;
	Fn<QString()> _agree;
	Fn<QString()> _cancel;
	rpl::event_stream<> _agreeClicks;
	rpl::event_stream<> _cancelClicks;
	QString _lastClickedMention;
	bool _attentionAgree = false;

	bool _ageErrorShown = false;
	Animation _ageErrorAnimation;

};

} // namespace Window
