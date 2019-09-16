/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/algorithm.h"
#include "ui/click_handler.h"

class TextClickHandler : public ClickHandler {
public:
	TextClickHandler(bool fullDisplayed = true)
	: _fullDisplayed(fullDisplayed) {
	}

	QString copyToClipboardText() const override {
		return url();
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
	UrlClickHandler(const QString &url, bool fullDisplayed = true);

	QString copyToClipboardContextItemText() const override;

	QString dragText() const override {
		return url();
	}

	TextEntity getTextEntity() const override;

	static void Open(QString url, QVariant context = {});
	void onClick(ClickContext context) const override {
		const auto button = context.button;
		if (button == Qt::LeftButton || button == Qt::MiddleButton) {
			Open(url(), context.other);
		}
	}

	[[nodiscard]] static bool IsEmail(const QString &url) {
		const auto at = url.indexOf('@'), slash = url.indexOf('/');
		return ((at > 0) && (slash < 0 || slash > at));
	}

protected:
	QString url() const override;
	QString readable() const override {
		return _readable;
	}

private:
	[[nodiscard]] bool isEmail() const {
		return IsEmail(_originalUrl);
	}

	QString _originalUrl, _readable;

};
