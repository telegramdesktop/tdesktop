/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Ui {

[[nodiscard]] QColor ColorFromSerialized(quint32 serialized);
[[nodiscard]] std::optional<QColor> MaybeColorFromSerialized(
	quint32 serialized);

} // namespace Ui
