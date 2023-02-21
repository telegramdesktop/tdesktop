/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace PowerSaving {
enum Flag : uint32;
} // namespace PowerSaving

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Settings {

void PowerSavingBox(not_null<Ui::GenericBox*> box);

struct PowerSavingLabel {
	PowerSaving::Flag flags;
	QString label;
};

struct NestedPowerSavingLabels {
	std::optional<rpl::producer<QString>> nestedLabel;
	std::vector<PowerSavingLabel> restrictionLabels;
};

[[nodiscard]] std::vector<NestedPowerSavingLabels> PowerSavingLabelsList();

} // namespace PowerSaving
