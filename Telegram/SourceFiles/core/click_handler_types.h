/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/basic_click_handlers.h"

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {
class ElementDelegate;
} // namespace HistoryView

namespace Window {
class SessionController;
} // namespace Window

[[nodiscard]] bool UrlRequiresConfirmation(const QUrl &url);

struct ClickHandlerContext {
	FullMsgId itemId;
	Fn<HistoryView::ElementDelegate*()> elementDelegate;
	base::weak_ptr<Window::SessionController> sessionWindow;
	bool skipBotAutoLogin = false;
};
Q_DECLARE_METATYPE(ClickHandlerContext);

class HiddenUrlClickHandler : public UrlClickHandler {
public:
	HiddenUrlClickHandler(QString url) : UrlClickHandler(url, false) {
	}
	QString copyToClipboardContextItemText() const override {
		return (url().isEmpty() || url().startsWith(qstr("internal:")))
			? QString()
			: UrlClickHandler::copyToClipboardContextItemText();
	}

	static void Open(QString url, QVariant context = {});
	void onClick(ClickContext context) const override {
		const auto button = context.button;
		if (button == Qt::LeftButton || button == Qt::MiddleButton) {
			Open(url(), context.other);
		}
	}

	TextEntity getTextEntity() const override;

};

class UserData;
class BotGameUrlClickHandler : public UrlClickHandler {
public:
	BotGameUrlClickHandler(UserData *bot, QString url)
	: UrlClickHandler(url, false)
	, _bot(bot) {
	}
	void onClick(ClickContext context) const override;

private:
	UserData *_bot;

};

class MentionClickHandler : public TextClickHandler {
public:
	MentionClickHandler(const QString &tag) : _tag(tag) {
	}

	void onClick(ClickContext context) const override;

	QString dragText() const override {
		return _tag;
	}

	QString copyToClipboardContextItemText() const override;

	TextEntity getTextEntity() const override;

protected:
	QString url() const override {
		return _tag;
	}

private:
	QString _tag;

};

class MentionNameClickHandler : public ClickHandler {
public:
	MentionNameClickHandler(
		not_null<Main::Session*> session,
		QString text,
		UserId userId,
		uint64 accessHash)
	: _session(session)
	, _text(text)
	, _userId(userId)
	, _accessHash(accessHash) {
	}

	void onClick(ClickContext context) const override;

	TextEntity getTextEntity() const override;

	QString tooltip() const override;

private:
	const not_null<Main::Session*> _session;
	QString _text;
	UserId _userId;
	uint64 _accessHash;

};

class HashtagClickHandler : public TextClickHandler {
public:
	HashtagClickHandler(const QString &tag) : _tag(tag) {
	}

	void onClick(ClickContext context) const override;

	QString dragText() const override {
		return _tag;
	}

	QString copyToClipboardContextItemText() const override;

	TextEntity getTextEntity() const override;

protected:
	QString url() const override {
		return _tag;
	}

private:
	QString _tag;

};

class CashtagClickHandler : public TextClickHandler {
public:
	CashtagClickHandler(const QString &tag) : _tag(tag) {
	}

	void onClick(ClickContext context) const override;

	QString dragText() const override {
		return _tag;
	}

	QString copyToClipboardContextItemText() const override;

	TextEntity getTextEntity() const override;

protected:
	QString url() const override {
		return _tag;
	}

private:
	QString _tag;

};

class PeerData;
class BotCommandClickHandler : public TextClickHandler {
public:
	BotCommandClickHandler(const QString &cmd) : _cmd(cmd) {
	}

	void onClick(ClickContext context) const override;

	QString dragText() const override {
		return _cmd;
	}

	TextEntity getTextEntity() const override;

protected:
	QString url() const override {
		return _cmd;
	}

private:
	QString _cmd;

};
