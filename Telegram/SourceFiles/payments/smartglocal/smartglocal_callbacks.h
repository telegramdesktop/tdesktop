/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include <functional>

namespace SmartGlocal {

class Token;
class Error;

using TokenCompletionCallback = std::function<void(Token, Error)>;

} // namespace SmartGlocal
