/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rect_part.h"

namespace style {
struct FlatLabel;
struct DividerBar;
struct DividerLabel;
} // namespace style

namespace st {
extern const style::margins &defaultBoxDividerLabelPadding;
extern const style::DividerBar &defaultDividerBar;
extern const style::DividerLabel &defaultDividerLabel;
} // namespace st

namespace Ui {

class FlatLabel;
class VerticalLayout;

void AddSkip(not_null<Ui::VerticalLayout*> container);
void AddSkip(not_null<Ui::VerticalLayout*> container, int skip);
void AddDivider(
	not_null<Ui::VerticalLayout*> container,
	const style::DividerBar &st = st::defaultDividerBar);
not_null<Ui::FlatLabel*> AddDividerText(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text,
	const style::margins &margins = st::defaultBoxDividerLabelPadding,
	const style::DividerLabel &st = st::defaultDividerLabel,
	RectParts parts = RectPart::Top | RectPart::Bottom);
not_null<Ui::FlatLabel*> AddDividerText(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<TextWithEntities> text,
	const style::margins &margins = st::defaultBoxDividerLabelPadding,
	const style::DividerLabel &st = st::defaultDividerLabel,
	RectParts parts = RectPart::Top | RectPart::Bottom);
not_null<Ui::FlatLabel*> AddSubsectionTitle(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text,
	style::margins addPadding = {},
	const style::FlatLabel *st = nullptr);

} // namespace Ui
