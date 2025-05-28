/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

template <typename Object>
class object_ptr;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Settings {

[[nodiscard]] object_ptr<Ui::RpWidget> CreateValidateGoodIcon(
	not_null<Main::Session*> session);

} // namespace Settings

