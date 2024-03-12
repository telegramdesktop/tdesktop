/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Iv {

struct Source;

struct Options {
};

struct Prepared {
	QString name;
	QByteArray content;
	QByteArray script;
	QString url;
	QString hash;
	base::flat_map<QByteArray, QByteArray> embeds;
	base::flat_set<QByteArray> channelIds;
	bool rtl = false;
	bool hasCode = false;
	bool hasEmbeds = false;
};

struct Geo {
	float64 lat = 0.;
	float64 lon = 0.;
	uint64 access = 0;
};

[[nodiscard]] QByteArray GeoPointId(Geo point);
[[nodiscard]] Geo GeoPointFromId(QByteArray data);

class Data final {
public:
	Data(const MTPDwebPage &webpage, const MTPPage &page);
	~Data();

	[[nodiscard]] QString id() const;
	[[nodiscard]] bool partial() const;

	void prepare(const Options &options, Fn<void(Prepared)> done) const;

private:
	const std::unique_ptr<Source> _source;

};

[[nodiscard]] QString SiteNameFromUrl(const QString &url);

[[nodiscard]] bool ShowButton();

void RecordShowFailure();
[[nodiscard]] bool FailedToShow();

} // namespace Iv
