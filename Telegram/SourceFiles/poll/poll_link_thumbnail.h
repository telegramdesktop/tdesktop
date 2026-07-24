/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/dynamic_image.h"

namespace Poll {

[[nodiscard]] std::shared_ptr<Ui::DynamicImage> MakeLinkThumbnail();

} // namespace Poll
