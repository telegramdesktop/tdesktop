/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {

class FlatLabel;
class VerticalLayout;

void AddSkip(not_null<Ui::VerticalLayout*> container);
void AddSkip(not_null<Ui::VerticalLayout*> container, int skip);
void AddDivider(not_null<Ui::VerticalLayout*> container);
void AddDividerText(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text);
void AddDividerText(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<TextWithEntities> text);
not_null<Ui::FlatLabel*> AddSubsectionTitle(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text,
	style::margins addPadding = {},
	const style::FlatLabel *st = nullptr);

} // namespace Ui
