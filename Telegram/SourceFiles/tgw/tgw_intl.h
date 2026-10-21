/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace twidget {
class IntlProvider;
} // namespace twidget

namespace Tgw {

[[nodiscard]] QString UiLanguageTag();
[[nodiscard]] std::shared_ptr<twidget::IntlProvider> CreateIntl();

} // namespace Tgw
