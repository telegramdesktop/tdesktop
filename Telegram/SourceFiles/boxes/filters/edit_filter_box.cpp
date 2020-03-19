/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/filters/edit_filter_box.h"

#include "boxes/filters/edit_filter_chats_list.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "settings/settings_common.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

namespace {

using namespace Settings;

constexpr auto kMaxFilterTitleLength = 12;

using Flag = Data::ChatFilter::Flag;
using Flags = Data::ChatFilter::Flags;
using ExceptionPeersRef = const base::flat_set<not_null<History*>> &;
using ExceptionPeersGetter = ExceptionPeersRef(Data::ChatFilter::*)() const;

constexpr auto kAllTypes = {
	Flag::Contacts,
	Flag::NonContacts,
	Flag::Groups,
	Flag::Channels,
	Flag::Bots,
	Flag::NoMuted,
	Flag::NoRead,
	Flag::NoArchived,
};

class FilterChatsPreview final : public Ui::RpWidget {
public:
	FilterChatsPreview(
		not_null<QWidget*> parent,
		Flags flags,
		const base::flat_set<not_null<History*>> &peers);

	[[nodiscard]] rpl::producer<Flag> flagRemoved() const;
	[[nodiscard]] rpl::producer<not_null<History*>> peerRemoved() const;

	void updateData(
		Flags flags,
		const base::flat_set<not_null<History*>> &peers);

	int resizeGetHeight(int newWidth) override;

private:
	using Button = base::unique_qptr<Ui::IconButton>;
	struct FlagButton {
		Flag flag = Flag();
		Button button;
	};
	struct PeerButton {
		not_null<History*> history;
		Button button;
	};

	void paintEvent(QPaintEvent *e) override;

	void refresh();
	void removeFlag(Flag flag);
	void removePeer(not_null<History*> history);

	std::vector<FlagButton> _removeFlag;
	std::vector<PeerButton> _removePeer;

	rpl::event_stream<Flag> _flagRemoved;
	rpl::event_stream<not_null<History*>> _peerRemoved;

};

not_null<FilterChatsPreview*> SetupChatsPreview(
		not_null<Ui::VerticalLayout*> content,
		not_null<Data::ChatFilter*> data,
		Flags flags,
		ExceptionPeersGetter peers) {
	const auto preview = content->add(object_ptr<FilterChatsPreview>(
		content,
		data->flags() & flags,
		(data->*peers)()));

	preview->flagRemoved(
	) | rpl::start_with_next([=](Flag flag) {
		*data = Data::ChatFilter(
			data->id(),
			data->title(),
			data->iconEmoji(),
			(data->flags() & ~flag),
			data->always(),
			data->pinned(),
			data->never());
	}, preview->lifetime());

	preview->peerRemoved(
	) | rpl::start_with_next([=](not_null<History*> history) {
		auto always = data->always();
		auto pinned = data->pinned();
		auto never = data->never();
		always.remove(history);
		pinned.erase(ranges::remove(pinned, history), end(pinned));
		never.remove(history);
		*data = Data::ChatFilter(
			data->id(),
			data->title(),
			data->iconEmoji(),
			data->flags(),
			std::move(always),
			std::move(pinned),
			std::move(never));
	}, preview->lifetime());

	return preview;
}

FilterChatsPreview::FilterChatsPreview(
	not_null<QWidget*> parent,
	Flags flags,
	const base::flat_set<not_null<History*>> &peers)
: RpWidget(parent) {
	updateData(flags, peers);
}

void FilterChatsPreview::refresh() {
	resizeToWidth(width());
}

void FilterChatsPreview::updateData(
		Flags flags,
		const base::flat_set<not_null<History*>> &peers) {
	_removeFlag.clear();
	_removePeer.clear();
	const auto makeButton = [&](Fn<void()> handler) {
		auto result = base::make_unique_q<Ui::IconButton>(
			this,
			st::windowFilterSmallRemove);
		result->setClickedCallback(std::move(handler));
		return result;
	};
	for (const auto flag : kAllTypes) {
		if (flags & flag) {
			_removeFlag.push_back({
				flag,
				makeButton([=] { removeFlag(flag); }) });
		}
	}
	for (const auto history : peers) {
		_removePeer.push_back({
			history,
			makeButton([=] { removePeer(history); }) });
	}
	refresh();
}

int FilterChatsPreview::resizeGetHeight(int newWidth) {
	const auto right = st::windowFilterSmallRemoveRight;
	const auto add = (st::windowFilterSmallItem.height
		- st::windowFilterSmallRemove.height) / 2;
	auto top = 0;
	const auto moveNextButton = [&](not_null<Ui::IconButton*> button) {
		button->moveToRight(right, top + add, newWidth);
		top += st::windowFilterSmallItem.height;
	};
	for (const auto &[flag, button] : _removeFlag) {
		moveNextButton(button.get());
	}
	for (const auto &[history, button] : _removePeer) {
		moveNextButton(button.get());
	}
	return top;
}

void FilterChatsPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto top = 0;
	const auto &st = st::windowFilterSmallItem;
	const auto iconLeft = st.photoPosition.x();
	const auto iconTop = st.photoPosition.y();
	const auto nameLeft = st.namePosition.x();
	p.setFont(st::windowFilterSmallItem.nameStyle.font);
	const auto nameTop = st.namePosition.y();
	for (const auto &[flag, button] : _removeFlag) {
		PaintFilterChatsTypeIcon(
			p,
			flag,
			iconLeft,
			top + iconTop,
			width(),
			st.photoSize);

		p.setPen(st::contactsNameFg);
		p.drawTextLeft(
			nameLeft,
			top + nameTop,
			width(),
			FilterChatsTypeName(flag));
		top += st.height;
	}
	for (const auto &[history, button] : _removePeer) {
		history->peer->paintUserpicLeft(
			p,
			iconLeft,
			top + iconTop,
			width(),
			st.photoSize);
		p.setPen(st::contactsNameFg);
		history->peer->nameText().drawLeftElided(
			p,
			nameLeft,
			top + nameTop,
			button->x() - nameLeft,
			width());
		top += st.height;
	}
}

void FilterChatsPreview::removeFlag(Flag flag) {
	const auto i = ranges::find(_removeFlag, flag, &FlagButton::flag);
	Assert(i != end(_removeFlag));
	_removeFlag.erase(i);
	refresh();
	_flagRemoved.fire_copy(flag);
}

void FilterChatsPreview::removePeer(not_null<History*> history) {
	const auto i = ranges::find(_removePeer, history, &PeerButton::history);
	Assert(i != end(_removePeer));
	_removePeer.erase(i);
	refresh();
	_peerRemoved.fire_copy(history);
}

rpl::producer<Flag> FilterChatsPreview::flagRemoved() const {
	return _flagRemoved.events();
}

rpl::producer<not_null<History*>> FilterChatsPreview::peerRemoved() const {
	return _peerRemoved.events();
}

void EditExceptions(
		not_null<Window::SessionController*> window,
		not_null<QObject*> context,
		Flags options,
		not_null<Data::ChatFilter*> data,
		Fn<void()> refresh) {
	const auto include = (options & Flag::Contacts) != Flags(0);
	auto controller = std::make_unique<EditFilterChatsListController>(
		window,
		(include
			? tr::lng_filters_include_title()
			: tr::lng_filters_exclude_title()),
		options,
		data->flags() & options,
		include ? data->always() : data->never());
	const auto rawController = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_settings_save(), crl::guard(context, [=] {
			const auto peers = box->peerListCollectSelectedRows();
			auto &&histories = ranges::view::all(
				peers
			) | ranges::view::transform([=](not_null<PeerData*> peer) {
				return window->session().data().history(peer);
			});
			auto changed = base::flat_set<not_null<History*>>{
				histories.begin(),
				histories.end()
			};
			auto removeFrom = include ? data->never() : data->always();
			for (const auto &history : changed) {
				removeFrom.remove(history);
			}
			auto pinned = data->pinned();
			pinned.erase(ranges::remove_if(pinned, [&](not_null<History*> history) {
				const auto contains = changed.contains(history);
				return include ? !contains : contains;
			}), end(pinned));
			*data = Data::ChatFilter(
				data->id(),
				data->title(),
				data->iconEmoji(),
				(data->flags() & ~options) | rawController->chosenOptions(),
				include ? std::move(changed) : std::move(removeFrom),
				std::move(pinned),
				include ? std::move(removeFrom) : std::move(changed));
			refresh();
			box->closeBox();
		}));
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	window->window().show(
		Box<PeerListBox>(
			std::move(controller),
			std::move(initBox)),
		Ui::LayerOption::KeepOther);
}

} // namespace

void EditFilterBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		const Data::ChatFilter &filter,
		Fn<void(const Data::ChatFilter &)> doneCallback) {
	const auto creating = filter.title().isEmpty();
	box->setTitle(creating ? tr::lng_filters_new() : tr::lng_filters_edit());

	const auto content = box->verticalLayout();
	const auto name = content->add(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_filters_new_name(),
			filter.title()),
		st::markdownLinkFieldPadding);
	name->setMaxLength(kMaxFilterTitleLength);

	const auto data = box->lifetime().make_state<Data::ChatFilter>(filter);

	constexpr auto kTypes = Flag::Contacts
		| Flag::NonContacts
		| Flag::Groups
		| Flag::Channels
		| Flag::Bots;
	constexpr auto kExcludeTypes = Flag::NoMuted
		| Flag::NoArchived
		| Flag::NoRead;

	box->setFocusCallback([=] {
		name->setFocusFast();
	});

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_filters_include());

	const auto include = SetupChatsPreview(
		content,
		data,
		kTypes,
		&Data::ChatFilter::always);

	const auto includeAdd = AddButton(
		content,
		tr::lng_filters_add_chats() | Ui::Text::ToUpper(),
		st::settingsUpdate);

	AddSkip(content);
	AddDividerText(content, tr::lng_filters_include_about());
	AddSkip(content);

	AddSubsectionTitle(content, tr::lng_filters_exclude());

	const auto exclude = SetupChatsPreview(
		content,
		data,
		kExcludeTypes,
		&Data::ChatFilter::never);

	const auto excludeAdd = AddButton(
		content,
		tr::lng_filters_add_chats() | Ui::Text::ToUpper(),
		st::settingsUpdate);

	AddSkip(content);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_filters_exclude_about(),
			st::boxDividerLabel),
		st::settingsDividerLabelPadding);

	const auto refreshPreviews = [=] {
		include->updateData(data->flags() & kTypes, data->always());
		exclude->updateData(data->flags() & kExcludeTypes, data->never());
	};
	includeAdd->setClickedCallback([=] {
		EditExceptions(window, box, kTypes, data, refreshPreviews);
	});
	excludeAdd->setClickedCallback([=] {
		EditExceptions(window, box, kExcludeTypes, data, refreshPreviews);
	});

	const auto save = [=] {
		const auto title = name->getLastText().trimmed();
		if (title.isEmpty()) {
			name->showError();
			return;
		} else if (!(data->flags() & kTypes) && data->always().empty()) {
			window->window().showToast(tr::lng_filters_empty(tr::now));
			return;
		} else if ((data->flags() == (kTypes | Flag::NoArchived))
			&& data->always().empty()
			&& data->never().empty()) {
			window->window().showToast(tr::lng_filters_default(tr::now));
			return;
		}
		const auto result = Data::ChatFilter(
			data->id(),
			title,
			data->iconEmoji(),
			data->flags(),
			data->always(),
			data->pinned(),
			data->never());
		box->closeBox();

		doneCallback(result);
	};
	box->addButton(
		creating ? tr::lng_filters_create_button() : tr::lng_settings_save(),
		save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}