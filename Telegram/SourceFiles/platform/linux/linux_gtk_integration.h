/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/file_utilities.h"

namespace Platform {
namespace internal {

inline constexpr auto kDisableGtkIntegration = "TDESKTOP_DISABLE_GTK_INTEGRATION"_cs;

class GtkIntegration {
public:
	static GtkIntegration *Instance();

	void load();
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] bool checkVersion(
		uint major,
		uint minor,
		uint micro) const;

	[[nodiscard]] std::optional<bool> getBoolSetting(
		const QString &propertyName) const;

	[[nodiscard]] std::optional<int> getIntSetting(
		const QString &propertyName) const;

	[[nodiscard]] std::optional<QString> getStringSetting(
		const QString &propertyName) const;

	[[nodiscard]] std::optional<int> scaleFactor() const;

	using FileDialogType = ::FileDialog::internal::Type;
	[[nodiscard]] bool fileDialogSupported() const;
	[[nodiscard]] bool useFileDialog(
		FileDialogType type = FileDialogType::ReadFile) const;
	[[nodiscard]] bool getFileDialog(
		QPointer<QWidget> parent,
		QStringList &files,
		QByteArray &remoteContent,
		const QString &caption,
		const QString &filter,
		FileDialogType type,
		QString startFile) const;
	
	[[nodiscard]] bool showOpenWithDialog(const QString &filepath) const;

	[[nodiscard]] QImage getImageFromClipboard() const;

private:
	GtkIntegration();
};

} // namespace internal
} // namespace Platform
