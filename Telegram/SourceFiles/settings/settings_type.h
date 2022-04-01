/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Settings {

struct SectionMeta;
using Type = not_null<SectionMeta*>(*)();

} // namespace Settings
