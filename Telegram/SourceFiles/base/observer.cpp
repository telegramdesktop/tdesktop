/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/observer.h"

namespace base {
namespace internal {
namespace {

bool CantUseObservables = false;
void (*HandleDelayedMethod)() = nullptr;

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
	if (HandleDelayedMethod) {
		HandleDelayedMethod();
	}
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

void InitObservables(void(*HandleDelayed)()) {
	internal::HandleDelayedMethod = HandleDelayed;
}

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
