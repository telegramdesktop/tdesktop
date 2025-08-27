/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/filters/edit_filter_chats_list.h"

#include "core/ui_integration.h"
#include "data/data_chat_filters.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "history/history.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "main/main_app_config.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "base/object_ptr.h"
#include "data/data_user.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"

namespace {

using Flag = Data::ChatFilter::Flag;
using Flags = Data::ChatFilter::Flags;

constexpr auto kAllTypes = {
	Flag::NewChats,
	Flag::ExistingChats,
	Flag::Contacts,
	Flag::NonContacts,
	Flag::Groups,
	Flag::Channels,
	Flag::Bots,
	Flag::NoMuted,
	Flag::NoRead,
	Flag::NoArchived,
};

struct RowSelectionChange {
	not_null<PeerListRow*> row;
	bool checked = false;
};

class TypeRow final : public PeerListRow {
public:
	explicit TypeRow(Flag flag);

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

private:
	[[nodiscard]] Flag flag() const;

};

class ExceptionRow final : public ChatsListBoxController::Row {
public:
	ExceptionRow(
		not_null<History*> history,
		not_null<PeerListDelegate*> delegate);

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

	void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) override;

private:
	Ui::Text::String _filtersText;

};

class TypeController final : public PeerListController {
public:
	TypeController(
		not_null<Main::Session*> session,
		Flags options,
		Flags selected);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] rpl::producer<Flags> selectedChanges() const;
	[[nodiscard]] auto rowSelectionChanges() const
		-> rpl::producer<RowSelectionChange>;

private:
	[[nodiscard]] std::unique_ptr<PeerListRow> createRow(Flag flag) const;
	[[nodiscard]] Flags collectSelectedOptions() const;

	const not_null<Main::Session*> _session;
	Flags _options;

	rpl::event_stream<> _selectionChanged;
	rpl::event_stream<RowSelectionChange> _rowSelectionChanges;

};

[[nodiscard]] uint64 TypeId(Flag flag) {
	return PeerId(FakeChatId(static_cast<BareId>(flag))).value;
}

TypeRow::TypeRow(Flag flag) : PeerListRow(TypeId(flag)) {
}

QString TypeRow::generateName() {
	return FilterChatsTypeName(flag());
}

QString TypeRow::generateShortName() {
	return generateName();
}

PaintRoundImageCallback TypeRow::generatePaintUserpicCallback(
		bool forceRound) {
	const auto flag = this->flag();
	return [=](QPainter &p, int x, int y, int outerWidth, int size) {
		PaintFilterChatsTypeIcon(p, flag, x, y, outerWidth, size);
	};
}

Flag TypeRow::flag() const {
	return static_cast<Flag>(id() & 0xFFFF);
}

ExceptionRow::ExceptionRow(
	not_null<History*> history,
	not_null<PeerListDelegate*> delegate)
: Row(history) {
	auto filters = TextWithEntities();
	for (const auto &filter : history->owner().chatsFilters().list()) {
		if (filter.contains(history) && filter.id()) {
			if (!filters.empty()) {
				filters.append(u", "_q);
			}
			auto title = filter.title();
			filters.append(title.isStatic
				? Data::ForceCustomEmojiStatic(std::move(title.text))
				: std::move(title.text));
		}
	}
	if (!filters.empty()) {
		const auto repaint = [=] { delegate->peerListUpdateRow(this); };
		_filtersText.setMarkedText(
			st::defaultTextStyle,
			filters,
			kMarkupTextOptions,
			Core::TextContext({
				.session = &history->session(),
				.repaint = repaint,
			}));
	} else if (peer()->isSelf()) {
		setCustomStatus(tr::lng_saved_forward_here(tr::now));
	}
}

QString ExceptionRow::generateName() {
	const auto peer = this->peer();
	return peer->isSelf()
		? tr::lng_saved_messages(tr::now)
		: peer->isRepliesChat()
		? tr::lng_replies_messages(tr::now)
		: peer->isVerifyCodes()
		? tr::lng_verification_codes(tr::now)
		: Row::generateName();
}

QString ExceptionRow::generateShortName() {
	return generateName();
}

PaintRoundImageCallback ExceptionRow::generatePaintUserpicCallback(
		bool forceRound) {
	const auto peer = this->peer();
	const auto saved = peer->isSelf();
	const auto replies = peer->isRepliesChat();
	auto userpic = saved ? Ui::PeerUserpicView() : ensureUserpicView();
	if (forceRound && peer->isForum()) {
		return ForceRoundUserpicCallback(peer);
	}
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		using namespace Ui;
		if (saved) {
			EmptyUserpic::PaintSavedMessages(p, x, y, outerWidth, size);
		} else if (replies) {
			EmptyUserpic::PaintRepliesMessages(p, x, y, outerWidth, size);
		} else {
			peer->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
		}
	};
}

void ExceptionRow::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	if (_filtersText.isEmpty()) {
		Row::paintStatusText(
			p,
			st,
			x,
			y,
			availableWidth,
			outerWidth,
			selected);
	} else {
		p.setPen(selected ? st.statusFgOver : st.statusFg);
		_filtersText.draw(p, {
			.position = { x, y },
			.outerWidth = outerWidth,
			.availableWidth = availableWidth,
			.palette = &st::defaultTextPalette,
			.now = crl::now(),
			.pausedEmoji = false,
			.elisionLines = 1,
		});
	}
}

TypeController::TypeController(
	not_null<Main::Session*> session,
	Flags options,
	Flags selected)
: _session(session)
, _options(options) {
}

Main::Session &TypeController::session() const {
	return *_session;
}

void TypeController::prepare() {
	for (const auto flag : kAllTypes) {
		if (_options & flag) {
			delegate()->peerListAppendRow(createRow(flag));
		}
	}
	delegate()->peerListRefreshRows();
}

Flags TypeController::collectSelectedOptions() const {
	auto result = Flags();
	for (const auto flag : kAllTypes) {
		if (const auto row = delegate()->peerListFindRow(TypeId(flag))) {
			if (row->checked()) {
				result |= flag;
			}
		}
	}
	return result;
}

void TypeController::rowClicked(not_null<PeerListRow*> row) {
	const auto checked = !row->checked();
	delegate()->peerListSetRowChecked(row, checked);
	_rowSelectionChanges.fire({ row, checked });
}

std::unique_ptr<PeerListRow> TypeController::createRow(Flag flag) const {
	return std::make_unique<TypeRow>(flag);
}

rpl::producer<Flags> TypeController::selectedChanges() const {
	return _rowSelectionChanges.events(
	) | rpl::map([=] {
		return collectSelectedOptions();
	});
}

auto TypeController::rowSelectionChanges() const
-> rpl::producer<RowSelectionChange> {
	return _rowSelectionChanges.events();
}

} // namespace

[[nodiscard]] QString FilterChatsTypeName(Flag flag) {
	switch (flag) {
	case Flag::NewChats: return tr::lng_filters_type_new(tr::now);
	case Flag::ExistingChats: return tr::lng_filters_type_existing(tr::now);
	case Flag::Contacts: return tr::lng_filters_type_contacts(tr::now);
	case Flag::NonContacts:
		return tr::lng_filters_type_non_contacts(tr::now);
	case Flag::Groups: return tr::lng_filters_type_groups(tr::now);
	case Flag::Channels: return tr::lng_filters_type_channels(tr::now);
	case Flag::Bots: return tr::lng_filters_type_bots(tr::now);
	case Flag::NoMuted: return tr::lng_filters_type_no_muted(tr::now);
	case Flag::NoArchived: return tr::lng_filters_type_no_archived(tr::now);
	case Flag::NoRead: return tr::lng_filters_type_no_read(tr::now);
	}
	Unexpected("Flag in TypeName.");
}

void PaintFilterChatsTypeIcon(
		QPainter &p,
		Data::ChatFilter::Flag flag,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto &color1 = [&]() -> const style::color& {
		switch (flag) {
		case Flag::NewChats: return st::historyPeer5UserpicBg;
		case Flag::ExistingChats: return st::historyPeer8UserpicBg;
		case Flag::Contacts: return st::historyPeer4UserpicBg;
		case Flag::NonContacts: return st::historyPeer7UserpicBg;
		case Flag::Groups: return st::historyPeer2UserpicBg;
		case Flag::Channels: return st::historyPeer1UserpicBg;
		case Flag::Bots: return st::historyPeer6UserpicBg;
		case Flag::NoMuted: return st::historyPeer6UserpicBg;
		case Flag::NoArchived: return st::historyPeer4UserpicBg;
		case Flag::NoRead: return st::historyPeer7UserpicBg;
		}
		Unexpected("Flag in color paintFlagIcon.");
	}();
	const auto &color2 = [&]() -> const style::color& {
		switch (flag) {
		case Flag::NewChats: return st::historyPeer5UserpicBg2;
		case Flag::ExistingChats: return st::historyPeer8UserpicBg2;
		case Flag::Contacts: return st::historyPeer4UserpicBg2;
		case Flag::NonContacts: return st::historyPeer7UserpicBg2;
		case Flag::Groups: return st::historyPeer2UserpicBg2;
		case Flag::Channels: return st::historyPeer1UserpicBg2;
		case Flag::Bots: return st::historyPeer6UserpicBg2;
		case Flag::NoMuted: return st::historyPeer6UserpicBg2;
		case Flag::NoArchived: return st::historyPeer4UserpicBg2;
		case Flag::NoRead: return st::historyPeer7UserpicBg2;
		}
		Unexpected("Flag in color paintFlagIcon.");
	}();
	const auto &icon = [&]() -> const style::icon& {
		switch (flag) {
		case Flag::NewChats: return st::windowFilterTypeNewChats;
		case Flag::ExistingChats: return st::windowFilterTypeExistingChats;
		case Flag::Contacts: return st::windowFilterTypeContacts;
		case Flag::NonContacts: return st::windowFilterTypeNonContacts;
		case Flag::Groups: return st::windowFilterTypeGroups;
		case Flag::Channels: return st::windowFilterTypeChannels;
		case Flag::Bots: return st::windowFilterTypeBots;
		case Flag::NoMuted: return st::windowFilterTypeNoMuted;
		case Flag::NoArchived: return st::windowFilterTypeNoArchived;
		case Flag::NoRead: return st::windowFilterTypeNoRead;
		}
		Unexpected("Flag in icon paintFlagIcon.");
	}();
	const auto rect = style::rtlrect(x, y, size, size, outerWidth);
	auto hq = PainterHighQualityEnabler(p);
	auto bg = QLinearGradient(x, y, x, y + size);
	bg.setStops({ { 0., color1->c }, { 1., color2->c } });
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawEllipse(rect);
	icon.paintInCenter(p, rect);
}

object_ptr<Ui::RpWidget> CreatePeerListSectionSubtitle(
		not_null<QWidget*> parent,
		rpl::producer<QString> text) {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		parent,
		st::windowFilterChatsSectionSubtitleHeight);

	const auto raw = result.data();
	raw->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(raw);
		p.fillRect(clip, st::searchedBarBg);
	}, raw->lifetime());

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		std::move(text),
		st::windowFilterChatsSectionSubtitle);
	raw->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto padding = st::windowFilterChatsSectionSubtitlePadding;
		const auto available = width - padding.left() - padding.right();
		label->resizeToNaturalWidth(available);
		label->moveToLeft(padding.left(), padding.top(), width);
	}, label->lifetime());

	return result;
}

EditFilterChatsListController::EditFilterChatsListController(
	not_null<Main::Session*> session,
	rpl::producer<QString> title,
	Flags options,
	Flags selected,
	const base::flat_set<not_null<History*>> &peers,
	int limit,
	Fn<void()> showLimitReached)
: ChatsListBoxController(session)
, _session(session)
, _showLimitReached(std::move(showLimitReached))
, _title(std::move(title))
, _peers(peers)
, _options(options & ~Flag::Chatlist)
, _selected(selected)
, _limit(limit)
, _chatlist(options & Flag::Chatlist) {
}

Main::Session &EditFilterChatsListController::session() const {
	return *_session;
}

int EditFilterChatsListController::selectedTypesCount() const {
	Expects(_chatlist || !_options || _typesDelegate != nullptr);

	if (_chatlist || !_options) {
		return 0;
	}
	auto result = 0;
	for (auto i = 0; i != _typesDelegate->peerListFullRowsCount(); ++i) {
		if (_typesDelegate->peerListRowAt(i)->checked()) {
			++result;
		}
	}
	return result;
}

void EditFilterChatsListController::rowClicked(not_null<PeerListRow*> row) {
	const auto count = delegate()->peerListSelectedRowsCount()
		- selectedTypesCount();
	if (count < _limit || row->checked()) {
		delegate()->peerListSetRowChecked(row, !row->checked());
		updateTitle();
	} else if (const auto copy = _showLimitReached) {
		copy();
	}
}

void EditFilterChatsListController::itemDeselectedHook(
		not_null<PeerData*> peer) {
	updateTitle();
}

bool EditFilterChatsListController::isForeignRow(PeerListRowId itemId) {
	return ranges::contains(kAllTypes, itemId, TypeId);
}

bool EditFilterChatsListController::handleDeselectForeignRow(
		PeerListRowId itemId) {
	if (isForeignRow(itemId)) {
		_deselectOption(itemId);
		return true;
	}
	return false;
}

void EditFilterChatsListController::prepareViewHook() {
	delegate()->peerListSetTitle(std::move(_title));
	if (!_chatlist && _options) {
		delegate()->peerListSetAboveWidget(prepareTypesList());
	}

	const auto count = int(_peers.size());
	const auto rows = std::make_unique<std::optional<ExceptionRow>[]>(count);
	auto i = 0;
	for (const auto &history : _peers) {
		rows[i++].emplace(history, delegate());
	}
	auto pointers = std::vector<ExceptionRow*>();
	pointers.reserve(count);
	for (auto i = 0; i != count; ++i) {
		pointers.push_back(&*rows[i]);
	}
	delegate()->peerListAddSelectedRows(pointers);
	updateTitle();
}

object_ptr<Ui::RpWidget> EditFilterChatsListController::prepareTypesList() {
	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();
	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_filters_edit_types()));
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::membersMarginTop));
	_typesDelegate = container->lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = container->lifetime().make_state<TypeController>(
		&session(),
		_options,
		_selected);
	controller->setStyleOverrides(&st::windowFilterSmallList);
	const auto content = result->add(object_ptr<PeerListContent>(
		container,
		controller));
	_typesDelegate->setContent(content);
	controller->setDelegate(_typesDelegate);
	for (const auto flag : kAllTypes) {
		if (_selected & flag) {
			if (const auto row = _typesDelegate->peerListFindRow(TypeId(flag))) {
				content->changeCheckState(row, true, anim::type::instant);
				this->delegate()->peerListSetForeignRowChecked(
					row,
					true,
					anim::type::instant);
			}
		}
	}
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::membersMarginBottom));
	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_filters_edit_chats()));

	controller->selectedChanges(
	) | rpl::start_with_next([=](Flags selected) {
		_selected = selected;
	}, _lifetime);

	controller->rowSelectionChanges(
	) | rpl::start_with_next([=](RowSelectionChange update) {
		this->delegate()->peerListSetForeignRowChecked(
			update.row,
			update.checked,
			anim::type::normal);
	}, _lifetime);

	_deselectOption = [=](PeerListRowId itemId) {
		if (const auto row = _typesDelegate->peerListFindRow(itemId)) {
			_typesDelegate->peerListSetRowChecked(row, false);
		}
	};

	return result;
}

auto EditFilterChatsListController::createRow(not_null<History*> history)
-> std::unique_ptr<Row> {
	const auto business = (_options & (Flag::NewChats | Flag::ExistingChats))
		|| (!_options && !_chatlist);
	if (business && (history->peer->isSelf() || !history->peer->isUser())) {
		return nullptr;
	}
	return history->inChatList()
		? std::make_unique<ExceptionRow>(history, delegate())
		: nullptr;
}

void EditFilterChatsListController::updateTitle() {
	const auto count = delegate()->peerListSelectedRowsCount()
		- selectedTypesCount();
	const auto additional = u"%1 / %2"_q.arg(count).arg(_limit);
	delegate()->peerListSetAdditionalTitle(rpl::single(additional));
}
