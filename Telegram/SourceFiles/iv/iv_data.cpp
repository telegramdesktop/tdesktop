/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_data.h"

#include "iv/iv_prepare.h"
#include "webview/webview_interface.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>

namespace Iv {
namespace {

bool FailureRecorded/* = false*/;

} // namespace

QByteArray GeoPointId(Geo point) {
	const auto lat = int(point.lat * 1000000);
	const auto lon = int(point.lon * 1000000);
	const auto combined = (std::uint64_t(std::uint32_t(lat)) << 32)
		| std::uint64_t(std::uint32_t(lon));
	return QByteArray::number(quint64(combined))
		+ ','
		+ QByteArray::number(point.access);
}

Geo GeoPointFromId(QByteArray data) {
	const auto parts = data.split(',');
	if (parts.size() != 2) {
		return {};
	}
	const auto combined = parts[0].toULongLong();
	const auto lat = int(std::uint32_t(combined >> 32));
	const auto lon = int(std::uint32_t(combined & 0xFFFFFFFFULL));
	return {
		.lat = lat / 1000000.,
		.lon = lon / 1000000.,
		.access = parts[1].toULongLong(),
	};
}

Data::Data(const MTPDwebPage &webpage, const MTPPage &page)
: _source(std::make_unique<Source>(Source{
	.pageId = webpage.vid().v,
	.page = page,
	.webpagePhoto = (webpage.vphoto()
		? *webpage.vphoto()
		: std::optional<MTPPhoto>()),
	.webpageDocument = (webpage.vdocument()
		? *webpage.vdocument()
		: std::optional<MTPDocument>()),
	.name = (webpage.vsite_name()
		? qs(*webpage.vsite_name())
		: SiteNameFromUrl(qs(webpage.vurl())))
})) {
}

QString Data::id() const {
	return qs(_source->page.data().vurl());
}

bool Data::partial() const {
	return _source->page.data().is_part();
}

Data::~Data() = default;

void Data::prepare(const Options &options, Fn<void(Prepared)> done) const {
	crl::async([source = *_source, options, done = std::move(done)] {
		done(Prepare(source, options));
	});
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

bool ShowButton() {
	static const auto Supported = Webview::NavigateToDataSupported();
	return Supported;
}

void RecordShowFailure() {
	FailureRecorded = true;
}

bool FailedToShow() {
	return FailureRecorded;
}

} // namespace Iv
