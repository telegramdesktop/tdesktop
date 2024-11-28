/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/bot/starref/info_bot_starref_setup_widget.h"

#include "apiwrap.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "core/click_handler_types.h"
#include "data/data_user.h"
#include "info/profile/info_profile_icon.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Info::BotStarRef::Setup {
namespace {

constexpr auto kDurationForeverValue = 999;
constexpr auto kCommissionDefault = 200;
constexpr auto kDurationDefault = 12;

} // namespace

struct State {
	not_null<UserData*> user;
	StarRefProgram program;
	bool exists = false;
};

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(QWidget *parent, not_null<Controller*> controller);

	[[nodiscard]] not_null<PeerData*> peer() const;
	[[nodiscard]] not_null<State*> state();

	void showFinished();
	void setInnerFocus();

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

private:
	void prepare();
	void setupInfo();
	void setupCommission();
	void setupDuration();
	void setupViewExisting();
	void setupEnd();

	[[nodiscard]] object_ptr<Ui::RpWidget> infoRow(
		rpl::producer<QString> title,
		rpl::producer<QString> text,
		not_null<const style::icon*> icon);

	const not_null<Controller*> _controller;
	State _state;
	const not_null<Ui::VerticalLayout*> _container;

};

[[nodiscard]] int ValueForCommission(const State &state) {
	return state.program.commission
		? state.program.commission
		: kCommissionDefault;
}

[[nodiscard]] int ValueForDurationMonths(const State &state) {
	return state.program.durationMonths
		? state.program.durationMonths
		: state.exists
		? kDurationForeverValue
		: kDurationDefault;
}

[[nodiscard]] State StateForPeer(not_null<PeerData*> peer) {
	const auto user = peer->asUser();
	const auto program = user->botInfo->starRefProgram;
	return State{
		.user = user,
		.program = program,
		.exists = (program.commission > 0),
	};
}

InnerWidget::InnerWidget(QWidget *parent, not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _state(StateForPeer(_controller->key().starrefPeer()))
, _container(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	prepare();
}

not_null<State*> InnerWidget::state() {
	return &_state;
}

void InnerWidget::prepare() {
	Ui::ResizeFitChild(this, _container);

	setupInfo();
	Ui::AddSkip(_container);
	Ui::AddDivider(_container);
	setupCommission();
	setupDuration();
	Ui::AddSkip(_container);
	setupViewExisting();
	setupEnd();
}

void InnerWidget::setupInfo() {
	AddSkip(_container, st::defaultVerticalListSkip * 2);

	_container->add(infoRow(
		tr::lng_star_ref_share_title(),
		tr::lng_star_ref_share_about(),
		&st::menuIconPremium));

	_container->add(infoRow(
		tr::lng_star_ref_launch_title(),
		tr::lng_star_ref_launch_about(),
		&st::menuIconChannel));

	_container->add(infoRow(
		tr::lng_star_ref_let_title(),
		tr::lng_star_ref_let_about(),
		&st::menuIconStarRefLink));
}

void InnerWidget::setupCommission() {
	Ui::AddSkip(_container);
	Ui::AddSubsectionTitle(_container, tr::lng_star_ref_commission_title());

	const auto commission = ValueForCommission(_state);

	auto values = std::vector<int>();
	if (commission > 0 && commission < 10) {
		values.push_back(commission);
	}
	for (auto i = 1; i != 91; ++i) {
		values.push_back(i * 10);
		if (i * 10 < commission && (i == 90 || (i + 1) * 10 > commission)) {
			values.push_back(commission);
		}
	}
	const auto valuesCount = int(values.size());

	auto sliderWithLabel = ::Settings::MakeSliderWithLabel(
		_container,
		st::settingsScale,
		st::settingsScaleLabel,
		st::normalFont->spacew * 2,
		st::settingsScaleLabel.style.font->width("89.9%"),
		true);
	_container->add(
		std::move(sliderWithLabel.widget),
		st::settingsBigScalePadding);
	const auto slider = sliderWithLabel.slider;
	const auto label = sliderWithLabel.label;

	const auto updateLabel = [=](int value) {
		const auto labelText = QString::number(value / 10.) + '%';
		label->setText(labelText);
	};
	const auto setCommission = [=](int value) {
		_state.program.commission = value;
		updateLabel(value);
	};
	updateLabel(commission);

	slider->setPseudoDiscrete(
		valuesCount,
		[=](int index) { return values[index]; },
		commission,
		setCommission,
		setCommission);

	Ui::AddSkip(_container);
	Ui::AddDividerText(_container, tr::lng_star_ref_commission_about());
}

void InnerWidget::setupDuration() {
	Ui::AddSkip(_container);
	Ui::AddSubsectionTitle(_container, tr::lng_star_ref_duration_title());

	auto values = std::vector<int>{ 1, 3, 6, 12, 24, 36, 999 };
	const auto valuesCount = int(values.size());

	auto sliderWithLabel = ::Settings::MakeSliderWithLabel(
		_container,
		st::settingsScale,
		st::settingsScaleLabel,
		st::normalFont->spacew * 2,
		st::settingsScaleLabel.style.font->width("3y"),
		true);
	_container->add(
		std::move(sliderWithLabel.widget),
		st::settingsBigScalePadding);
	const auto slider = sliderWithLabel.slider;
	const auto label = sliderWithLabel.label;

	const auto updateLabel = [=](int value) {
		const auto labelText = (value < 12)
			? (QString::number(value) + 'm')
			: (value < 999)
			? (QString::number(value / 12) + 'y')
			: u"inf"_q;
		label->setText(labelText);
	};
	const auto durationMonths = ValueForDurationMonths(_state);
	const auto setDurationMonths = [=](int value) {
		_state.program.durationMonths = (value == kDurationForeverValue)
			? 0
			: value;
		updateLabel(durationMonths);
	};
	updateLabel(durationMonths);

	slider->setPseudoDiscrete(
		valuesCount,
		[=](int index) { return values[index]; },
		durationMonths,
		setDurationMonths,
		setDurationMonths);

	Ui::AddSkip(_container);
	Ui::AddDividerText(_container, tr::lng_star_ref_duration_about());
}

void InnerWidget::setupViewExisting() {
	const auto button = AddViewListButton(
		_container,
		tr::lng_star_ref_existing_title(),
		tr::lng_star_ref_existing_about());
	button->setClickedCallback([=] {
		_controller->showToast(u"List or smth.."_q);
	});

	Ui::AddSkip(_container);
	Ui::AddDivider(_container);
	Ui::AddSkip(_container);
}

void InnerWidget::setupEnd() {
	if (!_state.exists) {
		return;
	}
	const auto end = _container->add(object_ptr<Ui::SettingsButton>(
		_container,
		tr::lng_star_ref_end(),
		st::settingsAttentionButton));
	end->setClickedCallback([=] {
		using Flag = MTPbots_UpdateStarRefProgram::Flag;
		const auto user = _state.user;
		const auto weak = Ui::MakeWeak(this);
		user->session().api().request(MTPbots_UpdateStarRefProgram(
			MTP_flags(0),
			user->inputUser,
			MTP_int(0),
			MTP_int(0)
		)).done([=] {
			user->botInfo->starRefProgram.commission = 0;
			user->botInfo->starRefProgram.durationMonths = 0;
			user->updateFullForced();
			if (weak) {
				_controller->showToast("Removed!");
				_controller->showBackFromStack();
			}
		}).fail(crl::guard(weak, [=] {
			_controller->showToast("Remove failed!");
		})).send();
	});
	Ui::AddSkip(_container);
	Ui::AddDivider(_container);
}

object_ptr<Ui::RpWidget> InnerWidget::infoRow(
		rpl::producer<QString> title,
		rpl::producer<QString> text,
		not_null<const style::icon*> icon) {
	auto result = object_ptr<Ui::VerticalLayout>(_container);
	const auto raw = result.data();

	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			std::move(title) | Ui::Text::ToBold(),
			st::defaultFlatLabel),
		st::settingsPremiumRowTitlePadding);
	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			std::move(text),
			st::boxDividerLabel),
		st::settingsPremiumRowAboutPadding);
	object_ptr<Info::Profile::FloatingIcon>(
		raw,
		*icon,
		st::starrefInfoIconPosition);

	return result;
}

not_null<PeerData*> InnerWidget::peer() const {
	return _controller->key().starrefPeer();
}

void InnerWidget::showFinished() {

}

void InnerWidget::setInnerFocus() {
	setFocus();
}

void InnerWidget::saveState(not_null<Memento*> memento) {

}

void InnerWidget::restoreState(not_null<Memento*> memento) {

}

Memento::Memento(not_null<Controller*> controller)
: ContentMemento(Tag(controller->starrefPeer(), controller->starrefType())) {
}

Memento::Memento(not_null<PeerData*> peer)
: ContentMemento(Tag(peer, Type::Setup)) {
}

Memento::~Memento() = default;

Section Memento::section() const {
	return Section(Section::Type::BotStarRef);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller)
, _inner(setInnerWidget(object_ptr<InnerWidget>(this, controller)))
, _state(_inner->state()) {
	_top = setupTop();
	_bottom = setupBottom();
}

not_null<PeerData*> Widget::peer() const {
	return _inner->peer();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	return (memento->starrefPeer() == peer());
}

rpl::producer<QString> Widget::title() {
	return tr::lng_star_ref_title();
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

rpl::producer<bool> Widget::desiredShadowVisibility() const {
	return rpl::single<bool>(true);
}

void Widget::showFinished() {
	_inner->showFinished();
}

void Widget::setInnerFocus() {
	_inner->setInnerFocus();
}

void Widget::enableBackButton() {
	_backEnabled = true;
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(controller());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

std::unique_ptr<Ui::Premium::TopBarAbstract> Widget::setupTop() {
	auto title = tr::lng_star_ref_title();
	auto about = tr::lng_star_ref_about() | Ui::Text::ToWithEntities();

	const auto controller = this->controller();
	const auto weak = base::make_weak(controller->parentController());
	const auto clickContextOther = [=] {
		return QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = weak,
			.botStartAutoSubmit = true,
		});
	};
	auto result = std::make_unique<Ui::Premium::TopBar>(
		this,
		st::starrefCover,
		Ui::Premium::TopBarDescriptor{
			.clickContextOther = clickContextOther,
			.logo = u"affiliate"_q,
			.title = std::move(title),
			.about = std::move(about),
			.light = true,
		});
	const auto raw = result.get();

	controller->wrapValue(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		raw->setRoundEdges(wrap == Info::Wrap::Layer);
	}, raw->lifetime());

	const auto baseHeight = st::starrefCoverHeight;
	raw->resize(width(), baseHeight);

	raw->additionalHeight(
	) | rpl::start_with_next([=](int additionalHeight) {
		raw->setMaximumHeight(baseHeight + additionalHeight);
		raw->setMinimumHeight(baseHeight + additionalHeight);
		setPaintPadding({ 0, raw->height(), 0, 0 });
	}, raw->lifetime());

	controller->wrapValue(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		const auto isLayer = (wrap == Info::Wrap::Layer);
		_back = base::make_unique_q<Ui::FadeWrap<Ui::IconButton>>(
			raw,
			object_ptr<Ui::IconButton>(
				raw,
				(isLayer
					? st::infoLayerTopBar.back
					: st::infoTopBar.back)),
			st::infoTopBarScale);
		_back->setDuration(0);
		_back->toggleOn(isLayer
			? _backEnabled.value() | rpl::type_erased()
			: rpl::single(true));
		_back->entity()->addClickHandler([=] {
			controller->showBackFromStack();
		});
		_back->toggledValue(
		) | rpl::start_with_next([=](bool toggled) {
			const auto &st = isLayer ? st::infoLayerTopBar : st::infoTopBar;
			raw->setTextPosition(
				toggled ? st.back.width : st.titlePosition.x(),
				st.titlePosition.y());
		}, _back->lifetime());

		if (!isLayer) {
			_close = nullptr;
		} else {
			_close = base::make_unique_q<Ui::IconButton>(
				raw,
				st::infoTopBarClose);
			_close->addClickHandler([=] {
				controller->parentController()->hideLayer();
				controller->parentController()->hideSpecialLayer();
			});
			raw->widthValue(
			) | rpl::start_with_next([=] {
				_close->moveToRight(0, 0);
			}, _close->lifetime());
		}
	}, raw->lifetime());

	raw->move(0, 0);
	widthValue() | rpl::start_with_next([=](int width) {
		raw->resizeToWidth(width);
		setScrollTopSkip(raw->height());
	}, raw->lifetime());

	return result;
}

std::unique_ptr<Ui::RpWidget> Widget::setupBottom() {
	auto result = std::make_unique<Ui::VerticalLayout>(this);
	const auto raw = result.get();

	auto text = base::timer_each(100) | rpl::map([=] {
		const auto till = _state->user->botInfo->starRefProgram.endDate;
		const auto now = base::unixtime::now();
		const auto left = (till > now) ? (till - now) : 0;
		return left
			? tr::lng_star_ref_start_disabled(
				tr::now,
				lt_time,
				QString::number(left))
			: _state->exists
			? tr::lng_star_ref_update(tr::now)
			: tr::lng_star_ref_start(tr::now);
	});
	const auto save = raw->add(
		object_ptr<Ui::RoundButton>(
			raw,
			rpl::duplicate(text),
			st::defaultActiveButton),
		st::starrefButtonMargin);
	std::move(text) | rpl::start_with_next([=] {
		save->resizeToWidth(raw->width()
			- st::starrefButtonMargin.left()
			- st::starrefButtonMargin.right());
	}, save->lifetime());
	save->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	const auto &margins = st::defaultBoxDividerLabelPadding;
	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			(_state->exists
				? tr::lng_star_ref_update_info
				: tr::lng_star_ref_start_info)(
					lt_terms,
					tr::lng_star_ref_button_link() | Ui::Text::ToLink(),
					Ui::Text::WithEntities),
			st::boxDividerLabel),
		QMargins(margins.left(), 0, margins.right(), 0));
	save->setClickedCallback([=] {
		using Flag = MTPbots_UpdateStarRefProgram::Flag;
		const auto weak = Ui::MakeWeak(this);
		const auto user = _state->user;
		auto program = StarRefProgram{
			.commission = _state->program.commission,
			.durationMonths = _state->program.durationMonths,
		};
		user->session().api().request(MTPbots_UpdateStarRefProgram(
			MTP_flags((program.commission > 0 && program.durationMonths > 0)
				? Flag::f_duration_months
				: Flag()),
			user->inputUser,
			MTP_int(program.commission),
			MTP_int(program.durationMonths)
		)).done([=] {
			user->botInfo->starRefProgram.commission = program.commission;
			user->botInfo->starRefProgram.durationMonths
				= program.durationMonths;
			if (weak) {
				controller()->showBackFromStack();
			}
		}).fail(crl::guard(weak, [=] {
			controller()->showToast("Failed!");
		})).send();
	});

	widthValue() | rpl::start_with_next([=](int width) {
		raw->resizeToWidth(width);
	}, raw->lifetime());

	rpl::combine(
		raw->heightValue(),
		heightValue()
	) | rpl::start_with_next([=](int height, int fullHeight) {
		setScrollBottomSkip(height);
		raw->move(0, fullHeight - height);
	}, raw->lifetime());

	return result;
}

std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(
			1,
			std::make_shared<Memento>(peer)));
}

not_null<Ui::AbstractButton*> AddViewListButton(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> title,
		rpl::producer<QString> subtitle) {
	const auto &stLabel = st::defaultFlatLabel;
	const auto iconSize = st::settingsPremiumIconDouble.size();
	const auto &titlePadding = st::settingsPremiumRowTitlePadding;
	const auto &descriptionPadding = st::settingsPremiumRowAboutPadding;

	const auto button = Ui::CreateChild<Ui::SettingsButton>(
		parent,
		rpl::single(QString()));

	const auto label = parent->add(
		object_ptr<Ui::FlatLabel>(
			parent,
			std::move(title) | Ui::Text::ToBold(),
			stLabel),
		titlePadding);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto description = parent->add(
		object_ptr<Ui::FlatLabel>(
			parent,
			std::move(subtitle),
			st::boxDividerLabel),
		descriptionPadding);
	description->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto dummy = Ui::CreateChild<Ui::AbstractButton>(parent);
	dummy->setAttribute(Qt::WA_TransparentForMouseEvents);

	parent->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		dummy->resize(s.width(), iconSize.height());
	}, dummy->lifetime());

	button->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		dummy->moveToLeft(0, r.y() + (r.height() - iconSize.height()) / 2);
	}, dummy->lifetime());

	::Settings::AddButtonIcon(dummy, st::settingsButton, {
		.icon = &st::settingsPremiumIconStar,
		.backgroundBrush = st::premiumIconBg3,
	});

	rpl::combine(
		parent->widthValue(),
		label->heightValue(),
		description->heightValue()
	) | rpl::start_with_next([=,
		topPadding = titlePadding,
		bottomPadding = descriptionPadding](
			int width,
			int topHeight,
			int bottomHeight) {
		button->resize(
			width,
			topPadding.top()
			+ topHeight
			+ topPadding.bottom()
			+ bottomPadding.top()
			+ bottomHeight
			+ bottomPadding.bottom());
	}, button->lifetime());
	label->topValue(
	) | rpl::start_with_next([=, padding = titlePadding.top()](int top) {
		button->moveToLeft(0, top - padding);
	}, button->lifetime());
	const auto arrow = Ui::CreateChild<Ui::IconButton>(
		button,
		st::backButton);
	arrow->setIconOverride(
		&st::settingsPremiumArrow,
		&st::settingsPremiumArrowOver);
	arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto &point = st::settingsPremiumArrowShift;
		arrow->moveToRight(
			-point.x(),
			point.y() + (s.height() - arrow->height()) / 2);
	}, arrow->lifetime());

	return button;
}

} // namespace Info::BotStarRef::Setup

