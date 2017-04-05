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
#pragma once

void initLocationManager();
void reinitLocationManager();
void deinitLocationManager();

class LocationCoords {
public:
	LocationCoords() = default;
	LocationCoords(float64 lat, float64 lon) : _lat(lat), _lon(lon) {
	}
	LocationCoords(const MTPDgeoPoint &point) : _lat(point.vlat.v), _lon(point.vlong.v) {
	}

	QString latAsString() const {
		return asString(_lat);
	}
	QString lonAsString() const {
		return asString(_lon);
	}
	MTPGeoPoint toMTP() const {
		return MTP_geoPoint(MTP_double(_lon), MTP_double(_lat));
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

};

struct LocationData {
	LocationData(const LocationCoords &coords) : coords(coords), loading(false) {
	}

	LocationCoords coords;
	ImagePtr thumb;
	bool loading;

	void load();
};

class LocationClickHandler : public ClickHandler {
public:
	LocationClickHandler(const LocationCoords &coords) : _coords(coords) {
		setup();
	}

	void onClick(Qt::MouseButton button) const override;

	QString tooltip() const override {
		return QString();
	}

	QString dragText() const override {
		return _text;
	}

	void copyToClipboard() const override {
		if (!_text.isEmpty()) {
			QApplication::clipboard()->setText(_text);
		}
	}
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
