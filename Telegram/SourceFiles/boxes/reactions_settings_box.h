/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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

void AddReactionAnimatedIcon(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<QPoint> iconPositionValue,
	int iconSize,
	const Data::Reaction &reaction,
	rpl::producer<> &&selects,
	rpl::producer<> &&destroys,
	not_null<rpl::lifetime*> stateLifetime);
void AddReactionCustomIcon(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<QPoint> iconPositionValue,
	int iconSize,
	not_null<Window::SessionController*> controller,
	DocumentId customId,
	rpl::producer<> &&destroys,
	not_null<rpl::lifetime*> stateLifetime);

void ReactionsSettingsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);
