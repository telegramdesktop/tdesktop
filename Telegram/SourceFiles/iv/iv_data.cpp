/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_data.h"

#include "iv/iv_rich_page.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>

namespace Iv {

Data::Data(
	const MTPDwebPage &webpage,
	std::shared_ptr<const RichPage> richPage)
: _pageId(webpage.vid().v)
, _hash(webpage.vhash().v)
, _url(qs(webpage.vurl()))
, _name(webpage.vsite_name()
	? qs(*webpage.vsite_name())
	: SiteNameFromUrl(_url))
, _partial(richPage ? richPage->part : false)
, _richPage(std::move(richPage)) {
}

QString Data::id() const {
	return _url;
}

QString Data::name() const {
	return _name;
}

uint64 Data::pageId() const {
	return _pageId;
}

int32 Data::hash() const {
	return _hash;
}

bool Data::partial() const {
	return _partial;
}

Data::~Data() = default;

const std::shared_ptr<const RichPage> &Data::richPage() const {
	return _richPage;
}

QString SiteNameFromUrl(const QString &url) {
	const auto u = QUrl(url);
	QString pretty = u.isValid() ? u.toDisplayString() : url;
	const auto m = QRegularExpression(u"^[a-zA-Z0-9]+://"_q).match(pretty);
	if (m.hasMatch()) pretty = pretty.mid(m.capturedLength());
	int32 slash = pretty.indexOf('/');
	if (slash > 0) pretty = pretty.mid(0, slash);
	QStringList components = pretty.split('.', Qt::SkipEmptyParts);
	if (components.size() >= 2) {
		components = components.mid(components.size() - 2);
		return components.at(0).at(0).toUpper()
			+ components.at(0).mid(1)
			+ '.'
			+ components.at(1);
	}
	return QString();
}

} // namespace Iv
