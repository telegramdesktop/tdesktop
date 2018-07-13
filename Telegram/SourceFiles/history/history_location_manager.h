/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

void initLocationManager();
void deinitLocationManager();

class LocationCoords {
public:
	LocationCoords() = default;
	explicit LocationCoords(const MTPDgeoPoint &point)
	: _lat(point.vlat.v)
	, _lon(point.vlong.v)
	, _access(point.vaccess_hash.v) {
	}

	QString latAsString() const {
		return asString(_lat);
	}
	QString lonAsString() const {
		return asString(_lon);
	}
	MTPGeoPoint toMTP() const {
		return MTP_geoPoint(
			MTP_double(_lon),
			MTP_double(_lat),
			MTP_long(_access));
	}

private:
	static QString asString(float64 value) {
		static constexpr auto kPrecision = 6;
		return QString::number(value, 'f', kPrecision);
	}

	friend inline bool operator==(const LocationCoords &a, const LocationCoords &b) {
		return (a._lat == b._lat) && (a._lon == b._lon);
	}

	friend inline bool operator<(const LocationCoords &a, const LocationCoords &b) {
		return (a._lat < b._lat) || ((a._lat == b._lat) && (a._lon < b._lon));
	}

	friend inline uint qHash(const LocationCoords &t, uint seed = 0) {
#ifndef OS_MAC_OLD
		return qHash(QtPrivate::QHashCombine().operator()(qHash(t._lat), t._lon), seed);
#else // OS_MAC_OLD
		uint h1 = qHash(t._lat, seed);
		uint h2 = qHash(t._lon, seed);
		return ((h1 << 16) | (h1 >> 16)) ^ h2 ^ seed;
#endif // OS_MAC_OLD
	}

	float64 _lat = 0;
	float64 _lon = 0;
	uint64 _access = 0;

};

struct LocationData {
	LocationData(const LocationCoords &coords) : coords(coords), loading(false) {
	}

	LocationCoords coords;
	ImagePtr thumb;
	bool loading;

	void load(Data::FileOrigin origin);
};

class LocationClickHandler : public ClickHandler {
public:
	LocationClickHandler(const LocationCoords &coords) : _coords(coords) {
		setup();
	}

	void onClick(ClickContext context) const override;

	QString tooltip() const override {
		return QString();
	}

	QString dragText() const override {
		return _text;
	}

	QString copyToClipboardText() const override;
	QString copyToClipboardContextItemText() const override;

private:

	void setup();
	LocationCoords _coords;
	QString _text;

};

class LocationManager : public QObject {
	Q_OBJECT

public:
	void init();
	void reinit();
	void deinit();

	void getData(LocationData *data);

	~LocationManager() {
		deinit();
	}

public slots:
	void onFinished(QNetworkReply *reply);
	void onFailed(QNetworkReply *reply);

private:
	void failed(LocationData *data);

	QNetworkAccessManager *manager = nullptr;
	QMap<QNetworkReply*, LocationData*> dataLoadings, imageLoadings;
	QMap<LocationData*, int32> serverRedirects;
	ImagePtr *notLoadedPlaceholder = nullptr;

};
