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

#include "core/click_handler.h"

class TextClickHandler : public ClickHandler {
public:

	TextClickHandler(bool fullDisplayed = true) : _fullDisplayed(fullDisplayed) {
	}

	void copyToClipboard() const override {
		QString u = url();
		if (!u.isEmpty()) {
			QApplication::clipboard()->setText(u);
		}
	}

	QString tooltip() const override {
		return _fullDisplayed ? QString() : readable();
	}

	void setFullDisplayed(bool full) {
		_fullDisplayed = full;
	}

protected:
	virtual QString url() const = 0;
	virtual QString readable() const {
		return url();
	}

	bool _fullDisplayed;

};

class UrlClickHandler : public TextClickHandler {
public:
	UrlClickHandler(const QString &url, bool fullDisplayed = true) : TextClickHandler(fullDisplayed), _url(url) {
		if (isEmail()) {
			_readable = _url;
		} else {
			QUrl u(_url), good(u.isValid() ? u.toEncoded() : QString());
			_readable = good.isValid() ? good.toDisplayString() : _url;
		}
	}
	QString copyToClipboardContextItem() const override;

	QString text() const override {
		return _url;
	}
	QString dragText() const override {
		return url();
	}

	static void doOpen(QString url);
	void onClick(Qt::MouseButton button) const override {
		if (button == Qt::LeftButton || button == Qt::MiddleButton) {
			doOpen(url());
		}
	}

protected:
	QString url() const override {
		if (isEmail()) {
			return _url;
		}

		QUrl u(_url), good(u.isValid() ? u.toEncoded() : QString());
		QString result(good.isValid() ? QString::fromUtf8(good.toEncoded()) : _url);

		if (!QRegularExpression(qsl("^[a-zA-Z]+:")).match(result).hasMatch()) { // no protocol
			return qsl("http://") + result;
		}
		return result;
	}
	QString readable() const override {
		return _readable;
	}

private:
	static bool isEmail(const QString &url) {
		int at = url.indexOf('@'), slash = url.indexOf('/');
		return ((at > 0) && (slash < 0 || slash > at));
	}
	bool isEmail() const {
		return isEmail(_url);
	}

	QString _url, _readable;

};
typedef QSharedPointer<TextClickHandler> TextClickHandlerPtr;

class HiddenUrlClickHandler : public UrlClickHandler {
public:
	HiddenUrlClickHandler(QString url) : UrlClickHandler(url, false) {
	}
	void onClick(Qt::MouseButton button) const override;

};

class MentionClickHandler : public TextClickHandler {
public:
	MentionClickHandler(const QString &tag) : _tag(tag) {
	}
	QString copyToClipboardContextItem() const override;

	QString text() const override {
		return _tag;
	}
	void onClick(Qt::MouseButton button) const override;

protected:
	QString url() const override {
		return _tag;
	}

private:
	QString _tag;

};

class HashtagClickHandler : public TextClickHandler {
public:
	HashtagClickHandler(const QString &tag) : _tag(tag) {
	}
	QString copyToClipboardContextItem() const override;

	QString text() const override {
		return _tag;
	}
	void onClick(Qt::MouseButton button) const override;

protected:
	QString url() const override {
		return _tag;
	}

private:
	QString _tag;

};

class BotCommandClickHandler : public TextClickHandler {
public:
	BotCommandClickHandler(const QString &cmd) : _cmd(cmd) {
	}
	QString text() const override {
		return _cmd;
	}
	void onClick(Qt::MouseButton button) const override;

protected:
	QString url() const override {
		return _cmd;
	}

private:
	QString _cmd;

};
