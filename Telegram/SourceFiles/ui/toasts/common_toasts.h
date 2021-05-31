/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"
#include "base/weak_ptr.h"

namespace Ui {
namespace Toast {
class Instance;
} // namespace Toast

struct MultilineToastArgs {
	QWidget *parentOverride = nullptr;
	TextWithEntities text;
	crl::time duration = 0;
};

base::weak_ptr<Toast::Instance> ShowMultilineToast(
	MultilineToastArgs &&args);

} // namespace Ui
