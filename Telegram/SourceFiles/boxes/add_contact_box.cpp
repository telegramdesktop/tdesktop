/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/add_contact_box.h"

#include "lang/lang_keys.h"
#include "base/call_delayed.h"
#include "base/random.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/premium_limits_box.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/peers/edit_peer_common.h"
#include "boxes/peers/edit_participant_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "countries/countries_instance.h" // Countries::ExtractPhoneCode.
#include "history/history_item_reply_markup.h"
#include "window/window_session_controller.h"
#include "menu/menu_ttl.h"
#include "ui/controls/userpic_button.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/special_fields.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/unread_badge.h"
#include "ui/ui_utility.h"
#include "ui/painter.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_cloud_file.h"
#include "apiwrap.h"
#include "api/api_invite_links.h"
#include "api/api_peer_photo.h"
#include "main/main_session.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace {

bool IsValidPhone(QString phone) {
	phone = phone.replace(QRegularExpression(u"[^\\d]"_q), QString());
	return (phone.length() >= 8)
		|| (phone == u"333"_q)
		|| (phone.startsWith(u"42"_q)
			&& (phone.length() == 2
				|| phone.length() == 5
				|| phone.length() == 6
				|| phone == u"4242"_q));
}

void ChatCreateDone(
		not_null<Window::SessionNavigation*> navigation,
		QImage image,
		TimeId ttlPeriod,
		const MTPUpdates &updates,
		Fn<void(not_null<PeerData*>)> done) {
	navigation->session().api().applyUpdates(updates);

	const auto success = base::make_optional(&updates)
		| [](auto updates) -> std::optional<const QVector<MTPChat>*> {
			switch (updates->type()) {
			case mtpc_updates:
				return &updates->c_updates().vchats().v;
			case mtpc_updatesCombined:
				return &updates->c_updatesCombined().vchats().v;
			}
			LOG(("API Error: unexpected update cons %1 "
				"(GroupInfoBox::creationDone)").arg(updates->type()));
			return std::nullopt;
		}
		| [](auto chats) {
			return (!chats->empty()
				&& chats->front().type() == mtpc_chat)
				? base::make_optional(chats)
				: std::nullopt;
		}
		| [&](auto chats) {
			return navigation->session().data().chat(
				chats->front().c_chat().vid());
		}
		| [&](not_null<ChatData*> chat) {
			if (!image.isNull()) {
				chat->session().api().peerPhoto().upload(
					chat,
					{ std::move(image) });
			}
			if (ttlPeriod) {
				chat->setMessagesTTL(ttlPeriod);
			}
			if (done) {
				done(chat);
			} else {
				const auto show = navigation->uiShow();
				navigation->showPeerHistory(chat);
				ChatInviteForbidden(
					show,
					chat,
					CollectForbiddenUsers(&chat->session(), updates));
			}
		};
	if (!success) {
		LOG(("API Error: chat not found in updates "
			"(ContactsBox::creationDone)"));
	}
}

void MustBePublicDestroy(not_null<ChannelData*> channel) {
	const auto session = &channel->session();
	session->api().request(MTPchannels_DeleteChannel(
		channel->inputChannel
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
	}).send();
}

void MustBePublicFailed(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel) {
	const auto text = channel->isMegagroup()
		? "Can't create a public group :("
		: "Can't create a public channel :(";
	navigation->showToast(text);
	MustBePublicDestroy(channel);
}

[[nodiscard]] Fn<void(not_null<PeerData*>)> WrapPeerDoneFromChannelDone(
		Fn<void(not_null<ChannelData*>)> channelDone) {
	if (!channelDone) {
		return nullptr;
	}
	return [=](not_null<PeerData*> peer) {
		if (const auto channel = peer->asChannel()) {
			const auto onstack = channelDone;
			onstack(channel);
		}
	};
}

} // namespace

TextWithEntities PeerFloodErrorText(
		not_null<Main::Session*> session,
		PeerFloodType type) {
	const auto link = Ui::Text::Link(
		tr::lng_cant_more_info(tr::now),
		session->createInternalLinkFull(u"spambot"_q));
	return ((type == PeerFloodType::InviteGroup)
		? tr::lng_cant_invite_not_contact
		: tr::lng_cant_send_to_not_contact)(
			tr::now,
			lt_more_info,
			link,
			Ui::Text::WithEntities);
}

void ShowAddParticipantsError(
		const QString &error,
		not_null<PeerData*> chat,
		const std::vector<not_null<UserData*>> &users,
		std::shared_ptr<Ui::Show> show) {
	if (error == u"USER_BOT"_q) {
		const auto channel = chat->asChannel();
		if ((users.size() == 1)
			&& users.front()->isBot()
			&& channel
			&& !channel->isMegagroup()
			&& channel->canAddAdmins()) {
			const auto makeAdmin = [=] {
				const auto user = users.front();
				const auto weak = std::make_shared<QPointer<EditAdminBox>>();
				const auto close = [=](auto&&...) {
					if (*weak) {
						(*weak)->closeBox();
					}
				};
				const auto saveCallback = SaveAdminCallback(
					channel,
					user,
					close,
					close);
				auto box = Box<EditAdminBox>(
					channel,
					user,
					ChatAdminRightsInfo(),
					QString());
				box->setSaveCallback(saveCallback);
				*weak = Ui::show(std::move(box));
			};
			Ui::show(
				Ui::MakeConfirmBox({
					.text = tr::lng_cant_invite_offer_admin(),
					.confirmed = makeAdmin,
					.confirmText = tr::lng_cant_invite_make_admin(),
				}),
				Ui::LayerOption::KeepOther);
			return;
		}
	}
	const auto hasBot = ranges::any_of(users, &UserData::isBot);
	if (error == u"PEER_FLOOD"_q) {
		const auto type = (chat->isChat() || chat->isMegagroup())
			? PeerFloodType::InviteGroup
			: PeerFloodType::InviteChannel;
		const auto text = PeerFloodErrorText(&chat->session(), type);
		Ui::show(Ui::MakeInformBox(text), Ui::LayerOption::KeepOther);
		return;
	} else if (error == u"USER_PRIVACY_RESTRICTED"_q && show) {
		ChatInviteForbidden(show, chat, users);
		return;
	}
	const auto text = [&] {
		if (error == u"USER_BOT"_q) {
			return tr::lng_cant_invite_bot_to_channel(tr::now);
		} else if (error == u"USER_LEFT_CHAT"_q) {
			// Trying to return a user who has left.
		} else if (error == u"USER_KICKED"_q) {
			// Trying to return a user who was kicked by admin.
			return tr::lng_cant_invite_banned(tr::now);
		} else if (error == u"USER_PRIVACY_RESTRICTED"_q) {
			return tr::lng_cant_invite_privacy(tr::now);
		} else if (error == u"USER_NOT_MUTUAL_CONTACT"_q) {
			// Trying to return user who does not have me in contacts.
			return tr::lng_failed_add_not_mutual(tr::now);
		} else if (error == u"USER_ALREADY_PARTICIPANT"_q && hasBot) {
			return tr::lng_bot_already_in_group(tr::now);
		} else if (error == u"BOT_GROUPS_BLOCKED"_q) {
			return tr::lng_error_cant_add_bot(tr::now);
		} else if (error == u"ADMINS_TOO_MUCH"_q) {
			return ((chat->isChat() || chat->isMegagroup())
				? tr::lng_error_admin_limit
				: tr::lng_error_admin_limit_channel)(tr::now);
		}
		return tr::lng_failed_add_participant(tr::now);
	}();
	Ui::show(Ui::MakeInformBox(text), Ui::LayerOption::KeepOther);
}

AddContactBox::AddContactBox(
	QWidget*,
	not_null<Main::Session*> session)
: AddContactBox(nullptr, session, QString(), QString(), QString()) {
}

AddContactBox::AddContactBox(
	QWidget*,
	not_null<Main::Session*> session,
	QString fname,
	QString lname,
	QString phone)
: _session(session)
, _first(this, st::defaultInputField, tr::lng_signup_firstname(), fname)
, _last(this, st::defaultInputField, tr::lng_signup_lastname(), lname)
, _phone(
	this,
	st::defaultInputField,
	tr::lng_contact_phone(),
	Countries::ExtractPhoneCode(session->user()->phone()),
	phone,
	[](const QString &s) { return Countries::Groups(s); })
, _invertOrder(langFirstNameGoesSecond()) {
	if (!phone.isEmpty()) {
		_phone->setDisabled(true);
	}
}

void AddContactBox::prepare() {
	if (_invertOrder) {
		setTabOrder(_last, _first);
	}
	const auto readyToAdd = !_phone->getLastText().isEmpty()
		&& (!_first->getLastText().isEmpty()
			|| !_last->getLastText().isEmpty());
	setTitle(readyToAdd
		? tr::lng_confirm_contact_data()
		: tr::lng_enter_contact_data());
	updateButtons();

	const auto submitted = [=] { submit(); };
	_first->submits(
	) | rpl::start_with_next(submitted, _first->lifetime());
	_last->submits(
	) | rpl::start_with_next(submitted, _last->lifetime());
	connect(_phone, &Ui::PhoneInput::submitted, [=] { submit(); });

	setDimensions(
		st::boxWideWidth,
		st::contactPadding.top()
			+ _first->height()
			+ st::contactSkip
			+ _last->height()
			+ st::contactPhoneSkip
			+ _phone->height()
			+ st::contactPadding.bottom()
			+ st::boxPadding.bottom());
}

void AddContactBox::setInnerFocus() {
	if ((_first->getLastText().isEmpty() && _last->getLastText().isEmpty())
		|| !_phone->isEnabled()) {
		(_invertOrder ? _last : _first)->setFocusFast();
		_phone->finishAnimating();
	} else {
		_phone->setFocusFast();
	}
}

void AddContactBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	auto p = QPainter(this);
	if (_retrying) {
		p.setPen(st::boxTextFg);
		p.setFont(st::boxTextFont);
		const auto textHeight = height()
			- st::contactPadding.top()
			- st::contactPadding.bottom()
			- st::boxPadding.bottom();
		p.drawText(
			QRect(
				st::boxPadding.left(),
				st::contactPadding.top(),
				width() - st::boxPadding.left() - st::boxPadding.right(),
				textHeight),
			tr::lng_contact_not_joined(tr::now, lt_name, _sentName),
			style::al_topleft);
	} else {
		st::contactUserIcon.paint(
			p,
			st::boxPadding.left() + st::contactIconPosition.x(),
			_first->y() + st::contactIconPosition.y(),
			width());
		st::contactPhoneIcon.paint(
			p,
			st::boxPadding.left() + st::contactIconPosition.x(),
			_phone->y() + st::contactIconPosition.y(),
			width());
	}
}

void AddContactBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_first->resize(
		width()
			- st::boxPadding.left()
			- st::contactPadding.left()
			- st::boxPadding.right(),
		_first->height());
	_last->resize(_first->width(), _last->height());
	_phone->resize(_first->width(), _last->height());
	const auto left = st::boxPadding.left() + st::contactPadding.left();
	const auto &firstRow = _invertOrder ? _last : _first;
	const auto &secondRow = _invertOrder ? _first : _last;
	const auto &thirdRow = _phone;
	firstRow->moveToLeft(left, st::contactPadding.top());
	secondRow->moveToLeft(
		left,
		firstRow->y() + firstRow->height() + st::contactSkip);
	thirdRow->moveToLeft(
		left,
		secondRow->y() + secondRow->height() + st::contactPhoneSkip);
}

void AddContactBox::submit() {
	if (_first->hasFocus()) {
		_last->setFocus();
	} else if (_last->hasFocus()) {
		if (_phone->isEnabled()) {
			_phone->setFocus();
		} else {
			save();
		}
	} else if (_phone->hasFocus()) {
		save();
	}
}

void AddContactBox::save() {
	if (_addRequest) {
		return;
	}

	auto firstName = TextUtilities::PrepareForSending(
		_first->getLastText());
	auto lastName = TextUtilities::PrepareForSending(
		_last->getLastText());
	const auto phone = _phone->getLastText().trimmed();
	if (firstName.isEmpty() && lastName.isEmpty()) {
		if (_invertOrder) {
			_last->setFocus();
			_last->showError();
		} else {
			_first->setFocus();
			_first->showError();
		}
		return;
	} else if (!IsValidPhone(phone)) {
		_phone->setFocus();
		_phone->showError();
		return;
	}
	if (firstName.isEmpty()) {
		firstName = lastName;
		lastName = QString();
	}
	const auto weak = Ui::MakeWeak(this);
	const auto session = _session;
	_sentName = firstName;
	_contactId = base::RandomValue<uint64>();
	_addRequest = _session->api().request(MTPcontacts_ImportContacts(
		MTP_vector<MTPInputContact>(
			1,
			MTP_inputPhoneContact(
				MTP_long(_contactId),
				MTP_string(phone),
				MTP_string(firstName),
				MTP_string(lastName)))
	)).done(crl::guard(weak, [=](
			const MTPcontacts_ImportedContacts &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		if (!weak) {
			return;
		}
		const auto extractUser = [&](const MTPImportedContact &data) {
			return data.match([&](const MTPDimportedContact &data) {
				return (data.vclient_id().v == _contactId)
					? session->data().userLoaded(data.vuser_id())
					: nullptr;
			});
		};
		const auto &list = data.vimported().v;
		const auto user = list.isEmpty()
			? nullptr
			: extractUser(list.front());
		if (user) {
			if (user->isContact() || user->session().supportMode()) {
				if (const auto window = user->session().tryResolveWindow()) {
					window->showPeerHistory(user);
				}
			}
			if (weak) { // showPeerHistory could close the box.
				getDelegate()->hideLayer();
			}
		} else if (isBoxShown()) {
			hideChildren();
			_retrying = true;
			updateButtons();
			update();
		}
	})).send();
}

void AddContactBox::retry() {
	_addRequest = 0;
	_contactId = 0;
	showChildren();
	_retrying = false;
	updateButtons();
	_first->setText(QString());
	_last->setText(QString());
	_phone->clearText();
	_phone->setDisabled(false);
	_first->setFocus();
	update();
}

void AddContactBox::updateButtons() {
	clearButtons();
	if (_retrying) {
		addButton(tr::lng_try_other_contact(), [=] { retry(); });
	} else {
		addButton(tr::lng_add_contact(), [=] { save(); });
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	}
}

GroupInfoBox::GroupInfoBox(
	QWidget*,
	not_null<Window::SessionNavigation*> navigation,
	Type type,
	const QString &title,
	Fn<void(not_null<ChannelData*>)> channelDone)
: _navigation(navigation)
, _api(&_navigation->session().mtp())
, _type(type)
, _initialTitle(title)
, _done(WrapPeerDoneFromChannelDone(std::move(channelDone))) {
}

GroupInfoBox::GroupInfoBox(
	QWidget*,
	not_null<Window::SessionNavigation*> navigation,
	not_null<UserData*> bot,
	RequestPeerQuery query,
	Fn<void(not_null<PeerData*>)> done)
: _navigation(navigation)
, _api(&_navigation->session().mtp())
, _type((query.type == RequestPeerQuery::Type::Broadcast)
	? Type::Channel
	: (query.groupIsForum == RequestPeerQuery::Restriction::Yes)
	? Type::Forum
	: (query.hasUsername == RequestPeerQuery::Restriction::Yes)
	? Type::Megagroup
	: Type::Group)
, _mustBePublic(query.hasUsername == RequestPeerQuery::Restriction::Yes)
, _canAddBot(query.isBotParticipant ? bot.get() : nullptr)
, _done(std::move(done)) {
}

void GroupInfoBox::prepare() {
	setMouseTracking(true);

	_photo.create(
		this,
		&_navigation->parentController()->window(),
		Ui::UserpicButton::Role::ChoosePhoto,
		st::defaultUserpicButton,
		(_type == Type::Forum));
	_photo->showCustomOnChosen();
	_title.create(
		this,
		st::defaultInputField,
		(_type == Type::Channel
			? tr::lng_dlg_new_channel_name
			: tr::lng_dlg_new_group_name)(),
		_initialTitle);
	_title->setMaxLength(Ui::EditPeer::kMaxGroupChannelTitle);
	_title->setInstantReplaces(Ui::InstantReplaces::Default());
	_title->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_title,
		&_navigation->session());

	if (_type != Type::Group) {
		_description.create(
			this,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			tr::lng_create_group_description());
		_description->show();
		_description->setMaxLength(Ui::EditPeer::kMaxChannelDescription);
		_description->setInstantReplaces(Ui::InstantReplaces::Default());
		_description->setInstantReplacesEnabled(
			Core::App().settings().replaceEmojiValue());
		_description->setSubmitSettings(
			Core::App().settings().sendSubmitWay());

		_description->heightChanges(
		) | rpl::start_with_next([=] {
			descriptionResized();
		}, _description->lifetime());
		_description->submits(
		) | rpl::start_with_next([=] { submit(); }, _description->lifetime());
		_description->cancelled(
		) | rpl::start_with_next([=] {
			closeBox();
		}, _description->lifetime());

		Ui::Emoji::SuggestionsController::Init(
			getDelegate()->outerContainer(),
			_description,
			&_navigation->session());
	}
	_title->submits(
	) | rpl::start_with_next([=] { submitName(); }, _title->lifetime());

	addButton(
		((_type != Type::Group || _canAddBot)
			? tr::lng_create_group_create()
			: tr::lng_create_group_next()),
		[=] { submit(); });
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	if (_type == Type::Group) {
		const auto top = addTopButton(st::infoTopBarMenu);
		const auto menu =
			top->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();
		top->setClickedCallback([=] {
			*menu = base::make_unique_q<Ui::PopupMenu>(
				top,
				st::popupMenuWithIcons);

			const auto text = tr::lng_manage_messages_ttl_menu(tr::now)
				+ (_ttlPeriod
					? ('\t' + Ui::FormatTTLTiny(_ttlPeriod))
					: QString());
			(*menu)->addAction(
				text,
				[=, show = uiShow()] {
					show->showBox(Box(TTLMenu::TTLBox, TTLMenu::Args{
						.show = show,
						.startTtl = _ttlPeriod,
						.about = nullptr,
						.callback = crl::guard(this, [=](
								TimeId t,
								Fn<void()> close) {
							_ttlPeriod = t;
							close();
						}),
					}));
				}, &st::menuIconTTL);
			(*menu)->popup(QCursor::pos());
			return true;
		});
	}

	updateMaxHeight();
}

void GroupInfoBox::setInnerFocus() {
	_title->setFocusFast();
}

void GroupInfoBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_photo->moveToLeft(
		st::boxPadding.left() + st::newGroupInfoPadding.left(),
		st::boxPadding.top() + st::newGroupInfoPadding.top());

	const auto nameLeft = st::defaultUserpicButton.size.width()
		+ st::newGroupNamePosition.x();
	_title->resize(
		width()
			- st::boxPadding.left()
			- st::newGroupInfoPadding.left()
			- st::boxPadding.right()
			- nameLeft,
		_title->height());
	_title->moveToLeft(
		st::boxPadding.left() + st::newGroupInfoPadding.left() + nameLeft,
		st::boxPadding.top()
			+ st::newGroupInfoPadding.top()
			+ st::newGroupNamePosition.y());
	if (_description) {
		_description->resize(
			width()
				- st::boxPadding.left()
				- st::newGroupInfoPadding.left()
				- st::boxPadding.right(),
			_description->height());
		const auto descriptionLeft = st::boxPadding.left()
			+ st::newGroupInfoPadding.left();
		const auto descriptionTop = st::boxPadding.top()
			+ st::newGroupInfoPadding.top()
			+ st::defaultUserpicButton.size.height()
			+ st::newGroupDescriptionPadding.top();
		_description->moveToLeft(descriptionLeft, descriptionTop);
	}
}

void GroupInfoBox::submitName() {
	if (_title->getLastText().trimmed().isEmpty()) {
		_title->setFocus();
		_title->showError();
	} else if (_description) {
		_description->setFocus();
	} else {
		submit();
	}
}

void GroupInfoBox::createGroup(
		QPointer<Ui::BoxContent> selectUsersBox,
		const QString &title,
		const std::vector<not_null<PeerData*>> &users) {
	if (_creationRequestId) {
		return;
	}
	using TLUsers = MTPInputUser;
	auto inputs = QVector<TLUsers>();
	inputs.reserve(users.size());
	for (auto peer : users) {
		auto user = peer->asUser();
		Assert(user != nullptr);
		if (!user->isSelf()) {
			inputs.push_back(user->inputUser);
		}
	}
	_creationRequestId = _api.request(MTPmessages_CreateChat(
		MTP_flags(_ttlPeriod
			? MTPmessages_CreateChat::Flag::f_ttl_period
			: MTPmessages_CreateChat::Flags(0)),
		MTP_vector<TLUsers>(inputs),
		MTP_string(title),
		MTP_int(_ttlPeriod)
	)).done([=](const MTPUpdates &result) {
		auto image = _photo->takeResultImage();
		const auto period = _ttlPeriod;
		const auto navigation = _navigation;
		const auto done = _done;

		getDelegate()->hideLayer(); // Destroys 'this'.
		ChatCreateDone(navigation, std::move(image), period, result, done);
	}).fail([=](const MTP::Error &error) {
		const auto &type = error.type();
		_creationRequestId = 0;
		const auto controller = _navigation->parentController();
		if (type == u"NO_CHAT_TITLE"_q) {
			const auto weak = Ui::MakeWeak(this);
			if (const auto strong = selectUsersBox.data()) {
				strong->closeBox();
			}
			if (weak) {
				_title->showError();
			}
		} else if (type == u"USERS_TOO_FEW"_q) {
			controller->show(
				Ui::MakeInformBox(tr::lng_cant_invite_privacy()));
		} else if (type == u"PEER_FLOOD"_q) {
			controller->show(Ui::MakeInformBox(
				PeerFloodErrorText(
					&_navigation->session(),
					PeerFloodType::InviteGroup)));
		} else if (type == u"USER_RESTRICTED"_q) {
			controller->show(Ui::MakeInformBox(tr::lng_cant_do_this()));
		}
	}).send();
}

void GroupInfoBox::submit() {
	if (_creationRequestId || _creatingInviteLink) {
		return;
	}

	auto title = TextUtilities::PrepareForSending(_title->getLastText());
	auto description = _description
		? TextUtilities::PrepareForSending(
			_description->getLastText(),
			TextUtilities::PrepareTextOption::CheckLinks)
		: QString();
	if (title.isEmpty()) {
		_title->setFocus();
		_title->showError();
		return;
	}
	if (_type != Type::Group) {
		createChannel(title, description);
	} else if (_canAddBot) {
		createGroup(nullptr, title, { not_null<PeerData*>(_canAddBot) });
	} else {
		auto initBox = [title, weak = Ui::MakeWeak(this)](
				not_null<PeerListBox*> box) {
			auto create = [box, title, weak] {
				if (const auto strong = weak.data()) {
					strong->createGroup(
						box.get(),
						title,
						box->collectSelectedRows());
				}
			};
			box->addButton(tr::lng_create_group_create(), std::move(create));
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		};
		Ui::show(
			Box<PeerListBox>(
				std::make_unique<AddParticipantsBoxController>(
					&_navigation->session()),
				std::move(initBox)),
			Ui::LayerOption::KeepOther);
	}
}

void GroupInfoBox::createChannel(
		const QString &title,
		const QString &description) {
	Expects(!_creationRequestId);

	using Flag = MTPchannels_CreateChannel::Flag;
	const auto flags = Flag()
		| ((_type == Type::Megagroup || _type == Type::Forum)
			? Flag::f_megagroup
			: Flag::f_broadcast)
		| ((_type == Type::Forum) ? Flag::f_forum : Flag())
		| ((_type == Type::Megagroup && _ttlPeriod)
			? MTPchannels_CreateChannel::Flag::f_ttl_period
			: MTPchannels_CreateChannel::Flags(0));
	_creationRequestId = _api.request(MTPchannels_CreateChannel(
		MTP_flags(flags),
		MTP_string(title),
		MTP_string(description),
		MTPInputGeoPoint(), // geo_point
		MTPstring(), // address
		MTP_int((_type == Type::Megagroup) ? _ttlPeriod : 0)
	)).done([=](const MTPUpdates &result) {
		_navigation->session().api().applyUpdates(result);

		const auto success = base::make_optional(&result)
			| [](auto updates) -> std::optional<const QVector<MTPChat>*> {
				switch (updates->type()) {
				case mtpc_updates:
					return &updates->c_updates().vchats().v;
				case mtpc_updatesCombined:
					return &updates->c_updatesCombined().vchats().v;
				}
				LOG(("API Error: unexpected update cons %1 "
					"(GroupInfoBox::createChannel)").arg(updates->type()));
				return std::nullopt;
			}
			| [](auto chats) {
				return (!chats->empty()
						&& chats->front().type() == mtpc_channel)
					? base::make_optional(chats)
					: std::nullopt;
			}
			| [&](auto chats) {
				return _navigation->session().data().channel(
					chats->front().c_channel().vid());
			}
			| [&](not_null<ChannelData*> channel) {
				auto image = _photo->takeResultImage();
				if (!image.isNull()) {
					channel->session().api().peerPhoto().upload(
						channel,
						{ std::move(image) });
				}
				if (_ttlPeriod && channel->isMegagroup()) {
					channel->setMessagesTTL(_ttlPeriod);
				}
				channel->session().api().requestFullPeer(channel);
				_createdChannel = channel;
				checkInviteLink();
			};
		if (!success) {
			LOG(("API Error: channel not found in updates "
				"(GroupInfoBox::creationDone)"));
			closeBox();
		}
	}).fail([this](const MTP::Error &error) {
		const auto &type = error.type();
		_creationRequestId = 0;
		const auto controller = _navigation->parentController();
		if (type == u"NO_CHAT_TITLE"_q) {
			_title->setFocus();
			_title->showError();
		} else if (type == u"USER_RESTRICTED"_q) {
			controller->show(
				Ui::MakeInformBox(tr::lng_cant_do_this()),
				Ui::LayerOption::CloseOther);
		} else if (type == u"CHANNELS_TOO_MUCH"_q) {
			controller->show(
				Box(ChannelsLimitBox, &controller->session()),
				Ui::LayerOption::CloseOther); // TODO
		}
	}).send();
}

void GroupInfoBox::checkInviteLink() {
	Expects(_createdChannel != nullptr);

	if (!_createdChannel->inviteLink().isEmpty()) {
		channelReady();
	} else if (_createdChannel->isFullLoaded() && !_creatingInviteLink) {
		_creatingInviteLink = true;
		_createdChannel->session().api().inviteLinks().create(
			_createdChannel,
			crl::guard(this, [=](auto&&) { channelReady(); }));
	} else {
		_createdChannel->session().changes().peerUpdates(
			_createdChannel,
			Data::PeerUpdate::Flag::FullInfo
		) | rpl::take(1) | rpl::start_with_next([=] {
			checkInviteLink();
		}, lifetime());
	}
}

void GroupInfoBox::channelReady() {
	if (_done && !_mustBePublic) {
		const auto callback = _done;
		const auto argument = _createdChannel;
		closeBox();
		callback(argument);
	} else {
		_navigation->parentController()->show(
			Box<SetupChannelBox>(
				_navigation,
				_createdChannel,
				_mustBePublic,
				_done),
			Ui::LayerOption::CloseOther);
	}
}

void GroupInfoBox::descriptionResized() {
	updateMaxHeight();
	update();
}

void GroupInfoBox::updateMaxHeight() {
	auto newHeight = st::boxPadding.top()
		+ st::newGroupInfoPadding.top()
		+ st::defaultUserpicButton.size.height()
		+ st::boxPadding.bottom()
		+ st::newGroupInfoPadding.bottom();
	if (_description) {
		newHeight += st::newGroupDescriptionPadding.top()
			+ _description->height()
			+ st::newGroupDescriptionPadding.bottom();
	}
	setDimensions(st::boxWideWidth, newHeight);
}

SetupChannelBox::SetupChannelBox(
	QWidget*,
	not_null<Window::SessionNavigation*> navigation,
	not_null<ChannelData*> channel,
	bool mustBePublic,
	Fn<void(not_null<PeerData*>)> done)
: _navigation(navigation)
, _channel(channel)
, _api(&_channel->session().mtp())
, _mustBePublic(mustBePublic)
, _done(std::move(done))
, _privacyGroup(
	std::make_shared<Ui::RadioenumGroup<Privacy>>(Privacy::Public))
, _public(
	this,
	_privacyGroup,
	Privacy::Public,
	(channel->isMegagroup()
		? tr::lng_create_public_group_title
		: tr::lng_create_public_channel_title)(tr::now),
	st::defaultBoxCheckbox)
, _private(
	this,
	_privacyGroup,
	Privacy::Private,
	(channel->isMegagroup()
		? tr::lng_create_private_group_title
		: tr::lng_create_private_channel_title)(tr::now),
	st::defaultBoxCheckbox)
, _aboutPublicWidth(st::boxWideWidth
	- st::boxPadding.left()
	- st::defaultBox.buttonPadding.right()
	- st::newGroupPadding.left()
	- st::defaultRadio.diameter
	- st::defaultBoxCheckbox.textPosition.x())
, _aboutPublic(
	st::defaultTextStyle,
	(channel->isMegagroup()
		? tr::lng_create_public_group_about
		: tr::lng_create_public_channel_about)(tr::now),
	kDefaultTextOptions,
	_aboutPublicWidth)
, _aboutPrivate(
	st::defaultTextStyle,
	(channel->isMegagroup()
		? tr::lng_create_private_group_about
		: tr::lng_create_private_channel_about)(tr::now),
	kDefaultTextOptions,
	_aboutPublicWidth)
, _link(
	this,
	st::setupChannelLink,
	nullptr,
	channel->username(),
	channel->session().createInternalLink(QString()))
, _checkTimer([=] { check(); }) {
	if (_mustBePublic) {
		_public.destroy();
		_private.destroy();
	}
}

void SetupChannelBox::prepare() {
	_aboutPublicHeight = _aboutPublic.countHeight(_aboutPublicWidth);

	if (_channel->inviteLink().isEmpty()) {
		_channel->session().api().requestFullPeer(_channel);
	}

	setMouseTracking(true);

	_checkRequestId = _api.request(MTPchannels_CheckUsername(
		_channel->inputChannel,
		MTP_string("preston")
	)).fail([=](const MTP::Error &error) {
		_checkRequestId = 0;
		firstCheckFail(parseError(error.type()));
	}).send();

	addButton(tr::lng_settings_save(), [=] { save(); });

	const auto cancel = [=] {
		if (_mustBePublic) {
			MustBePublicDestroy(_channel);
		}
		closeBox();
	};
	addButton(
		_mustBePublic ? tr::lng_cancel() : tr::lng_create_group_skip(),
		cancel);

	connect(_link, &Ui::MaskedInputField::changed, [=] { handleChange(); });
	_link->setVisible(_privacyGroup->value() == Privacy::Public);

	_privacyGroup->setChangedCallback([=](Privacy value) {
		privacyChanged(value);
	});

	_channel->session().changes().peerUpdates(
		_channel,
		Data::PeerUpdate::Flag::InviteLinks
	) | rpl::start_with_next([=] {
		rtlupdate(_invitationLink);
	}, lifetime());

	boxClosing() | rpl::start_with_next([=] {
		if (!_mustBePublic) {
			AddParticipantsBoxController::Start(_navigation, _channel);
		}
	}, lifetime());

	updateMaxHeight();
}

void SetupChannelBox::setInnerFocus() {
	if (!_link->isHidden()) {
		_link->setFocusFast();
	} else {
		BoxContent::setInnerFocus();
	}
}

void SetupChannelBox::updateMaxHeight() {
	auto newHeight = st::boxPadding.top()
		+ st::newGroupPadding.top()
		+ (_public
			? (_public->heightNoMargins()
				+ _aboutPublicHeight
				+ st::newGroupSkip)
			: 0)
		+ (_private
			? (_private->heightNoMargins()
				+ _aboutPrivate.countHeight(_aboutPublicWidth)
				+ st::newGroupSkip)
			: 0)
		+ st::newGroupPadding.bottom();
	if (!_channel->isMegagroup()
		|| _privacyGroup->value() == Privacy::Public) {
		newHeight += st::newGroupLinkPadding.top()
			+ _link->height()
			+ st::newGroupLinkPadding.bottom();
	}
	setDimensions(st::boxWideWidth, newHeight);
}

void SetupChannelBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_link->hasFocus()) {
			if (_link->text().trimmed().isEmpty()) {
				_link->setFocus();
				_link->showError();
			} else {
				save();
			}
		}
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void SetupChannelBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::boxBg);
	p.setPen(st::newGroupAboutFg);

	if (_public) {
		const auto aboutPublic = QRect(
			st::boxPadding.left()
				+ st::newGroupPadding.left()
				+ st::defaultRadio.diameter
				+ st::defaultBoxCheckbox.textPosition.x(),
			_public->bottomNoMargins(),
			_aboutPublicWidth,
			_aboutPublicHeight);
		_aboutPublic.drawLeft(
			p,
			aboutPublic.x(),
			aboutPublic.y(),
			aboutPublic.width(),
			width());
	}
	if (_private) {
		const auto aboutPrivate = QRect(
			st::boxPadding.left()
				+ st::newGroupPadding.left()
				+ st::defaultRadio.diameter
				+ st::defaultBoxCheckbox.textPosition.x(),
			_private->bottomNoMargins(),
			_aboutPublicWidth,
			_aboutPublicHeight);
		_aboutPrivate.drawLeft(
			p,
			aboutPrivate.x(),
			aboutPrivate.y(),
			aboutPrivate.width(),
			width());
	}
	if (!_channel->isMegagroup() || !_link->isHidden()) {
		p.setPen(st::boxTextFg);
		p.setFont(st::newGroupLinkFont);
		p.drawTextLeft(
			st::boxPadding.left()
				+ st::newGroupPadding.left()
				+ st::defaultInputField.textMargins.left(),
			_link->y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop,
			width(),
			(_link->isHidden()
				? tr::lng_create_group_invite_link
				: tr::lng_create_group_link)(tr::now));
	}

	if (_link->isHidden()) {
		if (!_channel->isMegagroup()) {
			QTextOption option(style::al_left);
			option.setWrapMode(QTextOption::WrapAnywhere);
			p.setFont(_linkOver
				? st::boxTextFont->underline()
				: st::boxTextFont);
			p.setPen(st::defaultLinkButton.color);
			const auto inviteLinkText = _channel->inviteLink().isEmpty()
				? tr::lng_group_invite_create(tr::now)
				: _channel->inviteLink();
			p.drawText(_invitationLink, inviteLinkText, option);
		}
	} else {
		const auto top = _link->y()
			- st::newGroupLinkPadding.top()
			+ st::newGroupLinkTop
			+ st::newGroupLinkFont->ascent
			- st::boxTextFont->ascent;
		if (!_errorText.isEmpty()) {
			p.setPen(st::boxTextFgError);
			p.setFont(st::boxTextFont);
			p.drawTextRight(st::boxPadding.right(), top, width(), _errorText);
		} else if (!_goodText.isEmpty()) {
			p.setPen(st::boxTextFgGood);
			p.setFont(st::boxTextFont);
			p.drawTextRight(st::boxPadding.right(), top, width(), _goodText);
		}
	}
}

void SetupChannelBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	const auto left = st::boxPadding.left() + st::newGroupPadding.left();
	if (_public && _private) {
		_public->moveToLeft(
			left,
			st::boxPadding.top() + st::newGroupPadding.top());
		_private->moveToLeft(
			left,
			_public->bottomNoMargins() + _aboutPublicHeight + st::newGroupSkip);
	}
	_link->resize(
		width()
			- st::boxPadding.left()
			- st::newGroupLinkPadding.left()
			- st::boxPadding.right(),
		_link->height());
	_link->moveToLeft(
		st::boxPadding.left() + st::newGroupLinkPadding.left(),
		(st::boxPadding.top()
			+ st::newGroupPadding.top()
			+ (_public
				? (_public->heightNoMargins()
					+ _aboutPublicHeight
					+ st::newGroupSkip)
				: 0)
			+ (_private
				? (_private->heightNoMargins()
					+ _aboutPrivate.countHeight(_aboutPublicWidth)
					+ st::newGroupSkip)
				: 0)
			+ st::newGroupPadding.bottom()
			+ st::newGroupLinkPadding.top()));
	_invitationLink = QRect(
		_link->x(),
		_link->y() + (_link->height() / 2) - st::boxTextFont->height,
		_link->width(),
		2 * st::boxTextFont->height);
}

void SetupChannelBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void SetupChannelBox::mousePressEvent(QMouseEvent *e) {
	if (!_linkOver) {
		return;
	} else if (!_channel->inviteLink().isEmpty()) {
		QGuiApplication::clipboard()->setText(_channel->inviteLink());
		showToast(tr::lng_create_channel_link_copied(tr::now));
	} else if (_channel->isFullLoaded() && !_creatingInviteLink) {
		_creatingInviteLink = true;
		_channel->session().api().inviteLinks().create(_channel);
	}
}

void SetupChannelBox::leaveEventHook(QEvent *e) {
	updateSelected(QCursor::pos());
}

void SetupChannelBox::updateSelected(const QPoint &cursorGlobalPosition) {
	QPoint p(mapFromGlobal(cursorGlobalPosition));

	bool linkOver = _invitationLink.contains(p);
	if (linkOver != _linkOver) {
		_linkOver = linkOver;
		update();
		setCursor(_linkOver ? style::cur_pointer : style::cur_default);
	}
}

void SetupChannelBox::save() {
	const auto saveUsername = [&](const QString &link) {
		_sentUsername = link;
		_saveRequestId = _api.request(MTPchannels_UpdateUsername(
			_channel->inputChannel,
			MTP_string(_sentUsername)
		)).done([=] {
			const auto done = _done;
			const auto channel = _channel;
			_channel->setName(
				TextUtilities::SingleLine(_channel->name()),
				_sentUsername);
			closeBox(); // Deletes `this`.
			if (done) {
				done(channel);
			}
		}).fail([=](const MTP::Error &error) {
			_saveRequestId = 0;
			updateFail(parseError(error.type()));
		}).send();
	};
	if (_saveRequestId) {
		return;
	} else if (_privacyGroup->value() == Privacy::Private) {
		closeBox();
	} else {
		const auto link = _link->text().trimmed();
		if (link.isEmpty()) {
			_link->setFocus();
			_link->showError();
			return;
		}
		saveUsername(link);
	}
}

void SetupChannelBox::handleChange() {
	const auto name = _link->text().trimmed();
	if (name.isEmpty()) {
		if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
			_errorText = _goodText = QString();
			update();
		}
		_checkTimer.cancel();
	} else {
		const auto len = int(name.size());
		for (auto i = 0; i < len; ++i) {
			const auto ch = name.at(i);
			if ((ch < 'A' || ch > 'Z')
				&& (ch < 'a' || ch > 'z')
				&& (ch < '0' || ch > '9')
				&& ch != '_') {
				const auto badSymbols =
					tr::lng_create_channel_link_bad_symbols(tr::now);
				if (_errorText != badSymbols) {
					_errorText = badSymbols;
					update();
				}
				_checkTimer.cancel();
				return;
			}
		}
		if (name.size() < Ui::EditPeer::kMinUsernameLength) {
			const auto tooShort =
				tr::lng_create_channel_link_too_short(tr::now);
			if (_errorText != tooShort) {
				_errorText = tooShort;
				update();
			}
			_checkTimer.cancel();
		} else {
			if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
				_errorText = _goodText = QString();
				update();
			}
			_checkTimer.callOnce(Ui::EditPeer::kUsernameCheckTimeout);
		}
	}
}

void SetupChannelBox::check() {
	if (_checkRequestId) {
		_api.request(_checkRequestId).cancel();
	}
	const auto link = _link->text().trimmed();
	if (link.size() >= Ui::EditPeer::kMinUsernameLength) {
		_checkUsername = link;
		_checkRequestId = _api.request(MTPchannels_CheckUsername(
			_channel->inputChannel,
			MTP_string(link)
		)).done([=](const MTPBool &result) {
			_checkRequestId = 0;
			_errorText = (mtpIsTrue(result)
					|| _checkUsername == _channel->username())
				? QString()
				: tr::lng_create_channel_link_occupied(tr::now);
			_goodText = _errorText.isEmpty()
				? tr::lng_create_channel_link_available(tr::now)
				: QString();
			update();
		}).fail([=](const MTP::Error &error) {
			_checkRequestId = 0;
			checkFail(parseError(error.type()));
		}).send();
	}
}

void SetupChannelBox::privacyChanged(Privacy value) {
	if (value == Privacy::Public) {
		if (_tooMuchUsernames) {
			_privacyGroup->setValue(Privacy::Private);
			const auto callback = crl::guard(this, [=] {
				_tooMuchUsernames = false;
				_privacyGroup->setValue(Privacy::Public);
				check();
			});
			Ui::show(
				Box(PublicLinksLimitBox, _navigation, callback),
				Ui::LayerOption::KeepOther);
			return;
		}
		_link->show();
		_link->setDisplayFocused(true);
		_link->setFocus();
	} else {
		_link->hide();
		setFocus();
	}
	if (_channel->isMegagroup()) {
		updateMaxHeight();
	}
	update();
}

SetupChannelBox::UsernameResult SetupChannelBox::parseError(
		const QString &error) {
	if (error == u"USERNAME_NOT_MODIFIED"_q) {
		return UsernameResult::Ok;
	} else if (error == u"USERNAME_INVALID"_q) {
		return UsernameResult::Invalid;
	} else if (error == u"USERNAME_OCCUPIED"_q) {
		return UsernameResult::Occupied;
	} else if (error == u"USERNAME_PURCHASE_AVAILABLE"_q) {
		return UsernameResult::Occupied;
	} else if (error == u"USERNAMES_UNAVAILABLE"_q) {
		return UsernameResult::Occupied;
	} else if (error == u"CHANNEL_PUBLIC_GROUP_NA"_q) {
		return UsernameResult::NA;
	} else if (error == u"CHANNELS_ADMIN_PUBLIC_TOO_MUCH"_q) {
		return UsernameResult::ChatsTooMuch;
	} else {
		return UsernameResult::Unknown;
	}
}

void SetupChannelBox::updateFail(UsernameResult result) {
	if ((result == UsernameResult::Ok)
		|| (_sentUsername == _channel->username())) {
		_channel->setName(
			TextUtilities::SingleLine(_channel->name()),
			TextUtilities::SingleLine(_sentUsername));
		closeBox();
	} else if (result == UsernameResult::Invalid) {
		_link->setFocus();
		_link->showError();
		_errorText = tr::lng_create_channel_link_invalid(tr::now);
		update();
	} else if (result == UsernameResult::Occupied) {
		_link->setFocus();
		_link->showError();
		_errorText = tr::lng_create_channel_link_occupied(tr::now);
		update();
	} else {
		_link->setFocus();
	}
}

void SetupChannelBox::checkFail(UsernameResult result) {
	if (result == UsernameResult::NA) {
		if (_mustBePublic) {
			mustBePublicFailed();
		}
		getDelegate()->hideLayer();
	} else if (result == UsernameResult::ChatsTooMuch) {
		if (_mustBePublic) {
			showRevokePublicLinkBoxForEdit();
		} else {
			_tooMuchUsernames = true;
			_privacyGroup->setValue(Privacy::Private);
		}
	} else if (result == UsernameResult::Invalid) {
		_errorText = tr::lng_create_channel_link_invalid(tr::now);
		update();
	} else if ((result == UsernameResult::Occupied)
			&& _checkUsername != _channel->username()) {
		_errorText = tr::lng_create_channel_link_occupied(tr::now);
		update();
	} else {
		_goodText = QString();
		_link->setFocus();
	}
}

void SetupChannelBox::showRevokePublicLinkBoxForEdit() {
	const auto channel = _channel;
	const auto mustBePublic = _mustBePublic;
	const auto done = _done;
	const auto navigation = _navigation;
	const auto revoked = std::make_shared<bool>(false);
	const auto callback = [=] {
		*revoked = true;
		navigation->parentController()->show(
			Box<SetupChannelBox>(navigation, channel, mustBePublic, done));
	};
	const auto revoker = navigation->parentController()->show(
		Box(PublicLinksLimitBox, navigation, callback));
	const auto session = &navigation->session();
	revoker->boxClosing(
	) | rpl::start_with_next(crl::guard(session, [=] {
		base::call_delayed(200, session, [=] {
			if (*revoked) {
				return;
			}
			MustBePublicDestroy(channel);
		});
	}), revoker->lifetime());
	closeBox();
}

void SetupChannelBox::mustBePublicFailed() {
	MustBePublicFailed(_navigation, _channel);
}

void SetupChannelBox::firstCheckFail(UsernameResult result) {
	if (result == UsernameResult::NA) {
		if (_mustBePublic) {
			mustBePublicFailed();
		}
		getDelegate()->hideLayer();
	} else if (result == UsernameResult::ChatsTooMuch) {
		if (_mustBePublic) {
			showRevokePublicLinkBoxForEdit();
		} else {
			_tooMuchUsernames = true;
			_privacyGroup->setValue(Privacy::Private);
		}
	} else {
		_goodText = QString();
		_link->setFocus();
	}
}

EditNameBox::EditNameBox(QWidget*, not_null<UserData*> user)
: _user(user)
, _api(&_user->session().mtp())
, _first(
	this,
	st::defaultInputField,
	tr::lng_signup_firstname(),
	_user->firstName)
, _last(
	this,
	st::defaultInputField,
	tr::lng_signup_lastname(),
	_user->lastName)
, _invertOrder(langFirstNameGoesSecond()) {
}

void EditNameBox::prepare() {
	auto newHeight = st::contactPadding.top() + _first->height();

	setTitle(tr::lng_edit_self_title());
	newHeight += st::contactSkip + _last->height();

	newHeight += st::boxPadding.bottom() + st::contactPadding.bottom();
	setDimensions(st::boxWidth, newHeight);

	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });
	if (_invertOrder) {
		setTabOrder(_last, _first);
	}
	_first->setMaxLength(Ui::EditPeer::kMaxUserFirstLastName);
	_last->setMaxLength(Ui::EditPeer::kMaxUserFirstLastName);

	_first->submits(
	) | rpl::start_with_next([=] { submit(); }, _first->lifetime());
	_last->submits(
	) | rpl::start_with_next([=] { submit(); }, _last->lifetime());

	_first->customTab(true);
	_last->customTab(true);

	_first->tabbed(
	) | rpl::start_with_next([=] {
		_last->setFocus();
	}, _first->lifetime());
	_last->tabbed(
	) | rpl::start_with_next([=] {
		_first->setFocus();
	}, _last->lifetime());
}

void EditNameBox::setInnerFocus() {
	(_invertOrder ? _last : _first)->setFocusFast();
}

void EditNameBox::submit() {
	if (_first->hasFocus()) {
		_last->setFocus();
	} else if (_last->hasFocus()) {
		if (_first->getLastText().trimmed().isEmpty()) {
			_first->setFocus();
			_first->showError();
		} else if (_last->getLastText().trimmed().isEmpty()) {
			_last->setFocus();
			_last->showError();
		} else {
			save();
		}
	}
}

void EditNameBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_first->resize(
		width()
			- st::boxPadding.left()
			- st::newGroupInfoPadding.left()
			- st::boxPadding.right(),
		_first->height());
	_last->resize(_first->size());
	const auto left = st::boxPadding.left() + st::newGroupInfoPadding.left();
	const auto skip = st::contactSkip;
	if (_invertOrder) {
		_last->moveToLeft(left, st::contactPadding.top());
		_first->moveToLeft(left, _last->y() + _last->height() + skip);
	} else {
		_first->moveToLeft(left, st::contactPadding.top());
		_last->moveToLeft(left, _first->y() + _first->height() + skip);
	}
}

void EditNameBox::save() {
	if (_requestId) {
		return;
	}

	auto first = TextUtilities::PrepareForSending(_first->getLastText());
	auto last = TextUtilities::PrepareForSending(_last->getLastText());
	if (first.isEmpty() && last.isEmpty()) {
		if (_invertOrder) {
			_last->setFocus();
			_last->showError();
		} else {
			_first->setFocus();
			_first->showError();
		}
		return;
	}
	if (first.isEmpty()) {
		first = last;
		last = QString();
	}
	_sentName = first;
	auto flags = MTPaccount_UpdateProfile::Flag::f_first_name
		| MTPaccount_UpdateProfile::Flag::f_last_name;
	_requestId = _api.request(MTPaccount_UpdateProfile(
		MTP_flags(flags),
		MTP_string(first),
		MTP_string(last),
		MTPstring()
	)).done([=](const MTPUser &user) {
		_user->owner().processUser(user);
		closeBox();
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		saveSelfFail(error.type());
	}).send();
}

void EditNameBox::saveSelfFail(const QString &error) {
	if (error == "NAME_NOT_MODIFIED") {
		_user->setName(
			TextUtilities::SingleLine(_first->getLastText().trimmed()),
			TextUtilities::SingleLine(_last->getLastText().trimmed()),
			QString(),
			TextUtilities::SingleLine(_user->username()));
		closeBox();
	} else if (error == "FIRSTNAME_INVALID") {
		_first->setFocus();
		_first->showError();
	} else if (error == "LASTNAME_INVALID") {
		_last->setFocus();
		_last->showError();
	} else {
		_first->setFocus();
	}
}
