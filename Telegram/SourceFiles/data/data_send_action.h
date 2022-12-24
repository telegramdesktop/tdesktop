/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

class History;

namespace HistoryView {
class SendActionPainter;
} // namespace HistoryView

namespace Data {

class Thread;

class SendActionManager final {
public:
	struct AnimationUpdate {
		not_null<Thread*> thread;
		int left = 0;
		int width = 0;
		int height = 0;
		bool textUpdated = false;
	};
	explicit SendActionManager();

	void registerFor(
		not_null<History*> history,
		MsgId rootId,
		not_null<UserData*> user,
		const MTPSendMessageAction &action,
		TimeId when);

	[[nodiscard]] auto animationUpdated() const
		-> rpl::producer<AnimationUpdate>;
	void updateAnimation(AnimationUpdate &&update);
	[[nodiscard]] auto speakingAnimationUpdated() const
		-> rpl::producer<not_null<History*>>;
	void updateSpeakingAnimation(not_null<History*> history);

	using SendActionPainter = HistoryView::SendActionPainter;
	[[nodiscard]] std::shared_ptr<SendActionPainter> repliesPainter(
		not_null<History*> history,
		MsgId rootId);
	void repliesPainterRemoved(
		not_null<History*> history,
		MsgId rootId);
	void repliesPaintersClear(
		not_null<History*> history,
		not_null<UserData*> user);

	void clear();

private:
	bool callback(crl::time now);
	[[nodiscard]] SendActionPainter *lookupPainter(
		not_null<History*> history,
		MsgId rootId);

	// When typing in this history started.
	base::flat_map<
		std::pair<not_null<History*>, MsgId>,
		crl::time> _sendActions;
	Ui::Animations::Basic _animation;

	rpl::event_stream<AnimationUpdate> _animationUpdate;
	rpl::event_stream<not_null<History*>> _speakingAnimationUpdate;

	base::flat_map<
		not_null<History*>,
		base::flat_map<
			MsgId,
			std::weak_ptr<SendActionPainter>>> _painters;

};

} // namespace Data
