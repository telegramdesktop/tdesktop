/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

namespace Api {
struct SendOptions;
} // namespace Api

namespace HistoryView {
class ComposeControls;
} // namespace HistoryView

namespace HistoryView::Controls {
struct VoiceToSend;
} // namespace HistoryView::Controls

namespace Media::Stories {

class Controller;

struct ReplyAreaData {
	UserData *user = nullptr;
	StoryId id = 0;

	friend inline auto operator<=>(ReplyAreaData, ReplyAreaData) = default;
	friend inline bool operator==(ReplyAreaData, ReplyAreaData) = default;
};

class ReplyArea final : public base::has_weak_ptr {
public:
	explicit ReplyArea(not_null<Controller*> controller);
	~ReplyArea();

	void show(ReplyAreaData data);

	[[nodiscard]] rpl::producer<bool> focusedValue() const;

private:
	using VoiceToSend = HistoryView::Controls::VoiceToSend;

	void initGeometry();
	void initActions();

	void send(Api::SendOptions options);
	void sendVoice(VoiceToSend &&data);
	void chooseAttach(std::optional<bool> overrideSendImagesAsPhotos);

	void showPremiumToast(not_null<DocumentData*> emoji);

	const not_null<Controller*> _controller;
	const std::unique_ptr<HistoryView::ComposeControls> _controls;

	ReplyAreaData _data;
	bool _choosingAttach = false;

	rpl::lifetime _lifetime;

};

} // namespace Media::Stories
