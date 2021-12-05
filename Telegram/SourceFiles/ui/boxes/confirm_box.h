/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {

class InformBox;
class ConfirmBox : public Ui::BoxContent, public ClickHandlerHost {
public:

	using ConfirmedCallback = std::variant<
		v::null_t,
		FnMut<void()>,
		FnMut<void(Fn<void()>)>>;

	ConfirmBox(
		QWidget*,
		const QString &text,
		ConfirmedCallback confirmedCallback = FnMut<void()>(),
		FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(
		QWidget*,
		const QString &text,
		const QString &confirmText,
		ConfirmedCallback confirmedCallback = FnMut<void()>(),
		FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(
		QWidget*,
		const QString &text,
		const QString &confirmText,
		const style::RoundButton &confirmStyle,
		ConfirmedCallback confirmedCallback = FnMut<void()>(),
		FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(
		QWidget*,
		const QString &text,
		const QString &confirmText,
		const QString &cancelText,
		ConfirmedCallback confirmedCallback = FnMut<void()>(),
		FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(
		QWidget*,
		const QString &text,
		const QString &confirmText,
		const style::RoundButton &confirmStyle,
		const QString &cancelText,
		ConfirmedCallback confirmedCallback = FnMut<void()>(),
		FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(
		QWidget*,
		const TextWithEntities &text,
		const QString &confirmText,
		ConfirmedCallback confirmedCallback = v::null,
		FnMut<void()> cancelledCallback = nullptr);

	void updateLink();

	// If strict cancel is set the cancelledCallback is only called
	// if the cancel button was pressed.
	void setStrictCancel(bool strictCancel) {
		_strictCancel = strictCancel;
	}

	void setMaxLineCount(int count);

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct InformBoxTag {
	};
	ConfirmBox(
		const InformBoxTag &,
		const QString &text,
		const QString &doneText,
		Fn<void()> closedCallback);
	ConfirmBox(
		const InformBoxTag &,
		const TextWithEntities &text,
		const QString &doneText,
		Fn<void()> closedCallback);
	FnMut<void()> generateInformCallback(Fn<void()> closedCallback);
	friend class InformBox;

	void confirmed();
	void init(const QString &text);
	void init(const TextWithEntities &text);
	void textUpdated();
	void updateHover();

	QString _confirmText;
	QString _cancelText;
	const style::RoundButton &_confirmStyle;
	bool _informative = false;

	Ui::Text::String _text;
	int _textWidth = 0;
	int _textHeight = 0;
	int _maxLineCount = 16;

	QPoint _lastMousePos;

	bool _confirmed = false;
	bool _cancelled = false;
	bool _strictCancel = false;
	ConfirmBox::ConfirmedCallback _confirmedCallback;
	FnMut<void()> _cancelledCallback;

};

class InformBox : public ConfirmBox {
public:
	InformBox(
		QWidget*,
		const QString &text,
		Fn<void()> closedCallback = nullptr);
	InformBox(
		QWidget*,
		const QString &text,
		const QString &doneText,
		Fn<void()> closedCallback = nullptr);
	InformBox(
		QWidget*,
		const TextWithEntities &text,
		Fn<void()> closedCallback = nullptr);
	InformBox(
		QWidget*,
		const TextWithEntities &text,
		const QString &doneText,
		Fn<void()> closedCallback = nullptr);

};

class ConfirmDontWarnBox : public Ui::BoxContent {
public:
	ConfirmDontWarnBox(
		QWidget*,
		rpl::producer<TextWithEntities> text,
		const QString &checkbox,
		rpl::producer<QString> confirm,
		FnMut<void(bool)> callback);

protected:
	void prepare() override;

private:
	not_null<Ui::RpWidget*> setupContent(
		rpl::producer<TextWithEntities> text,
		const QString &checkbox,
		FnMut<void(bool)> callback);

	rpl::producer<QString> _confirm;
	FnMut<void()> _callback;
	not_null<Ui::RpWidget*> _content;

};

} // namespace Ui
