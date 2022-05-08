/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_information.h"

#include "editor/photo_editor_layer_widget.h"
#include "settings/settings_common.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/special_buttons.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "boxes/add_contact_box.h"
#include "boxes/change_phone_box.h"
#include "boxes/username_box.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_peer_values.h"
#include "data/data_changes.h"
#include "dialogs/ui/dialogs_layout.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "menu/add_action_callback_factory.h"
#include "mtproto/mtproto_dc_options.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "core/file_utilities.h"
#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "base/random.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtCore/QBuffer>

namespace Settings {
namespace {

constexpr auto kSaveBioTimeout = 1000;

class AccountsList final {
public:
	AccountsList(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<> currentAccountActivations() const;

private:
	void setup();

	[[nodiscard]] not_null<Ui::SlideWrap<Ui::SettingsButton>*> setupAdd();
	void rebuild();

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::VerticalLayout*> _outer;
	int _outerIndex = 0;

	Ui::SlideWrap<Ui::SettingsButton> *_addAccount = nullptr;
	base::flat_map<
		not_null<Main::Account*>,
		base::unique_qptr<Ui::SettingsButton>> _watched;

	base::unique_qptr<Ui::PopupMenu> _contextMenu;
	std::unique_ptr<Ui::VerticalLayoutReorder> _reorder;
	int _reordering = 0;

	rpl::event_stream<> _currentAccountActivations;

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
				? Ui::Text::Link(Data::OnlineText(user, now))
				: Ui::Text::WithEntities(Data::OnlineText(user, now)));
			timer->callOnce(Data::OnlineChangeTimeout(user, now));
		};
		timer->setCallback(push);
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::OnlineStatus
		) | rpl::start_with_next(push, lifetime);
		return lifetime;
	};
}

[[nodiscard]] not_null<Ui::UserpicButton*> CreateUploadButton(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller) {
	const auto background = Ui::CreateChild<Ui::RpWidget>(parent.get());
	const auto upload = Ui::CreateChild<Ui::UserpicButton>(
		parent.get(),
		&controller->window(),
		tr::lng_settings_crop_profile(tr::now),
		Ui::UserpicButton::Role::ChoosePhoto,
		st::settingsInfoUpload);

	const auto border = st::settingsInfoUploadBorder;
	const auto size = upload->rect().marginsAdded(
		{ border, border, border, border }
	).size();

	background->resize(size);
	background->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(background);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::boxBg);
		p.setPen(Qt::NoPen);
		p.drawEllipse(background->rect());
	}, background->lifetime());

	upload->positionValue(
	) | rpl::start_with_next([=](QPoint position) {
		background->move(position - QPoint(border, border));
	}, background->lifetime());

	return upload;
}

void UploadPhoto(not_null<UserData*> user, QImage image) {
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	image.save(&buffer, "JPG", 87);
	user->setUserpic(base::RandomValue<PhotoId>(), ImageLocation(
		{ .data = InMemoryLocation{ .bytes = bytes } },
		image.width(),
		image.height()));
	user->session().api().peerPhoto().upload(user, std::move(image));
}

void SetupPhoto(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> self) {
	const auto wrap = container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::settingsInfoPhotoHeight));
	const auto photo = Ui::CreateChild<Ui::UserpicButton>(
		wrap,
		controller,
		self,
		Ui::UserpicButton::Role::OpenPhoto,
		st::settingsInfoPhoto);
	const auto upload = CreateUploadButton(wrap, controller);

	upload->chosenImages(
	) | rpl::start_with_next([=](QImage &&image) {
		UploadPhoto(self, image);
		photo->changeTo(std::move(image));
	}, upload->lifetime());

	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		Info::Profile::NameValue(self),
		st::settingsCoverName);
	const auto status = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		StatusValue(self),
		st::settingsCoverStatus);
	rpl::combine(
		wrap->widthValue(),
		photo->widthValue(),
		Info::Profile::NameValue(self),
		status->widthValue()
	) | rpl::start_with_next([=](
			int max,
			int photoWidth,
			const TextWithEntities&,
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

void AddRow(
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
	}) | rpl::start_with_next([=](Qt::MouseButton button) {
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
	}) | rpl::start_with_next([=](const TextWithEntities &text) {
		*forcopy = text.text;
	}, wrap->lifetime());
}

void SetupRows(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> self) {
	const auto session = &self->session();

	AddSkip(container);

	AddRow(
		container,
		tr::lng_settings_name_label(),
		Info::Profile::NameValue(self),
		tr::lng_profile_copy_fullname(tr::now),
		[=] { controller->show(Box<EditNameBox>(self)); },
		{ &st::settingsIconUser, kIconLightBlue });

	const auto showChangePhone = [=] {
		controller->showSettings(ChangePhone::Id());
		controller->window().activate();
	};
	AddRow(
		container,
		tr::lng_settings_phone_label(),
		Info::Profile::PhoneValue(self),
		tr::lng_profile_copy_phone(tr::now),
		showChangePhone,
		{ &st::settingsIconCalls, kIconGreen });

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
	auto value = rpl::combine(
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
	AddRow(
		container,
		std::move(label),
		std::move(value),
		tr::lng_context_copy_mention(tr::now),
		[=] { controller->show(Box<UsernameBox>(session)); },
		{ &st::settingsIconMention, kIconLightOrange });

	AddSkip(container);
}

void SetupBio(
		not_null<Ui::VerticalLayout*> container,
		not_null<UserData*> self) {
	const auto bioStyle = [] {
		auto result = st::settingsBio;
		result.textMargins.setRight(st::boxTextFont->spacew
			+ st::boxTextFont->width(QString::number(kMaxBioLength)));
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

	const auto countdown = Ui::CreateChild<Ui::FlatLabel>(
		container.get(),
		QString(),
		st::settingsBioCountdown);

	rpl::combine(
		bio->geometryValue(),
		countdown->widthValue()
	) | rpl::start_with_next([=](QRect geometry, int width) {
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
		const auto countLeft = qMax(kMaxBioLength - text.size(), 0);
		countdown->setText(QString::number(countLeft));
	};
	const auto save = [=] {
		self->session().api().saveSelfBio(
			TextUtilities::PrepareForSending(bio->getLastText()));
	};

	Info::Profile::AboutValue(
		self
	) | rpl::start_with_next([=](const TextWithEntities &text) {
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
	) | rpl::start_with_next([=](bool changed) {
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

	// We need 'bio' to still exist here as InputField, so we add this
	// to 'container' lifetime, not to the 'bio' lifetime.
	container->lifetime().add([=] {
		if (*generation > 0) {
			save();
		}
	});

	bio->setMaxLength(kMaxBioLength);
	bio->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
	auto cursor = bio->textCursor();
	cursor.setPosition(bio->getLastText().size());
	bio->setTextCursor(cursor);
	QObject::connect(bio, &Ui::InputField::submitted, [=] {
		save();
	});
	QObject::connect(bio, &Ui::InputField::changed, updated);
	bio->setInstantReplaces(Ui::InstantReplaces::Default());
	bio->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		container->window(),
		bio,
		&self->session());
	updated();

	AddDividerText(container, tr::lng_settings_about_bio());
}

void SetupAccountsWrap(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	AddDivider(container);
	AddSkip(container);

	SetupAccounts(container, controller);
}

[[nodiscard]] bool IsAltShift(Qt::KeyboardModifiers modifiers) {
	return (modifiers & Qt::ShiftModifier) && (modifiers & Qt::AltModifier);
}

[[nodiscard]] object_ptr<Ui::SettingsButton> MakeAccountButton(
		QWidget *parent,
		not_null<Window::SessionController*> window,
		not_null<Main::Account*> account,
		Fn<void()> callback) {
	const auto active = (account == &Core::App().activeAccount());
	const auto session = &account->session();
	const auto user = session->user();

	auto text = rpl::single(
		user->name
	) | rpl::then(session->changes().realtimeNameUpdates(
		user
	) | rpl::map([=] {
		return user->name;
	}));
	auto result = object_ptr<Ui::SettingsButton>(
		parent,
		std::move(text),
		st::mainMenuAddAccountButton);
	const auto raw = result.data();

	struct State {
		State(QWidget *parent) : userpic(parent) {
			userpic.setAttribute(Qt::WA_TransparentForMouseEvents);
		}

		Ui::RpWidget userpic;
		std::shared_ptr<Data::CloudImageView> view;
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = raw->lifetime().make_state<State>(raw);

	if (!active) {
		AddUnreadBadge(raw, rpl::single(rpl::empty) | rpl::then(
			session->data().unreadBadgeChanges()
		) | rpl::map([=] {
			auto &owner = session->data();
			return UnreadBadge{
				owner.unreadBadge(),
				owner.unreadBadgeMuted(),
			};
		}));
	}

	const auto userpicSkip = 2 * st::mainMenuAccountLine + st::lineWidth;
	const auto userpicSize = st::mainMenuAccountSize
		+ userpicSkip * 2;
	raw->heightValue(
	) | rpl::start_with_next([=](int height) {
		const auto left = st::mainMenuAddAccountButton.iconLeft
			+ (st::settingsIconAdd.width() - userpicSize) / 2;
		const auto top = (height - userpicSize) / 2;
		state->userpic.setGeometry(left, top, userpicSize, userpicSize);
	}, state->userpic.lifetime());

	state->userpic.paintRequest(
	) | rpl::start_with_next([=] {
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
	) | rpl::start_with_next([=](Qt::MouseButton which) {
		if (which == Qt::LeftButton) {
			callback();
			return;
		} else if (which != Qt::RightButton) {
			return;
		}
		const auto addAction = Menu::CreateAddActionCallback(state->menu);
		if (!state->menu && IsAltShift(raw->clickModifiers())) {
			state->menu = base::make_unique_q<Ui::PopupMenu>(
				raw,
				st::popupMenuWithIcons);
			Window::MenuAddMarkAsReadAllChatsAction(window, addAction);
			state->menu->popup(QCursor::pos());
			return;
		}
		if (&session->account() == &Core::App().activeAccount()
			|| state->menu) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			raw,
			st::popupMenuWithIcons);
		addAction(tr::lng_profile_copy_phone(tr::now), [=] {
			const auto phone = rpl::variable<TextWithEntities>(
				Info::Profile::PhoneValue(session->user()));
			QGuiApplication::clipboard()->setText(phone.current().text);
		}, &st::menuIconCopy);

		addAction(tr::lng_menu_activate(tr::now), [=] {
			Core::App().domain().activate(&session->account());
		}, &st::menuIconProfile);

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
		state->menu->popup(QCursor::pos());
	}, raw->lifetime());

	return result;
}

[[nodiscard]] std::vector<not_null<Main::Account*>> OrderedAccounts() {
	using namespace Main;

	const auto order = Core::App().settings().accountsOrder();
	auto accounts = ranges::views::all(
		Core::App().domain().accounts()
	) | ranges::views::transform([](const Domain::AccountWithIndex &a) {
		return not_null{ a.account.get() };
	}) | ranges::to_vector;
	ranges::stable_sort(accounts, [&](
			not_null<Account*> a,
			not_null<Account*> b) {
		const auto aIt = a->sessionExists()
			? ranges::find(order, a->session().uniqueId())
			: end(order);
		const auto bIt = b->sessionExists()
			? ranges::find(order, b->session().uniqueId())
			: end(order);
		return aIt < bIt;
	});
	return accounts;
}

AccountsList::AccountsList(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller)
: _controller(controller)
, _outer(container)
, _outerIndex(container->count()) {
	setup();
}

rpl::producer<> AccountsList::currentAccountActivations() const {
	return _currentAccountActivations.events();
}

void AccountsList::setup() {
	_addAccount = setupAdd();

	rpl::single(rpl::empty) | rpl::then(
		Core::App().domain().accountsChanges()
	) | rpl::start_with_next([=] {
		const auto &list = Core::App().domain().accounts();
		const auto exists = [&](not_null<Main::Account*> account) {
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
				) | rpl::start_with_next([=](Main::Session *session) {
					rebuild();
				}, _outer->lifetime());
			}
		}
		rebuild();
	}, _outer->lifetime());
}


not_null<Ui::SlideWrap<Ui::SettingsButton>*> AccountsList::setupAdd() {
	const auto result = _outer->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			_outer.get(),
			CreateButton(
				_outer.get(),
				tr::lng_menu_add_account(),
				st::mainMenuAddAccountButton,
				{
					&st::settingsIconAdd,
					0,
					IconType::Round,
					&st::windowBgActive
				})))->setDuration(0);
	const auto button = result->entity();

	const auto add = [=](MTP::Environment environment) {
		Core::App().preventOrInvoke([=] {
			Core::App().domain().addActivated(environment);
		});
	};

	button->setAcceptBoth(true);
	button->clicks(
	) | rpl::start_with_next([=](Qt::MouseButton which) {
		if (which == Qt::LeftButton) {
			add(MTP::Environment::Production);
			return;
		} else if (which != Qt::RightButton
			|| !IsAltShift(button->clickModifiers())) {
			return;
		}
		_contextMenu = base::make_unique_q<Ui::PopupMenu>(_outer);
		_contextMenu->addAction("Production Server", [=] {
			add(MTP::Environment::Production);
		});
		_contextMenu->addAction("Test Server", [=] {
			add(MTP::Environment::Test);
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
	) | rpl::start_with_next([=](Ui::VerticalLayoutReorder::Single data) {
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

	const auto list = OrderedAccounts();
	for (const auto &account : list) {
		auto i = _watched.find(account);
		Assert(i != _watched.end());

		auto &button = i->second;
		if (!account->sessionExists() || list.size() == 1) {
			button = nullptr;
		} else if (!button) {
			auto callback = [=] {
				if (_reordering) {
					return;
				}
				if (account == &Core::App().domain().active()) {
					_currentAccountActivations.fire({});
					return;
				}
				auto activate = [=, guard = _accountSwitchGuard.make_guard()]{
					if (guard) {
						_reorder->finishReordering();
						Core::App().domain().maybeActivate(account);
					}
				};
				base::call_delayed(
					st::defaultRippleAnimation.hideDuration,
					account,
					std::move(activate));
			};
			button.reset(inner->add(MakeAccountButton(
				inner,
				_controller,
				account,
				std::move(callback))));
		}
	}
	inner->resizeToWidth(_outer->width());

	_addAccount->toggle(
		(inner->count() < Main::Domain::kMaxAccounts),
		anim::type::instant);

	_reorder->start();
}

} // namespace

Information::Information(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

rpl::producer<QString> Information::title() {
	return tr::lng_settings_section_info();
}

void Information::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto self = controller->session().user();
	SetupPhoto(content, controller, self);
	SetupBio(content, self);
	SetupRows(content, controller, self);
	SetupAccountsWrap(content, controller);

	Ui::ResizeFitChild(this, content);
}

AccountsEvents SetupAccounts(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	const auto list = container->lifetime().make_state<AccountsList>(
		container,
		controller);
	return {
		.currentAccountActivations = list->currentAccountActivations(),
	};
}

Dialogs::Ui::UnreadBadgeStyle BadgeStyle() {
	auto result = Dialogs::Ui::UnreadBadgeStyle();
	result.font = st::mainMenuBadgeFont;
	result.size = st::mainMenuBadgeSize;
	result.sizeId = Dialogs::Ui::UnreadBadgeInMainMenu;
	return result;
}

void AddUnreadBadge(
		not_null<Ui::SettingsButton*> button,
		rpl::producer<UnreadBadge> value) {
	struct State {
		State(QWidget *parent) : widget(parent) {
			widget.setAttribute(Qt::WA_TransparentForMouseEvents);
		}

		Ui::RpWidget widget;
		Dialogs::Ui::UnreadBadgeStyle st = BadgeStyle();
		int count = 0;
		QString string;
	};
	const auto state = button->lifetime().make_state<State>(button);

	std::move(
		value
	) | rpl::start_with_next([=](UnreadBadge badge) {
		state->st.muted = badge.muted;
		state->count = badge.count;
		if (!state->count) {
			state->widget.hide();
			return;
		}
		state->string = Lang::FormatCountToShort(state->count).string;
		state->widget.resize(CountUnreadBadgeSize(state->string, state->st));
		if (state->widget.isHidden()) {
			state->widget.show();
		}
	}, state->widget.lifetime());

	state->widget.paintRequest(
	) | rpl::start_with_next([=] {
		auto p = Painter(&state->widget);
		Dialogs::Ui::PaintUnreadBadge(
			p,
			state->string,
			state->widget.width(),
			0,
			state->st);
	}, state->widget.lifetime());

	rpl::combine(
		button->sizeValue(),
		state->widget.sizeValue(),
		state->widget.shownValue()
	) | rpl::start_with_next([=](QSize outer, QSize inner, bool shown) {
		auto padding = button->st().padding;
		if (shown) {
			state->widget.moveToRight(
				padding.right(),
				(outer.height() - inner.height()) / 2,
				outer.width());
			padding.setRight(padding.right()
				+ inner.width()
				+ button->st().style.font->spacew);
		}
		button->setPaddingOverride(padding);
	}, state->widget.lifetime());
}

} // namespace Settings
