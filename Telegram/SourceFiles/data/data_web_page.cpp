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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "data/data_web_page.h"

#include "auth_session.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "ui/text/text_entity.h"

namespace {

QString SiteNameFromUrl(const QString &url) {
	QUrl u(url);
	QString pretty = u.isValid() ? u.toDisplayString() : url;
	QRegularExpressionMatch m = QRegularExpression(qsl("^[a-zA-Z0-9]+://")).match(pretty);
	if (m.hasMatch()) pretty = pretty.mid(m.capturedLength());
	int32 slash = pretty.indexOf('/');
	if (slash > 0) pretty = pretty.mid(0, slash);
	QStringList components = pretty.split('.', QString::SkipEmptyParts);
	if (components.size() >= 2) {
		components = components.mid(components.size() - 2);
		return components.at(0).at(0).toUpper() + components.at(0).mid(1) + '.' + components.at(1);
	}
	return QString();
}

} // namespace

bool WebPageData::applyChanges(
		const QString &newType,
		const QString &newUrl,
		const QString &newDisplayUrl,
		const QString &newSiteName,
		const QString &newTitle,
		const TextWithEntities &newDescription,
		PhotoData *newPhoto,
		DocumentData *newDocument,
		int newDuration,
		const QString &newAuthor,
		int newPendingTill) {
	if (newPendingTill != 0
		&& (!url.isEmpty() || newUrl.isEmpty())
		&& (!pendingTill
			|| pendingTill == newPendingTill
			|| newPendingTill < -1)) {
		return false;
	}

	const auto resultType = toWebPageType(newType);
	const auto resultUrl = TextUtilities::Clean(newUrl);
	const auto resultDisplayUrl = TextUtilities::Clean(
		newDisplayUrl);
	const auto possibleSiteName = TextUtilities::Clean(
		newSiteName);
	const auto resultTitle = TextUtilities::SingleLine(
		newTitle);
	const auto resultAuthor = TextUtilities::Clean(newAuthor);

	const auto viewTitleText = resultTitle.isEmpty()
		? TextUtilities::SingleLine(resultAuthor)
		: resultTitle;
	const auto resultSiteName = [&] {
		if (!possibleSiteName.isEmpty()) {
			return possibleSiteName;
		} else if (!newDescription.text.isEmpty()
			&& viewTitleText.isEmpty()
			&& !resultUrl.isEmpty()) {
			return SiteNameFromUrl(resultUrl);
		}
		return QString();
	}();

	if (type == resultType
		&& url == resultUrl
		&& displayUrl == resultDisplayUrl
		&& siteName == resultSiteName
		&& title == resultTitle
		&& description.text == newDescription.text
		&& photo == newPhoto
		&& document == newDocument
		&& duration == newDuration
		&& author == resultAuthor
		&& pendingTill == newPendingTill) {
		return false;
	}
	if (pendingTill > 0 && newPendingTill <= 0) {
		Auth().api().clearWebPageRequest(this);
	}
	type = resultType;
	url = resultUrl;
	displayUrl = resultDisplayUrl;
	siteName = resultSiteName;
	title = resultTitle;
	description = newDescription;
	photo = newPhoto;
	document = newDocument;
	duration = newDuration;
	author = resultAuthor;
	pendingTill = newPendingTill;
	++version;
	if (App::main()) App::main()->webPageUpdated(this);

	return true;
}