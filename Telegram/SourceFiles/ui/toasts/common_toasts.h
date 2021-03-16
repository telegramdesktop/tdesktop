/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"

namespace Ui {

struct MultilineToastArgs {
	TextWithEntities text;
	crl::time duration = 0;
	QWidget *parentOverride = nullptr;
};

void ShowMultilineToast(MultilineToastArgs &&args);

} // namespace Ui
