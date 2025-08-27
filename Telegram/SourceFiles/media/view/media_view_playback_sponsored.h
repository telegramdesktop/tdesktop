/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/components/sponsored_messages.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Media::View {

class PlaybackSponsored final : public base::has_weak_ptr {
public:
	PlaybackSponsored(
		not_null<Ui::RpWidget*> controls,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<HistoryItem*> item);
	~PlaybackSponsored();

	void start();
	void setPaused(bool paused);

	[[nodiscard]] rpl::lifetime &lifetime();

	[[nodiscard]] static bool Has(HistoryItem *item);

private:
	class Message;
	struct State {
		crl::time now = 0;
		Data::SponsoredForVideoState data;
	};

	void update();
	void finish();
	void updatePaused();
	void showPremiumPromo();
	void setPausedInside(bool paused);
	void setPausedOutside(bool paused);
	void show(const Data::SponsoredMessage &data);
	void hide(crl::time now);
	[[nodiscard]] State computeState() const;
	void saveState();

	const not_null<QWidget*> _parent;
	const not_null<Main::Session*> _session;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const FullMsgId _itemId;

	rpl::variable<QRect> _controlsGeometry;
	std::unique_ptr<Message> _widget;

	rpl::variable<crl::time> _allowCloseAt;
	crl::time _start = 0;
	bool _started = false;
	bool _paused = false;
	bool _pausedInside = false;
	bool _pausedOutside = false;
	base::Timer _timer;

	std::optional<Data::SponsoredForVideo> _data;

	rpl::lifetime _lifetime;

};

} // namespace Media::View
