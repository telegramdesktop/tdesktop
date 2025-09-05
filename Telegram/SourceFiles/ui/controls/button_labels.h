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

class RpWidget;

void SetButtonTwoLabels(
    not_null<Ui::RpWidget*> button,
    rpl::producer<TextWithEntities> title,
    rpl::producer<TextWithEntities> subtitle,
    const style::FlatLabel &st,
    const style::FlatLabel &subst,
    const style::color *textFg = nullptr);

} // namespace Ui