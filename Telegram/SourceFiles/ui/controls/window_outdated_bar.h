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

[[nodiscard]] object_ptr<RpWidget> CreateOutdatedBar(
	not_null<QWidget*> parent,
	const QString &workingPath);

} // namespace Ui
