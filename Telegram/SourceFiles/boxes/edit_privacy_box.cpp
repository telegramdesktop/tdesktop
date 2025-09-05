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
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/shadow.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "history/history.h"
#include "boxes/peer_list_controllers.h"
#include "settings/settings_premium.h"
#include "settings/settings_privacy_controllers.h"
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
#include "styles/style_window.h"

namespace {

constexpr auto kPremiumsRowId = PeerId(FakeChatId(BareId(1))).value;
constexpr auto kMiniAppsRowId = PeerId(FakeChatId(BareId(2))).value;
constexpr auto kDefaultPrivateMessagesPrice = 10;

using Exceptions = Api::UserPrivacy::Exceptions;

enum class SpecialRowType {
	Premiums,
	MiniApps,
};

[[nodiscard]] PaintRoundImageCallback GeneratePremiumsUserpicCallback(
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

[[nodiscard]] PaintRoundImageCallback GenerateMiniAppsUserpicCallback(
		bool forceRound) {
	return [=](QPainter &p, int x, int y, int outerWidth, int size) {
		const auto &color1 = st::historyPeer6UserpicBg;
		const auto &color2 = st::historyPeer6UserpicBg2;

		auto hq = PainterHighQualityEnabler(p);
		auto gradient = QLinearGradient(x, y, x, y + size);
		gradient.setStops({ { 0., color1->c }, { 1., color2->c } });

		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		if (forceRound) {
			p.drawEllipse(x, y, size, size);
		} else {
			const auto radius = size * Ui::ForumUserpicRadiusMultiplier();
			p.drawRoundedRect(x, y, size, size, radius, radius);
		}
		st::windowFilterTypeBots.paintInCenter(p, QRect(x, y, size, size));
	};
}

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
		std::optional<SpecialRowType> allowChooseSpecial);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;
	bool isForeignRow(PeerListRowId itemId) override;
	bool handleDeselectForeignRow(PeerListRowId itemId) override;

	[[nodiscard]] bool premiumsSelected() const;
	[[nodiscard]] bool miniAppsSelected() const;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

private:
	[[nodiscard]] object_ptr<Ui::RpWidget> prepareSpecialRowList(
		SpecialRowType type);

	const not_null<Main::Session*> _session;
	rpl::producer<QString> _title;
	Exceptions _selected;
	std::optional<SpecialRowType> _allowChooseSpecial;

	PeerListContentDelegate *_typesDelegate = nullptr;
	Fn<void(PeerListRowId)> _deselectOption;

};

struct RowSelectionChange {
	not_null<PeerListRow*> row;
	bool checked = false;
};

class SpecialRow final : public PeerListRow {
public:
	explicit SpecialRow(SpecialRowType type);

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;
	bool useForumLikeUserpic() const override;

};

class TypesController final : public PeerListController {
public:
	TypesController(not_null<Main::Session*> session, SpecialRowType type);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] bool specialSelected() const;
	[[nodiscard]] rpl::producer<bool> specialChanges() const;
	[[nodiscard]] auto rowSelectionChanges() const
		-> rpl::producer<RowSelectionChange>;

private:
	const not_null<Main::Session*> _session;
	const SpecialRowType _type;

	rpl::event_stream<> _selectionChanged;
	rpl::event_stream<RowSelectionChange> _rowSelectionChanges;

};

SpecialRow::SpecialRow(SpecialRowType type)
: PeerListRow((type == SpecialRowType::Premiums)
	? kPremiumsRowId
	: kMiniAppsRowId) {
	setCustomStatus((id() == kPremiumsRowId)
		? tr::lng_edit_privacy_premium_status(tr::now)
		: tr::lng_edit_privacy_miniapps_status(tr::now));
}

QString SpecialRow::generateName() {
	return (id() == kPremiumsRowId)
		? tr::lng_edit_privacy_premium(tr::now)
		: tr::lng_edit_privacy_miniapps(tr::now);
}

QString SpecialRow::generateShortName() {
	return generateName();
}

PaintRoundImageCallback SpecialRow::generatePaintUserpicCallback(
		bool forceRound) {
	return (id() == kPremiumsRowId)
		? GeneratePremiumsUserpicCallback(forceRound)
		: GenerateMiniAppsUserpicCallback(forceRound);
}

bool SpecialRow::useForumLikeUserpic() const {
	return true;
}

TypesController::TypesController(
	not_null<Main::Session*> session,
	SpecialRowType type)
: _session(session)
, _type(type) {
}

Main::Session &TypesController::session() const {
	return *_session;
}

void TypesController::prepare() {
	delegate()->peerListAppendRow(std::make_unique<SpecialRow>(_type));
	delegate()->peerListRefreshRows();
}

bool TypesController::specialSelected() const {
	const auto premiums = (_type == SpecialRowType::Premiums);
	const auto row = delegate()->peerListFindRow(premiums
		? kPremiumsRowId
		: kMiniAppsRowId);
	Assert(row != nullptr);

	return row->checked();
}

void TypesController::rowClicked(not_null<PeerListRow*> row) {
	const auto checked = !row->checked();
	delegate()->peerListSetRowChecked(row, checked);
	_rowSelectionChanges.fire({ row, checked });
}

rpl::producer<bool> TypesController::specialChanges() const {
	return _rowSelectionChanges.events(
	) | rpl::map([=] {
		return specialSelected();
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
	std::optional<SpecialRowType> allowChooseSpecial)
: ChatsListBoxController(session)
, _session(session)
, _title(std::move(title))
, _selected(selected)
, _allowChooseSpecial(allowChooseSpecial) {
}

Main::Session &PrivacyExceptionsBoxController::session() const {
	return *_session;
}

void PrivacyExceptionsBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(std::move(_title));
	if (_allowChooseSpecial || _selected.premiums || _selected.miniapps) {
		delegate()->peerListSetAboveWidget(prepareSpecialRowList(
			_allowChooseSpecial.value_or(_selected.premiums
				? SpecialRowType::Premiums
				: SpecialRowType::MiniApps)));
	}
	delegate()->peerListAddSelectedPeers(_selected.peers);
}

bool PrivacyExceptionsBoxController::isForeignRow(PeerListRowId itemId) {
	return (itemId == kPremiumsRowId)
		|| (itemId == kMiniAppsRowId);
}

bool PrivacyExceptionsBoxController::handleDeselectForeignRow(
		PeerListRowId itemId) {
	if (isForeignRow(itemId)) {
		_deselectOption(itemId);
		return true;
	}
	return false;
}

auto PrivacyExceptionsBoxController::prepareSpecialRowList(
	SpecialRowType type)
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
		type);
	const auto content = result->add(object_ptr<PeerListContent>(
		container,
		controller));
	_typesDelegate->setContent(content);
	controller->setDelegate(_typesDelegate);

	const auto selectType = [&](PeerListRowId id) {
		const auto row = _typesDelegate->peerListFindRow(id);
		if (row) {
			content->changeCheckState(row, true, anim::type::instant);
			this->delegate()->peerListSetForeignRowChecked(
				row,
				true,
				anim::type::instant);
		}
	};
	if (_selected.premiums) {
		selectType(kPremiumsRowId);
	} else if (_selected.miniapps) {
		selectType(kMiniAppsRowId);
	}
	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_edit_privacy_users_and_groups()));

	controller->specialChanges(
	) | rpl::start_with_next([=](bool chosen) {
		if (type == SpecialRowType::Premiums) {
			_selected.premiums = chosen;
		} else {
			_selected.miniapps = chosen;
		}
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
			} else if (itemId == kMiniAppsRowId) {
				_selected.miniapps = false;
			}
			_typesDelegate->peerListSetRowChecked(row, false);
		}
	};

	return result;
}

bool PrivacyExceptionsBoxController::premiumsSelected() const {
	return _selected.premiums;
}

bool PrivacyExceptionsBoxController::miniAppsSelected() const {
	return _selected.miniapps;
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

[[nodiscard]] object_ptr<Ui::RpWidget> MakeChargeStarsSlider(
		QWidget *parent,
		not_null<const style::MediaSlider*> sliderStyle,
		not_null<const style::FlatLabel*> labelStyle,
		int valuesCount,
		Fn<int(int)> valueByIndex,
		int value,
		int minValue,
		int maxValue,
		Fn<void(int)> valueProgress,
		Fn<void(int)> valueFinished) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto raw = result.data();

	const auto labels = raw->add(object_ptr<Ui::RpWidget>(raw));
	const auto min = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		QString::number(minValue),
		*labelStyle);
	const auto max = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		QString::number(maxValue),
		*labelStyle);
	const auto current = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		QString::number(value),
		*labelStyle);
	min->setTextColorOverride(st::windowSubTextFg->c);
	max->setTextColorOverride(st::windowSubTextFg->c);
	const auto slider = raw->add(object_ptr<Ui::MediaSliderWheelless>(
		raw,
		*sliderStyle));
	labels->resize(
		labels->width(),
		current->height() + st::defaultVerticalListSkip);
	struct State {
		int indexMin = 0;
		int index = 0;
	};
	const auto state = raw->lifetime().make_state<State>();
	const auto updateByIndex = [=] {
		const auto outer = labels->width();
		const auto minWidth = min->width();
		const auto maxWidth = max->width();
		const auto currentWidth = current->width();
		if (minWidth + maxWidth + currentWidth > outer) {
			return;
		}

		min->moveToLeft(0, 0, outer);
		max->moveToRight(0, 0, outer);
		current->moveToLeft((outer - current->width()) / 2, 0, outer);
	};
	const auto updateByValue = [=](int value) {
		current->setText(value > 0
			? tr::lng_action_gift_for_stars(tr::now, lt_count, value)
			: tr::lng_manage_monoforum_free(tr::now));

		state->index = 0;
		auto maxIndex = valuesCount - 1;
		while (state->index < maxIndex) {
			const auto mid = (state->index + maxIndex) / 2;
			const auto midValue = valueByIndex(mid);
			if (midValue == value) {
				state->index = mid;
				break;
			} else if (midValue < value) {
				state->index = mid + 1;
			} else {
				maxIndex = mid - 1;
			}
		}
		updateByIndex();
	};
	const auto progress = [=](int value) {
		updateByValue(value);
		valueProgress(value);
	};
	const auto finished = [=](int value) {
		updateByValue(value);
		valueFinished(value);
	};
	style::PaletteChanged() | rpl::start_with_next([=] {
		min->setTextColorOverride(st::windowSubTextFg->c);
		max->setTextColorOverride(st::windowSubTextFg->c);
	}, raw->lifetime());
	updateByValue(value);
	state->indexMin = 0;

	slider->setPseudoDiscrete(
		valuesCount,
		valueByIndex,
		value,
		progress,
		finished,
		state->indexMin);
	slider->resize(slider->width(), sliderStyle->seekSize.height());

	raw->widthValue() | rpl::start_with_next([=](int width) {
		labels->resizeToWidth(width);
		updateByIndex();
	}, slider->lifetime());

	return result;
}

void EditNoPaidMessagesExceptions(
		not_null<Window::SessionController*> window,
		const Api::UserPrivacy::Rule &value) {
	auto controller = std::make_unique<PrivacyExceptionsBoxController>(
		&window->session(),
		tr::lng_messages_privacy_remove_fee(),
		value.always,
		std::optional<SpecialRowType>());
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListBox*> box) {
		box->addButton(tr::lng_settings_save(), [=] {
			auto copy = value;
			auto &setTo = copy.always;
			setTo.peers = box->collectSelectedRows();
			setTo.premiums = false;
			setTo.miniapps = false;
			auto &removeFrom = copy.never;
			for (const auto peer : setTo.peers) {
				removeFrom.peers.erase(
					ranges::remove(removeFrom.peers, peer),
					end(removeFrom.peers));
			}
			window->session().api().userPrivacy().save(
				Api::UserPrivacy::Key::NoPaidMessages,
				copy);
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	window->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
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
	if (_controller->allowMiniAppsToggle(Exception::Always)
		&& _value.option == Option::Everyone) {
		// If we switch from Everyone to Contacts or Nobody suggest MiniApps.
		_value.always.miniapps = true;
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
		(_controller->allowPremiumsToggle(exception)
			? SpecialRowType::Premiums
			: _controller->allowMiniAppsToggle(exception)
			? SpecialRowType::MiniApps
			: std::optional<SpecialRowType>()));
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListBox*> box) {
		box->addButton(tr::lng_settings_save(), crl::guard(this, [=] {
			auto &setTo = exceptions(exception);
			setTo.peers = box->collectSelectedRows();
			setTo.premiums = controller->premiumsSelected();
			setTo.miniapps = controller->miniAppsSelected();
			const auto type = [&] {
				switch (exception) {
				case Exception::Always: return Exception::Never;
				case Exception::Never: return Exception::Always;
				}
				Unexpected("Invalid exception value.");
			}();
			auto &removeFrom = exceptions(type);
			for (const auto peer : exceptions(exception).peers) {
				removeFrom.peers.erase(
					ranges::remove(removeFrom.peers, peer),
					end(removeFrom.peers));
			}
			if (setTo.premiums) {
				removeFrom.premiums = false;
			}
			if (setTo.miniapps) {
				removeFrom.miniapps = false;
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
			return value.premiums
				? (!count
					? tr::lng_edit_privacy_premium(tr::now)
					: tr::lng_edit_privacy_exceptions_premium_and(
						tr::now,
						lt_users,
						users))
				: value.miniapps
				? (!count
					? tr::lng_edit_privacy_miniapps(tr::now)
					: tr::lng_edit_privacy_exceptions_miniapps_and(
						tr::now,
						lt_users,
						users))
				: users;
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
	constexpr auto kOptionCharge = 2;

	const auto session = &controller->session();
	const auto allowed = [=] {
		return session->premium()
			|| session->appConfig().newRequirePremiumFree();
	};
	const auto privacy = &session->api().globalPrivacy();
	const auto inner = box->verticalLayout();
	inner->add(object_ptr<Ui::PlainShadow>(box));

	Ui::AddSkip(inner, st::messagePrivacyTopSkip);
	Ui::AddSubsectionTitle(inner, tr::lng_messages_privacy_subtitle());
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		(!allowed()
			? kOptionAll
			: privacy->newRequirePremiumCurrent()
			? kOptionPremium
			: privacy->newChargeStarsCurrent()
			? kOptionCharge
			: kOptionAll));
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

	Ui::AddDividerText(inner, tr::lng_messages_privacy_about());

	const auto available = session->appConfig().paidMessagesAvailable();

	const auto charged = available
		? inner->add(
			object_ptr<Ui::Radiobutton>(
				inner,
				group,
				kOptionCharge,
				tr::lng_messages_privacy_charge(tr::now),
				st::messagePrivacyCheck),
			st::settingsSendTypePadding + style::margins(
				0,
				st::messagePrivacyBottomSkip,
				0,
				st::messagePrivacyBottomSkip))
		: nullptr;

	struct State {
		rpl::variable<int> stars;
	};
	const auto state = std::make_shared<State>();
	const auto savedValue = privacy->newChargeStarsCurrent();

	if (available) {
		Ui::AddDividerText(inner, tr::lng_messages_privacy_charge_about());

		const auto chargeWrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));
		const auto chargeInner = chargeWrap->entity();

		Ui::AddSkip(chargeInner);

		state->stars = SetupChargeSlider(
			chargeInner,
			session->user(),
			(savedValue > 0) ? savedValue : std::optional<int>(),
			kDefaultPrivateMessagesPrice);

		Ui::AddSkip(chargeInner);
		Ui::AddSubsectionTitle(
			chargeInner,
			tr::lng_messages_privacy_exceptions());

		const auto key = Api::UserPrivacy::Key::NoPaidMessages;
		session->api().userPrivacy().reload(key);
		auto label = session->api().userPrivacy().value(
			key
		) | rpl::map([=](const Api::UserPrivacy::Rule &value) {
			using namespace Settings;
			const auto always = ExceptionUsersCount(value.always.peers);
			return always
				? tr::lng_edit_privacy_exceptions_count(
					tr::now,
					lt_count,
					always)
				: tr::lng_edit_privacy_exceptions_add(tr::now);
		});

		const auto exceptions = Settings::AddButtonWithLabel(
			chargeInner,
			tr::lng_messages_privacy_remove_fee(),
			std::move(label),
			st::settingsButtonNoIcon);

		const auto shower = exceptions->lifetime().make_state<rpl::lifetime>();
		exceptions->setClickedCallback([=] {
			*shower = session->api().userPrivacy().value(
				key
			) | rpl::take(
				1
			) | rpl::start_with_next([=](const Api::UserPrivacy::Rule &value) {
				EditNoPaidMessagesExceptions(controller, value);
			});
		});
		Ui::AddSkip(chargeInner);
		Ui::AddDividerText(
			chargeInner,
			tr::lng_messages_privacy_remove_about());

		using namespace rpl::mappers;
		chargeWrap->toggleOn(group->value() | rpl::map(_1 == kOptionCharge));
		chargeWrap->finishAnimating();
	}
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
		if (charged) {
			CreateRadiobuttonLock(charged, st::messagePrivacyCheck);
		}

		group->setChangedCallback([=](int value) {
			if (value == kOptionPremium || value == kOptionCharge) {
				group->setValue(kOptionAll);
				showToast();
			}
		});

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
				const auto value = group->current();
				const auto premiumRequired = (value == kOptionPremium);
				const auto chargeStars = (value == kOptionCharge)
					? state->stars.current()
					: 0;
				privacy->updateMessagesPrivacy(premiumRequired, chargeStars);
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

rpl::producer<int> SetupChargeSlider(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		std::optional<int> savedValue,
		int defaultValue,
		bool allowZero) {
	struct State {
		rpl::variable<int> stars;
	};
	const auto broadcast = peer->isBroadcast();
	const auto group = !broadcast && !peer->isUser();
	const auto state = container->lifetime().make_state<State>();
	const auto chargeStars = savedValue.value_or(defaultValue);
	state->stars = chargeStars;

	Ui::AddSubsectionTitle(container, broadcast
		? tr::lng_manage_monoforum_price()
		: group
		? tr::lng_rights_charge_price()
		: tr::lng_messages_privacy_price());

	auto values = std::vector<int>();
	const auto minStars = allowZero ? 0 : 1;
	const auto maxStars = peer->session().appConfig().paidMessageStarsMax();
	if (chargeStars < minStars) {
		values.push_back(chargeStars);
	}
	for (auto i = minStars; i < std::min(100, maxStars); ++i) {
		values.push_back(i);
	}
	for (auto i = 100; i < std::min(1000, maxStars); i += 10) {
		if (i < chargeStars + 10 && chargeStars < i) {
			values.push_back(chargeStars);
		}
		values.push_back(i);
	}
	for (auto i = 1000; i < maxStars + 1; i += 100) {
		if (i < chargeStars + 100 && chargeStars < i) {
			values.push_back(chargeStars);
		}
		values.push_back(i);
	}
	const auto valuesCount = int(values.size());
	const auto setStars = [=](int value) {
		state->stars = value;
	};
	container->add(
		MakeChargeStarsSlider(
			container,
			&st::settingsScale,
			&st::settingsScaleLabel,
			valuesCount,
			[=](int index) { return values[index]; },
			chargeStars,
			minStars,
			maxStars,
			setStars,
			setStars),
		st::boxRowPadding);

	const auto skip = 2 * st::defaultVerticalListSkip;
	Ui::AddSkip(container, skip);

	const auto details = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	state->stars.value() | rpl::start_with_next([=](int stars) {
		while (details->count()) {
			delete details->widgetAt(0);
		}
		if (!stars) {
			Ui::AddDivider(details);
			return;
		}
		const auto &appConfig = peer->session().appConfig();
		const auto percent = appConfig.paidMessageCommission();
		const auto ratio = appConfig.starsWithdrawRate();
		const auto dollars = int(base::SafeRound(stars * ratio));
		const auto amount = Ui::FillAmountAndCurrency(dollars, u"USD"_q);
		Ui::AddDividerText(
			details,
			(broadcast
				? tr::lng_manage_monoforum_price_about
				: group
				? tr::lng_rights_charge_price_about
				: tr::lng_messages_privacy_price_about)(
					lt_percent,
					rpl::single(QString::number(percent / 10.) + '%'),
					lt_amount,
					rpl::single('~' + amount)));
	}, details->lifetime());
	return state->stars.value();
}

void EditDirectMessagesPriceBox(
		not_null<Ui::GenericBox*> box,
		not_null<ChannelData*> channel,
		std::optional<int> savedValue,
		Fn<void(std::optional<int>)> callback) {
	box->setTitle(tr::lng_manage_monoforum());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();

	Settings::AddDividerTextWithLottie(container, {
		.lottie = u"direct_messages"_q,
		.lottieSize = st::settingsFilterIconSize,
		.lottieMargins = st::settingsFilterIconPadding,
		.showFinished = box->showFinishes(),
		.about = tr::lng_manage_monoforum_about(
			Ui::Text::RichLangValue
		),
		.aboutMargins = st::settingsFilterDividerLabelPadding,
	});

	Ui::AddSkip(container);

	const auto toggle = container->add(object_ptr<Ui::SettingsButton>(
		box,
		tr::lng_manage_monoforum_allow(),
		st::settingsButtonNoIcon));
	toggle->toggleOn(rpl::single(savedValue.has_value()));

	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	const auto wrap = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		style::margins());
	wrap->toggle(savedValue.has_value(), anim::type::instant);
	wrap->toggleOn(toggle->toggledChanges());

	const auto result = box->lifetime().make_state<int>(
		savedValue.value_or(0));

	const auto inner = wrap->entity();
	Ui::AddSkip(inner);
	SetupChargeSlider(
		inner,
		channel,
		savedValue,
		channel->session().appConfig().paidMessageChannelStarsDefault(),
		true
	) | rpl::start_with_next([=](int stars) {
		*result = stars;
	}, box->lifetime());

	box->addButton(tr::lng_settings_save(), [=] {
		const auto weak = base::make_weak(box);
		callback(toggle->toggled() ? *result : std::optional<int>());
		if (const auto strong = weak.get()) {
			strong->closeBox();
		}
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}
