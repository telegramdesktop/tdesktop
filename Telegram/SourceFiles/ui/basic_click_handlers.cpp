/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/basic_click_handlers.h"

#include "ui/widgets/tooltip.h"
#include "ui/text/text_entity.h"
#include "ui/ui_integration.h"
#include "base/qthelp_url.h"

#include <QtCore/QUrl>
#include <QtCore/QRegularExpression>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>

UrlClickHandler::UrlClickHandler(const QString &url, bool fullDisplayed)
: TextClickHandler(fullDisplayed)
, _originalUrl(url) {
	if (isEmail()) {
		_readable = _originalUrl;
	} else {
		const auto original = QUrl(_originalUrl);
		const auto good = QUrl(original.isValid()
			? original.toEncoded()
			: QString());
		_readable = good.isValid() ? good.toDisplayString() : _originalUrl;
	}
}

QString UrlClickHandler::copyToClipboardContextItemText() const {
	return isEmail()
		? Ui::Integration::Instance().phraseContextCopyEmail()
		: Ui::Integration::Instance().phraseContextCopyLink();
}

QString UrlClickHandler::url() const {
	if (isEmail()) {
		return _originalUrl;
	}

	QUrl u(_originalUrl), good(u.isValid() ? u.toEncoded() : QString());
	QString result(good.isValid() ? QString::fromUtf8(good.toEncoded()) : _originalUrl);

	if (!result.isEmpty()
		&& !QRegularExpression(
			QStringLiteral("^[a-zA-Z]+:")).match(result).hasMatch()) {
		// No protocol.
		return QStringLiteral("http://") + result;
	}
	return result;
}

void UrlClickHandler::Open(QString url, QVariant context) {
	Ui::Tooltip::Hide();
	if (!Ui::Integration::Instance().handleUrlClick(url, context)
		&& !url.isEmpty()) {
		QDesktopServices::openUrl(url);
	}
}

auto UrlClickHandler::getTextEntity() const -> TextEntity {
	const auto type = isEmail() ? EntityType::Email : EntityType::Url;
	return { type, _originalUrl };
}
