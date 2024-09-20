/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/edit_privacy_box.h"

#include "api/api_global_privacy.h"
#include "boxes/filters/edit_filter_chats_list.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/shadow.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "history/history.h"
#include "boxes/peer_list_controllers.h"
#include "settings/settings_premium.h"
#include "settings/settings_privacy_security.h"
#include "calls/calls_instance.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_peer_values.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace {

constexpr auto kPremiumsRowId = PeerId(FakeChatId(BareId(1))).value;

using Exceptions = Api::UserPrivacy::Exceptions;

void CreateRadiobuttonLock(
		not_null<Ui::RpWidget*> widget,
		const style::Checkbox &st) {
	const auto lock = Ui::CreateChild<Ui::RpWidget>(widget.get());
	lock->setAttribute(Qt::WA_TransparentForMouseEvents);

	lock->resize(st::defaultRadio.diameter, st::defaultRadio.diameter);

	widget->sizeValue(
	) | rpl::start_with_next([=, &st](QSize size) {
		lock->move(st.checkPosition);
	}, lock->lifetime());

	lock->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(lock);
		auto hq = PainterHighQualityEnabler(p);
		const auto &icon = st::messagePrivacyLock;
		const auto size = st::defaultRadio.diameter;
		const auto image = icon.instance(st::checkboxFg->c);
		p.drawImage(QRectF(
			(size - icon.width()) / 2.,
			(size - icon.height()) / 2.,
			icon.width(),
			icon.height()), image);
	}, lock->lifetime());
}

void AddPremiumRequiredRow(
		not_null<Ui::RpWidget*> widget,
		not_null<Main::Session*> session,
		Fn<void()> clickedCallback,
		Fn<void()> setDefaultOption,
		const style::Checkbox &st) {
	const auto row = Ui::CreateChild<Ui::AbstractButton>(widget.get());

	widget->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		row->resize(s);
	}, row->lifetime());
	row->setClickedCallback(std::move(clickedCallback));

	CreateRadiobuttonLock(row, st);

	Data::AmPremiumValue(
		session
	) | rpl::start_with_next([=](bool premium) {
		row->setVisible(!premium);
		if (!premium) {
			setDefaultOption();
		}
	}, row->lifetime());
}

class PrivacyExceptionsBoxController : public ChatsListBoxController {
public:
	PrivacyExceptionsBoxController(
		not_null<Main::Session*> session,
		rpl::producer<QString> title,
		const Exceptions &selected,
		bool allowChoosePremiums);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;
	bool isForeignRow(PeerListRowId itemId) override;
	bool handleDeselectForeignRow(PeerListRowId itemId) override;

	[[nodiscard]] bool premiumsSelected() const;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

private:
	[[nodiscard]] object_ptr<Ui::RpWidget> preparePremiumsRowList();

	const not_null<Main::Session*> _session;
	rpl::producer<QString> _title;
	Exceptions _selected;
	bool _allowChoosePremiums = false;

	PeerListContentDelegate *_typesDelegate = nullptr;
	Fn<void(PeerListRowId)> _deselectOption;

};

struct RowSelectionChange {
	not_null<PeerListRow*> row;
	bool checked = false;
};

class PremiumsRow final : public PeerListRow {
public:
	PremiumsRow();

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;
	bool useForumLikeUserpic() const override;

};

class TypesController final : public PeerListController {
public:
	TypesController(not_null<Main::Session*> session, bool premiums);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] bool premiumsSelected() const;
	[[nodiscard]] rpl::producer<bool> premiumsChanges() const;
	[[nodiscard]] auto rowSelectionChanges() const
		-> rpl::producer<RowSelectionChange>;

private:
	const not_null<Main::Session*> _session;

	rpl::event_stream<> _selectionChanged;
	rpl::event_stream<RowSelectionChange> _rowSelectionChanges;

};

PremiumsRow::PremiumsRow() : PeerListRow(kPremiumsRowId) {
	setCustomStatus(tr::lng_edit_privacy_premium_status(tr::now));
}

QString PremiumsRow::generateName() {
	return tr::lng_edit_privacy_premium(tr::now);
}

QString PremiumsRow::generateShortName() {
	return generateName();
}

PaintRoundImageCallback PremiumsRow::generatePaintUserpicCallback(
		bool forceRound) {
	return [=](QPainter &p, int x, int y, int outerWidth, int size) {
		auto gradient = QLinearGradient(
			QPointF(x, y),
			QPointF(x + size, y + size));
		gradient.setStops(Ui::Premium::ButtonGradientStops());

		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		if (forceRound) {
			p.drawEllipse(x, y, size, size);
		} else {
			const auto radius = size * Ui::ForumUserpicRadiusMultiplier();
			p.drawRoundedRect(x, y, size, size, radius, radius);
		}
		st::settingsPrivacyPremium.paintInCenter(p, QRect(x, y, size, size));
	};
}

bool PremiumsRow::useForumLikeUserpic() const {
	return true;
}

TypesController::TypesController(
	not_null<Main::Session*> session,
	bool premiums)
: _session(session) {
}

Main::Session &TypesController::session() const {
	return *_session;
}

void TypesController::prepare() {
	delegate()->peerListAppendRow(std::make_unique<PremiumsRow>());
	delegate()->peerListRefreshRows();
}

bool TypesController::premiumsSelected() const {
	const auto row = delegate()->peerListFindRow(kPremiumsRowId);
	Assert(row != nullptr);

	return row->checked();
}

void TypesController::rowClicked(not_null<PeerListRow*> row) {
	const auto checked = !row->checked();
	delegate()->peerListSetRowChecked(row, checked);
	_rowSelectionChanges.fire({ row, checked });
}

rpl::producer<bool> TypesController::premiumsChanges() const {
	return _rowSelectionChanges.events(
	) | rpl::map([=] {
		return premiumsSelected();
	});
}

auto TypesController::rowSelectionChanges() const
-> rpl::producer<RowSelectionChange> {
	return _rowSelectionChanges.events();
}

PrivacyExceptionsBoxController::PrivacyExceptionsBoxController(
	not_null<Main::Session*> session,
	rpl::producer<QString> title,
	const Exceptions &selected,
	bool allowChoosePremiums)
: ChatsListBoxController(session)
, _session(session)
, _title(std::move(title))
, _selected(selected)
, _allowChoosePremiums(allowChoosePremiums) {
}

Main::Session &PrivacyExceptionsBoxController::session() const {
	return *_session;
}

void PrivacyExceptionsBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(std::move(_title));
	if (_allowChoosePremiums || _selected.premiums) {
		delegate()->peerListSetAboveWidget(preparePremiumsRowList());
	}
	delegate()->peerListAddSelectedPeers(_selected.peers);
}

bool PrivacyExceptionsBoxController::isForeignRow(PeerListRowId itemId) {
	return (itemId == kPremiumsRowId);
}

bool PrivacyExceptionsBoxController::handleDeselectForeignRow(
		PeerListRowId itemId) {
	if (isForeignRow(itemId)) {
		_deselectOption(itemId);
		return true;
	}
	return false;
}

auto PrivacyExceptionsBoxController::preparePremiumsRowList()
-> object_ptr<Ui::RpWidget> {
	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();
	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_edit_privacy_user_types()));
	auto &lifetime = container->lifetime();
	_typesDelegate = lifetime.make_state<PeerListContentDelegateSimple>();
	const auto controller = lifetime.make_state<TypesController>(
		&session(),
		_selected.premiums);
	const auto content = result->add(object_ptr<PeerListContent>(
		container,
		controller));
	_typesDelegate->setContent(content);
	controller->setDelegate(_typesDelegate);

	if (_selected.premiums) {
		const auto row = _typesDelegate->peerListFindRow(kPremiumsRowId);
		Assert(row != nullptr);

		content->changeCheckState(row, true, anim::type::instant);
		this->delegate()->peerListSetForeignRowChecked(
			row,
			true,
			anim::type::instant);
	}
	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_edit_privacy_users_and_groups()));

	controller->premiumsChanges(
	) | rpl::start_with_next([=](bool premiums) {
		_selected.premiums = premiums;
	}, lifetime);

	controller->rowSelectionChanges(
	) | rpl::start_with_next([=](RowSelectionChange update) {
		this->delegate()->peerListSetForeignRowChecked(
			update.row,
			update.checked,
			anim::type::normal);
	}, lifetime);

	_deselectOption = [=](PeerListRowId itemId) {
		if (const auto row = _typesDelegate->peerListFindRow(itemId)) {
			if (itemId == kPremiumsRowId) {
				_selected.premiums = false;
			}
			_typesDelegate->peerListSetRowChecked(row, false);
		}
	};

	return result;
}

[[nodiscard]] bool PrivacyExceptionsBoxController::premiumsSelected() const {
	return _selected.premiums;
}

void PrivacyExceptionsBoxController::rowClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();

	// This call may delete row, if it was a search result row.
	delegate()->peerListSetRowChecked(row, !row->checked());

	if (const auto channel = peer->asChannel()) {
		if (!channel->membersCountKnown()) {
			channel->updateFull();
		}
	}
}

auto PrivacyExceptionsBoxController::createRow(not_null<History*> history)
-> std::unique_ptr<Row> {
	const auto peer = history->peer;
	if (peer->isSelf() || peer->isRepliesChat() || peer->isVerifyCodes()) {
		return nullptr;
	} else if (!peer->isUser()
		&& !peer->isChat()
		&& !peer->isMegagroup()) {
		return nullptr;
	}
	auto result = std::make_unique<Row>(history);
	const auto count = [&] {
		if (const auto chat = history->peer->asChat()) {
			return chat->count;
		} else if (const auto channel = history->peer->asChannel()) {
			return channel->membersCountKnown()
				? channel->membersCount()
				: 0;
		}
		return 0;
	}();
	if (count > 0) {
		result->setCustomStatus(
			tr::lng_chat_status_members(tr::now, lt_count_decimal, count));
	}
	return result;
}

} // namespace

bool EditPrivacyController::hasOption(Option option) const {
	return (option != Option::CloseFriends);
}

QString EditPrivacyController::optionLabel(Option option) const {
	switch (option) {
	case Option::Everyone: return tr::lng_edit_privacy_everyone(tr::now);
	case Option::Contacts: return tr::lng_edit_privacy_contacts(tr::now);
	case Option::CloseFriends:
		return tr::lng_edit_privacy_close_friends(tr::now);
	case Option::Nobody: return tr::lng_edit_privacy_nobody(tr::now);
	}
	Unexpected("Option value in optionsLabelKey.");
}

EditPrivacyBox::EditPrivacyBox(
	QWidget*,
	not_null<Window::SessionController*> window,
	std::unique_ptr<EditPrivacyController> controller,
	const Value &value)
: _window(window)
, _controller(std::move(controller))
, _value(value) {
	if (_controller->allowPremiumsToggle(Exception::Always)
		&& _value.option == Option::Everyone) {
		// If we switch from Everyone to Contacts or Nobody suggest Premiums.
		_value.always.premiums = true;
	}
}

void EditPrivacyBox::prepare() {
	_controller->setView(this);

	setupContent();
}

void EditPrivacyBox::editExceptions(
		Exception exception,
		Fn<void()> done) {
	auto controller = std::make_unique<PrivacyExceptionsBoxController>(
		&_window->session(),
		_controller->exceptionBoxTitle(exception),
		exceptions(exception),
		_controller->allowPremiumsToggle(exception));
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListBox*> box) {
		box->addButton(tr::lng_settings_save(), crl::guard(this, [=] {
			exceptions(exception).peers = box->collectSelectedRows();
			exceptions(exception).premiums = controller->premiumsSelected();
			const auto type = [&] {
				switch (exception) {
				case Exception::Always: return Exception::Never;
				case Exception::Never: return Exception::Always;
				}
				Unexpected("Invalid exception value.");
			}();
			auto &removeFrom = exceptions(type).peers;
			for (const auto peer : exceptions(exception).peers) {
				removeFrom.erase(
					ranges::remove(removeFrom, peer),
					end(removeFrom));
			}
			done();
			box->closeBox();
		}));
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	_window->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
}

EditPrivacyBox::Exceptions &EditPrivacyBox::exceptions(Exception exception) {
	switch (exception) {
	case Exception::Always: return _value.always;
	case Exception::Never: return _value.never;
	}
	Unexpected("Invalid exception value.");
}

bool EditPrivacyBox::showExceptionLink(Exception exception) const {
	switch (exception) {
	case Exception::Always:
		return (_value.option == Option::Contacts)
			|| (_value.option == Option::CloseFriends)
			|| (_value.option == Option::Nobody);
	case Exception::Never:
		return (_value.option == Option::Everyone)
			|| (_value.option == Option::Contacts)
			|| (_value.option == Option::CloseFriends);
	}
	Unexpected("Invalid exception value.");
}

Ui::Radioenum<EditPrivacyBox::Option> *EditPrivacyBox::AddOption(
		not_null<Ui::VerticalLayout*> container,
		not_null<EditPrivacyController*> controller,
		const std::shared_ptr<Ui::RadioenumGroup<Option>> &group,
		Option option) {
	return container->add(
		object_ptr<Ui::Radioenum<Option>>(
			container,
			group,
			option,
			controller->optionLabel(option),
			st::settingsPrivacyOption),
		(st::settingsSendTypePadding + style::margins(
			-st::lineWidth,
			st::settingsPrivacySkipTop,
			0,
			0)));
}

Ui::FlatLabel *EditPrivacyBox::addLabel(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<TextWithEntities> text,
		int topSkip) {
	if (!text) {
		return nullptr;
	}
	auto label = object_ptr<Ui::FlatLabel>(
		container,
		rpl::duplicate(text),
		st::boxDividerLabel);
	const auto result = label.data();
	container->add(
		object_ptr<Ui::DividerLabel>(
			container,
			std::move(label),
			st::defaultBoxDividerLabelPadding),
		{ 0, topSkip, 0, 0 });
	return result;
}

Ui::FlatLabel *EditPrivacyBox::addLabelOrDivider(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<TextWithEntities> text,
		int topSkip) {
	if (const auto result = addLabel(container, std::move(text), topSkip)) {
		return result;
	}
	container->add(
		object_ptr<Ui::BoxContentDivider>(container),
		{ 0, topSkip, 0, 0 });
	return nullptr;
}

void EditPrivacyBox::setupContent() {
	using namespace Settings;

	setTitle(_controller->title());

	auto wrap = object_ptr<Ui::VerticalLayout>(this);
	const auto content = wrap.data();
	setInnerWidget(object_ptr<Ui::OverrideMargins>(
		this,
		std::move(wrap)));

	const auto group = std::make_shared<Ui::RadioenumGroup<Option>>(
		_value.option);
	const auto toggle = Ui::CreateChild<rpl::event_stream<Option>>(content);
	group->setChangedCallback([=](Option value) {
		_value.option = value;
		toggle->fire_copy(value);
	});
	auto optionValue = toggle->events_starting_with_copy(_value.option);

	const auto addOptionRow = [&](Option option) {
		return (_controller->hasOption(option) || (_value.option == option))
			? AddOption(content, _controller.get(), group, option)
			: nullptr;
	};
	const auto addExceptionLink = [=](Exception exception) {
		const auto update = Ui::CreateChild<rpl::event_stream<>>(content);
		auto label = update->events_starting_with({}) | rpl::map([=] {
			const auto &value = exceptions(exception);
			const auto count = Settings::ExceptionUsersCount(value.peers);
			const auto users = count
				? tr::lng_edit_privacy_exceptions_count(
					tr::now,
					lt_count,
					count)
				: tr::lng_edit_privacy_exceptions_add(tr::now);
			return !value.premiums
				? users
				: !count
				? tr::lng_edit_privacy_premium(tr::now)
				: tr::lng_edit_privacy_exceptions_premium_and(
					tr::now,
					lt_users,
					users);
		});
		_controller->handleExceptionsChange(
			exception,
			update->events_starting_with({}) | rpl::map([=] {
				return Settings::ExceptionUsersCount(
					exceptions(exception).peers);
			}));
		auto text = _controller->exceptionButtonTextKey(exception);
		const auto button = content->add(
			object_ptr<Ui::SlideWrap<Button>>(
				content,
				object_ptr<Button>(
					content,
					rpl::duplicate(text),
					st::settingsButtonNoIcon)));
		CreateRightLabel(
			button->entity(),
			std::move(label),
			st::settingsButtonNoIcon,
			std::move(text));
		button->toggleOn(rpl::duplicate(
			optionValue
		) | rpl::map([=] {
			return showExceptionLink(exception);
		}))->entity()->addClickHandler([=] {
			editExceptions(exception, [=] { update->fire({}); });
		});
		return button;
	};

	auto above = _controller->setupAboveWidget(
		_window,
		content,
		rpl::duplicate(optionValue),
		getDelegate()->outerContainer());
	if (above) {
		content->add(std::move(above));
	}

	Ui::AddSubsectionTitle(
		content,
		_controller->optionsTitleKey(),
		{ 0, st::settingsPrivacySkipTop, 0, 0 });

	const auto options = {
		Option::Everyone,
		Option::Contacts,
		Option::CloseFriends,
		Option::Nobody,
	};
	for (const auto &option : options) {
		if (const auto row = addOptionRow(option)) {
			const auto premiumCallback = _controller->premiumClickedCallback(
				option,
				_window);
			if (premiumCallback) {
				AddPremiumRequiredRow(
					row,
					&_window->session(),
					premiumCallback,
					[=] { group->setValue(Option::Everyone); },
					st::messagePrivacyCheck);
			}
		}
	}

	const auto warning = addLabelOrDivider(
		content,
		_controller->warning(),
		st::defaultVerticalListSkip + st::settingsPrivacySkipTop);
	if (warning) {
		_controller->prepareWarningLabel(warning);
	}

	auto middle = _controller->setupMiddleWidget(
		_window,
		content,
		rpl::duplicate(optionValue));
	if (middle) {
		content->add(std::move(middle));
	}

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(
		content,
		tr::lng_edit_privacy_exceptions(),
		{ 0, st::settingsPrivacySkipTop, 0, 0 });
	const auto always = addExceptionLink(Exception::Always);
	const auto never = addExceptionLink(Exception::Never);
	addLabel(
		content,
		_controller->exceptionsDescription() | Ui::Text::ToWithEntities(),
		st::defaultVerticalListSkip);

	auto below = _controller->setupBelowWidget(
		_window,
		content,
		rpl::duplicate(optionValue));
	if (below) {
		content->add(std::move(below));
	}

	addButton(tr::lng_settings_save(), [=] {
		const auto someAreDisallowed = (_value.option != Option::Everyone)
			|| !_value.never.peers.empty();
		_controller->confirmSave(someAreDisallowed, crl::guard(this, [=] {
			_value.ignoreAlways = !showExceptionLink(Exception::Always);
			_value.ignoreNever = !showExceptionLink(Exception::Never);

			_controller->saveAdditional();
			_window->session().api().userPrivacy().save(
				_controller->key(),
				_value);
			closeBox();
		}));
	});
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	const auto linkHeight = st::settingsButtonNoIcon.padding.top()
		+ st::settingsButtonNoIcon.height
		+ st::settingsButtonNoIcon.padding.bottom();

	widthValue(
	) | rpl::start_with_next([=](int width) {
		content->resizeToWidth(width);
	}, content->lifetime());

	content->heightValue(
	) | rpl::map([=](int height) {
		return height - always->height() - never->height() + 2 * linkHeight;
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, content->lifetime());
}

void EditMessagesPrivacyBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(tr::lng_messages_privacy_title());
	box->setWidth(st::boxWideWidth);

	constexpr auto kOptionAll = 0;
	constexpr auto kOptionPremium = 1;

	const auto allowed = [=] {
		return controller->session().premium()
			|| controller->session().appConfig().newRequirePremiumFree();
	};
	const auto privacy = &controller->session().api().globalPrivacy();
	const auto inner = box->verticalLayout();
	inner->add(object_ptr<Ui::PlainShadow>(box));

	Ui::AddSkip(inner, st::messagePrivacyTopSkip);
	Ui::AddSubsectionTitle(inner, tr::lng_messages_privacy_subtitle());
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		privacy->newRequirePremiumCurrent() ? kOptionPremium : kOptionAll);
	inner->add(
		object_ptr<Ui::Radiobutton>(
			inner,
			group,
			kOptionAll,
			tr::lng_messages_privacy_everyone(tr::now),
			st::messagePrivacyCheck),
		st::settingsSendTypePadding);
	const auto restricted = inner->add(
		object_ptr<Ui::Radiobutton>(
			inner,
			group,
			kOptionPremium,
			tr::lng_messages_privacy_restricted(tr::now),
			st::messagePrivacyCheck),
		st::settingsSendTypePadding + style::margins(
			0,
			st::messagePrivacyRadioSkip,
			0,
			st::messagePrivacyBottomSkip));

	using WeakToast = base::weak_ptr<Ui::Toast::Instance>;
	const auto toast = std::make_shared<WeakToast>();
	const auto showToast = [=] {
		auto link = Ui::Text::Link(
			Ui::Text::Semibold(
				tr::lng_messages_privacy_premium_link(tr::now)));
		(*toast) = controller->showToast({
			.text = tr::lng_messages_privacy_premium(
				tr::now,
				lt_link,
				link,
				Ui::Text::WithEntities),
			.filter = crl::guard(&controller->session(), [=](
					const ClickHandlerPtr &,
					Qt::MouseButton button) {
				if (button == Qt::LeftButton) {
					if (const auto strong = toast->get()) {
						strong->hideAnimated();
						(*toast) = nullptr;
						Settings::ShowPremium(
							controller,
							u"noncontact_peers_require_premium"_q);
						return true;
					}
				}
				return false;
			}),
		});
	};
	if (!allowed()) {
		CreateRadiobuttonLock(restricted, st::messagePrivacyCheck);

		group->setChangedCallback([=](int value) {
			if (value == kOptionPremium) {
				group->setValue(kOptionAll);
				showToast();
			}
		});
	}

	Ui::AddDividerText(inner, tr::lng_messages_privacy_about());
	if (!allowed()) {
		Ui::AddSkip(inner);
		Settings::AddButtonWithIcon(
			inner,
			tr::lng_messages_privacy_premium_button(),
			st::messagePrivacySubscribe,
			{ .icon = &st::menuBlueIconPremium }
		)->setClickedCallback([=] {
			Settings::ShowPremium(
				controller,
				u"noncontact_peers_require_premium"_q);
		});
		Ui::AddSkip(inner);
		Ui::AddDividerText(inner, tr::lng_messages_privacy_premium_about());
		box->addButton(tr::lng_about_done(), [=] {
			box->closeBox();
		});
	} else {
		box->addButton(tr::lng_settings_save(), [=] {
			if (allowed()) {
				privacy->updateNewRequirePremium(
					group->current() == kOptionPremium);
				box->closeBox();
			} else {
				showToast();
			}
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}
}
