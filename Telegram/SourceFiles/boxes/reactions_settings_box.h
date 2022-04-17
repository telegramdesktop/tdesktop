/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
struct Reaction;
} // namespace Data

void AddReactionLottieIcon(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<QPoint> iconPositionValue,
	int iconSize,
	const Data::Reaction &reaction,
	rpl::producer<> &&selects,
	rpl::producer<> &&destroys,
	not_null<rpl::lifetime*> stateLifetime);

void ReactionsSettingsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);
