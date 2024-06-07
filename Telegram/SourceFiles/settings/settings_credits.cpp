/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_credits.h"

#include "settings/settings_credits_graphics.h"
#include "api/api_credits.h"
#include "boxes/gift_premium_box.h"
#include "core/click_handler_types.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "info/statistics/info_statistics_list_controllers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common_session.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/boxes/boost_box.h" // Ui::StartFireworks.
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_credits.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"

namespace Settings {
namespace {

class Credits : public Section<Credits> {
public:
	Credits(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;

	void showFinished() override;

	[[nodiscard]] bool hasFlexibleTopBar() const override;

	void setStepDataReference(std::any &data) override;

	[[nodiscard]] rpl::producer<> sectionShowBack() override final;

private:
	void setupContent();
	void setupOptions(not_null<Ui::VerticalLayout*> container);
	void setupHistory(not_null<Ui::VerticalLayout*> container);

	const not_null<Window::SessionController*> _controller;

	QWidget *_parent = nullptr;

	QImage _star;
	QImage _balanceStar;

	base::unique_qptr<Ui::FadeWrap<Ui::IconButton>> _back;
	base::unique_qptr<Ui::IconButton> _close;
	rpl::variable<bool> _backToggles;
	rpl::variable<Info::Wrap> _wrap;
	Fn<void(bool)> _setPaused;

	rpl::event_stream<> _showBack;
	rpl::event_stream<> _showFinished;
	rpl::variable<QString> _buttonText;

};

Credits::Credits(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller)
, _star(GenerateStars(st::creditsTopupButton.height, 1))
, _balanceStar(GenerateStars(st::creditsBalanceStarHeight, 1)) {
	setupContent();
}

rpl::producer<QString> Credits::title() {
	return tr::lng_premium_summary_title();
}

bool Credits::hasFlexibleTopBar() const {
	return true;
}

rpl::producer<> Credits::sectionShowBack() {
	return _showBack.events();
}

void Credits::setStepDataReference(std::any &data) {
	using SectionCustomTopBarData = Info::Settings::SectionCustomTopBarData;
	const auto my = std::any_cast<SectionCustomTopBarData>(&data);
	if (my) {
		_backToggles = std::move(
			my->backButtonEnables
		) | rpl::map_to(true);
		_wrap = std::move(my->wrapValue);
	}
}

void Credits::setupHistory(not_null<Ui::VerticalLayout*> container) {
	const auto history = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = history->entity();

	Ui::AddSkip(content, st::settingsPremiumOptionsPadding.top());

	const auto fill = [=](
			not_null<PeerData*> premiumBot,
			const Data::CreditsStatusSlice &fullSlice,
			const Data::CreditsStatusSlice &inSlice,
			const Data::CreditsStatusSlice &outSlice) {
		const auto inner = content;
		if (fullSlice.list.empty()) {
			return;
		}
		const auto hasOneTab = inSlice.list.empty() && outSlice.list.empty();
		const auto hasIn = !inSlice.list.empty();
		const auto hasOut = !outSlice.list.empty();
		const auto fullTabText = tr::lng_credits_summary_history_tab_full(
			tr::now);
		const auto inTabText = tr::lng_credits_summary_history_tab_in(
			tr::now);
		const auto outTabText = tr::lng_credits_summary_history_tab_out(
			tr::now);
		if (hasOneTab) {
			Ui::AddSkip(inner);
			const auto header = inner->add(
				object_ptr<Statistic::Header>(inner),
				st::statisticsLayerMargins
					+ st::boostsChartHeaderPadding);
			header->resizeToWidth(header->width());
			header->setTitle(fullTabText);
			header->setSubTitle({});
		}

		class Slider final : public Ui::SettingsSlider {
		public:
			using Ui::SettingsSlider::SettingsSlider;
			void setNaturalWidth(int w) {
				_naturalWidth = w;
			}
			int naturalWidth() const override {
				return _naturalWidth;
			}

		private:
			int _naturalWidth = 0;

		};

		const auto slider = inner->add(
			object_ptr<Ui::SlideWrap<Slider>>(
				inner,
				object_ptr<Slider>(inner, st::defaultTabsSlider)),
			st::boxRowPadding);
		slider->toggle(!hasOneTab, anim::type::instant);

		slider->entity()->addSection(fullTabText);
		if (hasIn) {
			slider->entity()->addSection(inTabText);
		}
		if (hasOut) {
			slider->entity()->addSection(outTabText);
		}

		{
			const auto &st = st::defaultTabsSlider;
			slider->entity()->setNaturalWidth(0
				+ st.labelStyle.font->width(fullTabText)
				+ (hasIn ? st.labelStyle.font->width(inTabText) : 0)
				+ (hasOut ? st.labelStyle.font->width(outTabText) : 0)
				+ rect::m::sum::h(st::boxRowPadding));
		}

		const auto fullWrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));
		const auto inWrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));
		const auto outWrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));

		rpl::single(0) | rpl::then(
			slider->entity()->sectionActivated()
		) | rpl::start_with_next([=](int index) {
			if (index == 0) {
				fullWrap->toggle(true, anim::type::instant);
				inWrap->toggle(false, anim::type::instant);
				outWrap->toggle(false, anim::type::instant);
			} else if (index == 1) {
				inWrap->toggle(true, anim::type::instant);
				fullWrap->toggle(false, anim::type::instant);
				outWrap->toggle(false, anim::type::instant);
			} else {
				outWrap->toggle(true, anim::type::instant);
				fullWrap->toggle(false, anim::type::instant);
				inWrap->toggle(false, anim::type::instant);
			}
		}, inner->lifetime());

		const auto controller = _controller->parentController();
		const auto entryClicked = [=](const Data::CreditsHistoryEntry &e) {
			controller->uiShow()->show(Box(
				ReceiptCreditsBox,
				controller,
				premiumBot.get(),
				e));
		};

		Info::Statistics::AddCreditsHistoryList(
			controller->uiShow(),
			fullSlice,
			fullWrap->entity(),
			entryClicked,
			premiumBot,
			&_star,
			true,
			true);
		Info::Statistics::AddCreditsHistoryList(
			controller->uiShow(),
			inSlice,
			inWrap->entity(),
			entryClicked,
			premiumBot,
			&_star,
			true,
			false);
		Info::Statistics::AddCreditsHistoryList(
			controller->uiShow(),
			outSlice,
			outWrap->entity(),
			std::move(entryClicked),
			premiumBot,
			&_star,
			false,
			true);

		Ui::AddSkip(inner);
		Ui::AddSkip(inner);

		inner->resizeToWidth(container->width());
	};

	const auto apiLifetime = content->lifetime().make_state<rpl::lifetime>();
	{
		using Api = Api::CreditsHistory;
		const auto self = _controller->session().user();
		const auto apiFull = apiLifetime->make_state<Api>(self, true, true);
		const auto apiIn = apiLifetime->make_state<Api>(self, true, false);
		const auto apiOut = apiLifetime->make_state<Api>(self, false, true);
		apiFull->request({}, [=](Data::CreditsStatusSlice fullSlice) {
			apiIn->request({}, [=](Data::CreditsStatusSlice inSlice) {
				apiOut->request({}, [=](Data::CreditsStatusSlice outSlice) {
					::Api::PremiumPeerBot(
						&_controller->session()
					) | rpl::start_with_next([=](not_null<PeerData*> bot) {
						fill(bot, fullSlice, inSlice, outSlice);
						apiLifetime->destroy();
					}, *apiLifetime);
				});
			});
		});
	}
}

void Credits::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto paid = [=] {
		if (_parent) {
			Ui::StartFireworks(_parent);
		}
	};
	FillCreditOptions(_controller, content, 0, paid);
	setupHistory(content);

	Ui::ResizeFitChild(this, content);
}

QPointer<Ui::RpWidget> Credits::createPinnedToTop(
		not_null<QWidget*> parent) {
	_parent = parent;

	const auto content = [&]() -> Ui::Premium::TopBarAbstract* {
		const auto weak = base::make_weak(_controller);
		const auto clickContextOther = [=] {
			return QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = weak,
				.botStartAutoSubmit = true,
			});
		};
		return Ui::CreateChild<Ui::Premium::TopBar>(
			parent.get(),
			st::creditsPremiumCover,
			Ui::Premium::TopBarDescriptor{
				.clickContextOther = clickContextOther,
				.title = tr::lng_credits_summary_title(),
				.about = tr::lng_credits_summary_about(
					TextWithEntities::Simple),
				.light = true,
				.gradientStops = Ui::Premium::CreditsIconGradientStops(),
			});
	}();
	_setPaused = [=](bool paused) {
		content->setPaused(paused);
	};

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		content->setRoundEdges(wrap == Info::Wrap::Layer);
	}, content->lifetime());

	content->setMaximumHeight(st::settingsPremiumTopHeight);
	content->setMinimumHeight(st::infoLayerTopBarHeight);

	content->resize(content->width(), content->maximumHeight());
	content->additionalHeight(
	) | rpl::start_with_next([=](int additionalHeight) {
		const auto wasMax = (content->height() == content->maximumHeight());
		content->setMaximumHeight(st::settingsPremiumTopHeight
			+ additionalHeight);
		if (wasMax) {
			content->resize(content->width(), content->maximumHeight());
		}
	}, content->lifetime());

	{
		const auto balance = AddBalanceWidget(
			content,
			_controller->session().creditsValue(),
			true);
		const auto api = balance->lifetime().make_state<Api::CreditsStatus>(
			_controller->session().user());
		api->request({}, [=](Data::CreditsStatusSlice slice) {
			_controller->session().setCredits(slice.balance);
		});
		rpl::combine(
			balance->sizeValue(),
			content->sizeValue()
		) | rpl::start_with_next([=](const QSize &, const QSize &) {
			balance->moveToRight(
				(_close
					? _close->width() + st::creditsHistoryRightSkip
					: st::creditsHistoryRightSkip * 2),
				st::creditsHistoryRightSkip);
			balance->update();
		}, balance->lifetime());
	}

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		const auto isLayer = (wrap == Info::Wrap::Layer);
		_back = base::make_unique_q<Ui::FadeWrap<Ui::IconButton>>(
			content,
			object_ptr<Ui::IconButton>(
				content,
				(isLayer ? st::infoTopBarBack : st::infoLayerTopBarBack)),
			st::infoTopBarScale);
		_back->setDuration(0);
		_back->toggleOn(isLayer
			? _backToggles.value() | rpl::type_erased()
			: rpl::single(true));
		_back->entity()->addClickHandler([=] {
			_showBack.fire({});
		});
		_back->toggledValue(
		) | rpl::start_with_next([=](bool toggled) {
			const auto &st = isLayer ? st::infoLayerTopBar : st::infoTopBar;
			content->setTextPosition(
				toggled ? st.back.width : st.titlePosition.x(),
				st.titlePosition.y());
		}, _back->lifetime());

		if (!isLayer) {
			_close = nullptr;
		} else {
			_close = base::make_unique_q<Ui::IconButton>(
				content,
				st::infoTopBarClose);
			_close->addClickHandler([=] {
				_controller->parentController()->hideLayer();
				_controller->parentController()->hideSpecialLayer();
			});
			content->widthValue(
			) | rpl::start_with_next([=] {
				_close->moveToRight(0, 0);
			}, _close->lifetime());
		}
	}, content->lifetime());

	return Ui::MakeWeak(not_null<Ui::RpWidget*>{ content });
}

void Credits::showFinished() {
	_showFinished.fire({});
}

} // namespace

template <>
struct SectionFactory<Credits> : AbstractSectionFactory {
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue
	) const final override {
		return object_ptr<Credits>(parent, controller);
	}
	bool hasCustomTopBar() const final override {
		return true;
	}

	[[nodiscard]] static const std::shared_ptr<SectionFactory> &Instance() {
		static const auto result = std::make_shared<SectionFactory>();
		return result;
	}
};

Type CreditsId() {
	return Credits::Id();
}

} // namespace Settings
