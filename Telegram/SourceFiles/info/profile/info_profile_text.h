/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

#include <rpl/producer.h>

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {
class VerticalLayout;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info {
namespace Profile {

struct TextWithLabel {
	object_ptr<Ui::SlideWrap<Ui::VerticalLayout>> wrap;
	not_null<Ui::FlatLabel*> text;
	not_null<Ui::FlatLabel*> subtext;
};

TextWithLabel CreateTextWithLabel(
	QWidget *parent,
	rpl::producer<TextWithEntities> &&label,
	rpl::producer<TextWithEntities> &&text,
	const style::FlatLabel &textSt,
	const style::margins &padding);

} // namespace Profile
} // namespace Info
