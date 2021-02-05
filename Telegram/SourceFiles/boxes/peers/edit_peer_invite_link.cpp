/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_invite_link.h"

#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_histories.h"
#include "main/main_session.h"
#include "api/api_invite_links.h"
#include "base/unixtime.h"
#include "apiwrap.h"
#include "ui/controls/invite_link_buttons.h"
#include "ui/controls/invite_link_label.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/popup_menu.h"
#include "ui/abstract_button.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "boxes/share_box.h"
#include "history/view/history_view_group_call_tracker.h" // GenerateUs...
#include "history/history_message.h" // GetErrorTextForSending.
#include "history/history.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_box.h"
#include "mainwindow.h"
#include "facades.h" // Ui::showPerProfile.
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"
#include "settings/settings_common.h"
#include "mtproto/sender.h"
#include "styles/style_info.h"

#include <QtGui/QGuiApplication>

namespace {

constexpr auto kFirstPage = 20;
constexpr auto kPerPage = 100;

using LinkData = Api::InviteLink;

class Controller final : public PeerListController {
public:
	Controller(not_null<PeerData*> peer, const LinkData &data);

	void prepare() override;
	void loadMoreRows() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

private:
	void appendSlice(const Api::JoinedByLinkSlice &slice);
	[[nodiscard]] object_ptr<Ui::RpWidget> prepareHeader();

	const not_null<PeerData*> _peer;
	LinkData _data;

	mtpRequestId _requestId = 0;
	std::optional<Api::JoinedByLinkUser> _lastUser;
	bool _allLoaded = false;

	MTP::Sender _api;
	rpl::lifetime _lifetime;

};

class SingleRowController final : public PeerListController {
public:
	SingleRowController(not_null<PeerData*> peer, TimeId date);

	void prepare() override;
	void loadMoreRows() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

private:
	const not_null<PeerData*> _peer;
	TimeId _date = 0;

};

void AddHeaderBlock(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		const LinkData &data,
		TimeId now) {
	const auto link = data.link;
	const auto weak = Ui::MakeWeak(container);
	const auto copyLink = crl::guard(weak, [=] {
		CopyInviteLink(link);
	});
	const auto shareLink = crl::guard(weak, [=] {
		ShareInviteLinkBox(peer, link);
	});
	const auto revokeLink = crl::guard(weak, [=] {
		RevokeLink(peer, data.admin, data.link);
	});

	const auto createMenu = [=] {
		auto result = base::make_unique_q<Ui::PopupMenu>(container);
		result->addAction(
			tr::lng_group_invite_context_copy(tr::now),
			copyLink);
		result->addAction(
			tr::lng_group_invite_context_share(tr::now),
			shareLink);
		result->addAction(
			tr::lng_group_invite_context_revoke(tr::now),
			revokeLink);
		return result;
	};

	const auto prefix = qstr("https://");
	const auto label = container->lifetime().make_state<Ui::InviteLinkLabel>(
		container,
		rpl::single(link.startsWith(prefix)
			? link.mid(prefix.size())
			: link),
		createMenu);
	container->add(
		label->take(),
		st::inviteLinkFieldPadding);

	label->clicks(
	) | rpl::start_with_next(copyLink, label->lifetime());

	if ((data.expireDate <= 0 || now < data.expireDate)
		&& (data.usageLimit <= 0 || data.usage < data.usageLimit)) {
		AddCopyShareLinkButtons(
			container,
			copyLink,
			shareLink);
	}
}

void AddHeader(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		const LinkData &data) {
	using namespace Settings;

	if (!data.revoked && !data.permanent) {
		const auto now = base::unixtime::now();
		AddHeaderBlock(container, peer, data, now);
		AddSkip(container, st::inviteLinkJoinedRowPadding.bottom() * 2);
		if (data.expireDate > 0) {
			AddDividerText(
				container,
				(data.expireDate > now
					? tr::lng_group_invite_expires_at(
						lt_when,
						rpl::single(langDateTime(
							base::unixtime::parse(data.expireDate))))
					: tr::lng_group_invite_expired_already()));
		} else {
			AddDivider(container);
		}
	}
	AddSkip(container);
	AddSubsectionTitle(
		container,
		tr::lng_group_invite_created_by());

	const auto delegate = container->lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = container->lifetime().make_state<
		SingleRowController
	>(data.admin, data.date);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);
}

Controller::Controller(not_null<PeerData*> peer, const LinkData &data)
: _peer(peer)
, _data(data)
, _api(&_peer->session().api().instance()) {
}

object_ptr<Ui::RpWidget> Controller::prepareHeader() {
	using namespace Settings;

	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();
	AddHeader(container, _peer, _data);
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(
		container,
		(_data.usage
			? tr::lng_group_invite_joined(
				lt_count,
				rpl::single(float64(_data.usage)))
			: tr::lng_group_invite_no_joined()));
	return result;
}

void Controller::prepare() {
	delegate()->peerListSetAboveWidget(prepareHeader());
	_allLoaded = (_data.usage == 0);
	const auto &inviteLinks = _peer->session().api().inviteLinks();
	const auto slice = inviteLinks.joinedFirstSliceLoaded(_peer, _data.link);
	if (slice) {
		appendSlice(*slice);
	}
	loadMoreRows();
}

void Controller::loadMoreRows() {
	if (_requestId || _allLoaded) {
		return;
	}
	_requestId = _api.request(MTPmessages_GetChatInviteImporters(
		_peer->input,
		MTP_string(_data.link),
		MTP_int(_lastUser ? _lastUser->date : 0),
		_lastUser ? _lastUser->user->inputUser : MTP_inputUserEmpty(),
		MTP_int(_lastUser ? kPerPage : kFirstPage)
	)).done([=](const MTPmessages_ChatInviteImporters &result) {
		_requestId = 0;
		auto slice = Api::ParseJoinedByLinkSlice(_peer, result);
		_allLoaded = slice.users.empty();
		appendSlice(slice);
	}).fail([=](const RPCError &error) {
		_requestId = 0;
		_allLoaded = true;
	}).send();
}

void Controller::appendSlice(const Api::JoinedByLinkSlice &slice) {
	for (const auto &user : slice.users) {
		_lastUser = user;
		delegate()->peerListAppendRow(
			std::make_unique<PeerListRow>(user.user));
	}
	delegate()->peerListRefreshRows();
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	Ui::showPeerProfile(row->peer());
}

Main::Session &Controller::session() const {
	return _peer->session();
}

SingleRowController::SingleRowController(
	not_null<PeerData*> peer,
	TimeId date)
: _peer(peer)
, _date(date) {
}

void SingleRowController::prepare() {
	auto row = std::make_unique<PeerListRow>(_peer);
	row->setCustomStatus(langDateTime(base::unixtime::parse(_date)));
	delegate()->peerListAppendRow(std::move(row));
	delegate()->peerListRefreshRows();
}

void SingleRowController::loadMoreRows() {
}

void SingleRowController::rowClicked(not_null<PeerListRow*> row) {
	Ui::showPeerProfile(row->peer());
}

Main::Session &SingleRowController::session() const {
	return _peer->session();
}

} // namespace

void AddPermanentLinkBlock(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		rpl::producer<Api::InviteLink> fromList) {
	struct LinkData {
		QString link;
		int usage = 0;
	};
	const auto value = container->lifetime().make_state<
		rpl::variable<LinkData>
	>();
	if (admin->isSelf()) {
		*value = peer->session().changes().peerFlagsValue(
			peer,
			Data::PeerUpdate::Flag::InviteLinks
		) | rpl::map([=] {
			const auto &links = peer->session().api().inviteLinks().myLinks(
				peer).links;
			const auto link = links.empty() ? nullptr : &links.front();
			return (link && link->permanent && !link->revoked)
				? LinkData{ link->link, link->usage }
				: LinkData();
		});
	} else {
		*value = std::move(
			fromList
		) | rpl::map([](const Api::InviteLink &link) {
			return LinkData{ link.link, link.usage };
		});
	}
	const auto weak = Ui::MakeWeak(container);
	const auto copyLink = crl::guard(weak, [=] {
		if (const auto current = value->current(); !current.link.isEmpty()) {
			CopyInviteLink(current.link);
		}
	});
	const auto shareLink = crl::guard(weak, [=] {
		if (const auto current = value->current(); !current.link.isEmpty()) {
			ShareInviteLinkBox(peer, current.link);
		}
	});
	const auto revokeLink = crl::guard(weak, [=] {
		const auto box = std::make_shared<QPointer<ConfirmBox>>();
		const auto done = crl::guard(weak, [=] {
			const auto close = [=] {
				if (*box) {
					(*box)->closeBox();
				}
			};
			peer->session().api().inviteLinks().revokePermanent(
				peer,
				admin,
				value->current().link,
				close);
		});
		*box = Ui::show(
			Box<ConfirmBox>(tr::lng_group_invite_about_new(tr::now), done),
			Ui::LayerOption::KeepOther);
	});

	auto link = value->value(
	) | rpl::map([=](const LinkData &data) {
		const auto prefix = qstr("https://");
		return data.link.startsWith(prefix)
			? data.link.mid(prefix.size())
			: data.link;
	});
	const auto createMenu = [=] {
		auto result = base::make_unique_q<Ui::PopupMenu>(container);
		result->addAction(
			tr::lng_group_invite_context_copy(tr::now),
			copyLink);
		result->addAction(
			tr::lng_group_invite_context_share(tr::now),
			shareLink);
		result->addAction(
			tr::lng_group_invite_context_revoke(tr::now),
			revokeLink);
		return result;
	};
	const auto label = container->lifetime().make_state<Ui::InviteLinkLabel>(
		container,
		std::move(link),
		createMenu);
	container->add(
		label->take(),
		st::inviteLinkFieldPadding);

	label->clicks(
	) | rpl::start_with_next(copyLink, label->lifetime());

	AddCopyShareLinkButtons(
		container,
		copyLink,
		shareLink);

	struct JoinedState {
		QImage cachedUserpics;
		std::vector<HistoryView::UserpicInRow> list;
		int count = 0;
		bool allUserpicsLoaded = false;
		rpl::variable<Ui::JoinedCountContent> content;
		rpl::lifetime lifetime;
	};
	const auto state = container->lifetime().make_state<JoinedState>();
	const auto push = [=] {
		HistoryView::GenerateUserpicsInRow(
			state->cachedUserpics,
			state->list,
			st::inviteLinkUserpics,
			0);
		state->allUserpicsLoaded = ranges::all_of(
			state->list,
			[](const HistoryView::UserpicInRow &element) {
				return !element.peer->hasUserpic() || element.view->image();
			});
		state->content = Ui::JoinedCountContent{
			.count = state->count,
			.userpics = state->cachedUserpics
		};
	};
	value->value(
	) | rpl::map([=](const LinkData &data) {
		return peer->session().api().inviteLinks().joinedFirstSliceValue(
			peer,
			data.link,
			data.usage);
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](const Api::JoinedByLinkSlice &slice) {
		auto list = std::vector<HistoryView::UserpicInRow>();
		list.reserve(slice.users.size());
		for (const auto &item : slice.users) {
			const auto i = ranges::find(
				state->list,
				item.user,
				&HistoryView::UserpicInRow::peer);
			if (i != end(state->list)) {
				list.push_back(std::move(*i));
			} else {
				list.push_back({ item.user });
			}
		}
		state->count = slice.count;
		state->list = std::move(list);
		push();
	}, state->lifetime);

	peer->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return !state->allUserpicsLoaded;
	}) | rpl::start_with_next([=] {
		auto pushing = false;
		state->allUserpicsLoaded = true;
		for (const auto &element : state->list) {
			if (!element.peer->hasUserpic()) {
				continue;
			} else if (element.peer->userpicUniqueKey(element.view)
				!= element.uniqueKey) {
				pushing = true;
			} else if (!element.view->image()) {
				state->allUserpicsLoaded = false;
			}
		}
		if (pushing) {
			push();
		}
	}, state->lifetime);

	Ui::AddJoinedCountButton(
		container,
		state->content.value(),
		st::inviteLinkJoinedRowPadding
	)->setClickedCallback([=] {
	});

	container->add(object_ptr<Ui::SlideWrap<Ui::FixedHeightWidget>>(
		container,
		object_ptr<Ui::FixedHeightWidget>(
			container,
			st::inviteLinkJoinedRowPadding.bottom()))
	)->setDuration(0)->toggleOn(state->content.value(
	) | rpl::map([=](const Ui::JoinedCountContent &content) {
		return (content.count <= 0);
	}));
}

void CopyInviteLink(const QString &link) {
	QGuiApplication::clipboard()->setText(link);
	Ui::Toast::Show(tr::lng_group_invite_copied(tr::now));
}

void ShareInviteLinkBox(not_null<PeerData*> peer, const QString &link) {
	const auto session = &peer->session();
	const auto sending = std::make_shared<bool>();
	const auto box = std::make_shared<QPointer<ShareBox>>();

	auto copyCallback = [=] {
		QGuiApplication::clipboard()->setText(link);
		Ui::Toast::Show(tr::lng_group_invite_copied(tr::now));
	};
	auto submitCallback = [=](
			std::vector<not_null<PeerData*>> &&result,
			TextWithTags &&comment,
			Api::SendOptions options) {
		if (*sending || result.empty()) {
			return;
		}

		const auto error = [&] {
			for (const auto peer : result) {
				const auto error = GetErrorTextForSending(
					peer,
					{},
					comment);
				if (!error.isEmpty()) {
					return std::make_pair(error, peer);
				}
			}
			return std::make_pair(QString(), result.front());
		}();
		if (!error.first.isEmpty()) {
			auto text = TextWithEntities();
			if (result.size() > 1) {
				text.append(
					Ui::Text::Bold(error.second->name)
				).append("\n\n");
			}
			text.append(error.first);
			Ui::show(
				Box<InformBox>(text),
				Ui::LayerOption::KeepOther);
			return;
		}

		*sending = true;
		if (!comment.text.isEmpty()) {
			comment.text = link + "\n" + comment.text;
			const auto add = link.size() + 1;
			for (auto &tag : comment.tags) {
				tag.offset += add;
			}
		}
		const auto owner = &peer->owner();
		auto &api = peer->session().api();
		auto &histories = owner->histories();
		const auto requestType = Data::Histories::RequestType::Send;
		for (const auto peer : result) {
			const auto history = owner->history(peer);
			auto message = ApiWrap::MessageToSend(history);
			message.textWithTags = comment;
			message.action.options = options;
			message.action.clearDraft = false;
			api.sendMessage(std::move(message));
		}
		Ui::Toast::Show(tr::lng_share_done(tr::now));
		if (*box) {
			(*box)->closeBox();
		}
	};
	auto filterCallback = [](PeerData *peer) {
		return peer->canWrite();
	};
	*box = Ui::show(
		Box<ShareBox>(
			App::wnd()->sessionController(),
			std::move(copyCallback),
			std::move(submitCallback),
			std::move(filterCallback)),
		Ui::LayerOption::KeepOther);
}

void RevokeLink(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link) {
	const auto box = std::make_shared<QPointer<ConfirmBox>>();
	const auto revoke = [=] {
		const auto done = [=](const LinkData &data) {
			if (*box) {
				(*box)->closeBox();
			}
		};
		peer->session().api().inviteLinks().revoke(peer, admin, link, done);
	};
	*box = Ui::show(
		Box<ConfirmBox>(
			tr::lng_group_invite_revoke_about(tr::now),
			revoke),
		Ui::LayerOption::KeepOther);
}

void ShowInviteLinkBox(
		not_null<PeerData*> peer,
		const Api::InviteLink &link) {
	auto initBox = [=](not_null<Ui::BoxContent*> box) {
		box->setTitle((link.permanent && !link.revoked)
			? tr::lng_manage_peer_link_permanent()
			: tr::lng_manage_peer_link_invite());
		peer->session().api().inviteLinks().updates(
			peer
		) | rpl::start_with_next([=](const Api::InviteLinkUpdate &update) {
			if (update.was == link.link
				&& (!update.now || (!link.revoked && update.now->revoked))) {
				box->closeBox();
			}
		}, box->lifetime());
		box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
	};
	if (link.usage > 0) {
		Ui::show(
			Box<PeerListBox>(
				std::make_unique<Controller>(peer, link),
				std::move(initBox)),
			Ui::LayerOption::KeepOther);
	} else {
		Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
			initBox(box);
			const auto container = box->verticalLayout();
			AddHeader(container, peer, link);
		}), Ui::LayerOption::KeepOther);
	}
}
