/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Tgw {

enum class EnginePlace : uchar {
	InProcess,
	Helper,
	Nowhere,
};

void PrepareRemoteEngines();
[[nodiscard]] EnginePlace ChosenEnginePlace();
[[nodiscard]] bool StopRemoteEngines(Fn<void()> stopped);

} // namespace Tgw
