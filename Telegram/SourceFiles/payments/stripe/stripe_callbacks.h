/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include <functional>

namespace Stripe {

class Error;
class Token;

using TokenCompletionCallback = std::function<void(Token, Error)>;

} // namespace Stripe
