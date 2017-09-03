/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "base/lambda.h"
#include "base/algorithm.h"
#include <functional>

namespace rpl {

class lifetime {
public:
	lifetime() = default;
	lifetime(lifetime &&other);
	lifetime &operator=(lifetime &&other);

	template <typename Destroy, typename = decltype(std::declval<Destroy>()())>
	lifetime(Destroy &&destroy) : _destroy(std::forward<Destroy>(destroy)) {
	}

	void add(lifetime other) {
		_nested.push_back(std::move(other));
	}

	void destroy() {
		auto nested = std::exchange(_nested, std::vector<lifetime>());
		auto callback = std::exchange(_destroy, base::lambda_once<void()>());

		if (!nested.empty()) {
			nested.clear();
		}
		if (callback) {
			callback();
		}
	}

	~lifetime() {
		destroy();
	}

private:
	base::lambda_once<void()> _destroy;
	std::vector<lifetime> _nested;

};

lifetime::lifetime(lifetime &&other)
: _destroy(std::exchange(other._destroy, base::lambda_once<void()>()))
, _nested(std::exchange(other._nested, std::vector<lifetime>())) {
}

lifetime &lifetime::operator=(lifetime &&other) {
	std::swap(_destroy, other._destroy);
	std::swap(_nested, other._nested);
	other.destroy();
	return *this;
}

} // namespace rpl
