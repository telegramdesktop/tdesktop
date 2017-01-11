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

struct LocationCoords {
	LocationCoords() : lat(0), lon(0) {
	}
	LocationCoords(float64 lat, float64 lon) : lat(lat), lon(lon) {
	}
	LocationCoords(const MTPDgeoPoint &point) : lat(point.vlat.v), lon(point.vlong.v) {
	}
	float64 lat, lon;
};
inline bool operator==(const LocationCoords &a, const LocationCoords &b) {
	return (a.lat == b.lat) && (a.lon == b.lon);
}
inline bool operator<(const LocationCoords &a, const LocationCoords &b) {
	return (a.lat < b.lat) || ((a.lat == b.lat) && (a.lon < b.lon));
}
inline uint qHash(const LocationCoords &t, uint seed = 0) {
#ifndef OS_MAC_OLD
	return qHash(QtPrivate::QHashCombine().operator()(qHash(t.lat), t.lon), seed);
#else // OS_MAC_OLD
	uint h1 = qHash(t.lat, seed);
	uint h2 = qHash(t.lon, seed);
	return ((h1 << 16) | (h1 >> 16)) ^ h2 ^ seed;
#endif // OS_MAC_OLD
}

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
