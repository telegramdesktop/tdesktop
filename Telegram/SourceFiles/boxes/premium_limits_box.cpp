/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/premium_limits_box.h"

#include "ui/controls/peer_list_dummy.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/text/text_utilities.h"
#include "ui/toasts/common_toasts.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace {

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
	not_null<QWidget*> peerListToastParent() override;
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

not_null<QWidget*> InactiveDelegate::peerListToastParent() {
	Unexpected("...InactiveDelegate::peerListToastParent");
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
	delegate()->peerListSetTitle(tr::lng_blocked_list_title());
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	delegate()->peerListRefreshRows();

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

[[nodiscard]] float64 Limit(
		not_null<Main::Session*> session,
		const QString &key,
		double fallback) {
	return session->account().appConfig().get<double>(key, fallback);
}

void SimpleLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text,
		bool premium,
		bool fixed = false) {
	box->setWidth(st::boxWideWidth);

	const auto top = fixed
		? box->setPinnedToTopContent(object_ptr<Ui::VerticalLayout>(box))
		: box->verticalLayout();
	top->add(
		object_ptr<Ui::CenterWrap<>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(title),
				st::changePhoneTitle)),
		st::changePhoneTitlePadding);

	top->add(
		object_ptr<Ui::CenterWrap<>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(text),
				st::changePhoneDescription)),
		st::changePhoneDescriptionPadding);

	if (premium) {
		box->addButton(tr::lng_box_ok(), [=] {
			box->closeBox();
		});
	} else {
		box->addButton(tr::lng_limits_increase(), [=] {
			Ui::ShowMultilineToast({
				.text = { u"Premium!"_q },
			});
		});
	}
}

void SimplePinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const QString &keyDefault,
		int limitDefault,
		const QString &keyPremium,
		int limitPremium) {
	const auto premium = session->user()->isPremium();

	auto text = rpl::combine(
		tr::lng_filter_pin_limit1(
			lt_count,
			rpl::single(Limit(
				session,
				(premium ? keyPremium : keyDefault),
				premium ? limitPremium : limitDefault)),
			Ui::Text::RichLangValue),
		(premium
			? rpl::single(TextWithEntities())
			: tr::lng_filter_pin_limit2(
				lt_count,
				rpl::single(Limit(session, keyPremium, limitPremium)),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return b.text.isEmpty()
			? a
			: a.append(QChar(' ')).append(std::move(b));
	});
	SimpleLimitBox(
		box,
		session,
		tr::lng_filter_pin_limit_title(),
		std::move(text),
		premium);
}

} // namespace

void ChannelsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto premium = session->user()->isPremium();

	auto text = rpl::combine(
		tr::lng_channels_limit1(
			lt_count,
			rpl::single(Limit(
				session,
				(premium
					? "channels_limit_premium"
					: "channels_limit_default"),
				premium ? 1000 : 500)),
			Ui::Text::RichLangValue),
		(premium
			? tr::lng_channels_limit2_final(Ui::Text::RichLangValue)
			: tr::lng_channels_limit2(
				lt_count,
				rpl::single(Limit(session, "channels_limit_premium", 1000)),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return a.append(QChar(' ')).append(std::move(b));
	});

	SimpleLimitBox(
		box,
		session,
		tr::lng_channels_limit_title(),
		std::move(text),
		premium,
		true);

	const auto delegate = box->lifetime().make_state<InactiveDelegate>();
	const auto controller = box->lifetime().make_state<InactiveController>(
		session);

	const auto content = box->addRow(
		object_ptr<PeerListContent>(box, controller),
		{});
	delegate->setContent(content);
	controller->setDelegate(delegate);

	const auto count = 50;
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
			Ui::ShowMultilineToast({
				.text = { tr::lng_channels_leave_done(tr::now) },
			});
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
				Ui::ShowMultilineToast({
					.text = { u"Premium!"_q },
				});
			});
		}
	}, box->lifetime());
}

void PublicLinksLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto premium = session->user()->isPremium();

	auto text = rpl::combine(
		tr::lng_links_limit1(
			lt_count,
			rpl::single(Limit(
				session,
				(premium
					? "channels_public_limit_premium"
					: "channels_public_limit_default"),
				premium ? 20 : 10)),
			Ui::Text::RichLangValue),
		(premium
			? tr::lng_links_limit2_final(Ui::Text::RichLangValue)
			: tr::lng_links_limit2(
				lt_count,
				rpl::single(Limit(
					session,
					"channels_public_limit_premium",
					20)),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return a.append(QChar(' ')).append(std::move(b));
	});

	SimpleLimitBox(
		box,
		session,
		tr::lng_links_limit_title(),
		std::move(text),
		premium);
}

void FilterChatsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto premium = session->user()->isPremium();

	auto text = rpl::combine(
		tr::lng_filter_chats_limit1(
			lt_count,
			rpl::single(
				Limit(
					session,
					(premium
						? "dialog_filters_chats_limit_premium"
						: "dialog_filters_chats_limit_default"),
					premium ? 200 : 100)),
			Ui::Text::RichLangValue),
		(premium
			? rpl::single(TextWithEntities())
			: tr::lng_filter_chats_limit2(
				lt_count,
				rpl::single(Limit(
					session,
					"dialog_filters_chats_limit_premium",
					200)),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return b.text.isEmpty()
			? a
			: a.append(QChar(' ')).append(std::move(b));
	});

	SimpleLimitBox(
		box,
		session,
		tr::lng_filter_chats_limit_title(),
		std::move(text),
		premium);
}

void FiltersLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto premium = session->user()->isPremium();

	auto text = rpl::combine(
		tr::lng_filters_limit1(
			lt_count,
			rpl::single(Limit(
				session,
				(premium
					? "dialog_filters_limit_premium"
					: "dialog_filters_limit_default"),
				premium ? 20 : 10)),
			Ui::Text::RichLangValue),
		(premium
			? rpl::single(TextWithEntities())
			: tr::lng_filters_limit2(
				lt_count,
				rpl::single(
					Limit(session, "dialog_filters_limit_premium", 20)),
				Ui::Text::RichLangValue))
	) | rpl::map([](TextWithEntities &&a, TextWithEntities &&b) {
		return b.text.isEmpty()
			? a
			: a.append(QChar(' ')).append(std::move(b));
	});
	SimpleLimitBox(
		box,
		session,
		tr::lng_filters_limit_title(),
		std::move(text),
		premium);
}

void FilterPinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	SimplePinsLimitBox(
		box,
		session,
		"dialog_filters_pinned_limit_default",
		100,
		"dialog_filters_pinned_limit_premium",
		200);
}

void PinsLimitBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	SimplePinsLimitBox(
		box,
		session,
		"dialogs_pinned_limit_default",
		5,
		"dialogs_pinned_limit_premium",
		10);
}
