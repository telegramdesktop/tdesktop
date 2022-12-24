/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

[[nodiscard]] QColor ColorFromSerialized(quint32 serialized);
[[nodiscard]] std::optional<QColor> MaybeColorFromSerialized(
	quint32 serialized);

} // namespace Ui
