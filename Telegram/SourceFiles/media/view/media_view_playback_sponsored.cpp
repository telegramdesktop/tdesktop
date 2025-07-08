/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_playback_sponsored.h"

#include "data/components/sponsored_messages.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace Media::View {
namespace {

constexpr auto kStartDelayMin = crl::time(1000);
constexpr auto kDurationMin = 5 * crl::time(1000);

} // namespace

PlaybackSponsored::PlaybackSponsored(
	QWidget *parent,
	not_null<HistoryItem*> item)
: _parent(parent)
, _session(&item->history()->session())
, _itemId(item->fullId())
, _timer([=] { update(); }) {
	_session->sponsoredMessages().requestForVideo(item, crl::guard(this, [=](
			Data::SponsoredForVideo data) {
		if (data.list.empty()) {
			return;
		}
		_data = std::move(data);
		if (_data->state.initial()
			|| (_data->state.itemIndex > _data->list.size())
			|| (_data->state.itemIndex == _data->list.size()
				&& _data->state.leftTillShow <= 0)) {
			_data->state.itemIndex = 0;
			_data->state.leftTillShow = std::max(
				_data->startDelay,
				kStartDelayMin);
		}
		update();
	}));
}

PlaybackSponsored::~PlaybackSponsored() {
	saveState();
}

void PlaybackSponsored::start() {
	_started = true;
	if (!_paused) {
		_start = crl::now();
		update();
	}
}

void PlaybackSponsored::setPaused(bool paused) {
	if (_paused == paused) {
		return;
	}
	_paused = paused;
	if (!_started) {
		return;
	} else if (_paused) {
		update();
		const auto state = computeState();
		_start = 0;
		_timer.cancel();
	} else {
		_start = crl::now();
		update();
	}
}

void PlaybackSponsored::finish() {
	_timer.cancel();
	if (_data) {
		saveState();
		_data = std::nullopt;
	}
}

void PlaybackSponsored::update() {
	if (!_data || !_start) {
		return;
	}

	const auto [now, state] = computeState();
	const auto message = (_data->state.itemIndex < _data->list.size())
		? &_data->list[state.itemIndex]
		: nullptr;
	const auto duration = message
		? std::max(
			message->durationMin + kDurationMin,
			message->durationMax)
		: crl::time(0);
	if (_data->state.leftTillShow > 0 && state.leftTillShow <= 0) {
		_data->state.leftTillShow = 0;
		if (duration) {
			show(*message);

			_start = now;
			_timer.callOnce(duration);
			saveState();
		} else {
			finish();
		}
	} else if (_data->state.leftTillShow <= 0
		&& state.leftTillShow <= -duration) {
		hide();

		++_data->state.itemIndex;
		_data->state.leftTillShow = std::max(
			_data->betweenDelay,
			kStartDelayMin);
		_start = now;
		_timer.callOnce(_data->state.leftTillShow);
		saveState();
	} else {
		if (state.leftTillShow <= 0 && duration && !_widget) {
			show(*message);
		}
		_data->state = state;
		_timer.callOnce((state.leftTillShow > 0)
			? state.leftTillShow
			: (state.leftTillShow + duration));
	}
}

void PlaybackSponsored::show(const Data::SponsoredMessage &data) {
	_widget = std::make_unique<Ui::RpWidget>(_parent);
	_widget->setGeometry(_parent->rect());
	_widget->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(_widget.get());
		p.fillRect(_widget->rect(), QColor(0, 128, 0, 128));
	}, _widget->lifetime());
	_widget->show();
}

void PlaybackSponsored::hide() {
	_widget = nullptr;
}

void PlaybackSponsored::saveState() {
	_session->sponsoredMessages().updateForVideo(
		_itemId,
		computeState().data);
}

PlaybackSponsored::State PlaybackSponsored::computeState() const {
	auto result = State{ crl::now() };
	if (!_data) {
		return result;
	}
	result.data = _data->state;
	if (!_start) {
		return result;
	}
	const auto elapsed = result.now - _start;
	result.data.leftTillShow -= elapsed;
	return result;
}

rpl::lifetime &PlaybackSponsored::lifetime() {
	return _lifetime;
}

bool PlaybackSponsored::Has(HistoryItem *item) {
	return item
		&& item->history()->session().sponsoredMessages().canHaveFor(item);
}

} // namespace Media::View
