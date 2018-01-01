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
#include "base/observer.h"

namespace base {
namespace internal {
namespace {

bool CantUseObservables = false;

struct ObservableListWrap {
	~ObservableListWrap() {
		CantUseObservables = true;
	}
	OrderedSet<ObservableCallHandlers*> list;
};

ObservableListWrap &PendingObservables() {
	static ObservableListWrap result;
	return result;
}

ObservableListWrap &ActiveObservables() {
	static ObservableListWrap result;
	return result;
}

} // namespace

void RegisterPendingObservable(ObservableCallHandlers *handlers) {
	if (CantUseObservables) return;
	PendingObservables().list.insert(handlers);
	Global::RefHandleObservables().call();
}

void UnregisterActiveObservable(ObservableCallHandlers *handlers) {
	if (CantUseObservables) return;
	ActiveObservables().list.remove(handlers);
}

void UnregisterObservable(ObservableCallHandlers *handlers) {
	if (CantUseObservables) return;
	PendingObservables().list.remove(handlers);
	ActiveObservables().list.remove(handlers);
}

} // namespace internal

void HandleObservables() {
	if (internal::CantUseObservables) return;
	auto &active = internal::ActiveObservables().list;
	qSwap(active, internal::PendingObservables().list);
	while (!active.empty()) {
		auto first = *active.begin();
		(*first)();
		if (!active.empty() && *active.begin() == first) {
			active.erase(active.begin());
		}
	}
}

rpl::producer<> ObservableViewer(base::Observable<void> &observable) {
	return [&observable](const auto &consumer) {
		auto lifetime = rpl::lifetime();
		lifetime.make_state<base::Subscription>(
			observable.add_subscription([consumer]() {
				consumer.put_next({});
			}));
		return lifetime;
	};
}

} // namespace base
