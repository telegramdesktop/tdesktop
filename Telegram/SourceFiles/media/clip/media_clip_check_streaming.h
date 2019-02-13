/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media {
namespace Clip {

bool CheckStreamingSupport(
	const FileLocation &location,
	QByteArray data);

} // namespace Clip
} // namespace Media
