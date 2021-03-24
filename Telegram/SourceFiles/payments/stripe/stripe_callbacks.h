/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <functional>

namespace Stripe {

class Error;
class Token;

using TokenCompletionCallback = std::function<void(Token, Error)>;

} // namespace Stripe
