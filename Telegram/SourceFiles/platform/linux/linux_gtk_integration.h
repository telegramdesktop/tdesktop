/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {
namespace internal {

class GtkIntegration {
public:
	static GtkIntegration *Instance();

	void load();

	[[nodiscard]] bool showOpenWithDialog(const QString &filepath) const;

	[[nodiscard]] QImage getImageFromClipboard() const;

private:
	GtkIntegration();
};

} // namespace internal
} // namespace Platform
