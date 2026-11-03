/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class AbstractButton;
class GradientButton;
class RpWidget;
class SettingsButton;
class VerticalLayout;
} // namespace Ui

namespace Settings {

struct PremiumSummaryRow {
	not_null<Ui::SettingsButton*> button;
	not_null<Ui::AbstractButton*> icon;
};

[[nodiscard]] PremiumSummaryRow AddPremiumSummaryRow(
	not_null<Ui::VerticalLayout*> content,
	rpl::producer<QString> title,
	rpl::producer<QString> about,
	bool newBadge);
void AddPremiumSummaryIcons(
	not_null<Ui::VerticalLayout*> content,
	const std::vector<Ui::AbstractButton*> &iconContainers,
	const std::vector<const style::icon*> &icons);

void SetupPinnedSubscribeButton(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::GradientButton*> button,
	not_null<::Main::Session*> session,
	rpl::producer<> showFinished);

} // namespace Settings
