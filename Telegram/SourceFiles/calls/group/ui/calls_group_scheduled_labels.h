/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Calls::Group::Ui {

using namespace ::Ui;

[[nodiscard]] rpl::producer<QString> StartsWhenText(
	rpl::producer<TimeId> date);

[[nodiscard]] object_ptr<RpWidget> CreateGradientLabel(
	QWidget *parent,
	rpl::producer<QString> text);

} // namespace Calls::Group::Ui
