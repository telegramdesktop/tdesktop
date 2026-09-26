/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/community_pending_requests_box.h"

#include "data/data_channel.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "window/window_session_controller.h"

void ShowCommunityPendingRequestsBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> community) {
	navigation->showSection(
		std::make_shared<Info::Memento>(
			community,
			Info::Section::Type::CommunityRequests));
}
