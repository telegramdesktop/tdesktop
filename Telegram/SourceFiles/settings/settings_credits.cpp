/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_credits.h"

#include "api/api_credits.h"
#include "core/click_handler_types.h"
#include "data/data_user.h"
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "info/statistics/info_statistics_list_controllers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "payments/payments_checkout_process.h"
#include "payments/payments_form.h"
#include "settings/settings_common_session.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/boxes/boost_box.h" // Ui::StartFireworks.
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_credits.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"

#include <xxhash.h> // XXH64.

#include <QtSvg/QSvgRenderer>

namespace Settings {
namespace {

using SectionCustomTopBarData = Info::Settings::SectionCustomTopBarData;

[[nodiscard]] uint64 UniqueIdFromOption(
		const Data::CreditTopupOption &d) {
	const auto string = QString::number(d.credits)
		+ d.product
		+ d.currency
		+ QString::number(d.amount);

	return XXH64(string.data(), string.size() * sizeof(ushort), 0);
}

[[nodiscard]] QImage GenerateStarForLightTopBar(QRectF rect) {
	const auto strokeWidth = 3;

	auto colorized = qs(Ui::Premium::ColorizedSvg(
		Ui::Premium::CreditsIconGradientStops()));
	colorized.replace(
		"stroke=\"none\"",
		"stroke=\"" + st::creditsStroke->c.name() + "\"");
	colorized.replace("stroke-width=\"1\"", "stroke-width=\"3\"");
	auto svg = QSvgRenderer(colorized.toUtf8());
	svg.setViewBox(svg.viewBox() + Margins(strokeWidth));

	const auto size = Size(st::settingsButton.height);
	auto frame = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	frame.setDevicePixelRatio(style::DevicePixelRatio());

	frame.fill(Qt::transparent);
	{
		auto q = QPainter(&frame);
		svg.render(&q, Rect(size));
	}
	return frame;
}

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

	QImage _star;

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
, _star(GenerateStarForLightTopBar({})) {
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
	const auto my = std::any_cast<SectionCustomTopBarData>(&data);
	if (my) {
		_backToggles = std::move(
			my->backButtonEnables
		) | rpl::map_to(true);
		_wrap = std::move(my->wrapValue);
	}
}

void Credits::setupOptions(not_null<Ui::VerticalLayout*> container) {
	const auto options = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = options->entity();

	Ui::AddSkip(content, st::settingsPremiumOptionsPadding.top());

	const auto fill = [=](Data::CreditTopupOptions options) {
		while (content->count()) {
			delete content->widgetAt(0);
		}
		Ui::AddSubsectionTitle(
			content,
			tr::lng_credits_summary_options_subtitle());
		for (const auto &option : options) {
			const auto button = content->add(object_ptr<Ui::SettingsButton>(
				content,
				tr::lng_credits_summary_options_credits(
					lt_count_decimal,
					rpl::single(option.credits) | tr::to_count()),
				st::creditsTopupButton));
			const auto icon = Ui::CreateChild<Ui::RpWidget>(button);
			icon->resize(Size(button->st().height));
			icon->paintRequest(
			) | rpl::start_with_next([=](const QRect &rect) {
				auto p = QPainter(icon);
				p.drawImage(0, 0, _star);
			}, icon->lifetime());
			const auto price = Ui::CreateChild<Ui::FlatLabel>(
				button,
				Ui::FillAmountAndCurrency(option.amount, option.currency),
				st::creditsTopupPrice);
			button->sizeValue(
			) | rpl::start_with_next([=](const QSize &size) {
				const auto &st = button->st();
				price->moveToRight(st.padding.right(), st.padding.top());
				icon->moveToLeft(st.iconLeft, st.padding.top());
			}, button->lifetime());
			button->setClickedCallback([=] {
				const auto invoice = Payments::InvoiceCredits{
					.session = &_controller->session(),
					.randomId = UniqueIdFromOption(option),
					.credits = option.credits,
					.product = option.product,
					.currency = option.currency,
					.amount = option.amount,
				};

				const auto weak = Ui::MakeWeak(button);
				const auto done = [=](Payments::CheckoutResult result) {
					if (const auto strong = weak.data()) {
						strong->window()->setFocus();
						if (result == Payments::CheckoutResult::Paid) {
							Ui::StartFireworks(this);
						}
					}
				};

				Payments::CheckoutProcess::Start(std::move(invoice), done);
			});
			Ui::ToggleChildrenVisibility(button, true);
		}

		// Footer.
		{
			auto text = tr::lng_credits_summary_options_about(
				lt_link,
				tr::lng_credits_summary_options_about_link(
				) | rpl::map([](const QString &t) {
					using namespace Ui::Text;
					return Link(t, u"https://telegram.org/tos"_q);
				}),
				Ui::Text::RichLangValue);
			Ui::AddSkip(content);
			Ui::AddDividerText(content, std::move(text));
		}

		content->resizeToWidth(container->width());
	};

	using ApiOptions = Api::CreditsTopupOptions;
	const auto apiCredits = content->lifetime().make_state<ApiOptions>(
		_controller->session().user());

	apiCredits->request(
	) | rpl::start_with_error_done([=](const QString &error) {
		_controller->showToast(error);
	}, [=] {
		fill(apiCredits->options());
	}, content->lifetime());
}

void Credits::setupHistory(not_null<Ui::VerticalLayout*> container) {
	const auto history = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = history->entity();

	Ui::AddSkip(content, st::settingsPremiumOptionsPadding.top());

	const auto fill = [=](
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

		const auto entryClicked = [=](const Data::CreditsHistoryEntry &e) {
		};

		Info::Statistics::AddCreditsHistoryList(
			fullSlice,
			fullWrap->entity(),
			entryClicked,
			_controller->session().user(),
			&_star,
			true,
			true);
		Info::Statistics::AddCreditsHistoryList(
			inSlice,
			inWrap->entity(),
			entryClicked,
			_controller->session().user(),
			&_star,
			true,
			false);
		Info::Statistics::AddCreditsHistoryList(
			outSlice,
			outWrap->entity(),
			std::move(entryClicked),
			_controller->session().user(),
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
					fill(fullSlice, inSlice, outSlice);
					apiLifetime->destroy();
				});
			});
		});
	}
}

void Credits::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	setupOptions(content);
	setupHistory(content);

	Ui::ResizeFitChild(this, content);
}

QPointer<Ui::RpWidget> Credits::createPinnedToTop(
		not_null<QWidget*> parent) {

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
