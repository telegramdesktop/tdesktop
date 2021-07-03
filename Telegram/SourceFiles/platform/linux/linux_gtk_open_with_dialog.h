/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {
namespace File {
namespace internal {

class GtkOpenWithDialog {
public:
	GtkOpenWithDialog(
		const QString &parent,
		const QString &filepath);

	~GtkOpenWithDialog();

	[[nodiscard]] rpl::producer<bool> response();
	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	class Private;
	const std::unique_ptr<Private> _private;

	rpl::lifetime _lifetime;
};

[[nodiscard]] std::unique_ptr<GtkOpenWithDialog> CreateGtkOpenWithDialog(
	const QString &parent,
	const QString &filepath);

} // namespace internal
} // namespace File
} // namespace Platform
