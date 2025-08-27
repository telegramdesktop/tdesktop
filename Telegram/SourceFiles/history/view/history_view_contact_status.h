/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/shadow.h"

namespace Data {
class ForumTopic;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class FlatButton;
class IconButton;
class FlatLabel;
} // namespace Ui

namespace HistoryView {

class SlidingBar final {
public:
	SlidingBar(
		not_null<Ui::RpWidget*> parent,
		object_ptr<Ui::RpWidget> wrapped);

	void setVisible(bool visible);
	void raise();

	void move(int x, int y);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;
	void toggleContent(bool visible);

	void show() {
		setVisible(true);
	}
	void hide() {
		setVisible(false);
	}

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void setup(not_null<Ui::RpWidget*> parent);

	Ui::SlideWrap<Ui::RpWidget> _wrapped;
	Ui::PlainShadow _shadow;
	bool _shown = false;
	bool _contentShown = false;

	rpl::lifetime _lifetime;

};

class ContactStatus final {
public:
	ContactStatus(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer,
		bool showInForum);

	void show();
	void hide();

	[[nodiscard]] SlidingBar &bar() {
		return _bar;
	}

private:
	class Bar;
	class BgButton;

	struct State {
		enum class Type {
			None,
			ReportSpam,
			Add,
			AddOrBlock,
			UnarchiveOrBlock,
			UnarchiveOrReport,
			SharePhoneNumber,
			RequestChatInfo,
		};
		Type type = Type::None;
		int starsPerMessage = 0;
		QString requestChatName;
		TimeId requestDate = 0;
		bool requestChatIsBroadcast = false;
	};

	void setupState(not_null<PeerData*> peer, bool showInForum);
	void setupHandlers(not_null<PeerData*> peer);
	void setupAddHandler(not_null<UserData*> user);
	void setupBlockHandler(not_null<UserData*> user);
	void setupShareHandler(not_null<UserData*> user);
	void setupUnarchiveHandler(not_null<PeerData*> peer);
	void setupReportHandler(not_null<PeerData*> peer);
	void setupCloseHandler(not_null<PeerData*> peer);
	void setupRequestInfoHandler(not_null<PeerData*> peer);
	void setupEmojiStatusHandler(not_null<PeerData*> peer);

	static rpl::producer<State> PeerState(not_null<PeerData*> peer);

	const not_null<Window::SessionController*> _controller;
	State _state;
	TextWithEntities _status;
	Ui::Text::MarkedContext _context;
	QPointer<Bar> _inner;
	SlidingBar _bar;
	bool _hiddenByForum = false;
	bool _shown = false;

};

class BusinessBotStatus final {
public:
	BusinessBotStatus(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer);

	void show();
	void hide();

	[[nodiscard]] SlidingBar &bar() {
		return _bar;
	}

private:
	class Bar;

	struct State {
		UserData *bot = nullptr;
		QString manageUrl;
		bool canReply = false;
		bool paused = false;
	};

	void setupState(not_null<PeerData*> peer);
	void setupHandlers(not_null<PeerData*> peer);

	static rpl::producer<State> PeerState(not_null<PeerData*> peer);

	const not_null<Window::SessionController*> _controller;
	State _state;
	QPointer<Bar> _inner;
	SlidingBar _bar;
	bool _shown = false;

};

class TopicReopenBar final {
public:
	TopicReopenBar(
		not_null<Ui::RpWidget*> parent,
		not_null<Data::ForumTopic*> topic);

	[[nodiscard]] SlidingBar &bar() {
		return _bar;
	}

private:
	void setupState();
	void setupHandler();

	const not_null<Data::ForumTopic*> _topic;
	QPointer<Ui::FlatButton> _reopen;
	SlidingBar _bar;

};

class PaysStatus final {
public:
	PaysStatus(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<UserData*> user);

	void show();
	void hide();

	[[nodiscard]] SlidingBar &bar() {
		return _bar;
	}

private:
	class Bar;

	struct State {
		int perMessage = 0;
	};

	void setupState();
	void setupHandlers();

	const not_null<Window::SessionController*> _controller;
	const not_null<UserData*> _user;
	std::shared_ptr<rpl::variable<int>> _paidAlready;
	State _state;
	QPointer<Bar> _inner;
	SlidingBar _bar;
	bool _shown = false;

};

} // namespace HistoryView
