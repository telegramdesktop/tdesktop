/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_folders.h"

#include "apiwrap.h"
#include "api/api_chat_filters.h" // ProcessFilterRemove.
#include "boxes/premium_limits_box.h"
#include "boxes/filters/edit_filter_box.h"
#include "core/application.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "data/data_session.h"
#include "data/data_premium_limits.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/filter_icons.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

namespace Settings {
namespace {

using Flag = Data::ChatFilter::Flag;
using Flags = Data::ChatFilter::Flags;

class FilterRowButton final : public Ui::RippleButton {
public:
	FilterRowButton(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		const Data::ChatFilter &filter);
	FilterRowButton(
		not_null<QWidget*> parent,
		const Data::ChatFilter &filter,
		const QString &description);

	void setRemoved(bool removed);
	void updateData(const Data::ChatFilter &filter);
	void updateCount(const Data::ChatFilter &filter);

	[[nodiscard]] rpl::producer<> removeRequests() const;
	[[nodiscard]] rpl::producer<> restoreRequests() const;
	[[nodiscard]] rpl::producer<> addRequests() const;

private:
	enum class State {
		Suggested,
		Removed,
		Normal,
	};

	FilterRowButton(
		not_null<QWidget*> parent,
		Main::Session *session,
		const Data::ChatFilter &filter,
		const QString &description,
		State state);

	void paintEvent(QPaintEvent *e) override;

	void setup(const Data::ChatFilter &filter, const QString &status);
	void setState(State state, bool force = false);
	void updateButtonsVisibility();

	Main::Session *_session = nullptr;

	Ui::IconButton _remove;
	Ui::RoundButton _restore;
	Ui::RoundButton _add;

	Ui::Text::String _title;
	QString _status;
	Ui::FilterIcon _icon = Ui::FilterIcon();

	State _state = State::Normal;

};

struct FilterRow {
	not_null<FilterRowButton*> button;
	Data::ChatFilter filter;
	bool removed = false;
	mtpRequestId removePeersRequestId = 0;
	std::vector<not_null<PeerData*>> suggestRemovePeers;
	std::vector<not_null<PeerData*>> removePeers;
	bool added = false;
	bool postponedCountUpdate = false;
};

[[nodiscard]] int CountFilterChats(
		not_null<Main::Session*> session,
		const Data::ChatFilter &filter) {
	auto result = 0;
	const auto addList = [&](not_null<Dialogs::MainList*> list) {
		for (const auto &entry : list->indexed()->all()) {
			if (const auto history = entry->history()) {
				if (filter.contains(history)) {
					++result;
				}
			}
		}
	};
	addList(session->data().chatsList());
	const auto folderId = Data::Folder::kId;
	if (const auto folder = session->data().folderLoaded(folderId)) {
		addList(folder->chatsList());
	}
	return result;
}

[[nodiscard]] int ComputeCount(
		not_null<Main::Session*> session,
		const Data::ChatFilter &filter,
		bool check = false) {
	const auto &list = session->data().chatsFilters().list();
	const auto id = filter.id();
	const auto i = ranges::find(list, id, &Data::ChatFilter::id);
	if ((id && i != end(list))
		&& (!check
			|| (i->flags() == filter.flags()
				&& i->always() == filter.always()
				&& i->never() == filter.never()))) {
		const auto chats = session->data().chatsFilters().chatsList(id);
		return chats->indexed()->size();
	}
	return CountFilterChats(session, filter);
}

[[nodiscard]] QString ComputeCountString(
		not_null<Main::Session*> session,
		const Data::ChatFilter &filter,
		bool check = false) {
	const auto count = ComputeCount(session, filter, check);
	const auto result = count
		? tr::lng_filters_chats_count(tr::now, lt_count_short, count)
		: tr::lng_filters_no_chats(tr::now);
	return filter.chatlist()
		? (result
			+ QString::fromUtf8(" \xE2\x80\xA2 ")
			+ tr::lng_filters_shareable_status(tr::now))
		: result;
}

FilterRowButton::FilterRowButton(
	not_null<QWidget*> parent,
	not_null<Main::Session*> session,
	const Data::ChatFilter &filter)
: FilterRowButton(
	parent,
	session,
	filter,
	ComputeCountString(session, filter),
	State::Normal) {
}

FilterRowButton::FilterRowButton(
	not_null<QWidget*> parent,
	const Data::ChatFilter &filter,
	const QString &description)
: FilterRowButton(parent, nullptr, filter, description, State::Suggested) {
}

FilterRowButton::FilterRowButton(
	not_null<QWidget*> parent,
	Main::Session *session,
	const Data::ChatFilter &filter,
	const QString &status,
	State state)
: RippleButton(parent, st::defaultRippleAnimation)
, _session(session)
, _remove(this, st::filtersRemove)
, _restore(this, tr::lng_filters_restore(), st::stickersUndoRemove)
, _add(this, tr::lng_filters_recommended_add(), st::stickersTrendingAdd)
, _state(state) {
	_restore.setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	_add.setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	setup(filter, status);
}

void FilterRowButton::setRemoved(bool removed) {
	setState(removed ? State::Removed : State::Normal);
}

void FilterRowButton::updateData(const Data::ChatFilter &filter) {
	Expects(_session != nullptr);

	_title.setText(st::contactsNameStyle, filter.title());
	_icon = Ui::ComputeFilterIcon(filter);
	updateCount(filter);
}

void FilterRowButton::updateCount(const Data::ChatFilter &filter) {
	_status = ComputeCountString(_session, filter, true);
	update();
}

void FilterRowButton::setState(State state, bool force) {
	if (!force && _state == state) {
		return;
	}
	_state = state;
	setPointerCursor(_state == State::Normal);
	setDisabled(_state != State::Normal);
	updateButtonsVisibility();
	update();
}

void FilterRowButton::setup(
		const Data::ChatFilter &filter,
		const QString &status) {
	resize(width(), st::defaultPeerListItem.height);

	_title.setText(st::contactsNameStyle, filter.title());
	_status = status;
	_icon = Ui::ComputeFilterIcon(filter);

	setState(_state, true);

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto right = st::contactsPadding.right()
			+ st::contactsCheckPosition.x();
		const auto width = size.width();
		const auto height = size.height();
		_restore.moveToRight(right, (height - _restore.height()) / 2, width);
		_add.moveToRight(right, (height - _add.height()) / 2, width);
		const auto skipped = right - st::stickersRemoveSkip;
		_remove.moveToRight(skipped, (height - _remove.height()) / 2, width);
	}, lifetime());
}

void FilterRowButton::updateButtonsVisibility() {
	_remove.setVisible(_state == State::Normal);
	_restore.setVisible(_state == State::Removed);
	_add.setVisible(_state == State::Suggested);
}

rpl::producer<> FilterRowButton::removeRequests() const {
	return _remove.clicks() | rpl::to_empty;
}

rpl::producer<> FilterRowButton::restoreRequests() const {
	return _restore.clicks() | rpl::to_empty;
}

rpl::producer<> FilterRowButton::addRequests() const {
	return _add.clicks() | rpl::to_empty;
}

void FilterRowButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto over = isOver() || isDown();
	if (_state == State::Normal) {
		if (over) {
			p.fillRect(e->rect(), st::windowBgOver);
		}
		RippleButton::paintRipple(p, 0, 0);
	} else if (_state == State::Removed) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}

	const auto left = (_state == State::Suggested)
		? st::defaultSubsectionTitlePadding.left()
		: st::settingsButtonActive.padding.left();
	const auto buttonsLeft = std::min(
		_add.x(),
		std::min(_remove.x(), _restore.x()));
	const auto availableWidth = buttonsLeft - left;

	p.setPen(st::contactsNameFg);
	_title.drawLeftElided(
		p,
		left,
		st::contactsPadding.top() + st::contactsNameTop,
		availableWidth,
		width());

	p.setFont(st::contactsStatusFont);
	p.setPen(st::contactsStatusFg);
	p.drawTextLeft(
		left,
		st::contactsPadding.top() + st::contactsStatusTop,
		width(),
		_status);

	if (_state != State::Suggested) {
		const auto icon = Ui::LookupFilterIcon(_icon).normal;

		// For now.
		auto hq = PainterHighQualityEnabler(p);
		const auto iconWidth = icon->width() - style::ConvertScale(9);
		const auto scale = st::settingsIconAdd.width() / float64(iconWidth);
		p.translate(
			st::settingsButtonActive.iconLeft,
			(height() - icon->height() * scale) / 2);
		p.translate(-iconWidth / 2, -iconWidth / 2);
		p.scale(scale, scale);
		p.translate(iconWidth / 2, iconWidth / 2);
		icon->paint(
			p,
			0,
			0,
			width(),
			(over
				? st::activeButtonBgOver
				: st::activeButtonBg)->c);
	}
}

[[nodiscard]] Fn<void()> SetupFoldersContent(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	auto &lifetime = container->lifetime();

	const auto weak = Ui::MakeWeak(container);
	const auto session = &controller->session();
	const auto limit = [=] {
		return Data::PremiumLimits(session).dialogFiltersCurrent();
	};
	Ui::AddSkip(container, st::defaultVerticalListSkip);
	Ui::AddSubsectionTitle(container, tr::lng_filters_subtitle());

	struct State {
		std::vector<FilterRow> rows;
		rpl::variable<int> count;
		rpl::variable<int> suggested;
		Fn<void(const FilterRowButton*, Fn<void(Data::ChatFilter)>)> save;
	};

	const auto state = lifetime.make_state<State>();
	const auto find = [=](not_null<FilterRowButton*> button) {
		const auto i = ranges::find(state->rows, button, &FilterRow::button);
		Assert(i != end(state->rows));
		return &*i;
	};
	const auto showLimitReached = [=] {
		const auto removed = ranges::count_if(
			state->rows,
			&FilterRow::removed);
		const auto count = int(state->rows.size() - removed);
		if (count < limit()) {
			return false;
		}
		controller->show(Box(FiltersLimitBox, session, count));
		return true;
	};
	const auto markForRemovalSure = [=](not_null<FilterRowButton*> button) {
		const auto row = find(button);
		auto suggestRemoving = Api::ExtractSuggestRemoving(row->filter);
		if (row->removed || row->removePeersRequestId > 0) {
			return;
		} else if (!suggestRemoving.empty()) {
			const auto chosen = crl::guard(button, [=](
					std::vector<not_null<PeerData*>> peers) {
				const auto row = find(button);
				row->removePeers = std::move(peers);
				row->removed = true;
				button->setRemoved(true);
			});
			Api::ProcessFilterRemove(
				controller,
				row->filter.title(),
				row->filter.iconEmoji(),
				std::move(suggestRemoving),
				row->suggestRemovePeers,
				chosen);
		} else {
			row->removePeers = {};
			row->removed = true;
			button->setRemoved(true);
		}
	};
	const auto markForRemoval = [=](not_null<FilterRowButton*> button) {
		const auto row = find(button);
		if (row->removed || row->removePeersRequestId > 0) {
			return;
		} else if (row->filter.hasMyLinks()) {
			controller->show(Ui::MakeConfirmBox({
				.text = { tr::lng_filters_delete_sure(tr::now) },
				.confirmed = crl::guard(button, [=](Fn<void()> close) {
					markForRemovalSure(button);
					close();
				}),
				.confirmText = tr::lng_box_delete(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		} else {
			markForRemovalSure(button);
		}
	};
	const auto remove = [=](not_null<FilterRowButton*> button) {
		const auto row = find(button);
		if (row->removed || row->removePeersRequestId > 0) {
			return;
		} else if (row->filter.chatlist() && !row->removePeersRequestId) {
			row->removePeersRequestId = session->api().request(
				MTPchatlists_GetLeaveChatlistSuggestions(
					MTP_inputChatlistDialogFilter(
						MTP_int(row->filter.id())))
			).done(crl::guard(button, [=](const MTPVector<MTPPeer> &result) {
				const auto row = find(button);
				row->removePeersRequestId = -1;
				row->suggestRemovePeers = ranges::views::all(
					result.v
				) | ranges::views::transform([=](const MTPPeer &peer) {
					return session->data().peer(peerFromMTP(peer));
				}) | ranges::to_vector;
				markForRemoval(button);
			})).fail(crl::guard(button, [=] {
				const auto row = find(button);
				row->removePeersRequestId = -1;
				markForRemoval(button);
			})).send();
		} else {
			markForRemoval(button);
		}
	};
	const auto wrap = container->add(object_ptr<Ui::VerticalLayout>(
		container));
	const auto addFilter = [=](const Data::ChatFilter &filter) {
		const auto button = wrap->add(
			object_ptr<FilterRowButton>(wrap, session, filter));
		button->removeRequests(
		) | rpl::start_with_next([=] {
			remove(button);
		}, button->lifetime());
		button->restoreRequests(
		) | rpl::start_with_next([=] {
			if (showLimitReached()) {
				return;
			}
			button->setRemoved(false);
			find(button)->removed = false;
		}, button->lifetime());
		button->setClickedCallback([=] {
			const auto found = find(button);
			if (found->removed) {
				return;
			}
			const auto doneCallback = [=](const Data::ChatFilter &result) {
				find(button)->filter = result;
				button->updateData(result);
			};
			const auto saveAnd = [=](
					const Data::ChatFilter &data,
					Fn<void(Data::ChatFilter)> next) {
				doneCallback(data);
				state->save(button, next);
			};
			controller->window().show(Box(
				EditFilterBox,
				controller,
				found->filter,
				crl::guard(button, doneCallback),
				crl::guard(button, saveAnd)));
		});
		state->rows.push_back({ button, filter });
		state->count = state->rows.size();

		const auto filters = &controller->session().data().chatsFilters();
		const auto id = filter.id();
		if (ranges::contains(filters->list(), id, &Data::ChatFilter::id)) {
			filters->chatsList(id)->fullSize().changes(
			) | rpl::start_with_next([=] {
				const auto found = find(button);
				if (found->postponedCountUpdate) {
					return;
				}
				found->postponedCountUpdate = true;
				Ui::PostponeCall(button, [=] {
					const auto &list = filters->list();
					const auto i = ranges::find(
						list,
						id,
						&Data::ChatFilter::id);
					if (i == end(list)) {
						return;
					}
					const auto found = find(button);
					const auto &now = found->filter;
					if ((i->flags() != now.flags())
						|| (i->always() != now.always())
						|| (i->never() != now.never())) {
						return;
					}
					button->updateCount(now);
					found->postponedCountUpdate = false;
				});
			}, button->lifetime());
		}

		wrap->resizeToWidth(container->width());

		return button;
	};
	const auto &list = session->data().chatsFilters().list();
	for (const auto &filter : list) {
		if (filter.id()) {
			addFilter(filter);
		}
	}

	session->data().chatsFilters().isChatlistChanged(
	) | rpl::start_with_next([=](FilterId id) {
		const auto filters = &session->data().chatsFilters();
		const auto &list = filters->list();
		const auto i = ranges::find(list, id, &Data::ChatFilter::id);
		const auto j = ranges::find(state->rows, id, [](const auto &row) {
			return row.filter.id();
		});
		if (i == end(list) || j == end(state->rows)) {
			return;
		}
		j->filter = j->filter.withChatlist(i->chatlist(), i->hasMyLinks());
		j->button->updateCount(j->filter);
	}, container->lifetime());

	AddButtonWithIcon(
		container,
		tr::lng_filters_create(),
		st::settingsButtonActive,
		{ &st::settingsIconAdd, IconType::Round, &st::windowBgActive }
	)->setClickedCallback([=] {
		if (showLimitReached()) {
			return;
		}
		const auto created = std::make_shared<FilterRowButton*>(nullptr);
		const auto doneCallback = [=](const Data::ChatFilter &result) {
			if (const auto button = *created) {
				find(button)->filter = result;
				button->updateData(result);
			} else {
				*created = addFilter(result);
			}
		};
		const auto saveAnd = [=](
				const Data::ChatFilter &data,
				Fn<void(Data::ChatFilter)> next) {
			doneCallback(data);
			state->save(*created, next);
		};
		controller->window().show(Box(
			EditFilterBox,
			controller,
			Data::ChatFilter(),
			crl::guard(container, doneCallback),
			crl::guard(container, saveAnd)));
	});
	Ui::AddSkip(container);
	const auto nonEmptyAbout = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container))
	)->setDuration(0);
	const auto aboutRows = nonEmptyAbout->entity();
	Ui::AddDivider(aboutRows);
	Ui::AddSkip(aboutRows);
	Ui::AddSubsectionTitle(aboutRows, tr::lng_filters_recommended());

	rpl::single(rpl::empty) | rpl::then(
		session->data().chatsFilters().suggestedUpdated()
	) | rpl::map([=] {
		return session->data().chatsFilters().suggestedFilters();
	}) | rpl::filter([=](const std::vector<Data::SuggestedFilter> &list) {
		return !list.empty();
	}) | rpl::take(
		1
	) | rpl::start_with_next([=](
			const std::vector<Data::SuggestedFilter> &suggestions) {
		for (const auto &suggestion : suggestions) {
			const auto &filter = suggestion.filter;
			if (ranges::contains(state->rows, filter, &FilterRow::filter)) {
				continue;
			}
			state->suggested = state->suggested.current() + 1;
			const auto button = aboutRows->add(object_ptr<FilterRowButton>(
				aboutRows,
				filter,
				suggestion.description));
			button->addRequests(
				) | rpl::start_with_next([=] {
				if (showLimitReached()) {
					return;
				}
				addFilter(filter);
				state->suggested = state->suggested.current() - 1;
				delete button;
			}, button->lifetime());
		}
		aboutRows->resizeToWidth(container->width());
		Ui::AddSkip(aboutRows, st::defaultVerticalListSkip);
	}, aboutRows->lifetime());

	auto showSuggestions = rpl::combine(
		state->suggested.value(),
		state->count.value(),
		Data::AmPremiumValue(session)
	) | rpl::map([limit](int suggested, int count, bool) {
		return suggested > 0 && count < limit();
	});
	nonEmptyAbout->toggleOn(std::move(showSuggestions));

	const auto prepareGoodIdsForNewFilters = [=] {
		const auto &list = session->data().chatsFilters().list();

		auto localId = 1;
		const auto chooseNextId = [&] {
			++localId;
			while (ranges::contains(list, localId, &Data::ChatFilter::id)) {
				++localId;
			}
			return localId;
		};
		auto result = base::flat_map<not_null<FilterRowButton*>, FilterId>();
		for (auto &row : state->rows) {
			const auto id = row.filter.id();
			if (row.removed) {
				continue;
			} else if (!id
				|| !ranges::contains(list, id, &Data::ChatFilter::id)) {
				result.emplace(row.button, chooseNextId());
			}
		}
		return result;
	};

	state->save = [=](
			const FilterRowButton *single,
			Fn<void(Data::ChatFilter)> next) {
		auto ids = prepareGoodIdsForNewFilters();

		auto updated = Data::ChatFilter();

		auto order = std::vector<FilterId>();
		auto updates = std::vector<MTPUpdate>();
		auto addRequests = std::vector<MTPmessages_UpdateDialogFilter>();
		auto removeRequests = std::vector<MTPmessages_UpdateDialogFilter>();
		auto removeChatlistRequests = std::vector<MTPchatlists_LeaveChatlist>();

		auto &realFilters = session->data().chatsFilters();
		const auto &list = realFilters.list();
		order.reserve(state->rows.size());
		for (auto &row : state->rows) {
			if (row.button.get() == single) {
				updated = row.filter;
			}
			const auto id = row.filter.id();
			const auto removed = row.removed;
			const auto i = ranges::find(list, id, &Data::ChatFilter::id);
			if (removed && (i == end(list) || id == FilterId(0))) {
				continue;
			} else if (!removed && i != end(list) && *i == row.filter) {
				order.push_back(id);
				continue;
			}
			const auto newId = ids.take(row.button).value_or(id);
			if (newId != id) {
				row.filter = row.filter.withId(newId);
				row.button->updateData(row.filter);
				if (row.button.get() == single) {
					updated = row.filter;
				}
			}
			const auto tl = removed
				? MTPDialogFilter()
				: row.filter.tl(newId);
			const auto removeChatlistWithChats = removed
				&& row.filter.chatlist()
				&& !row.removePeers.empty();
			if (removeChatlistWithChats) {
				auto inputs = ranges::views::all(
					row.removePeers
				) | ranges::views::transform([](not_null<PeerData*> peer) {
					return MTPInputPeer(peer->input);
				}) | ranges::to<QVector<MTPInputPeer>>();
				removeChatlistRequests.push_back(
					MTPchatlists_LeaveChatlist(
						MTP_inputChatlistDialogFilter(MTP_int(newId)),
						MTP_vector<MTPInputPeer>(std::move(inputs))));
			} else {
				const auto request = MTPmessages_UpdateDialogFilter(
					MTP_flags(removed
						? MTPmessages_UpdateDialogFilter::Flag(0)
						: MTPmessages_UpdateDialogFilter::Flag::f_filter),
					MTP_int(newId),
					tl);
				if (removed) {
					removeRequests.push_back(request);
				} else {
					addRequests.push_back(request);
					order.push_back(newId);
				}
			}
			updates.push_back(MTP_updateDialogFilter(
				MTP_flags(removed
					? MTPDupdateDialogFilter::Flag(0)
					: MTPDupdateDialogFilter::Flag::f_filter),
				MTP_int(newId),
				tl));
		}
		if (!ranges::contains(order, FilterId(0))) {
			auto position = 0;
			for (const auto &filter : list) {
				const auto id = filter.id();
				if (!id) {
					break;
				} else if (const auto i = ranges::find(order, id)
					; i != order.end()) {
					position = int(i - order.begin()) + 1;
				}
			}
			order.insert(order.begin() + position, FilterId(0));
		}
		if (next) {
			// We're not closing the layer yet, so delete removed rows.
			for (auto i = state->rows.begin(); i != state->rows.end();) {
				if (i->removed) {
					const auto button = i->button;
					i = state->rows.erase(i);
					delete button;
				} else {
					++i;
				}
			}
		}
		crl::on_main(session, [
			session,
			next,
			updated,
			order = std::move(order),
			updates = std::move(updates),
			addRequests = std::move(addRequests),
			removeRequests = std::move(removeRequests),
			removeChatlistRequests = std::move(removeChatlistRequests)
		] {
			const auto api = &session->api();
			const auto filters = &session->data().chatsFilters();
			const auto ids = std::make_shared<
				base::flat_set<mtpRequestId>
			>();
			const auto checkFinished = [=] {
				if (ids->empty() && next) {
					Assert(updated.id() != 0);
					next(updated);
				}
			};
			for (const auto &update : updates) {
				filters->apply(update);
			}
			auto previousId = mtpRequestId(0);
			const auto sendRequests = [&](const auto &requests) {
				for (auto &request : requests) {
					previousId = api->request(
						std::move(request)
					).done([=](const auto &result, mtpRequestId id) {
						if constexpr (std::is_same_v<
								std::decay_t<decltype(result)>,
								MTPUpdates>) {
							session->api().applyUpdates(result);
						}
						ids->remove(id);
						checkFinished();
					}).afterRequest(previousId).send();
					ids->emplace(previousId);
				}
			};
			sendRequests(removeRequests);
			sendRequests(removeChatlistRequests);
			sendRequests(addRequests);
			if (!order.empty() && !addRequests.empty()) {
				filters->saveOrder(order, previousId);
			}
			checkFinished();
		});
	};
	return [copy = state->save] {
		copy(nullptr, nullptr);
	};
}

void SetupTopContent(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<> showFinished) {
	const auto divider = Ui::CreateChild<Ui::BoxContentDivider>(parent.get());
	const auto verticalLayout = parent->add(
		object_ptr<Ui::VerticalLayout>(parent.get()));

	auto icon = CreateLottieIcon(
		verticalLayout,
		{
			.name = u"filters"_q,
			.sizeOverride = {
				st::settingsFilterIconSize,
				st::settingsFilterIconSize,
			},
		},
		st::settingsFilterIconPadding);
	std::move(
		showFinished
	) | rpl::start_with_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::once);
	}, verticalLayout->lifetime());
	verticalLayout->add(std::move(icon.widget));

	verticalLayout->add(
		object_ptr<Ui::CenterWrap<>>(
			verticalLayout,
			object_ptr<Ui::FlatLabel>(
				verticalLayout,
				tr::lng_filters_about(),
				st::settingsFilterDividerLabel)),
		st::settingsFilterDividerLabelPadding);

	verticalLayout->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		divider->setGeometry(r);
	}, divider->lifetime());

}

} // namespace

Folders::Folders(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

Folders::~Folders() {
	if (!Core::Quitting()) {
		_save();
	}
}

rpl::producer<QString> Folders::title() {
	return tr::lng_filters_title();
}

void Folders::setupContent(not_null<Window::SessionController*> controller) {
	controller->session().data().chatsFilters().requestSuggested();

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupTopContent(content, _showFinished.events());

	_save = SetupFoldersContent(controller, content);

	Ui::ResizeFitChild(this, content);
}

void Folders::showFinished() {
	_showFinished.fire({});
}

} // namespace Settings
