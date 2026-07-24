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
	start(&peer->session(), peer->input());
}

void Manager::startTopic(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		const QString &topicTitle) {
	if (_panel) {
		_panel->activatePanel();
		return;
	}
	_controller = std::make_unique<Controller>(
		&peer->session().mtp(),
		peer->input(),
		int32(topicRootId.bare),
		uint64(peer->id.value),
		topicTitle);
	setupPanel(&peer->session());
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
	setupPanel(session);
}

void Manager::setupPanel(not_null<Main::Session*> session) {
	_panel = std::make_unique<View::PanelController>(
		session,
		_controller.get());
	session->account().sessionChanges(
	) | rpl::filter([=](Main::Session *value) {
		return (value != session);
	}) | rpl::on_next([=] {
		stop();
	}, _panel->lifetime());

	_viewChanges.fire(_panel.get());

	_panel->stopRequests(
	) | rpl::on_next([=] {
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

void Manager::stopWithConfirmation(Fn<void()> callback) {
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
