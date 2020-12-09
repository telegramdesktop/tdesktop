/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/send_action_animations.h"
#include "api/api_send_progress.h"

class UserData;

namespace Main {
class Session;
} // namespace Main

namespace Api {
enum class SendProgressType;
struct SendProgress;
} // namespace Api

namespace HistoryView {

class SendActionPainter final {
public:
	explicit SendActionPainter(not_null<History*> history);

	bool paint(
		Painter &p,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		style::color color,
		crl::time now);
	void paintSpeaking(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		style::color color,
		crl::time now);

	bool updateNeedsAnimating(
		crl::time now,
		bool force = false);
	bool updateNeedsAnimating(
		not_null<UserData*> user,
		const MTPSendMessageAction &action);
	void clear(not_null<UserData*> from);

private:
	const not_null<History*> _history;
	const base::weak_ptr<Main::Session> _weak;
	base::flat_map<not_null<UserData*>, crl::time> _typing;
	base::flat_map<not_null<UserData*>, crl::time> _speaking;
	base::flat_map<not_null<UserData*>, Api::SendProgress> _sendActions;
	QString _sendActionString;
	Ui::Text::String _sendActionText;
	Ui::SendActionAnimation _sendActionAnimation;
	Ui::SendActionAnimation _speakingAnimation;

};

} // namespace HistoryView
