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

namespace Main {
class Session;
} // namespace Main

namespace Media::View {

class PlaybackSponsored final : public base::has_weak_ptr {
public:
	PlaybackSponsored(QWidget *parent, not_null<HistoryItem*> item);
	~PlaybackSponsored();

	void start();
	void setPaused(bool paused);

	[[nodiscard]] rpl::lifetime &lifetime();

	[[nodiscard]] static bool Has(HistoryItem *item);

private:
	struct State {
		crl::time now = 0;
		Data::SponsoredForVideoState data;
	};

	void update();
	void finish();
	void show(const Data::SponsoredMessage &data);
	void hide();
	[[nodiscard]] State computeState() const;
	void saveState();

	const not_null<QWidget*> _parent;
	const not_null<Main::Session*> _session;
	const FullMsgId _itemId;

	std::unique_ptr<Ui::RpWidget> _widget;

	crl::time _start = 0;
	bool _started = false;
	bool _paused = false;
	base::Timer _timer;

	std::optional<Data::SponsoredForVideo> _data;

	rpl::lifetime _lifetime;

};

} // namespace Media::View
