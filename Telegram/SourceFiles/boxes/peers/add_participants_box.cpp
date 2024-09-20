/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/add_participants_box.h"

#include "api/api_chat_participants.h"
#include "api/api_invite_links.h"
#include "boxes/peers/edit_participant_box.h"
#include "boxes/peers/edit_peer_type_box.h"
#include "boxes/peers/replace_boost_box.h"
#include "boxes/max_invite_box.h"
#include "lang/lang_keys.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h"
#include "history/history.h"
#include "dialogs/dialogs_indexed_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/show_or_premium_box.h"
#include "ui/effects/premium_graphics.h"
#include "ui/text/text_utilities.h" // Ui::Text::RichLangValue
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/painter.h"
#include "base/unixtime.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "settings/settings_premium.h"
#include "window/window_session_controller.h"
#include "info/profile/info_profile_icon.h"
#include "apiwrap.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace {

constexpr auto kParticipantsFirstPageCount = 16;
constexpr auto kParticipantsPerPage = 200;
constexpr auto kUserpicsLimit = 3;

class ForbiddenRow final : public PeerListRow {
public:
	ForbiddenRow(not_null<PeerData*> peer, bool locked);

	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

private:
	const bool _locked = false;
	QImage _disabledFrame;
	InMemoryKey _userpicKey;
	int _paletteVersion = 0;

};

class InviteForbiddenController final : public PeerListController {
public:
	InviteForbiddenController(
		not_null<PeerData*> peer,
		ForbiddenInvites forbidden);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] bool canInvite() const {
		return _can;
	}
	[[nodiscard]] rpl::producer<int> selectedValue() const {
		return _selected.value();
	}

	void send(
		std::vector<not_null<PeerData*>> list,
		Ui::ShowPtr show,
		Fn<void()> close);

private:
	void appendRow(not_null<UserData*> user);
	[[nodiscard]] std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) const;
	[[nodiscard]] bool canInvite(not_null<PeerData*> peer) const;

	void setSimpleCover();
	void setComplexCover();

	const not_null<PeerData*> _peer;
	const ForbiddenInvites _forbidden;
	const std::vector<not_null<UserData*>> &_users;
	const bool _can = false;
	rpl::variable<int> _selected;
	bool _sending = false;

};

base::flat_set<not_null<UserData*>> GetAlreadyInFromPeer(PeerData *peer) {
	if (!peer) {
		return {};
	}
	if (const auto chat = peer->asChat()) {
		return chat->participants;
	} else if (const auto channel = peer->asChannel()) {
		if (channel->isMegagroup() && channel->canViewMembers()) {
			const auto &participants = channel->mgInfo->lastParticipants;
			return { participants.cbegin(), participants.cend() };
		}
	}
	return {};
}

void FillUpgradeToPremiumCover(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> peer,
		const ForbiddenInvites &forbidden) {
	const auto noneCanSend = (forbidden.premiumAllowsWrite.size()
		== forbidden.users.size());
	const auto &userpicUsers = (forbidden.premiumAllowsInvite.empty()
		|| noneCanSend)
		? forbidden.premiumAllowsWrite
		: forbidden.premiumAllowsInvite;
	Assert(!userpicUsers.empty());

	auto userpicPeers = userpicUsers | ranges::views::transform([](auto u) {
		return not_null<PeerData*>(u);
	}) | ranges::to_vector;
	container->add(object_ptr<Ui::PaddingWrap<>>(
		container,
		CreateUserpicsWithMoreBadge(
			container,
			rpl::single(std::move(userpicPeers)),
			kUserpicsLimit),
		st::inviteForbiddenUserpicsPadding)
	)->entity()->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto users = int(userpicUsers.size());
	const auto names = std::min(users, kUserpicsLimit);
	const auto remaining = std::max(users - kUserpicsLimit, 0);
	auto text = TextWithEntities();
	for (auto i = 0; i != names; ++i) {
		const auto name = userpicUsers[i]->shortName();
		if (text.empty()) {
			text = Ui::Text::Bold(name);
		} else if (i == names - 1 && !remaining) {
			text = tr::lng_invite_upgrade_users_few(
				tr::now,
				lt_users,
				text,
				lt_last,
				Ui::Text::Bold(name),
				Ui::Text::RichLangValue);
		} else {
			text.append(", ").append(Ui::Text::Bold(name));
		}
	}
	if (remaining > 0) {
		text = tr::lng_invite_upgrade_users_many(
			tr::now,
			lt_count,
			remaining,
			lt_users,
			text,
			Ui::Text::RichLangValue);
	}
	const auto inviteOnly = !forbidden.premiumAllowsInvite.empty()
		&& (forbidden.premiumAllowsWrite.size() != forbidden.users.size());
	text = (peer->isBroadcast()
		? (inviteOnly
			? tr::lng_invite_upgrade_channel_invite
			: tr::lng_invite_upgrade_channel_write)
		: (inviteOnly
			? tr::lng_invite_upgrade_group_invite
			: tr::lng_invite_upgrade_group_write))(
				tr::now,
				lt_count,
				int(userpicUsers.size()),
				lt_users,
				text,
				Ui::Text::RichLangValue);
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(text),
			st::inviteForbiddenInfo),
		st::inviteForbiddenInfoPadding);
}

void SimpleForbiddenBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		const ForbiddenInvites &forbidden) {
	box->setTitle(tr::lng_invite_upgrade_title());
	box->setWidth(st::boxWideWidth);
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	auto sshow = Main::MakeSessionShow(box->uiShow(), &peer->session());
	const auto container = box->verticalLayout();
	FillUpgradeToPremiumCover(container, sshow, peer, forbidden);

	const auto &stButton = st::premiumGiftBox;
	box->setStyle(stButton);
	auto raw = Settings::CreateSubscribeButton(
		sshow,
		ChatHelpers::ResolveWindowDefault(),
		{
			.parent = container,
			.computeRef = [] { return u"invite_privacy"_q; },
			.text = tr::lng_messages_privacy_premium_button(),
			.showPromo = true,
		});
	auto button = object_ptr<Ui::GradientButton>::fromRaw(raw);
	button->resizeToWidth(st::boxWideWidth
		- stButton.buttonPadding.left()
		- stButton.buttonPadding.right());
	box->setShowFinishedCallback([raw = button.data()] {
		raw->startGlareAnimation();
	});
	box->addButton(std::move(button));

	Data::AmPremiumValue(
		&peer->session()
	) | rpl::skip(1) | rpl::start_with_next([=] {
		box->closeBox();
	}, box->lifetime());
}

InviteForbiddenController::InviteForbiddenController(
	not_null<PeerData*> peer,
	ForbiddenInvites forbidden)
: _peer(peer)
, _forbidden(std::move(forbidden))
, _users(_forbidden.users)
, _can(peer->isChat()
	? peer->asChat()->canHaveInviteLink()
	: peer->asChannel()->canHaveInviteLink())
, _selected(_can
	? (int(_users.size()) - int(_forbidden.premiumAllowsWrite.size()))
	: 0) {
}

Main::Session &InviteForbiddenController::session() const {
	return _peer->session();
}

ForbiddenRow::ForbiddenRow(not_null<PeerData*> peer, bool locked)
: PeerListRow(peer)
, _locked(locked) {
	if (_locked) {
		setCustomStatus(tr::lng_invite_status_disabled(tr::now));
	}
}

PaintRoundImageCallback ForbiddenRow::generatePaintUserpicCallback(
		bool forceRound) {
	const auto peer = this->peer();
	const auto saved = peer->isSelf();
	const auto replies = peer->isRepliesChat();
	const auto verifyCodes = peer->isVerifyCodes();
	auto userpic = (saved || replies || verifyCodes)
		? Ui::PeerUserpicView()
		: ensureUserpicView();
	auto paint = [=](
			Painter &p,
			int x,
			int y,
			int outerWidth,
			int size) mutable {
		peer->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
	};
	if (!_locked) {
		return paint;
	}
	return [=](
			Painter &p,
			int x,
			int y,
			int outerWidth,
			int size) mutable {
		const auto wide = size + style::ConvertScale(3);
		const auto full = QSize(wide, wide) * style::DevicePixelRatio();
		auto repaint = false;
		if (_disabledFrame.size() != full) {
			repaint = true;
			_disabledFrame = QImage(
				full,
				QImage::Format_ARGB32_Premultiplied);
			_disabledFrame.setDevicePixelRatio(style::DevicePixelRatio());
		} else {
			repaint = (_paletteVersion != style::PaletteVersion())
				|| (!saved
					&& !replies
					&& !verifyCodes
					&& (_userpicKey != peer->userpicUniqueKey(userpic)));
		}
		if (repaint) {
			_paletteVersion = style::PaletteVersion();
			_userpicKey = peer->userpicUniqueKey(userpic);

			_disabledFrame.fill(Qt::transparent);
			auto p = Painter(&_disabledFrame);
			paint(p, 0, 0, wide, size);

			auto hq = PainterHighQualityEnabler(p);
			p.setBrush(st::boxBg);
			p.setPen(Qt::NoPen);
			const auto lock = st::inviteForbiddenLockIcon.size();
			const auto stroke = style::ConvertScale(2);
			const auto inner = QRect(
				size + (stroke / 2) - lock.width(),
				size + (stroke / 2) - lock.height(),
				lock.width(),
				lock.height());
			const auto half = stroke / 2.;
			const auto rect = QRectF(inner).marginsAdded(
				{ half, half, half, half });
			auto pen = st::boxBg->p;
			pen.setWidthF(stroke);
			p.setPen(pen);
			p.setBrush(st::inviteForbiddenLockBg);
			p.drawEllipse(rect);

			st::inviteForbiddenLockIcon.paintInCenter(p, inner);
		}
		p.drawImage(x, y, _disabledFrame);
	};
}

void InviteForbiddenController::setSimpleCover() {
	delegate()->peerListSetTitle(
		_can ? tr::lng_profile_add_via_link() : tr::lng_via_link_cant());
	const auto broadcast = _peer->isBroadcast();
	const auto count = int(_users.size());
	const auto phraseCounted = !_can
		? tr::lng_via_link_cant_many
		: broadcast
		? tr::lng_via_link_channel_many
		: tr::lng_via_link_group_many;
	const auto phraseNamed = !_can
		? tr::lng_via_link_cant_one
		: broadcast
		? tr::lng_via_link_channel_one
		: tr::lng_via_link_group_one;
	auto text = (count != 1)
		? phraseCounted(
			lt_count,
			rpl::single<float64>(count),
			Ui::Text::RichLangValue)
		: phraseNamed(
			lt_user,
			rpl::single(TextWithEntities{ _users.front()->name() }),
			Ui::Text::RichLangValue);
	delegate()->peerListSetAboveWidget(object_ptr<Ui::PaddingWrap<>>(
		(QWidget*)nullptr,
		object_ptr<Ui::FlatLabel>(
			(QWidget*)nullptr,
			std::move(text),
			st::requestPeerRestriction),
		st::boxRowPadding));
}

void InviteForbiddenController::setComplexCover() {
	delegate()->peerListSetTitle(tr::lng_invite_upgrade_title());

	auto cover = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = cover.data();
	const auto show = delegate()->peerListUiShow();
	FillUpgradeToPremiumCover(container, show, _peer, _forbidden);

	container->add(
		object_ptr<Ui::GradientButton>::fromRaw(
			Settings::CreateSubscribeButton(
				show,
				ChatHelpers::ResolveWindowDefault(),
				{
					.parent = container,
					.computeRef = [] { return u"invite_privacy"_q; },
					.text = tr::lng_messages_privacy_premium_button(),
				})),
				st::inviteForbiddenSubscribePadding);

	if (_forbidden.users.size() > _forbidden.premiumAllowsWrite.size()) {
		if (_can) {
			container->add(
				MakeShowOrLabel(container, tr::lng_invite_upgrade_or()),
				st::inviteForbiddenOrLabelPadding);
		}
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				(_can
					? tr::lng_invite_upgrade_via_title()
					: tr::lng_via_link_cant()),
				st::inviteForbiddenTitle),
			st::inviteForbiddenTitlePadding);

		const auto about = _can
			? (_peer->isBroadcast()
				? tr::lng_invite_upgrade_via_channel_about
				: tr::lng_invite_upgrade_via_group_about)(
					tr::now,
					Ui::Text::WithEntities)
			: (_forbidden.users.size() == 1
				? tr::lng_via_link_cant_one(
					tr::now,
					lt_user,
					TextWithEntities{ _forbidden.users.front()->shortName() },
					Ui::Text::RichLangValue)
				: tr::lng_via_link_cant_many(
					tr::now,
					lt_count,
					int(_forbidden.users.size()),
					Ui::Text::RichLangValue));
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::single(about),
				st::inviteForbiddenInfo),
			st::inviteForbiddenInfoPadding);
	}
	delegate()->peerListSetAboveWidget(std::move(cover));
}

void InviteForbiddenController::prepare() {
	if (session().premium()
		|| (_forbidden.premiumAllowsInvite.empty()
			&& _forbidden.premiumAllowsWrite.empty())) {
		setSimpleCover();
	} else {
		setComplexCover();
	}

	for (const auto &user : _users) {
		appendRow(user);
	}
	delegate()->peerListRefreshRows();
}

bool InviteForbiddenController::canInvite(not_null<PeerData*> peer) const {
	const auto user = peer->asUser();
	Assert(user != nullptr);

	return _can
		&& !ranges::contains(_forbidden.premiumAllowsWrite, not_null(user));
}

void InviteForbiddenController::rowClicked(not_null<PeerListRow*> row) {
	if (!canInvite(row->peer())) {
		return;
	}
	const auto checked = row->checked();
	delegate()->peerListSetRowChecked(row, !checked);
	_selected = _selected.current() + (checked ? -1 : 1);
}

void InviteForbiddenController::appendRow(not_null<UserData*> user) {
	if (!delegate()->peerListFindRow(user->id.value)) {
		auto row = createRow(user);
		const auto raw = row.get();
		delegate()->peerListAppendRow(std::move(row));
		if (canInvite(user)) {
			delegate()->peerListSetRowChecked(raw, true);
		}
	}
}

void InviteForbiddenController::send(
		std::vector<not_null<PeerData*>> list,
		Ui::ShowPtr show,
		Fn<void()> close) {
	if (_sending || list.empty()) {
		return;
	}
	_sending = true;
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto sendLink = [=] {
		const auto link = chat ? chat->inviteLink() : channel->inviteLink();
		if (link.isEmpty()) {
			return false;
		}
		auto &api = _peer->session().api();
		auto options = Api::SendOptions();
		for (const auto &to : list) {
			const auto history = to->owner().history(to);
			auto message = Api::MessageToSend(
				Api::SendAction(history, options));
			message.textWithTags = { link };
			message.action.clearDraft = false;
			api.sendMessage(std::move(message));
		}
		auto text = (list.size() == 1)
			? tr::lng_via_link_shared_one(
				tr::now,
				lt_user,
				TextWithEntities{ list.front()->name() },
				Ui::Text::RichLangValue)
			: tr::lng_via_link_shared_many(
				tr::now,
				lt_count,
				int(list.size()),
				Ui::Text::RichLangValue);
		close();
		show->showToast(std::move(text));
		return true;
	};
	const auto sendForFull = [=] {
		if (!sendLink()) {
			_peer->session().api().inviteLinks().create({
				_peer,
				[=](auto) {
					if (!sendLink()) {
						close();
					}
				},
			});
		}
	};
	if (_peer->isFullLoaded()) {
		sendForFull();
	} else if (!sendLink()) {
		_peer->session().api().requestFullPeer(_peer);
		_peer->session().changes().peerUpdates(
			_peer,
			Data::PeerUpdate::Flag::FullInfo
		) | rpl::start_with_next([=] {
			sendForFull();
		}, lifetime());
	}
}

std::unique_ptr<PeerListRow> InviteForbiddenController::createRow(
		not_null<UserData*> user) const {
	const auto locked = _can && !canInvite(user);
	return std::make_unique<ForbiddenRow>(user, locked);
}

} // namespace

AddParticipantsBoxController::AddParticipantsBoxController(
	not_null<Main::Session*> session)
: ContactsBoxController(session) {
}

AddParticipantsBoxController::AddParticipantsBoxController(
	not_null<PeerData*> peer)
: AddParticipantsBoxController(
	peer,
	GetAlreadyInFromPeer(peer)) {
}

AddParticipantsBoxController::AddParticipantsBoxController(
	not_null<PeerData*> peer,
	base::flat_set<not_null<UserData*>> &&alreadyIn)
: ContactsBoxController(&peer->session())
, _peer(peer)
, _alreadyIn(std::move(alreadyIn)) {
	if (needsInviteLinkButton()) {
		setStyleOverrides(&st::peerListWithInviteViaLink);
	}
	subscribeToMigration();
}

void AddParticipantsBoxController::subscribeToMigration() {
	Expects(_peer != nullptr);

	SubscribeToMigration(
		_peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) { _peer = channel; });
}

void AddParticipantsBoxController::rowClicked(not_null<PeerListRow*> row) {
	const auto premiumRequiredError = WritePremiumRequiredError;
	if (RecipientRow::ShowLockedError(this, row, premiumRequiredError)) {
		return;
	}
	const auto &serverConfig = session().serverConfig();
	auto count = fullCount();
	auto limit = _peer && (_peer->isChat() || _peer->isMegagroup())
		? serverConfig.megagroupSizeMax
		: serverConfig.chatSizeMax;
	if (count < limit || row->checked()) {
		delegate()->peerListSetRowChecked(row, !row->checked());
		updateTitle();
	} else if (const auto channel = _peer ? _peer->asChannel() : nullptr) {
		if (!_peer->isMegagroup()) {
			showBox(Box<MaxInviteBox>(_peer->asChannel()));
		}
	} else if (count >= serverConfig.chatSizeMax
		&& count < serverConfig.megagroupSizeMax) {
		showBox(Ui::MakeInformBox(tr::lng_profile_add_more_after_create()));
	}
}

void AddParticipantsBoxController::itemDeselectedHook(
		not_null<PeerData*> peer) {
	updateTitle();
}

void AddParticipantsBoxController::prepareViewHook() {
	updateTitle();

	TrackPremiumRequiredChanges(this, lifetime());
}

int AddParticipantsBoxController::alreadyInCount() const {
	if (!_peer) {
		return 1; // self
	}
	if (const auto chat = _peer->asChat()) {
		return qMax(chat->count, 1);
	} else if (const auto channel = _peer->asChannel()) {
		return qMax(channel->membersCount(), int(_alreadyIn.size()));
	}
	Unexpected("User in AddParticipantsBoxController::alreadyInCount");
}

bool AddParticipantsBoxController::isAlreadyIn(
		not_null<UserData*> user) const {
	if (!_peer) {
		return false;
	}
	if (const auto chat = _peer->asChat()) {
		return _alreadyIn.contains(user)
			|| chat->participants.contains(user);
	} else if (const auto channel = _peer->asChannel()) {
		return _alreadyIn.contains(user)
			|| (channel->isMegagroup()
				&& channel->canViewMembers()
				&& base::contains(channel->mgInfo->lastParticipants, user));
	}
	Unexpected("User in AddParticipantsBoxController::isAlreadyIn");
}

int AddParticipantsBoxController::fullCount() const {
	return alreadyInCount() + delegate()->peerListSelectedRowsCount();
}

std::unique_ptr<PeerListRow> AddParticipantsBoxController::createRow(
		not_null<UserData*> user) {
	if (user->isSelf()) {
		return nullptr;
	}
	const auto already = isAlreadyIn(user);
	const auto maybeLockedSt = already ? nullptr : &computeListSt().item;
	auto result = std::make_unique<RecipientRow>(user, maybeLockedSt);
	if (already) {
		result->setDisabledState(PeerListRow::State::DisabledChecked);
	}
	return result;
}

void AddParticipantsBoxController::updateTitle() {
	const auto additional = (_peer
		&& _peer->isChannel()
		&& !_peer->isMegagroup())
		? QString()
		: (u"%1 / %2"_q
		).arg(fullCount()
		).arg(session().serverConfig().megagroupSizeMax);
	delegate()->peerListSetTitle(tr::lng_profile_add_participant());
	delegate()->peerListSetAdditionalTitle(rpl::single(additional));

	addInviteLinkButton();
}

bool AddParticipantsBoxController::needsInviteLinkButton() {
	if (!_peer) {
		return false;
	} else if (const auto channel = _peer->asChannel()) {
		return channel->canHaveInviteLink();
	}
	return _peer->asChat()->canHaveInviteLink();
}

QPointer<Ui::BoxContent> AddParticipantsBoxController::showBox(
		object_ptr<Ui::BoxContent> box) const {
	const auto weak = Ui::MakeWeak(box.data());
	delegate()->peerListUiShow()->showBox(std::move(box));
	return weak;
}

void AddParticipantsBoxController::addInviteLinkButton() {
	if (!needsInviteLinkButton()) {
		return;
	}
	auto button = object_ptr<Ui::PaddingWrap<Ui::SettingsButton>>(
		nullptr,
		object_ptr<Ui::SettingsButton>(
			nullptr,
			tr::lng_profile_add_via_link(),
			st::inviteViaLinkButton),
		style::margins(0, st::membersMarginTop, 0, 0));

	const auto icon = Ui::CreateChild<Info::Profile::FloatingIcon>(
		button->entity(),
		st::inviteViaLinkIcon,
		QPoint());
	button->entity()->heightValue(
	) | rpl::start_with_next([=](int height) {
		icon->moveToLeft(
			st::inviteViaLinkIconPosition.x(),
			(height - st::inviteViaLinkIcon.height()) / 2);
	}, icon->lifetime());

	button->entity()->setClickedCallback([=] {
		showBox(Box<EditPeerTypeBox>(_peer));
	});
	button->entity()->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::start_with_next([=] {
		delegate()->peerListMouseLeftGeometry();
	}, button->lifetime());
	delegate()->peerListSetAboveWidget(std::move(button));
	delegate()->peerListRefreshRows();
}

void AddParticipantsBoxController::inviteSelectedUsers(
		not_null<PeerListBox*> box,
		Fn<void()> done) const {
	Expects(_peer != nullptr);

	const auto rows = box->collectSelectedRows();
	const auto users = ranges::views::all(
		rows
	) | ranges::views::transform([](not_null<PeerData*> peer) {
		Expects(peer->isUser());
		Expects(!peer->isSelf());

		return not_null<UserData*>(peer->asUser());
	}) | ranges::to_vector;
	if (users.empty()) {
		return;
	}
	const auto show = box->uiShow();
	const auto request = [=](bool checked) {
		_peer->session().api().chatParticipants().add(
			show,
			_peer,
			users,
			checked);
	};
	if (_peer->isChannel()) {
		request(false);
		return done();
	}
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		auto checkbox = object_ptr<Ui::Checkbox>(
			box.get(),
			tr::lng_participant_invite_history(),
			true,
			st::defaultBoxCheckbox);
		const auto weak = Ui::MakeWeak(checkbox.data());

		auto text = (users.size() == 1)
			? tr::lng_participant_invite_sure(
				tr::now,
				lt_user,
				{ users.front()->name()},
				lt_group,
				{ _peer->name()},
				Ui::Text::RichLangValue)
			: tr::lng_participant_invite_sure_many(
				tr::now,
				lt_count,
				int(users.size()),
				lt_group,
				{ _peer->name() },
				Ui::Text::RichLangValue);
		Ui::ConfirmBox(box, {
			.text = std::move(text),
			.confirmed = crl::guard(weak, [=](Fn<void()> &&close) {
				request(weak->checked());
				done();
				close();
			}),
			.confirmText = tr::lng_participant_invite(),
		});

		auto padding = st::boxPadding;
		padding.setTop(padding.bottom());
		box->addRow(std::move(checkbox), std::move(padding));
	}));
}

void AddParticipantsBoxController::Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChatData*> chat) {
	auto controller = std::make_unique<AddParticipantsBoxController>(chat);
	const auto weak = controller.get();
	const auto parent = navigation->parentController();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_participant_invite(), [=] {
			weak->inviteSelectedUsers(box, [=] {
				parent->showPeerHistory(
					chat,
					Window::SectionShow::Way::ClearStack,
					ShowAtTheEndMsgId);
			});
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	parent->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
}

void AddParticipantsBoxController::Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn,
		bool justCreated) {
	auto controller = std::make_unique<AddParticipantsBoxController>(
		channel,
		std::move(alreadyIn));
	const auto weak = controller.get();
	const auto parent = navigation->parentController();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_participant_invite(), [=] {
			weak->inviteSelectedUsers(box, [=] {
				if (channel->isMegagroup()) {
					parent->showPeerHistory(
						channel,
						Window::SectionShow::Way::ClearStack,
						ShowAtTheEndMsgId);
				} else {
					box->closeBox();
				}
			});
		});
		box->addButton(
			justCreated ? tr::lng_create_group_skip() : tr::lng_cancel(),
			[=] { box->closeBox(); });
		if (justCreated) {
			const auto weak = base::make_weak(parent);
			box->boxClosing() | rpl::start_with_next([=] {
				auto params = Window::SectionShow();
				params.activation = anim::activation::background;
				if (const auto strong = weak.get()) {
					strong->showPeerHistory(
						channel,
						params,
						ShowAtTheEndMsgId);
				}
			}, box->lifetime());
		}
	};
	parent->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
}

void AddParticipantsBoxController::Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn) {
	Start(navigation, channel, std::move(alreadyIn), false);
}

void AddParticipantsBoxController::Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel) {
	Start(navigation, channel, {}, true);
}

ForbiddenInvites CollectForbiddenUsers(
		not_null<Main::Session*> session,
		const MTPmessages_InvitedUsers &result) {
	const auto &data = result.data();
	const auto owner = &session->data();
	auto forbidden = ForbiddenInvites();
	for (const auto &missing : data.vmissing_invitees().v) {
		const auto &data = missing.data();
		const auto user = owner->userLoaded(data.vuser_id());
		if (user) {
			forbidden.users.push_back(user);
			if (data.is_premium_would_allow_invite()) {
				forbidden.premiumAllowsInvite.push_back(user);
			}
			if (data.is_premium_required_for_pm()) {
				forbidden.premiumAllowsWrite.push_back(user);
			}
		}
	}
	return forbidden;
}

bool ChatInviteForbidden(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		ForbiddenInvites forbidden) {
	if (forbidden.empty() || !show || !show->valid()) {
		return false;
	} else if (forbidden.users.size() <= kUserpicsLimit
		&& (forbidden.premiumAllowsWrite.size()
			== forbidden.users.size())) {
		show->show(Box(SimpleForbiddenBox, peer, forbidden));
		return true;
	}
	auto controller = std::make_unique<InviteForbiddenController>(
		peer,
		std::move(forbidden));
	const auto weak = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		const auto can = weak->canInvite();
		if (!can) {
			box->addButton(tr::lng_close(), [=] {
				box->closeBox();
			});
			return;
		}
		weak->selectedValue(
		) | rpl::map(
			rpl::mappers::_1 > 0
		) | rpl::distinct_until_changed(
		) | rpl::start_with_next([=](bool has) {
			box->clearButtons();
			if (has) {
				box->addButton(tr::lng_via_link_send(), [=] {
					weak->send(
						box->collectSelectedRows(),
						box->uiShow(),
						crl::guard(box, [=] { box->closeBox(); }));
				});
			}
			box->addButton(tr::lng_create_group_skip(), [=] {
				box->closeBox();
			});
		}, box->lifetime());

		Data::AmPremiumValue(
			&peer->session()
		) | rpl::skip(1) | rpl::start_with_next([=] {
			box->closeBox();
		}, box->lifetime());
	};
	show->showBox(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
	return true;
}

AddSpecialBoxController::AddSpecialBoxController(
	not_null<PeerData*> peer,
	Role role,
	AdminDoneCallback adminDoneCallback,
	BannedDoneCallback bannedDoneCallback)
: PeerListController(std::make_unique<AddSpecialBoxSearchController>(
	peer,
	&_additional))
, _peer(peer)
, _api(&_peer->session().mtp())
, _role(role)
, _additional(peer, Role::Members)
, _adminDoneCallback(std::move(adminDoneCallback))
, _bannedDoneCallback(std::move(bannedDoneCallback)) {
	subscribeToMigration();
}

Main::Session &AddSpecialBoxController::session() const {
	return _peer->session();
}

void AddSpecialBoxController::subscribeToMigration() {
	const auto chat = _peer->asChat();
	if (!chat) {
		return;
	}
	SubscribeToMigration(
		chat,
		lifetime(),
		[=](not_null<ChannelData*> channel) { migrate(chat, channel); });
}

void AddSpecialBoxController::migrate(
		not_null<ChatData*> chat,
		not_null<ChannelData*> channel) {
	_peer = channel;
	_additional.migrate(chat, channel);
}

QPointer<Ui::BoxContent> AddSpecialBoxController::showBox(
		object_ptr<Ui::BoxContent> box) const {
	const auto weak = Ui::MakeWeak(box.data());
	delegate()->peerListUiShow()->showBox(std::move(box));
	return weak;
}

std::unique_ptr<PeerListRow> AddSpecialBoxController::createSearchRow(
		not_null<PeerData*> peer) {
	if (_excludeSelf && peer->isSelf()) {
		return nullptr;
	}
	if (const auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
}

void AddSpecialBoxController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	auto title = [&] {
		switch (_role) {
		case Role::Members:
			return tr::lng_profile_participants_section();
		case Role::Admins:
			return tr::lng_channel_add_admin();
		case Role::Restricted:
			return tr::lng_channel_add_exception();
		case Role::Kicked:
			return tr::lng_channel_add_removed();
		}
		Unexpected("Role in AddSpecialBoxController::prepare()");
	}();
	delegate()->peerListSetTitle(std::move(title));
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));

	if (const auto chat = _peer->asChat()) {
		prepareChatRows(chat);
	} else {
		loadMoreRows();
	}
	delegate()->peerListRefreshRows();
}

void AddSpecialBoxController::prepareChatRows(not_null<ChatData*> chat) {
	_onlineSorter = std::make_unique<ParticipantsOnlineSorter>(
		chat,
		delegate());

	rebuildChatRows(chat);
	if (!delegate()->peerListFullRowsCount()) {
		chat->updateFullForced();
	}

	using UpdateFlag = Data::PeerUpdate::Flag;
	chat->session().changes().peerUpdates(
		chat,
		UpdateFlag::Members | UpdateFlag::Admins
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		_additional.fillFromPeer();
		if (update.flags & UpdateFlag::Members) {
			rebuildChatRows(chat);
		}
	}, lifetime());
}

void AddSpecialBoxController::rebuildChatRows(not_null<ChatData*> chat) {
	if (chat->participants.empty()) {
		// We get such updates often
		// (when participants list was invalidated).
		//while (delegate()->peerListFullRowsCount() > 0) {
		//	delegate()->peerListRemoveRow(
		//		delegate()->peerListRowAt(0));
		//}
		return;
	}

	auto &participants = chat->participants;
	auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count;) {
		auto row = delegate()->peerListRowAt(i);
		Assert(row->peer()->isUser());
		auto user = row->peer()->asUser();
		if (participants.contains(user)) {
			++i;
		} else {
			delegate()->peerListRemoveRow(row);
			--count;
		}
	}
	for (const auto &user : participants) {
		if (auto row = createRow(user)) {
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	_onlineSorter->sort();

	delegate()->peerListRefreshRows();
	setDescriptionText(QString());
}

void AddSpecialBoxController::loadMoreRows() {
	if (searchController() && searchController()->loadMoreRows()) {
		return;
	} else if (!_peer->isChannel() || _loadRequestId || _allLoaded) {
		return;
	}

	// First query is small and fast, next loads a lot of rows.
	const auto perPage = (_offset > 0)
		? kParticipantsPerPage
		: kParticipantsFirstPageCount;
	const auto participantsHash = uint64(0);
	const auto channel = _peer->asChannel();

	_loadRequestId = _api.request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsRecent(),
		MTP_int(_offset),
		MTP_int(perPage),
		MTP_long(participantsHash)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		_loadRequestId = 0;
		result.match([&](const MTPDchannels_channelParticipants &data) {
			const auto &[availableCount, list] = Api::ChatParticipants::Parse(
				channel,
				data);
			for (const auto &data : list) {
				if (const auto participant = _additional.applyParticipant(
						data)) {
					appendRow(participant);
				}
			}
			if (const auto size = list.size()) {
				_offset += size;
			} else {
				// To be sure - wait for a whole empty result list.
				_allLoaded = true;
			}
		}, [&](const MTPDchannels_channelParticipantsNotModified &) {
			LOG(("API Error: channels.channelParticipantsNotModified received!"));
		});
		if (delegate()->peerListFullRowsCount() > 0) {
			setDescriptionText(QString());
		} else if (_allLoaded) {
			setDescriptionText(tr::lng_blocked_list_not_found(tr::now));
		}
		delegate()->peerListRefreshRows();
	}).fail([this] {
		_loadRequestId = 0;
	}).send();
}

void AddSpecialBoxController::rowClicked(not_null<PeerListRow*> row) {
	const auto participant = row->peer();
	const auto user = participant->asUser();
	switch (_role) {
	case Role::Admins:
		Assert(user != nullptr);
		return showAdmin(user);
	case Role::Restricted:
		Assert(user != nullptr);
		return showRestricted(user);
	case Role::Kicked: return kickUser(participant);
	}
	Unexpected("Role in AddSpecialBoxController::rowClicked()");
}

template <typename Callback>
bool AddSpecialBoxController::checkInfoLoaded(
		not_null<PeerData*> participant,
		Callback callback) {
	if (_additional.infoLoaded(participant)) {
		return true;
	}

	// We don't know what this user status is in the group.
	const auto channel = _peer->asChannel();
	_api.request(MTPchannels_GetParticipant(
		channel->inputChannel,
		participant->input
	)).done([=](const MTPchannels_ChannelParticipant &result) {
		result.match([&](const MTPDchannels_channelParticipant &data) {
			channel->owner().processUsers(data.vusers());
			_additional.applyParticipant(
				Api::ChatParticipant(data.vparticipant(), channel));
		});
		callback();
	}).fail([=] {
		_additional.setExternal(participant);
		callback();
	}).send();
	return false;
}

void AddSpecialBoxController::showAdmin(
		not_null<UserData*> user,
		bool sure) {
	if (!checkInfoLoaded(user, [=] { showAdmin(user); })) {
		return;
	}
	_editBox = nullptr;
	if (_editParticipantBox) {
		_editParticipantBox->closeBox();
	}

	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto showAdminSure = crl::guard(this, [=] {
		showAdmin(user, true);
	});

	// Check restrictions.
	const auto canAddMembers = chat
		? chat->canAddMembers()
		: channel->canAddMembers();
	const auto canBanMembers = chat
		? chat->canBanMembers()
		: channel->canBanMembers();
	const auto adminRights = _additional.adminRights(user);
	if (adminRights.has_value()) {
		// The user is already an admin.
	} else if (_additional.isKicked(user)) {
		// The user is banned.
		if (canAddMembers) {
			if (canBanMembers) {
				if (!sure) {
					_editBox = showBox(
						Ui::MakeConfirmBox({
							tr::lng_sure_add_admin_unremove(),
							showAdminSure
						}));
					return;
				}
			} else {
				showBox(
					Ui::MakeInformBox(tr::lng_error_cant_add_admin_unban()));
				return;
			}
		} else {
			showBox(Ui::MakeInformBox(tr::lng_error_cant_add_admin_invite()));
			return;
		}
	} else if (_additional.restrictedRights(user).has_value()) {
		// The user is restricted.
		if (canBanMembers) {
			if (!sure) {
				_editBox = showBox(
					Ui::MakeConfirmBox({
						tr::lng_sure_add_admin_unremove(),
						showAdminSure
					}));
				return;
			}
		} else {
			showBox(Ui::MakeInformBox(tr::lng_error_cant_add_admin_unban()));
			return;
		}
	} else if (_additional.isExternal(user)) {
		// The user is not in the group yet.
		if (canAddMembers) {
			if (!sure) {
				auto text = ((_peer->isChat() || _peer->isMegagroup())
					? tr::lng_sure_add_admin_invite
					: tr::lng_sure_add_admin_invite_channel)();
				_editBox = showBox(
					Ui::MakeConfirmBox({
						std::move(text),
						showAdminSure
					}));
				return;
			}
		} else {
			showBox(Ui::MakeInformBox(tr::lng_error_cant_add_admin_invite()));
			return;
		}
	}

	// Finally show the admin.
	const auto currentRights = adminRights
		? *adminRights
		: ChatAdminRightsInfo();
	auto box = Box<EditAdminBox>(
		_peer,
		user,
		currentRights,
		_additional.adminRank(user),
		_additional.adminPromotedSince(user),
		_additional.adminPromotedBy(user));
	const auto show = delegate()->peerListUiShow();
	if (_additional.canAddOrEditAdmin(user)) {
		const auto done = crl::guard(this, [=](
				ChatAdminRightsInfo newRights,
				const QString &rank) {
			editAdminDone(user, newRights, rank);
		});
		const auto fail = crl::guard(this, [=] {
			if (_editParticipantBox) {
				_editParticipantBox->closeBox();
			}
		});
		box->setSaveCallback(
			SaveAdminCallback(show, _peer, user, done, fail));
	}
	_editParticipantBox = showBox(std::move(box));
}

void AddSpecialBoxController::editAdminDone(
		not_null<UserData*> user,
		ChatAdminRightsInfo rights,
		const QString &rank) {
	if (_editParticipantBox) {
		_editParticipantBox->closeBox();
	}

	_additional.applyAdminLocally(user, rights, rank);
	if (const auto callback = _adminDoneCallback) {
		callback(user, rights, rank);
	}
}

void AddSpecialBoxController::showRestricted(
		not_null<UserData*> user,
		bool sure) {
	if (!checkInfoLoaded(user, [=] { showRestricted(user); })) {
		return;
	}
	_editBox = nullptr;
	if (_editParticipantBox) {
		_editParticipantBox->closeBox();
	}

	const auto showRestrictedSure = crl::guard(this, [=] {
		showRestricted(user, true);
	});

	// Check restrictions.
	const auto restrictedRights = _additional.restrictedRights(user);
	if (restrictedRights.has_value()) {
		// The user is already banned or restricted.
	} else if (_additional.adminRights(user).has_value()
		|| _additional.isCreator(user)) {
		// The user is an admin or creator.
		if (!_additional.isCreator(user) && _additional.canEditAdmin(user)) {
			if (!sure) {
				_editBox = showBox(
					Ui::MakeConfirmBox({
						tr::lng_sure_ban_admin(),
						showRestrictedSure
					}));
				return;
			}
		} else {
			showBox(Ui::MakeInformBox(tr::lng_error_cant_ban_admin()));
			return;
		}
	}

	// Finally edit the restricted.
	const auto currentRights = restrictedRights
		? *restrictedRights
		: ChatRestrictionsInfo();
	auto box = Box<EditRestrictedBox>(
		_peer,
		user,
		_additional.adminRights(user).has_value(),
		currentRights,
		_additional.restrictedBy(user),
		_additional.restrictedSince(user));
	if (_additional.canRestrictParticipant(user)) {
		const auto done = crl::guard(this, [=](
				ChatRestrictionsInfo newRights) {
			editRestrictedDone(user, newRights);
		});
		const auto fail = crl::guard(this, [=] {
			if (_editParticipantBox) {
				_editParticipantBox->closeBox();
			}
		});
		box->setSaveCallback(
			SaveRestrictedCallback(_peer, user, done, fail));
	}
	_editParticipantBox = showBox(std::move(box));
}

void AddSpecialBoxController::editRestrictedDone(
		not_null<PeerData*> participant,
		ChatRestrictionsInfo rights) {
	if (_editParticipantBox) {
		_editParticipantBox->closeBox();
	}

	_additional.applyBannedLocally(participant, rights);
	if (const auto callback = _bannedDoneCallback) {
		callback(participant, rights);
	}
}

void AddSpecialBoxController::kickUser(
		not_null<PeerData*> participant,
		bool sure) {
	if (!checkInfoLoaded(participant, [=] { kickUser(participant); })) {
		return;
	}

	const auto kickUserSure = crl::guard(this, [=] {
		kickUser(participant, true);
	});

	// Check restrictions.
	const auto user = participant->asUser();
	if (user && (_additional.adminRights(user).has_value()
		|| (_additional.isCreator(user)))) {
		// The user is an admin or creator.
		if (!_additional.isCreator(user) && _additional.canEditAdmin(user)) {
			if (!sure) {
				_editBox = showBox(
					Ui::MakeConfirmBox({
						tr::lng_sure_ban_admin(),
						kickUserSure
					}));
				return;
			}
		} else {
			showBox(Ui::MakeInformBox(tr::lng_error_cant_ban_admin()));
			return;
		}
	}

	// Finally kick him.
	if (!sure) {
		const auto text = ((_peer->isChat() || _peer->isMegagroup())
			? tr::lng_profile_sure_kick
			: tr::lng_profile_sure_kick_channel)(
				tr::now,
				lt_user,
				participant->name());
		_editBox = showBox(Ui::MakeConfirmBox({ text, kickUserSure }));
		return;
	}

	const auto restrictedRights = _additional.restrictedRights(participant);
	const auto currentRights = restrictedRights
		? *restrictedRights
		: ChatRestrictionsInfo();

	const auto done = crl::guard(this, [=](
			ChatRestrictionsInfo newRights) {
		editRestrictedDone(participant, newRights);
	});
	const auto fail = crl::guard(this, [=] {
		_editBox = nullptr;
	});
	const auto callback = SaveRestrictedCallback(
		_peer,
		participant,
		done,
		fail);
	callback(currentRights, ChannelData::KickedRestrictedRights(participant));
}

bool AddSpecialBoxController::appendRow(not_null<PeerData*> participant) {
	if (delegate()->peerListFindRow(participant->id.value)
		|| (_excludeSelf && participant->isSelf())) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(participant));
	return true;
}

bool AddSpecialBoxController::prependRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id.value)) {
		return false;
	}
	delegate()->peerListPrependRow(createRow(user));
	return true;
}

std::unique_ptr<PeerListRow> AddSpecialBoxController::createRow(
		not_null<PeerData*> participant) const {
	return std::make_unique<PeerListRow>(participant);
}

AddSpecialBoxSearchController::AddSpecialBoxSearchController(
	not_null<PeerData*> peer,
	not_null<ParticipantsAdditionalData*> additional)
: _peer(peer)
, _additional(additional)
, _api(&_peer->session().mtp())
, _timer([=] { searchOnServer(); }) {
	subscribeToMigration();
}

void AddSpecialBoxSearchController::subscribeToMigration() {
	SubscribeToMigration(
		_peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) { _peer = channel; });
}

void AddSpecialBoxSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_offset = 0;
		_requestId = 0;
		_participantsLoaded = false;
		_chatsContactsAdded = false;
		_chatMembersAdded = false;
		_globalLoaded = false;
		if (!_query.isEmpty() && !searchParticipantsInCache()) {
			_timer.callOnce(AutoSearchTimeout);
		} else {
			_timer.cancel();
		}
	}
}

void AddSpecialBoxSearchController::searchOnServer() {
	Expects(!_query.isEmpty());

	loadMoreRows();
}

bool AddSpecialBoxSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

bool AddSpecialBoxSearchController::searchParticipantsInCache() {
	const auto i = _participantsCache.find(_query);
	if (i != _participantsCache.cend()) {
		_requestId = 0;
		searchParticipantsDone(
			_requestId,
			i->second.result,
			i->second.requestedCount);
		return true;
	}
	return false;
}

bool AddSpecialBoxSearchController::searchGlobalInCache() {
	auto it = _globalCache.find(_query);
	if (it != _globalCache.cend()) {
		_requestId = 0;
		searchGlobalDone(_requestId, it->second);
		return true;
	}
	return false;
}

bool AddSpecialBoxSearchController::loadMoreRows() {
	if (_query.isEmpty()) {
		return false;
	}
	if (_globalLoaded) {
		return true;
	}
	if (_participantsLoaded || _chatMembersAdded) {
		if (!_chatsContactsAdded) {
			addChatsContacts();
		}
		if (!isLoading() && !searchGlobalInCache()) {
			requestGlobal();
		}
	} else if (const auto chat = _peer->asChat()) {
		if (!_chatMembersAdded) {
			addChatMembers(chat);
		}
	} else if (!isLoading()) {
		requestParticipants();
	}
	return true;
}

void AddSpecialBoxSearchController::requestParticipants() {
	Expects(_peer->isChannel());

	// For search we request a lot of rows from the first query.
	// (because we've waited for search request by timer already,
	// so we don't expect it to be fast, but we want to fill cache).
	const auto perPage = kParticipantsPerPage;
	const auto participantsHash = uint64(0);
	const auto channel = _peer->asChannel();

	_requestId = _api.request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsSearch(MTP_string(_query)),
		MTP_int(_offset),
		MTP_int(perPage),
		MTP_long(participantsHash)
	)).done([=](
			const MTPchannels_ChannelParticipants &result,
			mtpRequestId requestId) {
		searchParticipantsDone(requestId, result, perPage);
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		if (_requestId == requestId) {
			_requestId = 0;
			_participantsLoaded = true;
			loadMoreRows();
			delegate()->peerListSearchRefreshRows();
		}
	}).send();

	auto entry = Query();
	entry.text = _query;
	entry.offset = _offset;
	_participantsQueries.emplace(_requestId, entry);
}

void AddSpecialBoxSearchController::searchParticipantsDone(
		mtpRequestId requestId,
		const MTPchannels_ChannelParticipants &result,
		int requestedCount) {
	Expects(_peer->isChannel());

	const auto channel = _peer->asChannel();
	auto query = _query;
	if (requestId) {
		const auto addToCache = [&] {
			auto it = _participantsQueries.find(requestId);
			if (it != _participantsQueries.cend()) {
				query = it->second.text;
				if (it->second.offset == 0) {
					auto &entry = _participantsCache[query];
					entry.result = result;
					entry.requestedCount = requestedCount;
				}
				_participantsQueries.erase(it);
			}
		};
		result.match([&](const MTPDchannels_channelParticipants &data) {
			Api::ChatParticipants::Parse(channel, data);
			addToCache();
		}, [&](const MTPDchannels_channelParticipantsNotModified &) {
			LOG(("API Error: "
				"channels.channelParticipantsNotModified received!"));
		});
	}

	if (_requestId != requestId) {
		return;
	}
	_requestId = 0;
	result.match([&](const MTPDchannels_channelParticipants &data) {
		const auto &list = data.vparticipants().v;
		if (list.size() < requestedCount) {
			// We want cache to have full information about a query with
			// small results count (that we don't need the second request).
			// So we don't wait for empty list unlike the non-search case.
			_participantsLoaded = true;
			if (list.empty() && _offset == 0) {
				// No results, request global search immediately.
				loadMoreRows();
			}
		}
		for (const auto &data : list) {
			if (const auto user = _additional->applyParticipant(
					Api::ChatParticipant(data, channel))) {
				delegate()->peerListSearchAddRow(user);
			}
		}
		_offset += list.size();
	}, [&](const MTPDchannels_channelParticipantsNotModified &) {
		_participantsLoaded = true;
	});

	delegate()->peerListSearchRefreshRows();
}

void AddSpecialBoxSearchController::requestGlobal() {
	if (_query.isEmpty()) {
		_globalLoaded = true;
		return;
	}

	auto perPage = SearchPeopleLimit;
	_requestId = _api.request(MTPcontacts_Search(
		MTP_string(_query),
		MTP_int(perPage)
	)).done([=](const MTPcontacts_Found &result, mtpRequestId requestId) {
		searchGlobalDone(requestId, result);
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		if (_requestId == requestId) {
			_requestId = 0;
			_globalLoaded = true;
			delegate()->peerListSearchRefreshRows();
		}
	}).send();
	_globalQueries.emplace(_requestId, _query);
}

void AddSpecialBoxSearchController::searchGlobalDone(
		mtpRequestId requestId,
		const MTPcontacts_Found &result) {
	Expects(result.type() == mtpc_contacts_found);

	auto &found = result.c_contacts_found();
	auto query = _query;
	if (requestId) {
		_peer->owner().processUsers(found.vusers());
		_peer->owner().processChats(found.vchats());
		auto it = _globalQueries.find(requestId);
		if (it != _globalQueries.cend()) {
			query = it->second;
			_globalCache[query] = result;
			_globalQueries.erase(it);
		}
	}

	const auto feedList = [&](const MTPVector<MTPPeer> &list) {
		for (const auto &mtpPeer : list.v) {
			const auto peerId = peerFromMTP(mtpPeer);
			if (const auto peer = _peer->owner().peerLoaded(peerId)) {
				if (const auto user = peer->asUser()) {
					_additional->checkForLoaded(user);
					delegate()->peerListSearchAddRow(user);
				}
			}
		}
	};
	if (_requestId == requestId) {
		_requestId = 0;
		_globalLoaded = true;
		feedList(found.vmy_results());
		feedList(found.vresults());
		delegate()->peerListSearchRefreshRows();
	}
}

void AddSpecialBoxSearchController::addChatMembers(
		not_null<ChatData*> chat) {
	if (chat->participants.empty()) {
		return;
	}

	_chatMembersAdded = true;
	const auto wordList = TextUtilities::PrepareSearchWords(_query);
	if (wordList.empty()) {
		return;
	}
	const auto allWordsAreFound = [&](
			const base::flat_set<QString> &nameWords) {
		const auto hasNamePartStartingWith = [&](const QString &word) {
			for (const auto &nameWord : nameWords) {
				if (nameWord.startsWith(word)) {
					return true;
				}
			}
			return false;
		};

		for (const auto &word : wordList) {
			if (!hasNamePartStartingWith(word)) {
				return false;
			}
		}
		return true;
	};

	for (const auto &user : chat->participants) {
		if (allWordsAreFound(user->nameWords())) {
			delegate()->peerListSearchAddRow(user);
		}
	}
	delegate()->peerListSearchRefreshRows();
}

void AddSpecialBoxSearchController::addChatsContacts() {
	_chatsContactsAdded = true;
	const auto wordList = TextUtilities::PrepareSearchWords(_query);
	if (wordList.empty()) {
		return;
	}
	const auto allWordsAreFound = [&](
			const base::flat_set<QString> &nameWords) {
		const auto hasNamePartStartingWith = [&](const QString &word) {
			for (const auto &nameWord : nameWords) {
				if (nameWord.startsWith(word)) {
					return true;
				}
			}
			return false;
		};

		for (const auto &word : wordList) {
			if (!hasNamePartStartingWith(word)) {
				return false;
			}
		}
		return true;
	};
	const auto getSmallestIndex = [&](not_null<Dialogs::IndexedList*> list)
	-> const Dialogs::List* {
		if (list->empty()) {
			return nullptr;
		}

		auto result = (const Dialogs::List*)nullptr;
		for (const auto &word : wordList) {
			const auto found = list->filtered(word[0]);
			if (!found || found->empty()) {
				return nullptr;
			}
			if (!result || result->size() > found->size()) {
				result = found;
			}
		}
		return result;
	};
	const auto filterAndAppend = [&](not_null<Dialogs::IndexedList*> list) {
		const auto index = getSmallestIndex(list);
		if (!index) {
			return;
		}
		for (const auto &row : *index) {
			if (const auto history = row->history()) {
				if (const auto user = history->peer->asUser()) {
					if (allWordsAreFound(user->nameWords())) {
						delegate()->peerListSearchAddRow(user);
					}
				}
			}
		}
	};
	filterAndAppend(_peer->owner().chatsList()->indexed());
	const auto id = Data::Folder::kId;
	if (const auto folder = _peer->owner().folderLoaded(id)) {
		filterAndAppend(folder->chatsList()->indexed());
	}
	filterAndAppend(_peer->owner().contactsNoChatsList());
	delegate()->peerListSearchRefreshRows();
}
