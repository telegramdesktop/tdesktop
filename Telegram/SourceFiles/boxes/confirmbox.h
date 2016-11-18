/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "boxes/abstractbox.h"

namespace Ui {
class Checkbox;
class RoundButton;
class FlatLabel;
} // namespace Ui

namespace st {
extern const style::RoundButton &defaultBoxButton;
extern const style::RoundButton &cancelBoxButton;
} // namespace style

class InformBox;
class ConfirmBox : public AbstractBox, public ClickHandlerHost {
	Q_OBJECT

public:
	ConfirmBox(const QString &text, const QString &doneText = QString(), const style::RoundButton &doneStyle = st::defaultBoxButton, const QString &cancelText = QString(), const style::RoundButton &cancelStyle = st::cancelBoxButton);

	void updateLink();

	// You can use this instead of connecting to "confirmed()" signal.
	void setConfirmedCallback(base::lambda_unique<void()> &&callback) {
		_confirmedCallback = std_::move(callback);
	}

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

public slots:
	void onCancel();

signals:
	void confirmed();
	void cancelled();
	void cancelPressed();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEvent(QEvent *e) override;

	void closePressed() override;

private slots:
	void onConfirmPressed();

private:
	ConfirmBox(const QString &text, const QString &doneText, const style::RoundButton &doneStyle, bool informative);
	friend class InformBox;

	void init(const QString &text);
	void onTextUpdated();

	bool _informative;

	Text _text;
	int32 _textWidth, _textHeight;

	void updateHover();

	QPoint _lastMousePos;

	ChildWidget<Ui::RoundButton> _confirm;
	ChildWidget<Ui::RoundButton> _cancel;

	base::lambda_unique<void()> _confirmedCallback;

};

class InformBox : public ConfirmBox {
public:
	InformBox(const QString &text, const QString &doneText = QString(), const style::RoundButton &doneStyle = st::defaultBoxButton) : ConfirmBox(text, doneText, doneStyle, true) {
	}

};

class SharePhoneConfirmBox : public ConfirmBox {
	Q_OBJECT

public:
	SharePhoneConfirmBox(PeerData *recipient);

signals:
	void confirmed(PeerData *recipient);

private slots:
	void onConfirm();

private:
	PeerData *_recipient;

};

class ConfirmLinkBox : public ConfirmBox {
	Q_OBJECT

public:
	ConfirmLinkBox(const QString &url);

public slots:
	void onOpenLink();

private:
	QString _url;

};

class ConfirmBotGameBox : public ConfirmBox {
	Q_OBJECT

public:
	ConfirmBotGameBox(UserData *bot, const QString &url);

public slots:
	void onOpenLink();

private:
	UserData *_bot;
	QString _url;

};

class MaxInviteBox : public AbstractBox {
	Q_OBJECT

public:
	MaxInviteBox(const QString &link);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEvent(QEvent *e) override;

private:
	void updateSelected(const QPoint &cursorGlobalPosition);
	void step_good(float64 ms, bool timer);

	ChildWidget<Ui::RoundButton> _close;

	Text _text;
	int32 _textWidth, _textHeight;

	QString _link;
	QRect _invitationLink;
	bool _linkOver;

	QPoint _lastMousePos;

	QString _goodTextLink;
	anim::fvalue a_goodOpacity;
	Animation _a_good;

};

class ConvertToSupergroupBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	ConvertToSupergroupBox(ChatData *chat);

public slots:
	void onConvert();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void convertDone(const MTPUpdates &updates);
	bool convertFail(const RPCError &error);

	ChatData *_chat;
	Text _text, _note;
	int32 _textWidth, _textHeight;

	ChildWidget<Ui::RoundButton> _convert;
	ChildWidget<Ui::RoundButton> _cancel;

};

class PinMessageBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	PinMessageBox(ChannelData *channel, MsgId msgId);

public slots:
	void onPin();

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void pinDone(const MTPUpdates &updates);
	bool pinFail(const RPCError &error);

	ChannelData *_channel;
	MsgId _msgId;

	ChildWidget<Ui::FlatLabel> _text;
	ChildWidget<Ui::Checkbox> _notify;

	ChildWidget<Ui::RoundButton> _pin;
	ChildWidget<Ui::RoundButton> _cancel;

	mtpRequestId _requestId = 0;

};

class RichDeleteMessageBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	RichDeleteMessageBox(ChannelData *channel, UserData *from, MsgId msgId);

public slots:
	void onDelete();

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	ChannelData *_channel;
	UserData *_from;
	MsgId _msgId;

	ChildWidget<Ui::FlatLabel> _text;
	ChildWidget<Ui::Checkbox> _banUser;
	ChildWidget<Ui::Checkbox> _reportSpam;
	ChildWidget<Ui::Checkbox> _deleteAll;

	ChildWidget<Ui::RoundButton> _delete;
	ChildWidget<Ui::RoundButton> _cancel;

};

class KickMemberBox : public ConfirmBox {
	Q_OBJECT

public:
	KickMemberBox(PeerData *chat, UserData *member);

private slots:
	void onConfirm();

private:
	PeerData *_chat;
	UserData *_member;

};

class ConfirmInviteBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	ConfirmInviteBox(const QString &title, const MTPChatPhoto &photo, int count, const QVector<UserData*> &participants);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	ChildWidget<Ui::FlatLabel> _title;
	ChildWidget<Ui::FlatLabel> _status;
	ImagePtr _photo;
	QVector<UserData*> _participants;

	ChildWidget<Ui::RoundButton> _join;
	ChildWidget<Ui::RoundButton> _cancel;
	int _userWidth = 0;

};
