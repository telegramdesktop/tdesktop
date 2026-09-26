/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "translate_provider.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {

[[nodiscard]] std::unique_ptr<TranslateProvider> CreateMTProtoTranslateProvider(
	not_null<Main::Session*> session);

} // namespace Ui
