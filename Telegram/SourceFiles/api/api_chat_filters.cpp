/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_chat_filters.h"

#include "apiwrap.h"
#include "boxes/peer_list_box.h"
#include "boxes/premium_limits_box.h"
#include "boxes/filters/edit_filter_links.h" // FilterChatStatusText
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/filter_link_header.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/filter_icons.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_filter_icons.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Api {
namespace {

enum class ToggleAction {
	Adding,
	Removing,
};

class ToggleChatsController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	ToggleChatsController(
		not_null<Window::SessionController*> window,
		ToggleAction action,
		const QString &title,
		std::vector<not_null<PeerData*>> chats,
		std::vector<not_null<PeerData*>> additional);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	[[nodiscard]] auto selectedValue() const
		-> rpl::producer<base::flat_set<not_null<PeerData*>>>;

	void adjust(int minHeight, int maxHeight, int addedTopHeight);
	void setRealContentHeight(rpl::producer<int> value);
	rpl::producer<int> boxHeightValue() const override;

private:
	void setupAboveWidget();
	void setupBelowWidget();
	void initDesiredHeightValue();
	void toggleAllSelected(bool select);

	const not_null<Window::SessionController*> _window;
	Ui::RpWidget *_addedTopWidget = nullptr;
	Ui::RpWidget *_addedBottomWidget = nullptr;

	ToggleAction _action = ToggleAction::Adding;
	QString _filterTitle;
	base::flat_set<not_null<PeerData*>> _checkable;
	std::vector<not_null<PeerData*>> _chats;
	std::vector<not_null<PeerData*>> _additional;
	rpl::variable<base::flat_set<not_null<PeerData*>>> _selected;

	int _minTopHeight = 0;
	rpl::variable<int> _maxTopHeight;
	rpl::variable<int> _aboveHeight;
	rpl::variable<int> _belowHeight;
	rpl::variable<int> _desiredHeight;

	base::unique_qptr<Ui::PopupMenu> _menu;

	rpl::lifetime _lifetime;

};

[[nodiscard]] tr::phrase<> TitleText(Ui::FilterLinkHeaderType type) {
	using Type = Ui::FilterLinkHeaderType;
	switch (type) {
	case Type::AddingFilter: return tr::lng_filters_by_link_title;
	case Type::AddingChats: return tr::lng_filters_by_link_more;
	case Type::AllAdded: return tr::lng_filters_by_link_already;
	case Type::Removing: return tr::lng_filters_by_link_remove;
	}
	Unexpected("Ui::FilterLinkHeaderType in TitleText.");
}

[[nodiscard]] TextWithEntities AboutText(
		Ui::FilterLinkHeaderType type,
		const QString &title) {
	using Type = Ui::FilterLinkHeaderType;
	auto boldTitle = Ui::Text::Bold(title);
	return (type == Type::AddingFilter)
		? tr::lng_filters_by_link_sure(
			tr::now,
			lt_folder,
			std::move(boldTitle),
			Ui::Text::WithEntities)
		: (type == Type::AddingChats)
		? tr::lng_filters_by_link_more_sure(
			tr::now,
			lt_folder,
			std::move(boldTitle),
			Ui::Text::WithEntities)
		: (type == Type::AllAdded)
		? tr::lng_filters_by_link_already_about(
			tr::now,
			lt_folder,
			std::move(boldTitle),
			Ui::Text::WithEntities)
		: tr::lng_filters_by_link_remove_sure(
			tr::now,
			lt_folder,
			std::move(boldTitle),
			Ui::Text::WithEntities);
}

void InitFilterLinkHeader(
		not_null<PeerListBox*> box,
		Fn<void(int minHeight, int maxHeight, int addedTopHeight)> adjust,
		Ui::FilterLinkHeaderType type,
		const QString &title,
		const QString &iconEmoji,
		rpl::producer<int> count) {
	const auto icon = Ui::LookupFilterIcon(
		Ui::LookupFilterIconByEmoji(
			iconEmoji
		).value_or(Ui::FilterIcon::Custom)).active;
	auto header = Ui::MakeFilterLinkHeader(box, {
		.type = type,
		.title = TitleText(type)(tr::now),
		.about = AboutText(type, title),
		.folderTitle = title,
		.folderIcon = icon,
		.badge = (type == Ui::FilterLinkHeaderType::AddingChats
			? std::move(count)
			: rpl::single(0)),
	});
	const auto widget = header.widget;
	widget->resizeToWidth(st::boxWideWidth);
	Ui::SendPendingMoveResizeEvents(widget);

	const auto min = widget->minimumHeight(), max = widget->maximumHeight();
	widget->resize(st::boxWideWidth, max);

	box->setAddedTopScrollSkip(max);
	std::move(
		header.wheelEvents
	) | rpl::start_with_next([=](not_null<QWheelEvent*> e) {
		box->sendScrollViewportEvent(e);
	}, widget->lifetime());

	std::move(
		header.closeRequests
	) | rpl::start_with_next([=] {
		box->closeBox();
	}, widget->lifetime());

	struct State {
		bool processing = false;
		int addedTopHeight = 0;
	};
	const auto state = widget->lifetime().make_state<State>();

	box->scrolls(
	) | rpl::filter([=] {
		return !state->processing;
	}) | rpl::start_with_next([=] {
		state->processing = true;
		const auto guard = gsl::finally([&] { state->processing = false; });

		const auto top = box->scrollTop();
		const auto headerHeight = std::max(max - top, min);
		const auto addedTopHeight = max - headerHeight;
		widget->resize(widget->width(), headerHeight);
		if (state->addedTopHeight < addedTopHeight) {
			adjust(min, max, addedTopHeight);
			box->setAddedTopScrollSkip(headerHeight);
		} else {
			box->setAddedTopScrollSkip(headerHeight);
			adjust(min, max, addedTopHeight);
		}
		state->addedTopHeight = addedTopHeight;
		box->peerListRefreshRows();
	}, widget->lifetime());

	box->setNoContentMargin(true);
	adjust(min, max, 0);
}

void ImportInvite(
		const QString &slug,
		FilterId filterId,
		const base::flat_set<not_null<PeerData*>> &peers,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	Expects(!peers.empty());

	const auto peer = peers.front();
	const auto api = &peer->session().api();
	const auto callback = [=](const MTPUpdates &result) {
		api->applyUpdates(result);
		if (slug.isEmpty()) {
			peer->owner().chatsFilters().moreChatsHide(filterId, true);
		}
		done();
	};
	const auto error = [=](const MTP::Error &error) {
		fail(error.type());
	};
	auto inputs = peers | ranges::views::transform([](auto peer) {
		return MTPInputPeer(peer->input);
	}) | ranges::to<QVector<MTPInputPeer>>();
	if (!slug.isEmpty()) {
		api->request(MTPchatlists_JoinChatlistInvite(
			MTP_string(slug),
			MTP_vector<MTPInputPeer>(std::move(inputs))
		)).done(callback).fail(error).send();
	} else {
		api->request(MTPchatlists_JoinChatlistUpdates(
			MTP_inputChatlistDialogFilter(MTP_int(filterId)),
			MTP_vector<MTPInputPeer>(std::move(inputs))
		)).done(callback).fail(error).send();
	}
}

ToggleChatsController::ToggleChatsController(
	not_null<Window::SessionController*> window,
	ToggleAction action,
	const QString &title,
	std::vector<not_null<PeerData*>> chats,
	std::vector<not_null<PeerData*>> additional)
: _window(window)
, _action(action)
, _filterTitle(title)
, _chats(std::move(chats))
, _additional(std::move(additional)) {
	setStyleOverrides(&st::filterLinkChatsList);
}

void ToggleChatsController::prepare() {
	auto selected = base::flat_set<not_null<PeerData*>>();
	const auto disabled = [](not_null<PeerData*> peer) {
		return peer->isChat()
			? peer->asChat()->isForbidden()
			: peer->isChannel()
			? peer->asChannel()->isForbidden()
			: false;
	};
	const auto add = [&](not_null<PeerData*> peer, bool additional = false) {
		const auto disable = disabled(peer);
		auto row = (additional || !disable)
			? std::make_unique<PeerListRow>(peer)
			: MakeFilterChatRow(
				peer,
				tr::lng_filters_link_inaccessible(tr::now),
				true);
		if (delegate()->peerListFindRow(peer->id.value)) {
			return;
		}
		const auto raw = row.get();
		delegate()->peerListAppendRow(std::move(row));
		if (!disable
			&& (!additional || _action == ToggleAction::Removing)) {
			_checkable.emplace(peer);
			if (const auto status = FilterChatStatusText(peer)
				; !status.isEmpty()) {
				raw->setCustomStatus(status);
			}
		}
		if (disable) {
		} else if (!additional) {
			delegate()->peerListSetRowChecked(raw, true);
			raw->finishCheckedAnimation();
			selected.emplace(peer);
		} else if (_action == ToggleAction::Adding) {
			raw->setDisabledState(PeerListRow::State::DisabledChecked);
			raw->setCustomStatus(peer->isBroadcast()
				? tr::lng_filters_link_already_channel(tr::now)
				: tr::lng_filters_link_already_group(tr::now));
		}
	};
	for (const auto &peer : _chats) {
		if (!disabled(peer)) {
			add(peer);
		}
	}
	for (const auto &peer : _additional) {
		add(peer, true);
	}
	for (const auto &peer : _chats) {
		if (disabled(peer)) {
			add(peer);
		}
	}
	setupAboveWidget();
	setupBelowWidget();
	initDesiredHeightValue();
	delegate()->peerListRefreshRows();
	_selected = std::move(selected);
}

void ToggleChatsController::rowClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	if (!_checkable.contains(peer)) {
		return;
	}
	const auto checked = row->checked();
	auto selected = _selected.current();
	delegate()->peerListSetRowChecked(row, !checked);
	if (checked) {
		selected.remove(peer);
	} else {
		selected.emplace(peer);
	}
	_selected = std::move(selected);
}

void ToggleChatsController::setupAboveWidget() {
	using namespace Settings;

	auto wrap = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = wrap.data();

	_addedTopWidget = container->add(object_ptr<Ui::RpWidget>(container));
	const auto realAbove = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	Ui::AddDivider(realAbove);
	const auto totalCount = [&] {
		if (_chats.empty()) {
			return _additional.size();
		} else if (_additional.empty()) {
			return _chats.size();
		}
		auto result = _chats.size();
		for (const auto &peer : _additional) {
			if (!ranges::contains(_chats, peer)) {
				++result;
			}
		}
		return result;
	};
	const auto count = (_action == ToggleAction::Removing)
		? totalCount()
		: _chats.empty()
		? _additional.size()
		: _chats.size();
	const auto selectableCount = int(_checkable.size());
	auto selectedCount = _selected.value(
	) | rpl::map([](const base::flat_set<not_null<PeerData*>> &selected) {
		return int(selected.size());
	});
	AddFilterSubtitleWithToggles(
		realAbove,
		(_action == ToggleAction::Removing
			? tr::lng_filters_by_link_quit
			: _chats.empty()
			? tr::lng_filters_by_link_in
			: tr::lng_filters_by_link_join)(
				lt_count,
				rpl::single(float64(count))),
		selectableCount,
		std::move(selectedCount),
		[=](bool select) { toggleAllSelected(select); });

	_aboveHeight = realAbove->heightValue();
	delegate()->peerListSetAboveWidget(std::move(wrap));
}

void ToggleChatsController::toggleAllSelected(bool select) {
	auto selected = _selected.current();
	if (!select) {
		if (selected.empty()) {
			return;
		}
		for (const auto &peer : selected) {
			const auto row = delegate()->peerListFindRow(peer->id.value);
			Assert(row != nullptr);
			delegate()->peerListSetRowChecked(row, false);
		}
		selected = {};
	} else {
		const auto count = delegate()->peerListFullRowsCount();
		for (auto i = 0; i != count; ++i) {
			const auto row = delegate()->peerListRowAt(i);
			const auto peer = row->peer();
			if (_action != ToggleAction::Adding ||
				!ranges::contains(_additional, peer)) {
				delegate()->peerListSetRowChecked(row, true);
				selected.emplace(peer);
			}
		}
	}
	_selected = std::move(selected);
}

void ToggleChatsController::setupBelowWidget() {
	if (_chats.empty()) {
		auto widget = object_ptr<Ui::RpWidget>((QWidget*)nullptr);
		_addedBottomWidget = widget.data();
		delegate()->peerListSetBelowWidget(std::move(widget));
		return;
	}
	auto layout = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto raw = layout.data();
	auto widget = object_ptr<Ui::DividerLabel>(
		(QWidget*)nullptr,
		std::move(layout),
		st::defaultBoxDividerLabelPadding);
	raw->add(object_ptr<Ui::FlatLabel>(
		raw,
		(_action == ToggleAction::Removing
			? tr::lng_filters_by_link_about_quit
			: tr::lng_filters_by_link_about)(tr::now),
		st::boxDividerLabel));
	_addedBottomWidget = raw->add(object_ptr<Ui::RpWidget>(raw));
	_belowHeight = widget->heightValue() | rpl::map([=](int value) {
		return value - _addedBottomWidget->height();
	});
	delegate()->peerListSetBelowWidget(std::move(widget));
}

Main::Session &ToggleChatsController::session() const {
	return _window->session();
}

auto ToggleChatsController::selectedValue() const
-> rpl::producer<base::flat_set<not_null<PeerData*>>> {
	return _selected.value();
}

void ToggleChatsController::adjust(
		int minHeight,
		int maxHeight,
		int addedTopHeight) {
	Expects(addedTopHeight >= 0);

	_addedTopWidget->resize(_addedTopWidget->width(), addedTopHeight);
	_minTopHeight = minHeight;
	_maxTopHeight = maxHeight;
}

void ToggleChatsController::setRealContentHeight(rpl::producer<int> value) {
	std::move(
		value
	) | rpl::start_with_next([=](int height) {
		const auto desired = _desiredHeight.current();
		if (height <= computeListSt().item.height) {
			return;
		} else if (height >= desired) {
			_addedBottomWidget->resize(_addedBottomWidget->width(), 0);
		} else {
			const auto available = desired - height;
			const auto required = _maxTopHeight.current() - _minTopHeight;
			const auto added = required - available;
			_addedBottomWidget->resize(
				_addedBottomWidget->width(),
				std::max(added, 0));
		}
	}, _lifetime);
}

void ToggleChatsController::initDesiredHeightValue() {
	using namespace rpl::mappers;

	const auto &st = computeListSt();
	const auto count = int(delegate()->peerListFullRowsCount());
	const auto middle = st.padding.top()
		+ (count * st.item.height)
		+ st.padding.bottom();
	_desiredHeight = rpl::combine(
		_maxTopHeight.value(),
		_aboveHeight.value(),
		_belowHeight.value(),
		_1 + _2 + middle + _3);
}

rpl::producer<int> ToggleChatsController::boxHeightValue() const {
	return _desiredHeight.value() | rpl::map([=](int value) {
		return std::min(value, st::boxMaxListHeight);
	});
}

void ShowImportError(
		not_null<Window::SessionController*> window,
		FilterId id,
		int added,
		const QString &error) {
	const auto session = &window->session();
	const auto &list = session->data().chatsFilters().list();
	const auto i = ranges::find(list, id, &Data::ChatFilter::id);
	const auto count = added
		+ ((i != end(list)) ? int(i->always().size()) : 0);
	if (error == u"CHANNELS_TOO_MUCH"_q) {
		window->show(Box(ChannelsLimitBox, session));
	} else if (error == u"FILTER_INCLUDE_TOO_MUCH"_q) {
		window->show(Box(FilterChatsLimitBox, session, count, true));
	} else if (error == u"CHATLISTS_TOO_MUCH"_q) {
		window->show(Box(ShareableFiltersLimitBox, session));
	} else {
		window->showToast((error == u"INVITE_SLUG_EXPIRED"_q)
			? tr::lng_group_invite_bad_link(tr::now)
			: error);
	}
}

void ShowImportToast(
		base::weak_ptr<Window::SessionController> weak,
		const QString &title,
		Ui::FilterLinkHeaderType type,
		int added) {
	const auto strong = weak.get();
	if (!strong) {
		return;
	}
	const auto created = (type == Ui::FilterLinkHeaderType::AddingFilter);
	const auto phrase = created
		? tr::lng_filters_added_title
		: tr::lng_filters_updated_title;
	auto text = Ui::Text::Bold(phrase(tr::now, lt_folder, title));
	if (added > 0) {
		const auto phrase = created
			? tr::lng_filters_added_also
			: tr::lng_filters_updated_also;
		text.append('\n').append(phrase(tr::now, lt_count, added));
	}
	strong->showToast(std::move(text));
}

void ProcessFilterInvite(
		base::weak_ptr<Window::SessionController> weak,
		const QString &slug,
		FilterId filterId,
		const QString &title,
		const QString &iconEmoji,
		std::vector<not_null<PeerData*>> peers,
		std::vector<not_null<PeerData*>> already) {
	const auto strong = weak.get();
	if (!strong) {
		return;
	}
	Core::App().hideMediaView();
	if (peers.empty() && !filterId) {
		strong->showToast(tr::lng_group_invite_bad_link(tr::now));
		return;
	}
	const auto fullyAdded = (peers.empty() && filterId);
	auto controller = std::make_unique<ToggleChatsController>(
		strong,
		ToggleAction::Adding,
		title,
		std::move(peers),
		std::move(already));
	const auto raw = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->setStyle(st::filterInviteBox);

		using Type = Ui::FilterLinkHeaderType;
		const auto type = fullyAdded
			? Type::AllAdded
			: !filterId
			? Type::AddingFilter
			: Type::AddingChats;
		auto badge = raw->selectedValue(
		) | rpl::map([=](const base::flat_set<not_null<PeerData*>> &peers) {
			return int(peers.size());
		});
		InitFilterLinkHeader(box, [=](int min, int max, int addedTop) {
			raw->adjust(min, max, addedTop);
		}, type, title, iconEmoji, rpl::duplicate(badge));

		raw->setRealContentHeight(box->heightValue());

		auto owned = Ui::FilterLinkProcessButton(
			box,
			type,
			title,
			std::move(badge));

		const auto button = owned.data();
		box->widthValue(
		) | rpl::start_with_next([=](int width) {
			const auto &padding = st::filterInviteBox.buttonPadding;
			button->resizeToWidth(width
				- padding.left()
				- padding.right());
			button->moveToLeft(padding.left(), padding.top());
		}, button->lifetime());

		box->addButton(std::move(owned));

		struct State {
			bool importing = false;
		};
		const auto state = box->lifetime().make_state<State>();

		raw->selectedValue(
		) | rpl::start_with_next([=](
				base::flat_set<not_null<PeerData*>> &&peers) {
			button->setClickedCallback([=] {
				if (peers.empty()) {
					box->closeBox();
				} else if (!state->importing) {
					state->importing = true;
					const auto added = int(peers.size());
					ImportInvite(slug, filterId, peers, crl::guard(box, [=] {
						ShowImportToast(weak, title, type, peers.size());
						box->closeBox();
					}), crl::guard(box, [=](QString text) {
						if (const auto strong = weak.get()) {
							ShowImportError(strong, filterId, added, text);
						}
						state->importing = false;
					}));
				}
			});
		}, box->lifetime());
	};
	strong->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
}

void ProcessFilterInvite(
		base::weak_ptr<Window::SessionController> weak,
		const QString &slug,
		FilterId filterId,
		std::vector<not_null<PeerData*>> peers,
		std::vector<not_null<PeerData*>> already) {
	const auto strong = weak.get();
	if (!strong) {
		return;
	}
	Core::App().hideMediaView();
	const auto &list = strong->session().data().chatsFilters().list();
	const auto it = ranges::find(list, filterId, &Data::ChatFilter::id);
	if (it == end(list)) {
		strong->showToast(u"Filter not found :shrug:"_q);
		return;
	}
	ProcessFilterInvite(
		weak,
		slug,
		filterId,
		it->title(),
		it->iconEmoji(),
		std::move(peers),
		std::move(already));
}

} // namespace

void SaveNewFilterPinned(
		not_null<Main::Session*> session,
		FilterId filterId) {
	const auto &order = session->data().pinnedChatsOrder(filterId);
	auto &filters = session->data().chatsFilters();
	const auto &filter = filters.applyUpdatedPinned(filterId, order);
	session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
		MTP_int(filterId),
		filter.tl()
	)).send();
}

void CheckFilterInvite(
		not_null<Window::SessionController*> controller,
		const QString &slug) {
	const auto session = &controller->session();
	const auto weak = base::make_weak(controller);
	session->api().checkFilterInvite(slug, [=](
			const MTPchatlists_ChatlistInvite &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		auto title = QString();
		auto iconEmoji = QString();
		auto filterId = FilterId();
		auto peers = std::vector<not_null<PeerData*>>();
		auto already = std::vector<not_null<PeerData*>>();
		auto &owner = strong->session().data();
		result.match([&](const auto &data) {
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
		});
		const auto parseList = [&](const MTPVector<MTPPeer> &list) {
			auto result = std::vector<not_null<PeerData*>>();
			result.reserve(list.v.size());
			for (const auto &peer : list.v) {
				result.push_back(owner.peer(peerFromMTP(peer)));
			}
			return result;
		};
		result.match([&](const MTPDchatlists_chatlistInvite &data) {
			title = qs(data.vtitle());
			iconEmoji = data.vemoticon().value_or_empty();
			peers = parseList(data.vpeers());
		}, [&](const MTPDchatlists_chatlistInviteAlready &data) {
			filterId = data.vfilter_id().v;
			peers = parseList(data.vmissing_peers());
			already = parseList(data.valready_peers());
		});

		const auto notLoaded = filterId
			&& !ranges::contains(
				owner.chatsFilters().list(),
				filterId,
				&Data::ChatFilter::id);
		if (notLoaded) {
			const auto lifetime = std::make_shared<rpl::lifetime>();
			owner.chatsFilters().changed(
			) | rpl::start_with_next([=] {
				lifetime->destroy();
				ProcessFilterInvite(
					weak,
					slug,
					filterId,
					std::move(peers),
					std::move(already));
			}, *lifetime);
			owner.chatsFilters().reload();
		} else if (filterId) {
			ProcessFilterInvite(
				weak,
				slug,
				filterId,
				std::move(peers),
				std::move(already));
		} else {
			ProcessFilterInvite(
				weak,
				slug,
				filterId,
				title,
				iconEmoji,
				std::move(peers),
				std::move(already));
		}
	}, [=](const MTP::Error &error) {
		if (error.code() != 400) {
			return;
		}
		ProcessFilterInvite(weak, slug, {}, {}, {}, {}, {});
	});
}

void ProcessFilterUpdate(
		base::weak_ptr<Window::SessionController> weak,
		FilterId filterId,
		std::vector<not_null<PeerData*>> missing) {
	if (const auto strong = missing.empty() ? weak.get() : nullptr) {
		strong->session().data().chatsFilters().moreChatsHide(filterId);
		return;
	}
	ProcessFilterInvite(weak, QString(), filterId, std::move(missing), {});
}

void ProcessFilterRemove(
		base::weak_ptr<Window::SessionController> weak,
		const QString &title,
		const QString &iconEmoji,
		std::vector<not_null<PeerData*>> all,
		std::vector<not_null<PeerData*>> suggest,
		Fn<void(std::vector<not_null<PeerData*>>)> done) {
	const auto strong = weak.get();
	if (!strong) {
		return;
	}
	Core::App().hideMediaView();
	if (all.empty() && suggest.empty()) {
		done({});
		return;
	}
	auto controller = std::make_unique<ToggleChatsController>(
		strong,
		ToggleAction::Removing,
		title,
		std::move(suggest),
		std::move(all));
	const auto raw = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->setStyle(st::filterInviteBox);

		const auto type = Ui::FilterLinkHeaderType::Removing;
		auto badge = raw->selectedValue(
		) | rpl::map([=](const base::flat_set<not_null<PeerData*>> &peers) {
			return int(peers.size());
		});
		InitFilterLinkHeader(box, [=](int min, int max, int addedTop) {
			raw->adjust(min, max, addedTop);
		}, type, title, iconEmoji, rpl::single(0));

		auto owned = Ui::FilterLinkProcessButton(
			box,
			type,
			title,
			std::move(badge));

		const auto button = owned.data();
		box->widthValue(
		) | rpl::start_with_next([=](int width) {
			const auto &padding = st::filterInviteBox.buttonPadding;
			button->resizeToWidth(width
				- padding.left()
				- padding.right());
			button->moveToLeft(padding.left(), padding.top());
		}, button->lifetime());

		box->addButton(std::move(owned));

		raw->selectedValue(
		) | rpl::start_with_next([=](
				base::flat_set<not_null<PeerData*>> &&peers) {
			button->setClickedCallback([=] {
				done(peers | ranges::to_vector);
				box->closeBox();
			});
		}, box->lifetime());
	};
	strong->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
}

[[nodiscard]] std::vector<not_null<PeerData*>> ExtractSuggestRemoving(
		const Data::ChatFilter &filter) {
	if (!filter.chatlist()) {
		return {};
	}
	return filter.always() | ranges::views::filter([](
		not_null<History*> history) {
		return history->peer->isChannel();
	}) | ranges::views::transform(&History::peer) | ranges::to_vector;
}

} // namespace Api
