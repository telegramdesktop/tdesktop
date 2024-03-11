/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_data.h"

#include "iv/iv_prepare.h"

namespace Iv {

QByteArray GeoPointId(Geo point) {
	const auto lat = int(point.lat * 1000000);
	const auto lon = int(point.lon * 1000000);
	const auto combined = (std::uint64_t(std::uint32_t(lat)) << 32)
		| std::uint64_t(std::uint32_t(lon));
	return QByteArray::number(combined)
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
	.page = page,
	.webpagePhoto = (webpage.vphoto()
		? *webpage.vphoto()
		: std::optional<MTPPhoto>()),
	.webpageDocument = (webpage.vdocument()
		? *webpage.vdocument()
		: std::optional<MTPDocument>()),
	.title = (webpage.vtitle()
		? qs(*webpage.vtitle())
		: qs(webpage.vauthor().value_or_empty()))
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

} // namespace Iv
