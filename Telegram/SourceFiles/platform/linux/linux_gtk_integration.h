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

class GtkIntegration {
public:
	static GtkIntegration *Instance();

	void load();

	[[nodiscard]] std::optional<int> scaleFactor() const;

	using FileDialogType = ::FileDialog::internal::Type;
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
