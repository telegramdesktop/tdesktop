/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
namespace Emoji {

class ManageSetsBox : public BoxContent {
public:
	explicit ManageSetsBox(QWidget*);

protected:
	void prepare() override;

};

} // namespace Emoji
} // namespace Ui
