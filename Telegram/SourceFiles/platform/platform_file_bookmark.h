/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#ifdef Q_OS_MAC
#include "platform/mac/file_bookmark_mac.h"
#else // Q_OS_MAC

namespace Platform {

class FileBookmark {
public:
	FileBookmark(const QByteArray &bookmark) {
	}
	bool check() const {
		return true;
	}
	bool enable() const {
		return true;
	}
	void disable() const {
	}
	const QString &name(const QString &original) const {
		return original;
	}
	QByteArray bookmark() const {
		return QByteArray();
	}

};

[[nodiscard]] inline QByteArray PathBookmark(const QString &path) {
	return QByteArray();
}

} // namespace Platform

#endif // Q_OS_MAC
