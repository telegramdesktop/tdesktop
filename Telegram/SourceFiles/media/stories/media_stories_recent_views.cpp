/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_recent_views.h"

#include "data/data_peer.h"
#include "main/main_session.h"
#include "media/stories/media_stories_controller.h"
#include "lang/lang_keys.h"
#include "ui/chat/group_call_userpics.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_media_view.h"

namespace Media::Stories {
namespace {

[[nodiscard]] rpl::producer<std::vector<Ui::GroupCallUser>> ContentByUsers(
		const std::vector<not_null<PeerData*>> &list) {
	struct Userpic {
		not_null<PeerData*> peer;
		mutable Ui::PeerUserpicView view;
		mutable InMemoryKey uniqueKey;
	};

	struct State {
		std::vector<Userpic> userpics;
		std::vector<Ui::GroupCallUser> current;
		base::has_weak_ptr guard;
		bool someUserpicsNotLoaded = false;
		bool scheduled = false;
	};

	static const auto size = st::storiesRecentViewsUserpics.size;

	static const auto GenerateUserpic = [](Userpic &userpic) {
		auto result = userpic.peer->generateUserpicImage(
			userpic.view,
			size * style::DevicePixelRatio());
		result.setDevicePixelRatio(style::DevicePixelRatio());
		return result;
	};

	static const auto RegenerateUserpics = [](not_null<State*> state) {
		Expects(state->userpics.size() == state->current.size());

		state->someUserpicsNotLoaded = false;
		const auto count = int(state->userpics.size());
		for (auto i = 0; i != count; ++i) {
			auto &userpic = state->userpics[i];
			auto &participant = state->current[i];
			const auto peer = userpic.peer;
			const auto key = peer->userpicUniqueKey(userpic.view);
			if (peer->hasUserpic() && peer->useEmptyUserpic(userpic.view)) {
				state->someUserpicsNotLoaded = true;
			}
			if (userpic.uniqueKey == key) {
				continue;
			}
			participant.userpicKey = userpic.uniqueKey = key;
			participant.userpic = GenerateUserpic(userpic);
		}
	};

	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto state = lifetime.make_state<State>();
		const auto pushNext = [=] {
			RegenerateUserpics(state);
			consumer.put_next_copy(state->current);
		};

		for (const auto &peer : list) {
			state->userpics.push_back(Userpic{
				.peer = peer,
			});
			state->current.push_back(Ui::GroupCallUser{
				.id = uint64(peer->id.value),
			});
			peer->loadUserpic();
		}
		pushNext();

		if (!list.empty()) {
			list.front()->session().downloaderTaskFinished(
			) | rpl::filter([=] {
				return state->someUserpicsNotLoaded && !state->scheduled;
			}) | rpl::start_with_next([=] {
				for (const auto &userpic : state->userpics) {
					if (userpic.peer->userpicUniqueKey(userpic.view)
						!= userpic.uniqueKey) {
						state->scheduled = true;
						crl::on_main(&state->guard, [=] {
							state->scheduled = false;
							pushNext();
						});
						return;
					}
				}
			}, lifetime);
		}
		return lifetime;
	};
}

} // namespace

RecentViews::RecentViews(not_null<Controller*> controller)
: _controller(controller) {
}

RecentViews::~RecentViews() = default;

void RecentViews::show(RecentViewsData data) {
	if (_data == data) {
		return;
	}
	const auto totalChanged = _text.isEmpty() || (_data.total != data.total);
	const auto usersChanged = !_userpics || (_data.list != data.list);
	_data = data;
	if (!_data.valid) {
		_text = {};
		_userpics = nullptr;
		_widget = nullptr;
		return;
	}
	if (!_widget) {
		const auto parent = _controller->wrap();
		auto widget = std::make_unique<Ui::RpWidget>(parent);
		const auto raw = widget.get();
		raw->show();

		_controller->layoutValue(
		) | rpl::start_with_next([=](const Layout &layout) {
			raw->setGeometry(layout.views);
		}, raw->lifetime());

		raw->paintRequest(
		) | rpl::start_with_next([=](QRect clip) {
			auto p = Painter(raw);
			const auto skip = st::storiesRecentViewsSkip;
			const auto full = _userpicsWidth + skip + _text.maxWidth();
			const auto use = std::min(full, raw->width());
			const auto ux = (raw->width() - use) / 2;
			const auto height = st::storiesRecentViewsUserpics.size;
			const auto uy = (raw->height() - height) / 2;
			const auto tx = ux + _userpicsWidth + skip;
			const auto ty = (raw->height() - st::normalFont->height) / 2;
			_userpics->paint(p, ux, uy, height);
			p.setPen(st::storiesComposeWhiteText);
			_text.drawElided(p, tx, ty, use - _userpicsWidth - skip);
		}, raw->lifetime());

		_widget = std::move(widget);
	}
	if (totalChanged) {
		_text.setText(st::defaultTextStyle, data.total
			? tr::lng_stories_views(tr::now, lt_count, data.total)
			: tr::lng_stories_no_views(tr::now));
	}
	if (!_userpics) {
		_userpics = std::make_unique<Ui::GroupCallUserpics>(
			st::storiesRecentViewsUserpics,
			rpl::single(true),
			[=] { _widget->update(); });

		_userpics->widthValue() | rpl::start_with_next([=](int width) {
			_userpicsWidth = width;
		}, _widget->lifetime());
	}
	if (usersChanged) {
		_userpicsLifetime = ContentByUsers(
			data.list
		) | rpl::start_with_next([=](
				const std::vector<Ui::GroupCallUser> &list) {
			_userpics->update(list, true);
		});
	}
}

} // namespace Media::Stories
