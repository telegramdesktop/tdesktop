/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

#include <QtNetwork/QLocalSocket>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QNetworkReply>

class UpdateChecker : public QObject {
	Q_OBJECT

public:
	UpdateChecker(QThread *thread, const QString &url);

	void unpackUpdate();

	int32 ready();
	int32 size();

	static void clearAll();

	~UpdateChecker();

public slots:

	void start();
	void partMetaGot();
	void partFinished(qint64 got, qint64 total);
	void partFailed(QNetworkReply::NetworkError e);
	void sendRequest();

private:
	void initOutput();

	void fatalFail();

	QString updateUrl;
	QNetworkAccessManager manager;
	QNetworkReply *reply;
	int32 already, full;
	QFile outputFile;

	QMutex mutex;

};

bool checkReadyUpdate();

#else // TDESKTOP_DISABLE_AUTOUPDATE
class UpdateChecker : public QObject {
	Q_OBJECT
};

#endif // TDESKTOP_DISABLE_AUTOUPDATE

QString countBetaVersionSignature(uint64 version);
