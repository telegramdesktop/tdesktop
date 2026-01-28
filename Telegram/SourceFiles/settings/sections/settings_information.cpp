/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_information.h"

#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/text/text_utilities.h"
#include "ui/delayed_activation.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/unread_badge_paint.h"
#include "ui/ui_utility.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "boxes/add_contact_box.h"
#include "boxes/premium_limits_box.h"
#include "boxes/username_box.h"
#include "boxes/peers/edit_peer_color_box.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_peer_values.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_premium_limits.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_badge.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "mtproto/mtproto_dc_options.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "api/api_user_names.h"
#include "api/api_user_privacy.h"
#include "base/call_delayed.h"
#include "base/options.h"
#include "base/unixtime.h"
#include "base/random.h"
#include "styles/style_chat.h" // popupMenuExpandedSeparator
#include "styles/style_dialogs.h" // dialogsPremiumIcon
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

#include <QtGui/QGuiApplication>
#include <QtCore/QBuffer>

namespace Settings {
namespace {

using namespace Builder;

struct InformationHighlightTargets {
	QPointer<Ui::RpWidget> photo;
	QPointer<Ui::RpWidget> uploadPhoto;
	QPointer<Ui::RpWidget> bio;
	QPointer<Ui::RpWidget> colorButton;
	QPointer<Ui::RpWidget> channelButton;
	QPointer<Ui::RpWidget> addAccount;
	QPointer<Ui::RpWidget> name;
	QPointer<Ui::RpWidget> phone;
	QPointer<Ui::RpWidget> username;
	QPointer<Ui::RpWidget> birthday;
};

constexpr auto kSaveBioTimeout = 1000;
constexpr auto kPlayStatusLimit = 2;

class ComposedBadge final : public Ui::RpWidget {
public:
	ComposedBadge(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::SettingsButton*> button,
		not_null<::Main::Session*> session,
		rpl::producer<QString> &&text,
		bool hasUnread,
		Fn<bool()> animationPaused);

private:
	rpl::variable<QString> _text;
	rpl::event_stream<int> _unreadWidth;
	rpl::event_stream<int> _premiumWidth;

	QPointer<Ui::RpWidget> _unread;
	Info::Profile::Badge _badge;

};

ComposedBadge::ComposedBadge(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::SettingsButton*> button,
	not_null<::Main::Session*> session,
	rpl::producer<QString> &&text,
	bool hasUnread,
	Fn<bool()> animationPaused)
: Ui::RpWidget(parent)
, _text(std::move(text))
, _badge(
		this,
		st::settingsInfoPeerBadge,
		session,
		Info::Profile::BadgeContentForPeer(session->user()),
		nullptr,
		std::move(animationPaused),
		kPlayStatusLimit,
		Info::Profile::BadgeType::Premium) {
	if (hasUnread) {
		_unread = Badge::CreateUnread(this, rpl::single(
			rpl::empty
		) | rpl::then(
			session->data().unreadBadgeChanges()
		) | rpl::map([=] {
			auto &owner = session->data();
			return Badge::UnreadBadge{
				owner.unreadWithMentionsBadge(),
				owner.unreadWithMentionsBadgeMuted(),
			};
		}));
		rpl::combine(
			_unread->shownValue(),
			_unread->widthValue()
		) | rpl::map([=](bool shown, int width) {
			return shown ? width : 0;
		}) | rpl::start_to_stream(_unreadWidth, _unread->lifetime());
	}

	_badge.updated(
	) | rpl::on_next([=] {
		if (const auto button = _badge.widget()) {
			button->widthValue(
			) | rpl::start_to_stream(_premiumWidth, button->lifetime());
		} else {
			_premiumWidth.fire(0);
		}
	}, lifetime());

	auto textWidth = _text.value() | rpl::map([=] {
		return button->fullTextWidth();
	});
	rpl::combine(
		_unreadWidth.events_starting_with(_unread ? _unread->width() : 0),
		_premiumWidth.events_starting_with(_badge.widget()
			? _badge.widget()->width()
			: 0),
		std::move(textWidth),
		button->sizeValue()
	) | rpl::on_next([=](
			int unreadWidth,
			int premiumWidth,
			int textWidth,
			const QSize &buttonSize) {
		const auto &st = button->st();
		const auto skip = st.style.font->spacew;
		const auto textRightPosition = st.padding.left()
			+ textWidth
			+ skip;
		const auto minWidth = unreadWidth + premiumWidth + skip;
		const auto maxTextWidth = buttonSize.width()
			- minWidth
			- st.padding.right();

		const auto finalTextRight = std::min(textRightPosition, maxTextWidth);

		resize(
			buttonSize.width() - st.padding.right() - finalTextRight,
			buttonSize.height());

		_badge.move(
			0,
			st.padding.top(),
			buttonSize.height() - st.padding.top());
		if (_unread) {
			_unread->moveToRight(
				0,
				(buttonSize.height() - _unread->height()) / 2);
		}
	}, lifetime());
}

class AccountsList final {
public:
	AccountsList(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<> closeRequests() const;
	[[nodiscard]] Ui::RpWidget *addAccountButton() const;

private:
	void setup();

	[[nodiscard]] not_null<Ui::SlideWrap<Ui::SettingsButton>*> setupAdd();
	void rebuild();

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::VerticalLayout*> _outer;
	int _outerIndex = 0;

	Ui::SlideWrap<Ui::SettingsButton> *_addAccount = nullptr;
	base::flat_map<
		not_null<::Main::Account*>,
		base::unique_qptr<Ui::SettingsButton>> _watched;

	base::unique_qptr<Ui::PopupMenu> _contextMenu;
	std::unique_ptr<Ui::VerticalLayoutReorder> _reorder;
	int _reordering = 0;

	rpl::event_stream<> _closeRequests;

	base::binary_guard _accountSwitchGuard;

};

[[nodiscard]] rpl::producer<TextWithEntities> StatusValue(
		not_null<UserData*> user) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto timer = lifetime.make_state<base::Timer>();
		const auto push = [=] {
			const auto now = base::unixtime::now();
			consumer.put_next(Data::OnlineTextActive(user, now)
				? tr::link(Data::OnlineText(user, now))
				: tr::marked(Data::OnlineText(user, now)));
			timer->callOnce(Data::OnlineChangeTimeout(user, now));
		};
		timer->setCallback(push);
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::OnlineStatus
		) | rpl::on_next(push, lifetime);
		return lifetime;
	};
}

void SetupPhoto(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> self,
		InformationHighlightTargets *targets) {
	const auto wrap = container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::settingsInfoPhotoHeight));
	const auto photo = Ui::CreateChild<Ui::UserpicButton>(
		wrap,
		controller,
		self,
		Ui::UserpicButton::Role::OpenPhoto,
		Ui::UserpicButton::Source::PeerPhoto,
		st::settingsInfoPhoto);
	const auto upload = CreateUploadSubButton(wrap, controller);
	if (targets) {
		targets->photo = photo;
		targets->uploadPhoto = upload;
	}

	upload->chosenImages(
	) | rpl::on_next([=](Ui::UserpicButton::ChosenImage &&chosen) {
		auto &image = chosen.image;
		UpdatePhotoLocally(self, image);
		photo->showCustom(base::duplicate(image));
		self->session().api().peerPhoto().upload(
			self,
			{
				std::move(image),
				chosen.markup.documentId,
				chosen.markup.colors,
			});
	}, upload->lifetime());

	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		Info::Profile::NameValue(self),
		st::settingsCoverName);
	const auto status = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		StatusValue(self),
		st::settingsCoverStatus);
	status->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		wrap->widthValue(),
		photo->widthValue(),
		Info::Profile::NameValue(self),
		status->widthValue()
	) | rpl::on_next([=](
			int max,
			int photoWidth,
			const QString&,
			int statusWidth) {
		photo->moveToLeft(
			(max - photoWidth) / 2,
			st::settingsInfoPhotoTop);
		upload->moveToLeft(
			((max - photoWidth) / 2
				+ photoWidth
				- upload->width()
				+ st::settingsInfoUploadLeft),
			photo->y() + photo->height() - upload->height());
		const auto skip = st::settingsButton.iconLeft;
		name->resizeToNaturalWidth(max - 2 * skip);
		name->moveToLeft(
			(max - name->width()) / 2,
			(photo->y() + photo->height() + st::settingsInfoPhotoSkip));
		status->moveToLeft(
			(max - statusWidth) / 2,
			(name->y() + name->height() + st::settingsInfoNameSkip));
	}, photo->lifetime());
}

void ShowMenu(
		QWidget *parent,
		const QString &copyButton,
		const QString &text) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(parent);

	menu->addAction(copyButton, [=] {
		QGuiApplication::clipboard()->setText(text);
	});
	menu->popup(QCursor::pos());
}

not_null<Ui::SettingsButton*> AddRow(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> label,
		rpl::producer<TextWithEntities> value,
		const QString &copyButton,
		Fn<void()> edit,
		IconDescriptor &&descriptor) {
	const auto wrap = AddButtonWithLabel(
		container,
		std::move(label),
		std::move(value) | rpl::map([](const auto &t) { return t.text; }),
		st::settingsButton,
		std::move(descriptor));
	const auto forcopy = Ui::CreateChild<QString>(wrap.get());
	wrap->setAcceptBoth();
	wrap->clicks(
	) | rpl::filter([=] {
		return !wrap->isDisabled();
	}) | rpl::on_next([=](Qt::MouseButton button) {
		if (button == Qt::LeftButton) {
			edit();
		} else if (!forcopy->isEmpty()) {
			ShowMenu(wrap, copyButton, *forcopy);
		}
	}, wrap->lifetime());

	auto existing = base::duplicate(
		value
	) | rpl::map([](const TextWithEntities &text) {
		return text.entities.isEmpty();
	});
	base::duplicate(
		value
	) | rpl::filter([](const TextWithEntities &text) {
		return text.entities.isEmpty();
	}) | rpl::on_next([=](const TextWithEntities &text) {
		*forcopy = text.text;
	}, wrap->lifetime());
	return wrap;
}

void SetupBirthday(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> self,
		InformationHighlightTargets *targets) {
	const auto session = &self->session();

	Ui::AddSkip(container);

	auto value = rpl::combine(
		Info::Profile::BirthdayValue(self),
		tr::lng_settings_birthday_add()
	) | rpl::map([](Data::Birthday birthday, const QString &add) {
		const auto text = Data::BirthdayText(birthday);
		return TextWithEntities{ !text.isEmpty() ? text : add };
	});
	const auto edit = [=] {
		Core::App().openInternalUrl(
			u"internal:edit_birthday"_q,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller),
			}));
	};
	const auto birthdayButton = AddRow(
		container,
		tr::lng_settings_birthday_label(),
		std::move(value),
		tr::lng_mediaview_copy(tr::now),
		edit,
		{ &st::menuIconGiftPremium });
	if (targets) {
		targets->birthday = birthdayButton;
	}

	const auto key = Api::UserPrivacy::Key::Birthday;
	session->api().userPrivacy().reload(key);
	auto isExactlyContacts = session->api().userPrivacy().value(
		key
	) | rpl::map([=](const Api::UserPrivacy::Rule &value) {
		return (value.option == Api::UserPrivacy::Option::Contacts)
			&& value.always.peers.empty()
			&& !value.always.premiums
			&& value.never.peers.empty();
	}) | rpl::distinct_until_changed();

	Ui::AddSkip(container);
	Ui::AddDividerText(container, rpl::conditional(
		std::move(isExactlyContacts),
		tr::lng_settings_birthday_contacts(
			lt_link,
			tr::lng_settings_birthday_contacts_link(
				tr::url(u"internal:edit_privacy_birthday"_q)),
			tr::marked),
		tr::lng_settings_birthday_about(
			lt_link,
			tr::lng_settings_birthday_about_link(
				tr::url(u"internal:edit_privacy_birthday"_q)),
			tr::marked)));
}

void SetupPersonalChannel(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> self,
		InformationHighlightTargets *targets) {
	Ui::AddSkip(container);

	auto value = rpl::combine(
		Info::Profile::PersonalChannelValue(self),
		tr::lng_settings_channel_add()
	) | rpl::map([](ChannelData *channel, const QString &add) {
		return TextWithEntities{ channel ? channel->name() : add };
	});
	const auto edit = [=] {
		Core::App().openInternalUrl(
			u"internal:edit_personal_channel"_q,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller),
			}));
	};
	const auto channelButton = AddRow(
		container,
		tr::lng_settings_channel_label(),
		std::move(value),
		tr::lng_mediaview_copy(tr::now),
		edit,
		{ &st::menuIconChannel });

	const auto colorButton = AddPeerColorButton(
		container,
		controller->uiShow(),
		self,
		st::settingsColorButton);
	if (targets) {
		targets->channelButton = channelButton;
		targets->colorButton = colorButton;
	}

	Ui::AddSkip(container);
	Ui::AddDivider(container);
}

void SetupRows(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> self,
		InformationHighlightTargets *targets) {
	const auto session = &self->session();

	Ui::AddSkip(container);

	const auto showEditName = [=] {
		if (controller->showFrozenError()) {
			return;
		}
		controller->show(Box<EditNameBox>(self));
	};
	const auto nameButton = AddRow(
		container,
		tr::lng_settings_name_label(),
		Info::Profile::NameValue(self) | rpl::map(tr::marked),
		tr::lng_profile_copy_fullname(tr::now),
		showEditName,
		{ &st::menuIconProfile });
	if (targets) {
		targets->name = nameButton;
	}

	const auto showChangePhone = [=] {
		controller->show(
			Ui::MakeInformBox(tr::lng_change_phone_error()));
		controller->window().activate();
	};
	const auto phoneButton = AddRow(
		container,
		tr::lng_settings_phone_label(),
		Info::Profile::PhoneValue(self),
		tr::lng_profile_copy_phone(tr::now),
		showChangePhone,
		{ &st::menuIconPhone });
	if (targets) {
		targets->phone = phoneButton;
	}

	auto username = Info::Profile::UsernameValue(self);
	auto empty = base::duplicate(
		username
	) | rpl::map([](const TextWithEntities &username) {
		return username.text.isEmpty();
	});
	auto label = rpl::combine(
		tr::lng_settings_username_label(),
		std::move(empty)
	) | rpl::map([](const QString &label, bool empty) {
		return empty ? "t.me/username" : label;
	});
	auto usernameValue = rpl::combine(
		std::move(username),
		tr::lng_settings_username_add()
	) | rpl::map([](const TextWithEntities &username, const QString &add) {
		if (!username.text.isEmpty()) {
			return username;
		}
		auto result = TextWithEntities{ add };
		result.entities.push_back({
			EntityType::CustomUrl,
			0,
			int(add.size()),
			"internal:edit_username" });
		return result;
	});
	session->api().usernames().requestToCache(session->user());
	const auto usernameButton = AddRow(
		container,
		std::move(label),
		std::move(usernameValue),
		tr::lng_context_copy_mention(tr::now),
		[=] {
			if (controller->showFrozenError()) {
				return;
			}
			const auto box = controller->show(
				Box(UsernamesBox, session->user()));
			box->boxClosing(
			) | rpl::on_next([=] {
				session->api().usernames().requestToCache(session->user());
			}, box->lifetime());
		},
		{ &st::menuIconUsername });
	if (targets) {
		targets->username = usernameButton;
	}

	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_settings_username_about());
}

void SetupBio(
		not_null<Ui::VerticalLayout*> container,
		not_null<UserData*> self,
		InformationHighlightTargets *targets) {
	const auto limits = Data::PremiumLimits(&self->session());
	const auto defaultLimit = limits.aboutLengthDefault();
	const auto premiumLimit = limits.aboutLengthPremium();
	const auto bioStyle = [=] {
		auto result = st::settingsBio;
		result.textMargins.setRight(st::boxTextFont->spacew
			+ st::boxTextFont->width('-' + QString::number(premiumLimit)));
		return result;
	};
	const auto style = Ui::AttachAsChild(container, bioStyle());
	const auto current = Ui::AttachAsChild(container, self->about());
	const auto changed = Ui::CreateChild<rpl::event_stream<bool>>(
		container.get());
	const auto bio = container->add(
		object_ptr<Ui::InputField>(
			container,
			*style,
			Ui::InputField::Mode::MultiLine,
			tr::lng_bio_placeholder(),
			*current),
		st::settingsBioMargins);
	if (targets) {
		targets->bio = bio;
	}

	const auto countdown = Ui::CreateChild<Ui::FlatLabel>(
		container.get(),
		QString(),
		st::settingsBioCountdown);

	rpl::combine(
		bio->geometryValue(),
		countdown->widthValue()
	) | rpl::on_next([=](QRect geometry, int width) {
		countdown->move(
			geometry.x() + geometry.width() - width,
			geometry.y() + style->textMargins.top());
	}, countdown->lifetime());

	const auto assign = [=](QString text) {
		auto position = bio->textCursor().position();
		bio->setText(text.replace('\n', ' '));
		auto cursor = bio->textCursor();
		cursor.setPosition(position);
		bio->setTextCursor(cursor);
	};
	const auto updated = [=] {
		auto text = bio->getLastText();
		if (text.indexOf('\n') >= 0) {
			assign(text);
			text = bio->getLastText();
		}
		changed->fire(*current != text);
		const auto limit = self->isPremium() ? premiumLimit : defaultLimit;
		const auto countLeft = limit - Ui::ComputeFieldCharacterCount(bio);
		countdown->setText(QString::number(countLeft));
		countdown->setTextColorOverride(
			countLeft < 0 ? st::boxTextFgError->c : std::optional<QColor>());
	};
	const auto save = [=] {
		self->session().api().saveSelfBio(
			TextUtilities::PrepareForSending(bio->getLastText()));
	};

	Info::Profile::AboutValue(
		self
	) | rpl::on_next([=](const TextWithEntities &text) {
		const auto wasChanged = (*current != bio->getLastText());
		*current = text.text;
		if (wasChanged) {
			changed->fire(*current != bio->getLastText());
		} else {
			assign(text.text);
			*current = bio->getLastText();
		}
	}, bio->lifetime());

	const auto generation = Ui::CreateChild<int>(bio);
	changed->events(
	) | rpl::on_next([=](bool changed) {
		if (changed) {
			const auto saved = *generation = std::abs(*generation) + 1;
			base::call_delayed(kSaveBioTimeout, bio, [=] {
				if (*generation == saved) {
					save();
					*generation = 0;
				}
			});
		} else if (*generation > 0) {
			*generation = -*generation;
		}
	}, bio->lifetime());

	container->lifetime().add([=] {
		if (*generation > 0) {
			save();
		}
	});

	bio->setMaxLength(premiumLimit * 2);
	bio->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
	auto cursor = bio->textCursor();
	cursor.setPosition(bio->getLastText().size());
	bio->setTextCursor(cursor);
	bio->submits() | rpl::on_next([=] { save(); }, bio->lifetime());
	bio->changes() | rpl::on_next(updated, bio->lifetime());
	bio->setInstantReplaces(Ui::InstantReplaces::Default());
	bio->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		container->window(),
		bio,
		&self->session());
	updated();

	Ui::AddDividerText(container, tr::lng_settings_about_bio());
}

void SetupAccountsWrap(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		InformationHighlightTargets *targets) {
	Ui::AddSkip(container);

	auto events = SetupAccounts(container, controller);
	if (targets) {
		targets->addAccount = events.addAccountButton;
	}
}

[[nodiscard]] bool IsAltShift(Qt::KeyboardModifiers modifiers) {
	return (modifiers & Qt::ShiftModifier) && (modifiers & Qt::AltModifier);
}

[[nodiscard]] object_ptr<Ui::SettingsButton> MakeAccountButton(
		QWidget *parent,
		not_null<Window::SessionController*> window,
		not_null<::Main::Account*> account,
		Fn<void(Qt::KeyboardModifiers)> callback,
		bool locked) {
	const auto active = (account == &window->session().account());
	const auto session = &account->session();
	const auto user = session->user();

	auto text = rpl::single(
		user->name()
	) | rpl::then(session->changes().realtimeNameUpdates(
		user
	) | rpl::map([=] {
		return user->name();
	}));
	auto result = object_ptr<Ui::SettingsButton>(
		parent,
		rpl::duplicate(text),
		st::mainMenuAddAccountButton);
	const auto raw = result.data();

	{
		const auto container = Badge::AddRight(raw, st::mainMenuAccountLine);
		const auto composedBadge = Ui::CreateChild<ComposedBadge>(
			container.get(),
			raw,
			session,
			std::move(text),
			!active,
			[=] { return window->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer); });
		composedBadge->sizeValue(
		) | rpl::on_next([=](const QSize &s) {
			container->resize(s);
		}, container->lifetime());
	}

	struct State {
		State(QWidget *parent) : userpic(parent) {
			userpic.setAttribute(Qt::WA_TransparentForMouseEvents);
		}

		Ui::RpWidget userpic;
		Ui::PeerUserpicView view;
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = raw->lifetime().make_state<State>(raw);

	const auto userpicSkip = 2 * st::mainMenuAccountLine + st::lineWidth;
	const auto userpicSize = st::mainMenuAccountSize
		+ userpicSkip * 2;
	raw->heightValue(
	) | rpl::on_next([=](int height) {
		const auto left = st::mainMenuAddAccountButton.iconLeft
			+ (st::settingsIconAdd.width() - userpicSize) / 2;
		const auto top = (height - userpicSize) / 2;
		state->userpic.setGeometry(left, top, userpicSize, userpicSize);
	}, state->userpic.lifetime());

	state->userpic.paintRequest(
	) | rpl::on_next([=] {
		auto p = Painter(&state->userpic);
		const auto size = st::mainMenuAccountSize;
		const auto line = st::mainMenuAccountLine;
		const auto skip = 2 * line + st::lineWidth;
		const auto full = size + skip * 2;
		user->paintUserpicLeft(p, state->view, skip, skip, full, size);
		if (active) {
			const auto shift = st::lineWidth + (line * 0.5);
			const auto diameter = full - 2 * shift;
			const auto rect = QRectF(shift, shift, diameter, diameter);
			auto hq = PainterHighQualityEnabler(p);
			auto pen = st::windowBgActive->p; // The same as '+' in add.
			pen.setWidthF(line);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(rect);
		}
	}, state->userpic.lifetime());

	raw->setAcceptBoth(true);
	raw->clicks(
	) | rpl::on_next([=](Qt::MouseButton which) {
		if (which == Qt::LeftButton) {
			callback(raw->clickModifiers());
			return;
		} else if (which == Qt::MiddleButton) {
			callback(Qt::ControlModifier);
			return;
		} else if (which != Qt::RightButton) {
			return;
		}
		if (state->menu) {
			return;
		}
		const auto isActive = session == &window->session();
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			raw,
			st::popupMenuExpandedSeparator);
		const auto addAction = Ui::Menu::CreateAddActionCallback(
			state->menu);
		if (!isActive) {
			addAction(tr::lng_context_new_window(tr::now), [=] {
				Ui::PreventDelayedActivation();
				callback(Qt::ControlModifier);
			}, &st::menuIconNewWindow);
			Window::AddSeparatorAndShiftUp(addAction);
		}

		addAction(tr::lng_profile_copy_phone(tr::now), [=] {
			const auto phone = rpl::variable<TextWithEntities>(
				Info::Profile::PhoneValue(session->user()));
			QGuiApplication::clipboard()->setText(phone.current().text);
		}, &st::menuIconCopy);

		if (!locked) {
			if (!isActive) {
				addAction(tr::lng_menu_activate(tr::now), [=] {
					callback({});
				}, &st::menuIconProfile);
			}
			Window::MenuAddMarkAsReadAllChatsAction(
				session,
				window->uiShow(),
				addAction);
		}

		if (!isActive) {
			auto logoutCallback = [=] {
				const auto callback = [=](Fn<void()> &&close) {
					close();
					Core::App().logoutWithChecks(&session->account());
				};
				window->show(
					Ui::MakeConfirmBox({
						.text = tr::lng_sure_logout(),
						.confirmed = crl::guard(session, callback),
						.confirmText = tr::lng_settings_logout(),
						.confirmStyle = &st::attentionBoxButton,
					}),
					Ui::LayerOption::CloseOther);
			};
			addAction({
				.text = tr::lng_settings_logout(tr::now),
				.handler = std::move(logoutCallback),
				.icon = &st::menuIconLeaveAttention,
				.isAttention = true,
			});
		}
		state->menu->popup(QCursor::pos());
	}, raw->lifetime());

	return result;
}

AccountsList::AccountsList(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller)
: _controller(controller)
, _outer(container)
, _outerIndex(container->count()) {
	setup();
}

rpl::producer<> AccountsList::closeRequests() const {
	return _closeRequests.events();
}

Ui::RpWidget *AccountsList::addAccountButton() const {
	return _addAccount ? _addAccount->entity() : nullptr;
}

void AccountsList::setup() {
	_addAccount = setupAdd();

	rpl::single(rpl::empty) | rpl::then(
		Core::App().domain().accountsChanges()
	) | rpl::on_next([=] {
		const auto &list = Core::App().domain().accounts();
		const auto exists = [&](not_null<::Main::Account*> account) {
			for (const auto &[index, existing] : list) {
				if (account == existing.get()) {
					return true;
				}
			}
			return false;
		};
		for (auto i = _watched.begin(); i != _watched.end();) {
			if (!exists(i->first)) {
				i = _watched.erase(i);
			} else {
				++i;
			}
		}
		for (const auto &[index, account] : list) {
			if (_watched.emplace(account.get()).second) {
				account->sessionChanges(
				) | rpl::on_next([=] {
					rebuild();
				}, _outer->lifetime());
			}
		}
		rebuild();
	}, _outer->lifetime());

	Core::App().domain().maxAccountsChanges(
	) | rpl::on_next([=] {
		for (auto i = _watched.begin(); i != _watched.end(); i++) {
			i->second = nullptr;
		}
		rebuild();
	}, _outer->lifetime());
}


not_null<Ui::SlideWrap<Ui::SettingsButton>*> AccountsList::setupAdd() {
	const auto result = _outer->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			_outer.get(),
			CreateButtonWithIcon(
				_outer.get(),
				tr::lng_menu_add_account(),
				st::mainMenuAddAccountButton,
				{
					&st::settingsIconAdd,
					IconType::Round,
					&st::windowBgActive
				})))->setDuration(0);
	const auto button = result->entity();

	using Environment = MTP::Environment;
	const auto add = [=](Environment environment, bool newWindow = false) {
		auto &domain = _controller->session().domain();
		auto found = false;
		for (const auto &[index, account] : domain.accounts()) {
			const auto raw = account.get();
			if (!raw->sessionExists()
				&& raw->mtp().environment() == environment) {
				found = true;
			}
		}
		if (!found && domain.accounts().size() >= domain.maxAccounts()) {
			_controller->show(
				Box(AccountsLimitBox, &_controller->session()));
		} else if (newWindow) {
			domain.addActivated(environment, true);
		} else {
			_controller->window().preventOrInvoke([=] {
				_controller->session().domain().addActivated(environment);
			});
		}
	};

	button->setAcceptBoth(true);
	button->clicks(
	) | rpl::on_next([=](Qt::MouseButton which) {
		if (which == Qt::LeftButton) {
			const auto modifiers = button->clickModifiers();
			const auto newWindow = (modifiers & Qt::ControlModifier);
			add(Environment::Production, newWindow);
			return;
		} else if (which != Qt::RightButton
			|| !IsAltShift(button->clickModifiers())) {
#ifdef _DEBUG
			if (which != Qt::RightButton) {
				return;
			}
#else // _DEBUG
			return;
#endif // !_DEBUG
		}
		_contextMenu = base::make_unique_q<Ui::PopupMenu>(_outer);
		_contextMenu->addAction("Production Server", [=] {
			add(Environment::Production);
		});
		_contextMenu->addAction("Test Server", [=] {
			add(Environment::Test);
		});
		_contextMenu->popup(QCursor::pos());
	}, button->lifetime());

	return result;
}

void AccountsList::rebuild() {
	const auto inner = _outer->insert(
		_outerIndex,
		object_ptr<Ui::VerticalLayout>(_outer.get()));

	_reorder = std::make_unique<Ui::VerticalLayoutReorder>(inner);
	_reorder->updates(
	) | rpl::on_next([=](Ui::VerticalLayoutReorder::Single data) {
		using State = Ui::VerticalLayoutReorder::State;
		if (data.state == State::Started) {
			++_reordering;
		} else {
			Ui::PostponeCall(inner, [=] {
				--_reordering;
			});
			if (data.state == State::Applied) {
				std::vector<uint64> order;
				order.reserve(inner->count());
				for (auto i = 0; i < inner->count(); i++) {
					for (const auto &[account, button] : _watched) {
						if (button.get() == inner->widgetAt(i)) {
							order.push_back(account->session().uniqueId());
						}
					}
				}
				Core::App().settings().setAccountsOrder(order);
				Core::App().saveSettings();
			}
		}
	}, inner->lifetime());

	const auto premiumLimit = _controller->session().domain().maxAccounts();
	const auto list = _controller->session().domain().orderedAccounts();
	for (const auto &account : list) {
		auto i = _watched.find(account);
		Assert(i != _watched.end());

		auto &button = i->second;
		if (!account->sessionExists() || list.size() == 1) {
			button = nullptr;
		} else if (!button) {
			const auto nextIsLocked = (inner->count() >= premiumLimit);
			auto callback = [=](Qt::KeyboardModifiers modifiers) {
				if (_reordering) {
					return;
				}
				if (account == &_controller->session().account()) {
					_closeRequests.fire({});
					return;
				}
				const auto newWindow = (modifiers & Qt::ControlModifier);
				auto activate = [=, guard = _accountSwitchGuard.make_guard()]{
					if (guard) {
						_reorder->finishReordering();
						if (newWindow) {
							_closeRequests.fire({});
							Core::App().ensureSeparateWindowFor(account);
						}
						Core::App().domain().maybeActivate(account);
					}
				};
				if (const auto window = Core::App().separateWindowFor(
						account)) {
					_closeRequests.fire({});
					window->activate();
				} else {
					base::call_delayed(
						st::defaultRippleAnimation.hideDuration,
						account,
						std::move(activate));
				}
			};
			button.reset(inner->add(MakeAccountButton(
				inner,
				_controller,
				account,
				std::move(callback),
				nextIsLocked)));
		}
	}
	inner->resizeToWidth(_outer->width());

	const auto count = int(list.size());

	_reorder->addPinnedInterval(
		premiumLimit,
		std::max(1, count - premiumLimit));

	_addAccount->toggle(
		(count < ::Main::Domain::kPremiumMaxAccounts),
		anim::type::instant);

	_reorder->start();
}

void BuildInformationSection(SectionBuilder &builder) {
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"edit/bio"_q,
			.title = tr::lng_bio_placeholder(tr::now),
			.keywords = { u"bio"_q, u"about"_q, u"description"_q },
		};
	});
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"edit/name"_q,
			.title = tr::lng_settings_name_label(tr::now),
			.keywords = { u"name"_q, u"first"_q, u"last"_q },
		};
	});
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"edit/phone"_q,
			.title = tr::lng_settings_phone_label(tr::now),
			.keywords = { u"phone"_q, u"number"_q, u"mobile"_q },
		};
	});
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"edit/username"_q,
			.title = tr::lng_settings_username_label(tr::now),
			.keywords = { u"username"_q, u"link"_q, u"t.me"_q },
		};
	});
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"edit/your-color"_q,
			.title = tr::lng_settings_theme_name_color(tr::now),
			.keywords = { u"color"_q, u"theme"_q, u"name"_q },
		};
	});
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"edit/channel"_q,
			.title = tr::lng_settings_channel_label(tr::now),
			.keywords = { u"channel"_q, u"personal"_q },
		};
	});
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"edit/birthday"_q,
			.title = tr::lng_settings_birthday_label(tr::now),
			.keywords = { u"birthday"_q, u"date"_q, u"birth"_q },
		};
	});
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"edit/add-account"_q,
			.title = tr::lng_menu_add_account(tr::now),
			.keywords = { u"account"_q, u"add"_q, u"switch"_q, u"multiple"_q },
		};
	});
}

class Information : public Section<Information> {
public:
	Information(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;
	void showFinished() override;

private:
	void setupContent();

	QPointer<Ui::RpWidget> _photo;
	QPointer<Ui::RpWidget> _uploadPhoto;
	QPointer<Ui::RpWidget> _bio;
	QPointer<Ui::RpWidget> _colorButton;
	QPointer<Ui::RpWidget> _channelButton;
	QPointer<Ui::RpWidget> _addAccount;
	QPointer<Ui::RpWidget> _name;
	QPointer<Ui::RpWidget> _phone;
	QPointer<Ui::RpWidget> _username;
	QPointer<Ui::RpWidget> _birthday;

};

Information::Information(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Information::title() {
	return tr::lng_settings_section_info();
}

void Information::showFinished() {
	Section<Information>::showFinished();
}

void Information::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const SectionBuildMethod buildMethod = [
		photo = &_photo,
		uploadPhoto = &_uploadPhoto,
		bio = &_bio,
		colorButton = &_colorButton,
		channelButton = &_channelButton,
		addAccount = &_addAccount,
		name = &_name,
		phone = &_phone,
		username = &_username,
		birthday = &_birthday
	](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		auto &lifetime = container->lifetime();
		const auto highlights = lifetime.make_state<HighlightRegistry>();
		const auto isPaused = Window::PausedIn(
			controller,
			Window::GifPauseReason::Layer);

		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(showOther),
			.isPaused = isPaused,
			.highlights = highlights,
		});

		const auto self = controller->session().user();
		auto targets = InformationHighlightTargets();

		SetupPhoto(container, controller, self, &targets);
		SetupBio(container, self, &targets);
		SetupRows(container, controller, self, &targets);
		SetupPersonalChannel(container, controller, self, &targets);
		SetupBirthday(container, controller, self, &targets);
		SetupAccountsWrap(container, controller, &targets);

		*photo = targets.photo;
		*uploadPhoto = targets.uploadPhoto;
		*bio = targets.bio;
		*colorButton = targets.colorButton;
		*channelButton = targets.channelButton;
		*addAccount = targets.addAccount;
		*name = targets.name;
		*phone = targets.phone;
		*username = targets.username;
		*birthday = targets.birthday;

		if (highlights) {
			if (*photo) {
				highlights->push_back({
					u"profile-photo"_q,
					{ photo->data(), { .shape = HighlightShape::Ellipse } },
				});
			}
			if (*uploadPhoto) {
				highlights->push_back({
					u"profile-photo/use-emoji"_q,
					{ uploadPhoto->data(), { .shape = HighlightShape::Ellipse } },
				});
			}
			if (*bio) {
				highlights->push_back({
					u"edit/bio"_q,
					{ bio->data(), { .margin = st::settingsBioHighlightMargin } },
				});
			}
			if (*colorButton) {
				highlights->push_back({
					u"edit/your-color"_q,
					{ colorButton->data(), { .rippleShape = true } },
				});
			}
			if (*channelButton) {
				highlights->push_back({
					u"edit/channel"_q,
					{ channelButton->data(), { .rippleShape = true } },
				});
			}
			if (*addAccount) {
				highlights->push_back({
					u"edit/add-account"_q,
					{ addAccount->data(), { .rippleShape = true } },
				});
			}
			if (*name) {
				highlights->push_back({
					u"edit/name"_q,
					{ name->data(), { .rippleShape = true } },
				});
			}
			if (*phone) {
				highlights->push_back({
					u"edit/phone"_q,
					{ phone->data(), { .rippleShape = true } },
				});
			}
			if (*username) {
				highlights->push_back({
					u"edit/username"_q,
					{ username->data(), { .rippleShape = true } },
				});
			}
			if (*birthday) {
				highlights->push_back({
					u"edit/birthday"_q,
					{ birthday->data(), { .rippleShape = true } },
				});
			}
		}

		std::move(showFinished) | rpl::on_next([=] {
			for (const auto &[id, entry] : *highlights) {
				if (entry.widget) {
					controller->checkHighlightControl(
						id,
						entry.widget,
						base::duplicate(entry.args));
				}
			}
		}, lifetime);
	};

	build(content, buildMethod);

	Ui::ResizeFitChild(this, content);
}

const auto kMeta = BuildHelper({
	.id = Information::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_section_info,
	.icon = &st::menuIconProfile,
}, [](SectionBuilder &builder) {
	BuildInformationSection(builder);
});

} // namespace

Type InformationId() {
	return Information::Id();
}

AccountsEvents SetupAccounts(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	const auto list = container->lifetime().make_state<AccountsList>(
		container,
		controller);
	return {
		.closeRequests = list->closeRequests(),
		.addAccountButton = list->addAccountButton(),
	};
}

void UpdatePhotoLocally(not_null<UserData*> user, const QImage &image) {
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	image.save(&buffer, "JPG", 87);
	user->setUserpic(
		base::RandomValue<PhotoId>(),
		ImageLocation(
			{ .data = InMemoryLocation{ .bytes = bytes } },
			image.width(),
			image.height()),
		false);
}

namespace Badge {

Ui::UnreadBadgeStyle Style() {
	auto result = Ui::UnreadBadgeStyle();
	result.font = st::mainMenuBadgeFont;
	result.size = st::mainMenuBadgeSize;
	result.sizeId = Ui::UnreadBadgeSize::MainMenu;
	return result;
}

not_null<Ui::RpWidget*> AddRight(
		not_null<Ui::SettingsButton*> button,
		int rightPadding) {
	const auto widget = Ui::CreateChild<Ui::RpWidget>(button.get());

	rpl::combine(
		button->sizeValue(),
		widget->sizeValue(),
		widget->shownValue()
	) | rpl::on_next([=](QSize outer, QSize inner, bool shown) {
		auto padding = button->st().padding;
		if (shown) {
			widget->moveToRight(
				padding.right() + rightPadding,
				(outer.height() - inner.height()) / 2,
				outer.width());
			padding.setRight(padding.right() + inner.width() + rightPadding);
		}
		button->setPaddingOverride(padding);
		button->update();
	}, widget->lifetime());

	return widget;
}

not_null<Ui::RpWidget*> CreateUnread(
		not_null<Ui::RpWidget*> container,
		rpl::producer<UnreadBadge> value) {
	struct State {
		State(QWidget *parent) : widget(parent) {
			widget.setAttribute(Qt::WA_TransparentForMouseEvents);
		}

		Ui::RpWidget widget;
		Ui::UnreadBadgeStyle st = Style();
		int count = 0;
		QString string;
	};
	const auto state = container->lifetime().make_state<State>(container);

	std::move(
		value
	) | rpl::on_next([=](UnreadBadge badge) {
		state->st.muted = badge.muted;
		state->count = badge.count;
		if (!state->count) {
			state->widget.hide();
			return;
		}
		state->string = Lang::FormatCountToShort(state->count).string;
		state->widget.resize(Ui::CountUnreadBadgeSize(state->string, state->st));
		if (state->widget.isHidden()) {
			state->widget.show();
		}
	}, state->widget.lifetime());

	state->widget.paintRequest(
	) | rpl::on_next([=] {
		auto p = Painter(&state->widget);
		Ui::PaintUnreadBadge(
			p,
			state->string,
			state->widget.width(),
			0,
			state->st);
	}, state->widget.lifetime());

	return &state->widget;
}

void AddUnread(
		not_null<Ui::SettingsButton*> button,
		rpl::producer<UnreadBadge> value) {
	const auto container = AddRight(button);
	const auto badge = CreateUnread(container, std::move(value));
	badge->sizeValue(
	) | rpl::on_next([=](const QSize &s) {
		container->resize(s);
	}, container->lifetime());
}

} // namespace Badge

namespace Builder {

SectionBuildMethod InformationSection = kMeta.build;

} // namespace Builder
} // namespace Settings
