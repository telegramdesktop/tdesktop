/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/widget_helper_thread_mac.h"

#include <gsl/gsl>

#include <pthread.h>

namespace WidgetHelper {

int StartDetachedThread(void *(*entry)(void*), void *argument) {
	auto attributes = pthread_attr_t();
	if (const auto error = pthread_attr_init(&attributes)) {
		return error;
	}
	const auto destroy = gsl::finally([&] {
		pthread_attr_destroy(&attributes);
	});
	if (const auto error = pthread_attr_setstacksize(
			&attributes,
			kThreadStackBytes)) {
		return error;
	} else if (const auto error = pthread_attr_setdetachstate(
			&attributes,
			PTHREAD_CREATE_DETACHED)) {
		return error;
	}
	auto thread = pthread_t();
	return pthread_create(&thread, &attributes, entry, argument);
}

} // namespace WidgetHelper
