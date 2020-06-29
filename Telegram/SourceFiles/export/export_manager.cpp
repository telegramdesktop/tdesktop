/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_manager.h"

#include "export/export_controller.h"
#include "export/view/export_view_panel_controller.h"
#include "data/data_peer.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "ui/layers/box_content.h"
#include "base/unixtime.h"

namespace Export {

Manager::Manager() = default;

Manager::~Manager() = default;

void Manager::start(not_null<PeerData*> peer) {
	start(&peer->session(), peer->input);
}

void Manager::start(
		not_null<Main::Session*> session,
		const MTPInputPeer &singlePeer) {
	if (_panel) {
		_panel->activatePanel();
		return;
	}
	_controller = std::make_unique<Controller>(
		&session->mtp(),
		singlePeer);
	_panel = std::make_unique<View::PanelController>(
		session,
		_controller.get());
	session->account().sessionChanges(
	) | rpl::filter([=](Main::Session *value) {
		return (value != session);
	}) | rpl::start_with_next([=] {
		stop();
	}, _panel->lifetime());

	_viewChanges.fire(_panel.get());

	_panel->stopRequests(
	) | rpl::start_with_next([=] {
		LOG(("Export Info: Stop requested."));
		stop();
	}, _controller->lifetime());
}

rpl::producer<View::PanelController*> Manager::currentView(
) const {
	return _viewChanges.events_starting_with(_panel.get());
}

bool Manager::inProgress() const {
	return _controller != nullptr;
}

bool Manager::inProgress(not_null<Main::Session*> session) const {
	return _panel && (&_panel->session() == session);
}

void Manager::stopWithConfirmation(FnMut<void()> callback) {
	if (!_panel) {
		callback();
		return;
	}
	auto closeAndCall = [=, callback = std::move(callback)]() mutable {
		auto saved = std::move(callback);
		LOG(("Export Info: Stop With Confirmation."));
		stop();
		if (saved) {
			saved();
		}
	};
	_panel->stopWithConfirmation(std::move(closeAndCall));
}

void Manager::stop() {
	if (_panel) {
		LOG(("Export Info: Destroying."));
		_panel = nullptr;
		_viewChanges.fire(nullptr);
	}
	_controller = nullptr;
}

} // namespace Export
