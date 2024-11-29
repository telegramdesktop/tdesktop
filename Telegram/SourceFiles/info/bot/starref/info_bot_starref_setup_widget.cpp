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
#include "info/bot/starref/info_bot_starref_common.h"
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
#include "ui/painter.h"
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
constexpr auto kDisabledFade = 0.3;

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

[[nodiscard]] object_ptr<Ui::RpWidget> MakeSliderWithTopTag(
		QWidget *parent,
		not_null<const style::MediaSlider*> sliderStyle,
		not_null<const style::FlatLabel*> labelStyle,
		int valuesCount,
		Fn<int(int)> valueByIndex,
		int value,
		Fn<void(int)> valueProgress,
		Fn<void(int)> valueFinished,
		Fn<QString(int)> textByValue,
		bool forbidLessThanValue) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto raw = result.data();

	const auto labels = raw->add(object_ptr<Ui::RpWidget>(raw));
	const auto min = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		textByValue(valueByIndex(0)),
		*labelStyle);
	const auto max = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		textByValue(valueByIndex(valuesCount - 1)),
		*labelStyle);
	const auto current = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		textByValue(value),
		*labelStyle);
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
	const auto updatePalette = [=] {
		const auto disabled = anim::color(
			st::windowSubTextFg,
			st::windowBg,
			kDisabledFade);
		min->setTextColorOverride(!state->index
			? st::windowActiveTextFg->c
			: (state->indexMin > 0)
			? disabled
			: st::windowSubTextFg->c);
		max->setTextColorOverride((state->index == valuesCount - 1)
			? st::windowActiveTextFg->c
			: st::windowSubTextFg->c);
		current->setTextColorOverride(st::windowActiveTextFg->c);
	};
	const auto updateByIndex = [=] {
		updatePalette();

		current->setVisible(state->index > 0
			&& state->index < valuesCount - 1);
		const auto outer = labels->width();
		const auto minWidth = min->width();
		const auto maxWidth = max->width();
		const auto currentWidth = current->width();
		if (minWidth + maxWidth + currentWidth > outer) {
			return;
		}

		min->moveToLeft(0, 0, outer);
		max->moveToRight(0, 0, outer);

		const auto sliderSkip = sliderStyle->seekSize.width();
		const auto availableForCurrent = outer - sliderSkip;
		const auto ratio = state->index / float64(valuesCount - 1);
		const auto desiredLeft = (sliderSkip / 2)
			+ availableForCurrent * ratio
			- (currentWidth / 2);
		const auto minLeft = minWidth;
		const auto maxLeft = outer - maxWidth - currentWidth;
		current->moveToLeft(
			std::clamp(int(base::SafeRound(desiredLeft)), minLeft, maxLeft),
			0,
			outer);
	};
	const auto updateByValue = [=](int value) {
		current->setText(textByValue(value));

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
		updatePalette();
	}, raw->lifetime());
	updateByValue(value);
	state->indexMin = forbidLessThanValue ? state->index : 0;

	slider->setPseudoDiscrete(
		valuesCount,
		valueByIndex,
		value,
		progress,
		finished,
		state->indexMin);
	slider->resize(slider->width(), sliderStyle->seekSize.height());

	if (state->indexMin > 0) {
		const auto overlay = Ui::CreateChild<Ui::RpWidget>(slider);
		overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
		slider->sizeValue() | rpl::start_with_next([=](QSize size) {
			overlay->setGeometry(0, 0, size.width(), size.height());
		}, slider->lifetime());
		overlay->paintRequest() | rpl::start_with_next([=] {
			auto p = QPainter(overlay);
			const auto sections = valuesCount - 1;
			const auto shift = sliderStyle->seekSize.width();
			const auto skip = shift / 2.;
			const auto available = overlay->width() - shift;
			const auto till = state->indexMin / float64(sections);
			const auto now = state->index / float64(sections);
			const auto edge = available * now;
			const auto right = int(base::SafeRound(
				std::min(skip + available * till, edge)));
			if (right > 0) {
				p.setOpacity(kDisabledFade);
				p.fillRect(0, 0, right, overlay->height(), st::windowBg);
			}
		}, overlay->lifetime());
	}

	raw->widthValue() | rpl::start_with_next([=](int width) {
		labels->resizeToWidth(width);
		updateByIndex();
	}, slider->lifetime());

	return result;
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeSliderWithTopLabels(
		QWidget *parent,
		not_null<const style::MediaSlider*> sliderStyle,
		not_null<const style::FlatLabel*> labelStyle,
		int valuesCount,
		Fn<int(int)> valueByIndex,
		int value,
		Fn<void(int)> valueProgress,
		Fn<void(int)> valueFinished,
		Fn<QString(int)> textByValue,
		bool forbidLessThanValue) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto raw = result.data();

	const auto labels = raw->add(object_ptr<Ui::RpWidget>(raw));
	const auto slider = raw->add(object_ptr<Ui::MediaSliderWheelless>(
		raw,
		*sliderStyle));

	struct State {
		std::vector<not_null<Ui::FlatLabel*>> labels;
		int indexMin = 0;
		int index = 0;
	};
	const auto state = raw->lifetime().make_state<State>();

	for (auto i = 0; i != valuesCount; ++i) {
		state->labels.push_back(Ui::CreateChild<Ui::FlatLabel>(
			labels,
			textByValue(valueByIndex(i)),
			*labelStyle));
	}
	labels->widthValue() | rpl::start_with_next([=](int outer) {
		const auto shift = sliderStyle->seekSize.width() / 2;
		const auto available = outer - sliderStyle->seekSize.width();
		for (auto i = 0; i != state->labels.size(); ++i) {
			const auto label = state->labels[i];
			const auto width = label->width();
			const auto half = width / 2;
			const auto progress = (i / float64(valuesCount - 1));
			const auto left = int(base::SafeRound(progress * available));
			label->moveToLeft(
				std::max(std::min(shift + left - half, outer - width), 0),
				0,
				outer);
		}
	}, slider->lifetime());
	labels->resize(
		labels->width(),
		state->labels.back()->height() + st::defaultVerticalListSkip);

	const auto updatePalette = [=] {
		const auto disabled = anim::color(
			st::windowSubTextFg,
			st::windowBg,
			kDisabledFade);
		for (auto i = 0; i != state->labels.size(); ++i) {
			state->labels[i]->setTextColorOverride((state->index == i)
				? st::windowActiveTextFg->c
				: (state->index < state->indexMin)
				? disabled
				: st::windowSubTextFg->c);
		}
	};
	const auto updateByIndex = [=] {
		updatePalette();
	};
	const auto updateByValue = [=](int value) {
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
		updatePalette();
	}, raw->lifetime());
	updateByValue(value);
	state->indexMin = forbidLessThanValue ? state->index : 0;

	slider->setPseudoDiscrete(
		valuesCount,
		valueByIndex,
		value,
		progress,
		finished,
		state->indexMin);
	slider->resize(slider->width(), sliderStyle->seekSize.height());

	if (state->indexMin > 0) {
		const auto overlay = Ui::CreateChild<Ui::RpWidget>(slider);
		overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
		slider->sizeValue() | rpl::start_with_next([=](QSize size) {
			overlay->setGeometry(0, 0, size.width(), size.height());
		}, slider->lifetime());
		overlay->paintRequest() | rpl::start_with_next([=] {
			auto p = QPainter(overlay);

			const auto sections = valuesCount - 1;
			const auto shift = sliderStyle->seekSize.width();
			const auto skip = shift / 2.;
			const auto available = overlay->width() - shift;
			auto hq = PainterHighQualityEnabler(p);
			const auto stroke = style::ConvertScale(3);
			p.setPen(QPen(st::windowBg, stroke));
			const auto diameter = shift - stroke;
			const auto radius = diameter / 2.;
			const auto top = (sliderStyle->seekSize.height() / 2.) - radius;
			for (auto i = 0; i != valuesCount; ++i) {
				if (i < state->index) {
					p.setBrush(st::sliderBgActive);
				} else if (i > state->index) {
					p.setBrush(st::sliderBgInactive);
				} else {
					continue;
				}
				const auto progress = i / float64(sections);
				const auto position = skip + available * progress;
				p.drawEllipse(position - radius, top, diameter, diameter);
			}

			const auto till = state->indexMin / float64(sections);
			const auto now = state->index / float64(sections);
			const auto edge = available * now;
			const auto right = int(base::SafeRound(
				std::min(skip + available * till + radius, edge)));
			if (right > 0) {
				p.setOpacity(kDisabledFade);
				p.fillRect(0, 0, right, overlay->height(), st::windowBg);
			}
		}, overlay->lifetime());
	}

	raw->widthValue() | rpl::start_with_next([=](int width) {
		labels->resizeToWidth(width);
		updateByIndex();
	}, slider->lifetime());

	return result;
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
		&st::menuIconStarRefShare));

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
	const auto setCommission = [=](int value) {
		_state.program.commission = value;
	};
	_container->add(
		MakeSliderWithTopTag(
			_container,
			&st::settingsScale,
			&st::settingsScaleLabel,
			valuesCount,
			[=](int index) { return values[index]; },
			commission,
			setCommission,
			setCommission,
			[=](int value) { return FormatStarRefCommission(value); },
			_state.exists),
		st::boxRowPadding);
	_state.program.commission = commission;

	Ui::AddSkip(_container, st::defaultVerticalListSkip * 2);
	Ui::AddDividerText(_container, tr::lng_star_ref_commission_about());
}

void InnerWidget::setupDuration() {
	Ui::AddSkip(_container);
	Ui::AddSubsectionTitle(_container, tr::lng_star_ref_duration_title());

	const auto durationMonths = ValueForDurationMonths(_state);

	auto values = std::vector<int>{ 1, 3, 6, 12, 24, 36, 999 };
	if (!ranges::contains(values, durationMonths)) {
		values.push_back(durationMonths);
		ranges::sort(values);
	}
	const auto valuesCount = int(values.size());
	const auto setDurationMonths = [=](int value) {
		_state.program.durationMonths = (value == kDurationForeverValue)
			? 0
			: value;
	};
	const auto label = [=](int value) {
		return (value < 12)
			? (QString::number(value) + 'm')
			: (value < 999)
			? (QString::number(value / 12) + 'y')
			: QString::fromUtf8("\xE2\x88\x9E"); // utf-8 infinity
	};
	_container->add(
		MakeSliderWithTopLabels(
			_container,
			&st::settingsScale,
			&st::settingsScaleLabel,
			valuesCount,
			[=](int index) { return values[index]; },
			durationMonths,
			setDurationMonths,
			setDurationMonths,
			label,
			_state.exists),
		st::boxRowPadding);
	_state.program.durationMonths = durationMonths;

	Ui::AddSkip(_container, st::defaultVerticalListSkip * 2);
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
					tr::lng_star_ref_button_link(
					) | Ui::Text::ToLink(tr::lng_star_ref_tos_url(tr::now)),
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

} // namespace Info::BotStarRef::Setup

