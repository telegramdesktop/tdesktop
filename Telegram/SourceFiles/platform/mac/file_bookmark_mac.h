/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {

class FileBookmark final {
public:
	FileBookmark(const QByteArray &bookmark);
	~FileBookmark();

	[[nodiscard]] bool check() const;
	bool enable() const;
	void disable() const;
	[[nodiscard]] const QString &name(const QString &original) const;
	[[nodiscard]] QByteArray bookmark() const;

private:
#ifdef OS_MAC_STORE
	struct Data;
	Data *data = nullptr;
#endif // OS_MAC_STORE

};

[[nodiscard]] QByteArray PathBookmark(const QString &path);

} // namespace Platform
