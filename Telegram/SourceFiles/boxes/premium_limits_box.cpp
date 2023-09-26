/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/premium_limits_box.h"

#include "ui/boxes/confirm_box.h"
#include "ui/controls/peer_list_dummy.h"
#include "ui/effects/premium_graphics.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/text/text_utilities.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/prepare_short_info_box.h" // PrepareShortInfoBox
#include "window/window_session_controller.h"
#include "data/data_chat_filters.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_premium_limits.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "settings/settings_premium.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "styles/style_premium.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"

namespace {

struct InfographicDescriptor {
	float64 defaultLimit = 0;
	float64 current = 0;
	float64 premiumLimit = 0;
	const style::icon *icon;
	std::optional<tr::phrase<lngtag_count>> phrase;
	bool complexRatio = false;
};

void AddSubsectionTitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text) {
	const auto &subtitlePadding = st::settingsButton.padding;
	Settings::AddSubsectionTitle(
		container,
		std::move(text),
		{ 0, subtitlePadding.top(), 0, -subtitlePadding.bottom() });
}

class InactiveController final : public PeerListController {
public:
	explicit InactiveController(not_null<Main::Session*> session);
	~InactiveController();

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

private:
	void appendRow(not_null<PeerData*> peer, TimeId date);
	[[nodiscard]] std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> peer,
		TimeId date) const;

	const not_null<Main::Session*> _session;
	mtpRequestId _requestId = 0;

};

class PublicsController final : public PeerListController {
public:
	PublicsController(
		not_null<Window::SessionNavigation*> navigation,
		Fn<void()> closeBox);
	~PublicsController();

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;

private:
	void appendRow(not_null<PeerData*> peer);
	[[nodiscard]] std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> peer) const;

	const not_null<Window::SessionNavigation*> _navigation;
	Fn<void()> _closeBox;
	mtpRequestId _requestId = 0;

};

class InactiveDelegate final : public PeerListContentDelegate {
public:
	void peerListSetTitle(rpl::producer<QString> title) override;
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override;
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override;
	int peerListSelectedRowsCount() override;
	void peerListScrollToTop() override;
	void peerListAddSelectedPeerInBunch(
		not_null<PeerData*> peer) override;
	void peerListAddSelectedRowInBunch(
		not_null<PeerListRow*> row) override;
	void peerListFinishSelectedRowsBunch() override;
	void peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) override;
	void peerListShowBox(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options = Ui::LayerOption::KeepOther) override;
	void peerListHideLayer() override;
	std::shared_ptr<Main::SessionShow> peerListUiShow() override;
	void peerListSetRowChecked(
		not_null<PeerListRow*> row,
		bool checked) override;

	[[nodiscard]] rpl::producer<int> selectedCountChanges() const;
	[[nodiscard]] const base::flat_set<PeerListRowId> &selected() const;

private:
	base::flat_set<PeerListRowId> _selectedIds;
	rpl::event_stream<int> _selectedCountChanges;

};

void InactiveDelegate::peerListSetTitle(rpl::producer<QString> title) {
}

void InactiveDelegate::peerListSetAdditionalTitle(
	rpl::producer<QString> title) {
}

bool InactiveDelegate::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return _selectedIds.contains(row->id());
}

int InactiveDelegate::peerListSelectedRowsCount() {
	return int(_selectedIds.size());
}

void InactiveDelegate::peerListScrollToTop() {
}

void InactiveDelegate::peerListAddSelectedPeerInBunch(
		not_null<PeerData*> peer) {
	_selectedIds.emplace(PeerListRowId(peer->id.value));
	_selectedCountChanges.fire(int(_selectedIds.size()));
}

void InactiveDelegate::peerListAddSelectedRowInBunch(
		not_null<PeerListRow*> row) {
	_selectedIds.emplace(row->id());
	_selectedCountChanges.fire(int(_selectedIds.size()));
}

void InactiveDelegate::peerListSetRowChecked(
		not_null<PeerListRow*> row,
		bool checked) {
	if (checked) {
		_selectedIds.emplace(row->id());
	} else {
		_selectedIds.remove(row->id());
	}
	_selectedCountChanges.fire(int(_selectedIds.size()));
	PeerListContentDelegate::peerListSetRowChecked(row, checked);
}

void InactiveDelegate::peerListFinishSelectedRowsBunch() {
}

void InactiveDelegate::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

void InactiveDelegate::peerListShowBox(
	object_ptr<Ui::BoxContent> content,
	Ui::LayerOptions options) {
}

void InactiveDelegate::peerListHideLayer() {
}

std::shared_ptr<Main::SessionShow> InactiveDelegate::peerListUiShow() {
	Unexpected("...InactiveDelegate::peerListUiShow");
}

rpl::producer<int> InactiveDelegate::selectedCountChanges() const {
	return _selectedCountChanges.events();
}

const base::flat_set<PeerListRowId> &InactiveDelegate::selected() const {
	return _selectedIds;
}

InactiveController::InactiveController(not_null<Main::Session*> session)
: _session(session) {
}

InactiveController::~InactiveController() {
	if (_requestId) {
		_session->api().request(_requestId).cancel();
	}
}

Main::Session &InactiveController::session() const {
	return *_session;
}

void InactiveController::prepare() {
	_requestId = _session->api().request(MTPchannels_GetInactiveChannels(
	)).done([=](const MTPmessages_InactiveChats &result) {
		_requestId = 0;
		result.match([&](const MTPDmessages_inactiveChats &data) {
			_session->data().processUsers(data.vusers());
			const auto &list = data.vchats().v;
			const auto &dates = data.vdates().v;
			for (auto i = 0, count = int(list.size()); i != count; ++i) {
				const auto peer = _session->data().processChat(list[i]);
				const auto date = (i < dates.size()) ? dates[i].v : TimeId();
				appendRow(peer, date);
			}
			delegate()->peerListRefreshRows();
		});
	}).send();
}

void InactiveController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

void InactiveController::appendRow(
		not_null<PeerData*> participant,
		TimeId date) {
	if (!delegate()->peerListFindRow(participant->id.value)) {
		delegate()->peerListAppendRow(createRow(participant, date));
	}
}

std::unique_ptr<PeerListRow> InactiveController::createRow(
		not_null<PeerData*> peer,
		TimeId date) const {
	auto result = std::make_unique<PeerListRow>(peer);
	const auto active = base::unixtime::parse(date).date();
	const auto now = QDate::currentDate();
	const auto time = [&] {
		const auto days = active.daysTo(now);
		if (now < active) {
			return QString();
		} else if (active == now) {
			const auto unixtime = base::unixtime::now();
			const auto delta = int64(unixtime) - int64(date);
			if (delta <= 0) {
				return QString();
			} else if (delta >= 3600) {
				return tr::lng_hours(tr::now, lt_count, delta / 3600);
			} else if (delta >= 60) {
				return tr::lng_minutes(tr::now, lt_count, delta / 60);
			} else {
				return tr::lng_seconds(tr::now, lt_count, delta);
			}
		} else if (days >= 365) {
			return tr::lng_years(tr::now, lt_count, days / 365);
		} else if (days >= 31) {
			return tr::lng_months(tr::now, lt_count, days / 31);
		} else if (days >= 7) {
			return tr::lng_weeks(tr::now, lt_count, days / 7);
		} else {
			return tr::lng_days(tr::now, lt_count, days);
		}
	}();
	result->setCustomStatus(tr::lng_channels_leave_status(
		tr::now,
		lt_type,
		(peer->isBroadcast()
			? tr::lng_channel_status(tr::now)
			: tr::lng_group_status(tr::now)),
		lt_time,
		time));
	return result;
}

PublicsController::PublicsController(
	not_null<Window::SessionNavigation*> navigation,
	Fn<void()> closeBox)
: _navigation(navigation)
, _closeBox(std::move(closeBox)) {
}

PublicsController::~PublicsController() {
	if (_requestId) {
		_navigation->session().api().request(_requestId).cancel();
	}
}

Main::Session &PublicsController::session() const {
	return _navigation->session();
}

void PublicsController::prepare() {
	_requestId = _navigation->session().api().request(
		MTPchannels_GetAdminedPublicChannels(MTP_flags(0))
	).done([=](const MTPmessages_Chats &result) {
		_requestId = 0;

		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		auto &owner = _navigation->session().data();
		for (const auto &chat : chats) {
			if (const auto peer = owner.processChat(chat)) {
				if (!peer->isChannel() || peer->userName().isEmpty()) {
					continue;
				}
				appendRow(peer);
			}
			delegate()->peerListRefreshRows();
		}
	}).send();
}

void PublicsController::rowClicked(not_null<PeerListRow*> row) {
	_navigation->parentController()->show(
		PrepareShortInfoBox(row->peer(), _navigation));
}

void PublicsController::rowRightActionClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	const auto textMethod = peer->isMegagroup()
		? tr::lng_channels_too_much_public_revoke_confirm_group
		: tr::lng_channels_too_much_public_revoke_confirm_channel;
	const auto text = textMethod(
		tr::now,
		lt_link,
		peer->session().createInternalLink(peer->userName()),
		lt_group,
		peer->name());
	const auto confirmText = tr::lng_channels_too_much_public_revoke(
		tr::now);
	const auto closeBox = _closeBox;
	const auto once = std::make_shared<bool>(false);
	auto callback = crl::guard(_navigation, [=](Fn<void()> close) {
		if (*once) {
			return;
		}
		*once = true;
		peer->session().api().request(MTPchannels_UpdateUsername(
			peer->asChannel()->inputChannel,
			MTP_string()
		)).done([=] {
			peer->session().api().request(MTPchannels_DeactivateAllUsernames(
				peer->asChannel()->inputChannel
			)).done([=] {
				closeBox();
				close();
			}).send();
		}).send();
	});
	_navigation->parentController()->show(
		Ui::MakeConfirmBox({
			.text = text,
			.confirmed = std::move(callback),
			.confirmText = confirmText,
		}));
}

void PublicsController::appendRow(not_null<PeerData*> participant) {
	if (!delegate()->peerListFindRow(participant->id.value)) {
		delegate()->peerListAppendRow(createRow(participant));
	}
}

std::unique_ptr<PeerListRow> PublicsController::createRow(
		not_null<PeerData*> peer) const {
	auto result = std::make_unique<PeerListRowWithLink>(peer);
	result->setActionLink(tr::lng_channels_too_much_public_revoke(tr::now));
	result->setCustomStatus(
		_navigation->session().createInternalLink(peer->userName()));
	return result;
}

void SimpleLimitBox(
		not_null<Ui::GenericBox*> box,
		const style::PremiumLimits *stOverride,
		not_null<Main::Session*> session,
		bool premiumPossible,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text,
		const QString &refAddition,
		const InfographicDescriptor &descriptor,
		bool fixed = false) {
	const auto &st = stOverride ? *stOverride : st::defaultPremiumLimits;

	box->setWidth(st::boxWideWidth);

	const auto top = fixed
		? box->setPinnedToTopContent(object_ptr<Ui::VerticalLayout>(box))
		: box->verticalLayout();

	Settings::AddSkip(top, st::premiumInfographicPadding.top());
	Ui::Premium::AddBubbleRow(
		top,
		st::defaultPremiumBubble,
		BoxShowFinishes(box),
		0,
		descriptor.current,
		descriptor.premiumLimit,
		premiumPossible,
		descriptor.phrase,
		descriptor.icon);
	Settings::AddSkip(top, st::premiumLineTextSkip);
	if (premiumPossible) {
		Ui::Premium::AddLimitRow(
			top,
			st,
			descriptor.premiumLimit,
			descriptor.phrase,
			0,
			(descriptor.complexRatio
				? (float64(descriptor.current) / descriptor.premiumLimit)
				: Ui::Premium::kLimitRowRatio));
		Settings::AddSkip(top, st::premiumInfographicPadding.bottom());
	}

	box->setTitle(std::move(title));

	auto padding = st::boxPadding;
	padding.setTop(padding.bottom());
	top->add(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::aboutRevokePublicLabel),
		padding);

	if (session->premium() || !premiumPossible) {
		box->addButton(tr::lng_box_ok(), [=] {
			box->closeBox();
		});
	} else {
		box->addButton(tr::lng_limits_increase(), [=] {
			Settings::ShowPremium(session, LimitsPremiumRef(refAddition));
		});

		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}

	if (fixed) {
		Settings::AddSkip(top, st::settingsButton.padding.bottom());
		Settings::AddDivider(top);
	}
}

void SimpleLimitBox(
		not_null<Ui::GenericBox*> box,
		const style::PremiumLimits *stOverride,
		not_null<Main::Session*> session,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text,
		const QString &refAddition,
		const InfographicDescriptor &descriptor,
		bool fixed = false) {
	SimpleLimitBox(
		box,
		stOverride,
		session,
		session->premiumPossible(),
		std::move(title),
		std::move(text),
		refAddition,
		descriptor,
		fixed);
}

[[nodiscard]] int PinsCount(not_null<Dialogs::MainList*> list) {
	return list->pinned()->order().size();
}

void SimplePinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const QString &refAddition,
		float64 defaultLimit,
		float64 premiumLimit,
		float64 currentCount) {
	const auto premium = session->premium();
	const auto premiumPossible = session->premiumPossible();

	const auto current = std::clamp(currentCount, defaultLimit, premiumLimit);

	auto text = rpl::combine(
		tr::lng_filter_pin_limit1(
			lt_count,
			rpl::single(premium ? premiumLimit : defaultLimit),
			Ui::Text::RichLangValue),
		((premium || !premiumPossible)
			? rpl::single(TextWithEntities())
			: tr::lng_filter_pin_limit2(
				lt_count,
				rpl::single(premiumLimit),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return b.text.isEmpty()
			? a
			: a.append(QChar(' ')).append(std::move(b));
	});
	SimpleLimitBox(
		box,
		nullptr,
		session,
		tr::lng_filter_pin_limit_title(),
		std::move(text),
		refAddition,
		{ defaultLimit, current, premiumLimit, &st::premiumIconPins });
}

} // namespace

void ChannelsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto premium = session->premium();
	const auto premiumPossible = session->premiumPossible();

	const auto limits = Data::PremiumLimits(session);
	const auto defaultLimit = float64(limits.channelsDefault());
	const auto premiumLimit = float64(limits.channelsPremium());
	const auto current = (premium ? premiumLimit : defaultLimit);

	auto text = rpl::combine(
		tr::lng_channels_limit1(
			lt_count,
			rpl::single(current),
			Ui::Text::RichLangValue),
		((premium || !premiumPossible)
			? tr::lng_channels_limit2_final(Ui::Text::RichLangValue)
			: tr::lng_channels_limit2(
				lt_count,
				rpl::single(premiumLimit),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return a.append(QChar(' ')).append(std::move(b));
	});

	SimpleLimitBox(
		box,
		nullptr,
		session,
		tr::lng_channels_limit_title(),
		std::move(text),
		"channels",
		{ defaultLimit, current, premiumLimit, &st::premiumIconGroups },
		true);

	AddSubsectionTitle(box->verticalLayout(), tr::lng_channels_leave_title());

	const auto delegate = box->lifetime().make_state<InactiveDelegate>();
	const auto controller = box->lifetime().make_state<InactiveController>(
		session);

	const auto content = box->addRow(
		object_ptr<PeerListContent>(box, controller),
		{});
	delegate->setContent(content);
	controller->setDelegate(delegate);

	const auto count = 100;
	const auto placeholder = box->addRow(
		object_ptr<PeerListDummy>(box, count, st::defaultPeerList),
		{});

	using namespace rpl::mappers;
	content->heightValue(
	) | rpl::filter(_1 > 0) | rpl::start_with_next([=] {
		delete placeholder;
	}, placeholder->lifetime());

	delegate->selectedCountChanges(
	) | rpl::start_with_next([=](int count) {
		const auto leave = [=](const base::flat_set<PeerListRowId> &ids) {
			for (const auto rowId : ids) {
				const auto id = peerToChannel(PeerId(rowId));
				if (const auto channel = session->data().channelLoaded(id)) {
					session->api().leaveChannel(channel);
				}
			}
			box->showToast(tr::lng_channels_leave_done(tr::now));
			box->closeBox();
		};
		box->clearButtons();
		if (count) {
			box->addButton(
				tr::lng_channels_leave(lt_count, rpl::single(count * 1.)),
				[=] { leave(delegate->selected()); });
		} else if (premium) {
			box->addButton(tr::lng_box_ok(), [=] {
				box->closeBox();
			});
		} else {
			box->addButton(tr::lng_limits_increase(), [=] {
				Settings::ShowPremium(session, LimitsPremiumRef("channels"));
			});
		}
	}, box->lifetime());
}

void PublicLinksLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> navigation,
		Fn<void()> retry) {
	const auto session = &navigation->session();
	const auto premium = session->premium();
	const auto premiumPossible = session->premiumPossible();

	const auto limits = Data::PremiumLimits(session);
	const auto defaultLimit = float64(limits.channelsPublicDefault());
	const auto premiumLimit = float64(limits.channelsPublicPremium());
	const auto current = (premium ? premiumLimit : defaultLimit);

	auto text = rpl::combine(
		tr::lng_links_limit1(
			lt_count,
			rpl::single(current),
			Ui::Text::RichLangValue),
		((premium || !premiumPossible)
			? tr::lng_links_limit2_final(Ui::Text::RichLangValue)
			: tr::lng_links_limit2(
				lt_count,
				rpl::single(premiumLimit),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return a.append(QChar(' ')).append(std::move(b));
	});

	SimpleLimitBox(
		box,
		nullptr,
		session,
		tr::lng_links_limit_title(),
		std::move(text),
		"channels_public",
		{ defaultLimit, current, premiumLimit, &st::premiumIconLinks },
		true);

	AddSubsectionTitle(box->verticalLayout(), tr::lng_links_revoke_title());

	const auto delegate = box->lifetime().make_state<InactiveDelegate>();
	const auto controller = box->lifetime().make_state<PublicsController>(
		navigation,
		crl::guard(box, [=] { box->closeBox(); retry(); }));

	const auto content = box->addRow(
		object_ptr<PeerListContent>(box, controller),
		{});
	delegate->setContent(content);
	controller->setDelegate(delegate);

	const auto count = defaultLimit;
	const auto placeholder = box->addRow(
		object_ptr<PeerListDummy>(box, count, st::defaultPeerList),
		{});

	using namespace rpl::mappers;
	content->heightValue(
	) | rpl::filter(_1 > 0) | rpl::start_with_next([=] {
		delete placeholder;
	}, placeholder->lifetime());
}

void FilterChatsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		int currentCount,
		bool include) {
	const auto premium = session->premium();
	const auto premiumPossible = session->premiumPossible();

	const auto limits = Data::PremiumLimits(session);
	const auto defaultLimit = float64(limits.dialogFiltersChatsDefault());
	const auto premiumLimit = float64(limits.dialogFiltersChatsPremium());
	const auto current = std::clamp(
		float64(currentCount),
		defaultLimit,
		premiumLimit);

	auto text = rpl::combine(
		(include
			? tr::lng_filter_chats_limit1
			: tr::lng_filter_chats_exlude_limit1)(
				lt_count,
				rpl::single(premium ? premiumLimit : defaultLimit),
				Ui::Text::RichLangValue),
		((premium || !premiumPossible)
			? rpl::single(TextWithEntities())
			: tr::lng_filter_chats_limit2(
				lt_count,
				rpl::single(premiumLimit),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return b.text.isEmpty()
			? a
			: a.append(QChar(' ')).append(std::move(b));
	});

	SimpleLimitBox(
		box,
		nullptr,
		session,
		tr::lng_filter_chats_limit_title(),
		std::move(text),
		"dialog_filters_chats",
		{ defaultLimit, current, premiumLimit, &st::premiumIconChats });
}

void FilterLinksLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto premium = session->premium();
	const auto premiumPossible = session->premiumPossible();

	const auto limits = Data::PremiumLimits(session);
	const auto defaultLimit = float64(limits.dialogFiltersLinksDefault());
	const auto premiumLimit = float64(limits.dialogFiltersLinksPremium());
	const auto current = (premium ? premiumLimit : defaultLimit);

	auto text = rpl::combine(
		tr::lng_filter_links_limit1(
			lt_count,
			rpl::single(premium ? premiumLimit : defaultLimit),
			Ui::Text::RichLangValue),
		((premium || !premiumPossible)
			? rpl::single(TextWithEntities())
			: tr::lng_filter_links_limit2(
				lt_count,
				rpl::single(premiumLimit),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return b.text.isEmpty()
			? a
			: a.append(QChar(' ')).append(std::move(b));
	});

	SimpleLimitBox(
		box,
		nullptr,
		session,
		tr::lng_filter_links_limit_title(),
		std::move(text),
		"chatlist_invites",
		{
			defaultLimit,
			current,
			premiumLimit,
			&st::premiumIconChats,
			std::nullopt,
			true });
}


void FiltersLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		std::optional<int> filtersCountOverride) {
	const auto premium = session->premium();
	const auto premiumPossible = session->premiumPossible();

	const auto limits = Data::PremiumLimits(session);
	const auto defaultLimit = float64(limits.dialogFiltersDefault());
	const auto premiumLimit = float64(limits.dialogFiltersPremium());
	const auto cloud = int(ranges::count_if(
		session->data().chatsFilters().list(),
		[](const Data::ChatFilter &f) { return f.id() != FilterId(); }));
	const auto current = float64(filtersCountOverride.value_or(cloud));

	auto text = rpl::combine(
		tr::lng_filters_limit1(
			lt_count,
			rpl::single(premium ? premiumLimit : defaultLimit),
			Ui::Text::RichLangValue),
		((premium || !premiumPossible)
			? rpl::single(TextWithEntities())
			: tr::lng_filters_limit2(
				lt_count,
				rpl::single(premiumLimit),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return b.text.isEmpty()
			? a
			: a.append(QChar(' ')).append(std::move(b));
	});
	SimpleLimitBox(
		box,
		nullptr,
		session,
		tr::lng_filters_limit_title(),
		std::move(text),
		"dialog_filters",
		{ defaultLimit, current, premiumLimit, &st::premiumIconFolders });
}

void ShareableFiltersLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto premium = session->premium();
	const auto premiumPossible = session->premiumPossible();

	const auto limits = Data::PremiumLimits(session);
	const auto defaultLimit = float64(limits.dialogShareableFiltersDefault());
	const auto premiumLimit = float64(limits.dialogShareableFiltersPremium());
	const auto current = float64(ranges::count_if(
		session->data().chatsFilters().list(),
		[](const Data::ChatFilter &f) { return f.chatlist(); }));

	auto text = rpl::combine(
		tr::lng_filter_shared_limit1(
			lt_count,
			rpl::single(premium ? premiumLimit : defaultLimit),
			Ui::Text::RichLangValue),
		((premium || !premiumPossible)
			? rpl::single(TextWithEntities())
			: tr::lng_filter_shared_limit2(
				lt_count,
				rpl::single(premiumLimit),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return b.text.isEmpty()
			? a
			: a.append(QChar(' ')).append(std::move(b));
	});
	SimpleLimitBox(
		box,
		nullptr,
		session,
		tr::lng_filter_shared_limit_title(),
		std::move(text),
		"chatlists_joined",
		{
			defaultLimit,
			current,
			premiumLimit,
			&st::premiumIconFolders,
			std::nullopt,
			true });
}

void FilterPinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		FilterId filterId) {
	const auto limits = Data::PremiumLimits(session);
	SimplePinsLimitBox(
		box,
		session,
		"dialog_filters_pinned",
		limits.dialogFiltersChatsDefault(),
		limits.dialogFiltersChatsPremium(),
		PinsCount(session->data().chatsFilters().chatsList(filterId)));
}

void FolderPinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto limits = Data::PremiumLimits(session);
	SimplePinsLimitBox(
		box,
		session,
		"dialogs_folder_pinned",
		limits.dialogsFolderPinnedDefault(),
		limits.dialogsFolderPinnedPremium(),
		PinsCount(session->data().folder(Data::Folder::kId)->chatsList()));
}

void PinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto limits = Data::PremiumLimits(session);
	SimplePinsLimitBox(
		box,
		session,
		"dialog_pinned",
		limits.dialogsPinnedDefault(),
		limits.dialogsPinnedPremium(),
		PinsCount(session->data().chatsList()));
}

void ForumPinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Data::Forum*> forum) {
	const auto current = forum->owner().pinnedChatsLimit(forum) * 1.;

	auto text = tr::lng_forum_pin_limit(
		lt_count,
		rpl::single(current),
		Ui::Text::RichLangValue);
	SimpleLimitBox(
		box,
		nullptr,
		&forum->session(),
		false,
		tr::lng_filter_pin_limit_title(),
		std::move(text),
		QString(),
		{ current, current, current * 2, &st::premiumIconPins });
}

void CaptionLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		int remove,
		const style::PremiumLimits *stOverride) {
	const auto premium = session->premium();
	const auto premiumPossible = session->premiumPossible();

	const auto limits = Data::PremiumLimits(session);
	const auto defaultLimit = float64(limits.captionLengthDefault());
	const auto premiumLimit = float64(limits.captionLengthPremium());
	const auto currentLimit = premium ? premiumLimit : defaultLimit;
	const auto current = std::clamp(
		remove + currentLimit,
		defaultLimit,
		premiumLimit);

	auto text = rpl::combine(
		tr::lng_caption_limit1(
			lt_count,
			rpl::single(currentLimit),
			Ui::Text::RichLangValue),
		(!premiumPossible
			? rpl::single(TextWithEntities())
			: tr::lng_caption_limit2(
				lt_count,
				rpl::single(premiumLimit),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return b.text.isEmpty()
			? a
			: a.append(QChar(' ')).append(std::move(b));
	});

	SimpleLimitBox(
		box,
		stOverride,
		session,
		tr::lng_caption_limit_title(),
		std::move(text),
		"caption_length",
		{ defaultLimit, current, premiumLimit, &st::premiumIconChats });
}

void CaptionLimitReachedBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		int remove,
		const style::PremiumLimits *stOverride) {
	Ui::ConfirmBox(box, Ui::ConfirmBoxArgs{
		.text = tr::lng_caption_limit_reached(tr::now, lt_count, remove),
		.labelStyle = stOverride ? &stOverride->boxLabel : nullptr,
		.inform = true,
	});
	if (!session->premium()) {
		box->addLeftButton(tr::lng_limits_increase(), [=] {
			box->getDelegate()->showBox(
				Box(CaptionLimitBox, session, remove, stOverride),
				Ui::LayerOption::KeepOther,
				anim::type::normal);
			box->closeBox();
		});
	}
}

void FileSizeLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		uint64 fileSizeBytes,
		const style::PremiumLimits *stOverride) {
	const auto limits = Data::PremiumLimits(session);
	const auto defaultLimit = float64(limits.uploadMaxDefault());
	const auto premiumLimit = float64(limits.uploadMaxPremium());

	const auto defaultGb = float64(int(defaultLimit + 999) / 2000);
	const auto premiumGb = float64(int(premiumLimit + 999) / 2000);

	const auto tooLarge = (fileSizeBytes > premiumLimit * 512ULL * 1024);
	const auto showLimit = tooLarge ? premiumGb : defaultGb;
	const auto premiumPossible = !tooLarge && session->premiumPossible();

	const auto current = (fileSizeBytes && premiumPossible)
		? std::clamp(
			float64(((fileSizeBytes / uint64(1024 * 1024)) + 499) / 1000),
			defaultGb,
			premiumGb)
		: showLimit;
	const auto gb = [](int count) {
		return tr::lng_file_size_limit(tr::now, lt_count, count);
	};

	auto text = rpl::combine(
		tr::lng_file_size_limit1(
			lt_size,
			rpl::single(Ui::Text::Bold(gb(showLimit))),
			Ui::Text::RichLangValue),
		(!premiumPossible
			? rpl::single(TextWithEntities())
			: tr::lng_file_size_limit2(
				lt_size,
				rpl::single(Ui::Text::Bold(gb(premiumGb))),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return a.append(QChar(' ')).append(std::move(b));
	});

	SimpleLimitBox(
		box,
		stOverride,
		session,
		premiumPossible,
		tr::lng_file_size_limit_title(),
		std::move(text),
		"upload_max_fileparts",
		{
			defaultGb,
			current,
			(tooLarge ? showLimit * 2 : premiumGb),
			&st::premiumIconFiles,
			tr::lng_file_size_limit
		});
}

void AccountsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto defaultLimit = Main::Domain::kMaxAccounts;
	const auto premiumLimit = Main::Domain::kPremiumMaxAccounts;

	using Args = Ui::Premium::AccountsRowArgs;
	const auto accounts = session->domain().orderedAccounts();
	auto promotePossible = ranges::views::all(
		accounts
	) | ranges::views::filter([&](not_null<Main::Account*> account) {
		return account->sessionExists()
			&& !account->session().premium()
			&& account->session().premiumPossible();
	}) | ranges::views::transform([&](not_null<Main::Account*> account) {
		const auto user = account->session().user();
		return Args::Entry{ user->name(), PaintUserpicCallback(user, false)};
	}) | ranges::views::take(defaultLimit) | ranges::to_vector;

	const auto premiumPossible = !promotePossible.empty();
	const auto current = int(accounts.size());

	auto text = rpl::combine(
		tr::lng_accounts_limit1(
			lt_count,
			rpl::single<float64>(current),
			Ui::Text::RichLangValue),
		((!premiumPossible || current > premiumLimit)
			? rpl::single(TextWithEntities())
			: tr::lng_accounts_limit2(Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return b.text.isEmpty()
			? a
			: a.append(QChar(' ')).append(std::move(b));
	});

	box->setWidth(st::boxWideWidth);

	const auto top = box->verticalLayout();
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(0);

	Settings::AddSkip(top, st::premiumInfographicPadding.top());
	Ui::Premium::AddBubbleRow(
		top,
		st::defaultPremiumBubble,
		BoxShowFinishes(box),
		0,
		current,
		(!premiumPossible
			? (current * 2)
			: (current > defaultLimit)
			? (current + 1)
			: (defaultLimit * 2)),
		premiumPossible,
		std::nullopt,
		&st::premiumIconAccounts);
	Settings::AddSkip(top, st::premiumLineTextSkip);
	if (premiumPossible) {
		Ui::Premium::AddLimitRow(
			top,
			st::defaultPremiumLimits,
			(QString::number(std::max(current, defaultLimit) + 1)
				+ ((current + 1 == premiumLimit) ? "" : "+")),
			QString::number(defaultLimit));
		Settings::AddSkip(top, st::premiumInfographicPadding.bottom());
	}
	box->setTitle(tr::lng_accounts_limit_title());

	auto padding = st::boxPadding;
	padding.setTop(padding.bottom());
	top->add(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::aboutRevokePublicLabel),
		padding);

	if (!premiumPossible || current > premiumLimit) {
		box->addButton(tr::lng_box_ok(), [=] {
			box->closeBox();
		});
		return;
	}
	auto switchingLifetime = std::make_shared<rpl::lifetime>();
	box->addButton(tr::lng_continue(), [=]() mutable {
		const auto ref = QString();

		const auto wasAccount = &session->account();
		const auto nowAccount = accounts[group->value()];
		if (wasAccount == nowAccount) {
			Settings::ShowPremium(session, ref);
			return;
		}

		if (*switchingLifetime) {
			return;
		}
		*switchingLifetime = session->domain().activeSessionChanges(
		) | rpl::start_with_next([=](Main::Session *session) mutable {
			if (session) {
				Settings::ShowPremium(session, ref);
			}
			if (switchingLifetime) {
				base::take(switchingLifetime)->destroy();
			}
		});
		session->domain().activate(nowAccount);
	});

	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});

	auto args = Args{
		.group = group,
		.st = st::premiumAccountsCheckbox,
		.stName = st::shareBoxListItem.nameStyle,
		.stNameFg = st::shareBoxListItem.nameFg,
		.entries = std::move(promotePossible),
	};
	if (!args.entries.empty()) {
		box->addSkip(st::premiumAccountsPadding.top());
		Ui::Premium::AddAccountsRow(box->verticalLayout(), std::move(args));
		box->addSkip(st::premiumAccountsPadding.bottom());
	}
}

QString LimitsPremiumRef(const QString &addition) {
	return "double_limits__" + addition;
}
