/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// This code is licensed under GPLv3. Attribution to original source is appreciated.
// Author: https://github.com/DyingLay

AhiGram: Settings header
*/

#pragma once

#include "settings/settings_common_session.h"

namespace AhiGram {
namespace Settings {

class AhiGramSettings : public ::Settings::Section<AhiGramSettings> {
public:
	AhiGramSettings(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

	const not_null<Window::SessionController*> _controller;
};

[[nodiscard]] ::Settings::Type AhiGramSettingsId();

} // namespace Settings
} // namespace AhiGram