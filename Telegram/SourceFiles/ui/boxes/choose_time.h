/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {

class RpWidget;

struct ChooseTimeResult {
	object_ptr<RpWidget> widget;
	rpl::producer<TimeId> secondsValue;
};

ChooseTimeResult ChooseTimeWidget(
	not_null<RpWidget*> parent,
	TimeId startSeconds);

} // namespace Ui
