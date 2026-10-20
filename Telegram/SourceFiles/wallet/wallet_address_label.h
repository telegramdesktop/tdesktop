/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Wallet {

[[nodiscard]] object_ptr<Ui::FlatLabel> MakeAddressLabel(
	not_null<QWidget*> parent,
	const QString &address,
	const style::FlatLabel &st,
	Fn<void()> clicked);

} // namespace Wallet
