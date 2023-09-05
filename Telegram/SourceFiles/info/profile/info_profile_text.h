/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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
	const style::FlatLabel &labelSt,
	const style::FlatLabel &textSt,
	const style::margins &padding);

} // namespace Profile
} // namespace Info
