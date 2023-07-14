/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace Ui {

class AbstractCheckView;

struct ForwardOptions {
	bool dropNames = false;
	bool hasCaptions = false;
	bool dropCaptions = false;
};

void FillForwardOptions(
	Fn<not_null<AbstractCheckView*>(
		rpl::producer<QString> &&,
		bool)> createView,
	int count,
	ForwardOptions options,
	Fn<void(ForwardOptions)> optionsChanged,
	rpl::lifetime &lifetime);

void ForwardOptionsBox(
	not_null<GenericBox*> box,
	int count,
	ForwardOptions options,
	Fn<void(ForwardOptions)> optionsChanged,
	Fn<void()> changeRecipient);

} // namespace Ui
