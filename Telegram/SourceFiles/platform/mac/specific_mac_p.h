/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// e is NSEvent*
bool objc_handleMediaKeyEvent(void *e);

void objc_debugShowAlert(const QString &str);
void objc_outputDebugString(const QString &str);

void objc_start();
void objc_ignoreApplicationActivationRightNow();
void objc_finish();

void objc_activateProgram(WId winId);
bool objc_moveFile(const QString &from, const QString &to);
void objc_deleteDir(const QString &dir);

double objc_appkitVersion();

QString objc_documentsPath();
QString objc_appDataPath();
QByteArray objc_downloadPathBookmark(const QString &path);
QByteArray objc_pathBookmark(const QString &path);
void objc_downloadPathEnableAccess(const QByteArray &bookmark);

class objc_FileBookmark {
public:
	objc_FileBookmark(const QByteArray &bookmark);
	bool valid() const;
	bool enable() const;
	void disable() const;

	const QString &name(const QString &original) const;
	QByteArray bookmark() const;

	~objc_FileBookmark();

private:
#ifdef OS_MAC_STORE
	class objc_FileBookmarkData;
	objc_FileBookmarkData *data = nullptr;
#endif // OS_MAC_STORE

};
