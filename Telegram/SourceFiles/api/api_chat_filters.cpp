/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_chat_filters.h"

#include "data/data_session.h"
#include "data/data_chat_filters.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Api {

void SaveNewFilterPinned(
		not_null<Main::Session*> session,
		FilterId filterId) {
	const auto &order = session->data().pinnedChatsOrder(
		nullptr,
		filterId);
	auto &filters = session->data().chatsFilters();
	const auto &filter = filters.applyUpdatedPinned(
		filterId,
		order);
	session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
		MTP_int(filterId),
		filter.tl()
	)).send();

}

void SaveNewOrder(
		not_null<Main::Session*> session,
		const std::vector<FilterId> &order) {
	auto &filters = session->data().chatsFilters();
	auto ids = QVector<MTPint>();
	ids.reserve(order.size());
	for (const auto id : order) {
		ids.push_back(MTP_int(id));
	}
	const auto wrapped = MTP_vector<MTPint>(ids);
	filters.apply(MTP_updateDialogFilterOrder(wrapped));
	session->api().request(MTPmessages_UpdateDialogFiltersOrder(
		wrapped
	)).send();
}

} // namespace Api
