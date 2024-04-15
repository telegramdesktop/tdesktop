/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/ui_integration.h" // Core::MarkedTextContext.

template <typename Object>
class object_ptr;

namespace Main {
class Session;
} // namespace Main

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Ui {

[[nodiscard]] object_ptr<Ui::FlatLabel> CreateLabelWithCustomEmoji(
	QWidget *parent,
	rpl::producer<TextWithEntities> &&text,
	Core::MarkedTextContext context,
	const style::FlatLabel &st);

} // namespace Ui
