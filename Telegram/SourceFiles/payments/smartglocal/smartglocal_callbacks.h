/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace SmartGlocal {

class Token;
class Error;

using TokenCompletionCallback = std::function<void(Token, Error)>;

} // namespace SmartGlocal
