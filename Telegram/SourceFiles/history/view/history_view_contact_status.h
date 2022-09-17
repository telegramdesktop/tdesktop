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

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class FlatButton;
class IconButton;
class FlatLabel;
} // namespace Ui

namespace HistoryView {

class ContactStatus final {
public:
	ContactStatus(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer);

	void show();
	void raise();

	void move(int x, int y);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
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
		QString requestChatName;
		bool requestChatIsBroadcast = false;
		TimeId requestDate = 0;
	};

	void setupWidgets(not_null<Ui::RpWidget*> parent);
	void setupState(not_null<PeerData*> peer);
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
	Fn<std::any(Fn<void()> customEmojiRepaint)> _context;
	Ui::SlideWrap<Bar> _bar;
	Ui::PlainShadow _shadow;
	bool _shown = false;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
