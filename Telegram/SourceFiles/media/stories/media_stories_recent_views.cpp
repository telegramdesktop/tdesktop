/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_recent_views.h"

#include "api/api_who_reacted.h" // FormatReadDate.
#include "chat_helpers/compose/compose_show.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_peer.h"
#include "data/data_stories.h"
#include "main/main_session.h"
#include "media/stories/media_stories_controller.h"
#include "lang/lang_keys.h"
#include "ui/chat/group_call_userpics.h"
#include "ui/controls/who_reacted_context_action.h"
#include "ui/layers/box_content.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_media_view.h"

namespace Media::Stories {
namespace {

constexpr auto kAddPerPage = 50;
constexpr auto kLoadViewsPages = 2;

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

	static const auto size = st::storiesWhoViewed.userpics.size;

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

void RecentViews::show(
		RecentViewsData data,
		rpl::producer<Data::ReactionId> likedValue) {
	const auto guard = gsl::finally([&] {
		if (_likeIcon && likedValue) {
			std::move(
				likedValue
			) | rpl::map([](const Data::ReactionId &id) {
				return !id.empty();
			}) | rpl::start_with_next([=](bool liked) {
				const auto icon = liked
					? &st::storiesComposeControls.liked
					: &st::storiesLikesIcon;
				_likeIcon->setIconOverride(icon, icon);
			}, _likeIcon->lifetime());
		}
	});

	if (_data == data) {
		return;
	}
	const auto countersChanged = _text.isEmpty()
		|| (_data.total != data.total)
		|| (_data.reactions != data.reactions);
	const auto usersChanged = !_userpics || (_data.list != data.list);
	_data = data;
	if (!_data.self) {
		_text = {};
		_clickHandlerLifetime.destroy();
		_userpicsLifetime.destroy();
		_userpics = nullptr;
		_widget = nullptr;
	} else {
		if (!_widget) {
			setupWidget();
		}
		if (!_userpics) {
			setupUserpics();
		}
		if (countersChanged) {
			updateText();
		}
		if (usersChanged) {
			updateUserpics();
		}
		refreshClickHandler();
	}

	if (!_data.channel) {
		_likeIcon = nullptr;
		_likeWrap = nullptr;
		_viewsWrap = nullptr;
	} else {
		_viewsCounter = Lang::FormatCountDecimal(std::max(_data.total, 1));
		_likesCounter = _data.reactions
			? Lang::FormatCountDecimal(_data.reactions)
			: QString();
		if (!_likeWrap || !_likeIcon || !_viewsWrap) {
			setupViewsReactions();
		}
	}
}

Ui::RpWidget *RecentViews::likeButton() const {
	return _likeWrap.get();
}

Ui::RpWidget *RecentViews::likeIconWidget() const {
	return _likeIcon.get();
}

void RecentViews::refreshClickHandler() {
	const auto nowEmpty = _data.list.empty();
	const auto wasEmpty = !_clickHandlerLifetime;
	const auto raw = _widget.get();
	if (wasEmpty == nowEmpty) {
		return;
	} else if (nowEmpty) {
		_clickHandlerLifetime.destroy();
	} else {
		_clickHandlerLifetime = raw->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return (_data.total > 0)
				&& (e->type() == QEvent::MouseButtonPress)
				&& (static_cast<QMouseEvent*>(e.get())->button()
					== Qt::LeftButton);
		}) | rpl::start_with_next([=] {
			showMenu();
		});
	}
	raw->setCursor(_clickHandlerLifetime
		? style::cur_pointer
		: style::cur_default);
}

void RecentViews::updateUserpics() {
	_userpicsLifetime = ContentByUsers(
		_data.list
	) | rpl::start_with_next([=](
			const std::vector<Ui::GroupCallUser> &list) {
		_userpics->update(list, true);
	});
	_userpics->finishAnimating();
}

void RecentViews::setupUserpics() {
	_userpics = std::make_unique<Ui::GroupCallUserpics>(
		st::storiesWhoViewed.userpics,
		rpl::single(true),
		[=] { _widget->update(); });

	_userpics->widthValue() | rpl::start_with_next([=](int width) {
		if (_userpicsWidth != width) {
			_userpicsWidth = width;
			updatePartsGeometry();
		}
	}, _widget->lifetime());
}

void RecentViews::setupWidget() {
	_widget = std::make_unique<Ui::RpWidget>(_controller->wrap());
	const auto raw = _widget.get();
	raw->show();

	_controller->layoutValue(
	) | rpl::start_with_next([=](const Layout &layout) {
		_outer = layout.views;
		updatePartsGeometry();
	}, raw->lifetime());

	raw->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = Painter(raw);
		_userpics->paint(
			p,
			_userpicsPosition.x(),
			_userpicsPosition.y(),
			st::storiesWhoViewed.userpics.size);
		p.setPen(st::storiesComposeWhiteText);
		_text.drawElided(
			p,
			_textPosition.x(),
			_textPosition.y(),
			raw->width() - _userpicsWidth - st::storiesRecentViewsSkip);
	}, raw->lifetime());
}

void RecentViews::setupViewsReactions() {
	_viewsWrap = std::make_unique<Ui::RpWidget>(_controller->wrap());
	_likeWrap = std::make_unique<Ui::AbstractButton>(_controller->wrap());
	_likeIcon = std::make_unique<Ui::IconButton>(
		_likeWrap.get(),
		st::storiesComposeControls.like);
	_likeIcon->setAttribute(Qt::WA_TransparentForMouseEvents);

	_controller->layoutValue(
	) | rpl::start_with_next([=](const Layout &layout) {
		_outer = QRect(
			layout.content.x(),
			layout.views.y(),
			layout.content.width(),
			layout.views.height());
		updateViewsReactionsGeometry();
	}, _likeWrap->lifetime());

	const auto views = Ui::CreateChild<Ui::FlatLabel>(
		_viewsWrap.get(),
		_viewsCounter.value(),
		st::storiesViewsText);
	views->show();
	views->setAttribute(Qt::WA_TransparentForMouseEvents);
	views->move(st::storiesViewsTextPosition);

	views->widthValue(
	) | rpl::start_with_next([=](int width) {
		_viewsWrap->resize(views->x() + width, _likeIcon->height());
		updateViewsReactionsGeometry();
	}, _viewsWrap->lifetime());
	_viewsWrap->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(_viewsWrap.get());
		const auto &icon = st::storiesViewsIcon;
		const auto top = (_viewsWrap->height() - icon.height()) / 2;
		icon.paint(p, 0, top, _viewsWrap->width());
	}, _viewsWrap->lifetime());

	_likeIcon->move(0, 0);
	const auto likes = Ui::CreateChild<Ui::FlatLabel>(
		_likeWrap.get(),
		_likesCounter.value(),
		st::storiesLikesText);
	likes->show();
	likes->setAttribute(Qt::WA_TransparentForMouseEvents);
	likes->move(st::storiesLikesTextPosition);

	likes->widthValue(
	) | rpl::start_with_next([=](int width) {
		width += width
			? st::storiesLikesTextRightSkip
			: st::storiesLikesEmptyRightSkip;
		_likeWrap->resize(likes->x() + width, _likeIcon->height());
		updateViewsReactionsGeometry();
	}, _likeWrap->lifetime());

	_viewsWrap->show();
	_likeIcon->show();
	_likeWrap->show();

	_likeWrap->setClickedCallback([=] {
		_controller->toggleLiked();
	});
}

void RecentViews::updateViewsReactionsGeometry() {
	_viewsWrap->move(_outer.topLeft() + st::storiesViewsPosition);
	_likeWrap->move(_outer.topLeft()
		+ QPoint(_outer.width() - _likeWrap->width(), 0)
		+ st::storiesLikesPosition);
}

void RecentViews::updatePartsGeometry() {
	const auto skip = st::storiesRecentViewsSkip;
	const auto full = _userpicsWidth + skip + _text.maxWidth();
	const auto use = std::min(full, _outer.width());
	const auto ux = _outer.x() + (_outer.width() - use) / 2;
	const auto uheight = st::storiesWhoViewed.userpics.size;
	const auto uy = _outer.y() + (_outer.height() - uheight) / 2;
	const auto tx = ux + _userpicsWidth + skip;
	const auto theight = st::normalFont->height;
	const auto ty = _outer.y() + (_outer.height() - theight) / 2;
	const auto my = std::min(uy, ty);
	const auto mheight = std::max(uheight, theight);
	const auto padding = skip;
	_userpicsPosition = QPoint(padding, uy - my);
	_textPosition = QPoint(tx - ux + padding, ty - my);
	_widget->setGeometry(ux - padding, my, use + 2 * padding, mheight);
	_widget->update();
}

void RecentViews::updateText() {
	const auto text = _data.total
		? (tr::lng_stories_views(tr::now, lt_count, _data.total)
			+ (_data.reactions
				? (u"  "_q + QChar(10084) + QString::number(_data.reactions))
				: QString()))
		: tr::lng_stories_no_views(tr::now);
	_text.setText(st::defaultTextStyle, text);
	updatePartsGeometry();
}

void RecentViews::showMenu() {
	if (_menu || _data.list.empty()) {
		return;
	}

	const auto views = _controller->views(kAddPerPage * 2, true);
	if (views.list.empty() && !views.total) {
		return;
	}

	using namespace Ui;
	_menuShortLifetime.destroy();
	_menu = base::make_unique_q<PopupMenu>(
		_widget.get(),
		st::storiesViewsMenu);
	auto count = 0;
	const auto session = &_controller->story()->session();
	const auto added = std::min(int(views.list.size()), kAddPerPage);
	const auto add = std::min(views.total, kAddPerPage);
	const auto now = QDateTime::currentDateTime();
	for (const auto &entry  : views.list) {
		addMenuRow(entry, now);
		if (++count >= add) {
			break;
		}
	}
	while (count++ < add) {
		addMenuRowPlaceholder(session);
	}
	rpl::merge(
		_controller->moreViewsLoaded(),
		rpl::combine(
			_menu->scrollTopValue(),
			_menuEntriesCount.value()
		) | rpl::filter([=](int scrollTop, int count) {
			const auto fullHeight = count
				* (st::defaultWhoRead.photoSkip * 2
					+ st::defaultWhoRead.photoSize);
			return fullHeight
				< (scrollTop
					+ st::storiesViewsMenu.maxHeight * kLoadViewsPages);
		}) | rpl::to_empty
	) | rpl::start_with_next([=] {
		rebuildMenuTail();
	}, _menuShortLifetime);

	_controller->setMenuShown(true);
	_menu->setDestroyedCallback(crl::guard(_widget.get(), [=] {
		_controller->setMenuShown(false);
		_waitingForUserpicsLifetime.destroy();
		_waitingForUserpics.clear();
		_menuShortLifetime.destroy();
		_menuEntries.clear();
		_menuEntriesCount = 0;
		_menuPlaceholderCount = 0;
	}));

	const auto size = _menu->size();
	const auto geometry = _widget->mapToGlobal(_widget->rect());
	_menu->setForcedVerticalOrigin(PopupMenu::VerticalOrigin::Bottom);
	_menu->popup(QPoint(
		geometry.x() + (_widget->width() - size.width()) / 2,
		geometry.y() + _widget->height()));

	_menuEntriesCount = _menuEntriesCount.current() + added;
}

void RecentViews::addMenuRow(Data::StoryView entry, const QDateTime &now) {
	Expects(_menu != nullptr);

	const auto peer = entry.peer;
	const auto date = Api::FormatReadDate(entry.date, now);
	const auto show = _controller->uiShow();
	const auto prepare = [&](Ui::PeerUserpicView &view) {
		const auto size = st::storiesWhoViewed.photoSize;
		auto userpic = peer->generateUserpicImage(
			view,
			size * style::DevicePixelRatio());
		userpic.setDevicePixelRatio(style::DevicePixelRatio());
		return Ui::WhoReactedEntryData{
			.text = peer->name(),
			.date = date,
			.customEntityData = Data::ReactionEntityData(entry.reaction),
			.userpic = std::move(userpic),
			.callback = [=] { show->show(PrepareShortInfoBox(peer)); },
		};
	};
	if (_menuPlaceholderCount > 0) {
		const auto i = _menuEntries.end() - (_menuPlaceholderCount--);
		auto data = prepare(i->view);
		i->peer = peer;
		i->date = date;
		i->customEntityData = data.customEntityData;
		i->callback = data.callback;
		i->action->setData(std::move(data));
	} else {
		auto view = Ui::PeerUserpicView();
		auto data = prepare(view);
		auto callback = data.callback;
		auto customEntityData = data.customEntityData;
		auto action = base::make_unique_q<Ui::WhoReactedEntryAction>(
			_menu->menu(),
			Data::ReactedMenuFactory(&entry.peer->session()),
			_menu->menu()->st(),
			prepare(view));
		const auto raw = action.get();
		_menu->addAction(std::move(action));
		_menuEntries.push_back({
			.action = raw,
			.peer = peer,
			.date = date,
			.customEntityData = std::move(customEntityData),
			.callback = std::move(callback),
			.view = std::move(view),
		});
	}
	const auto i = end(_menuEntries) - _menuPlaceholderCount - 1;
	i->key = peer->userpicUniqueKey(i->view);
	if (peer->hasUserpic() && peer->useEmptyUserpic(i->view)) {
		if (_waitingForUserpics.emplace(i - begin(_menuEntries)).second
			&& _waitingForUserpics.size() == 1) {
			subscribeToMenuUserpicsLoading(&peer->session());
		}
	}
}

void RecentViews::addMenuRowPlaceholder(not_null<Main::Session*> session) {
	auto action = base::make_unique_q<Ui::WhoReactedEntryAction>(
		_menu->menu(),
		Data::ReactedMenuFactory(session),
		_menu->menu()->st(),
		Ui::WhoReactedEntryData{ .preloader = true });
	const auto raw = action.get();
	_menu->addAction(std::move(action));
	_menuEntries.push_back({ .action = raw });
	++_menuPlaceholderCount;
}

void RecentViews::rebuildMenuTail() {
	const auto elements = _menuEntries.size() - _menuPlaceholderCount;
	const auto views = _controller->views(elements + kAddPerPage, false);
	if (views.list.size() <= elements) {
		return;
	}
	const auto now = QDateTime::currentDateTime();
	const auto added = std::min(
		_menuPlaceholderCount + kAddPerPage,
		int(views.list.size() - elements));
	for (auto i = elements, till = i + added; i != till; ++i) {
		const auto &entry = views.list[i];
		addMenuRow(entry, now);
	}
	_menuEntriesCount = _menuEntriesCount.current() + added;
}

void RecentViews::subscribeToMenuUserpicsLoading(
		not_null<Main::Session*> session) {
	_shortAnimationPlaying = style::ShortAnimationPlaying();
	_waitingForUserpicsLifetime = rpl::merge(
		_shortAnimationPlaying.changes() | rpl::filter([=](bool playing) {
			return !playing && _waitingUserpicsCheck;
		}) | rpl::to_empty,
		session->downloaderTaskFinished(
		) | rpl::filter([=] {
			if (_shortAnimationPlaying.current()) {
				_waitingUserpicsCheck = true;
				return false;
			}
			return true;
		})
	) | rpl::start_with_next([=] {
		_waitingUserpicsCheck = false;
		for (auto i = begin(_waitingForUserpics)
			; i != end(_waitingForUserpics)
			;) {
			auto &entry = _menuEntries[*i];
			auto &view = entry.view;
			const auto peer = entry.peer;
			const auto key = peer->userpicUniqueKey(view);
			const auto update = (entry.key != key);
			if (update) {
				const auto size = st::storiesWhoViewed.photoSize;
				auto userpic = peer->generateUserpicImage(
					view,
					size * style::DevicePixelRatio());
				userpic.setDevicePixelRatio(style::DevicePixelRatio());
				entry.action->setData({
					.text = peer->name(),
					.date = entry.date,
					.customEntityData = entry.customEntityData,
					.userpic = std::move(userpic),
					.callback = entry.callback,
				});
				entry.key = key;
				if (!peer->hasUserpic() || !peer->useEmptyUserpic(view)) {
					i = _waitingForUserpics.erase(i);
					continue;
				}
			}
			++i;
		}
		if (_waitingForUserpics.empty()) {
			_waitingForUserpicsLifetime.destroy();
		}
	});
}

} // namespace Media::Stories
