/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"

namespace Gram {

[[nodiscard]] QByteArray HmacSha512(
	bytes::const_span key,
	bytes::const_span data);

} // namespace Gram
