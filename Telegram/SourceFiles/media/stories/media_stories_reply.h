/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HistoryView {
class ComposeControls;
} // namespace HistoryView

namespace Media::Stories {

class Delegate;

struct ReplyAreaData {
	not_null<UserData*> user;

	friend inline auto operator<=>(ReplyAreaData, ReplyAreaData) = default;
};

class ReplyArea final {
public:
	explicit ReplyArea(not_null<Delegate*> delegate);
	~ReplyArea();

private:
	void showPremiumToast(not_null<DocumentData*> emoji);

	const not_null<Delegate*> _delegate;
	const std::unique_ptr<HistoryView::ComposeControls> _controls;

	rpl::lifetime _lifetime;

};

} // namespace Media::Stories
