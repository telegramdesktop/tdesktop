/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Export {

class Controller;

namespace View {
class PanelController;
} // namespace View

class Manager final {
public:
	Manager();
	~Manager();

	void start(not_null<PeerData*> peer);
	void start(
		not_null<Main::Session*> session,
		const MTPInputPeer &singlePeer = MTP_inputPeerEmpty());

	[[nodiscard]] rpl::producer<View::PanelController*> currentView() const;
	[[nodiscard]] bool inProgress() const;
	[[nodiscard]] bool inProgress(not_null<Main::Session*> session) const;
	void stopWithConfirmation(FnMut<void()> callback);
	void stop();

private:
	std::unique_ptr<Controller> _controller;
	std::unique_ptr<View::PanelController> _panel;
	rpl::event_stream<View::PanelController*> _viewChanges;

};

} // namespace Export
