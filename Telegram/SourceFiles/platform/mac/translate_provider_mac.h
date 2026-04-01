// This file is part of Telegram Desktop,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "translate_provider.h"

namespace Platform {

[[nodiscard]] std::unique_ptr<Ui::TranslateProvider>
CreateTranslateProvider();

[[nodiscard]] bool IsTranslateProviderAvailable();

} // namespace Platform
