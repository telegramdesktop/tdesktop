/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
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
#include "payments/payments_checkout_process.h"
#include "payments/payments_form.h"
#include "settings/settings_common_session.h"
#include "settings/settings_credits_graphics.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/credits_graphics.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"

#include <xxhash.h> // XXH64.

#include <QtSvg/QSvgRenderer>

namespace Settings {
namespace {

[[nodiscard]] uint64 UniqueIdFromOption(
		const Data::CreditTopupOption &d) {
	const auto string = QString::number(d.credits)
		+ d.product
		+ d.currency
		+ QString::number(d.amount);

	return XXH64(string.data(), string.size() * sizeof(ushort), 0);
}

class Balance final
	: public Ui::RpWidget
	, public Ui::AbstractTooltipShower {
public:
	using Ui::RpWidget::RpWidget;

	void setBalance(uint64 balance) {
		_balance = balance;
		_tooltip = Lang::FormatCountDecimal(balance);
	}

	void enterEventHook(QEnterEvent *e) override {
		if (_balance >= 10'000) {
			Ui::Tooltip::Show(1000, this);
		}
	}

	void leaveEventHook(QEvent *e) override {
		Ui::Tooltip::Hide();
	}

	QString tooltipText() const override {
		return _tooltip;
	}

	QPoint tooltipPos() const override {
		return QCursor::pos();
	}

	bool tooltipWindowActive() const override {
		return Ui::AppInFocus() && Ui::InFocusChain(window());
	}

private:
	QString _tooltip;
	uint64 _balance = 0;

};

} // namespace

QImage GenerateStars(int height, int count) {
	constexpr auto kOutlineWidth = .6;
	constexpr auto kStrokeWidth = 3;
	constexpr auto kShift = 3;

	auto colorized = qs(Ui::Premium::ColorizedSvg(
		Ui::Premium::CreditsIconGradientStops()));
	colorized.replace(
		u"stroke=\"none\""_q,
		u"stroke=\"%1\""_q.arg(st::creditsStroke->c.name()));
	colorized.replace(
		u"stroke-width=\"1\""_q,
		u"stroke-width=\"%1\""_q.arg(kStrokeWidth));
	auto svg = QSvgRenderer(colorized.toUtf8());
	svg.setViewBox(svg.viewBox() + Margins(kStrokeWidth));

	const auto starSize = Size(height - kOutlineWidth * 2);

	auto frame = QImage(
		QSize(
			(height + kShift * (count - 1)) * style::DevicePixelRatio(),
			height * style::DevicePixelRatio()),
		QImage::Format_ARGB32_Premultiplied);
	frame.setDevicePixelRatio(style::DevicePixelRatio());
	frame.fill(Qt::transparent);
	const auto drawSingle = [&](QPainter &q) {
		const auto s = kOutlineWidth;
		q.save();
		q.translate(s, s);
		q.setCompositionMode(QPainter::CompositionMode_Clear);
		svg.render(&q, QRectF(QPointF(s, 0), starSize));
		svg.render(&q, QRectF(QPointF(s, s), starSize));
		svg.render(&q, QRectF(QPointF(0, s), starSize));
		svg.render(&q, QRectF(QPointF(-s, s), starSize));
		svg.render(&q, QRectF(QPointF(-s, 0), starSize));
		svg.render(&q, QRectF(QPointF(-s, -s), starSize));
		svg.render(&q, QRectF(QPointF(0, -s), starSize));
		svg.render(&q, QRectF(QPointF(s, -s), starSize));
		q.setCompositionMode(QPainter::CompositionMode_SourceOver);
		svg.render(&q, Rect(starSize));
		q.restore();
	};
	{
		auto q = QPainter(&frame);
		q.translate(frame.width() / style::DevicePixelRatio() - height, 0);
		for (auto i = count; i > 0; --i) {
			drawSingle(q);
			q.translate(-kShift, 0);
		}
	}
	return frame;
}

void FillCreditOptions(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		int minimumCredits,
		Fn<void()> paid) {
	const auto options = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = options->entity();

	Ui::AddSkip(content, st::settingsPremiumOptionsPadding.top());

	const auto singleStarWidth = GenerateStars(
		st::creditsTopupButton.height,
		1).width() / style::DevicePixelRatio();

	const auto fill = [=](Data::CreditTopupOptions options) {
		while (content->count()) {
			delete content->widgetAt(0);
		}
		Ui::AddSubsectionTitle(
			content,
			tr::lng_credits_summary_options_subtitle());
		const auto &st = st::creditsTopupButton;
		const auto diffBetweenTextAndStar = st.padding.left()
			- st.iconLeft
			- singleStarWidth;
		const auto buttonHeight = st.height + rect::m::sum::v(st.padding);
		const auto minCredits = (!options.empty()
				&& (minimumCredits > options.back().credits))
			? 0
			: minimumCredits;
		for (auto i = 0; i < options.size(); i++) {
			const auto &option = options[i];
			if (option.credits < minCredits) {
				continue;
			}
			const auto button = content->add(object_ptr<Ui::SettingsButton>(
				content,
				rpl::never<QString>(),
				st));
			const auto text = button->lifetime().make_state<Ui::Text::String>(
				st.style,
				tr::lng_credits_summary_options_credits(
					tr::now,
					lt_count_decimal,
					option.credits));
			const auto price = Ui::CreateChild<Ui::FlatLabel>(
				button,
				Ui::FillAmountAndCurrency(option.amount, option.currency),
				st::creditsTopupPrice);
			const auto inner = Ui::CreateChild<Ui::RpWidget>(button);
			const auto stars = GenerateStars(st.height, (i + 1));
			inner->paintRequest(
			) | rpl::start_with_next([=](const QRect &rect) {
				auto p = QPainter(inner);
				p.drawImage(
					0,
					(buttonHeight - stars.height()) / 2,
					stars);
				const auto textLeft = diffBetweenTextAndStar
					+ stars.width() / style::DevicePixelRatio();
				p.setPen(st.textFg);
				text->draw(p, {
					.position = QPoint(textLeft, 0),
					.availableWidth = inner->width() - textLeft,
				});
			}, inner->lifetime());
			button->sizeValue(
			) | rpl::start_with_next([=](const QSize &size) {
				price->moveToRight(st.padding.right(), st.padding.top());
				inner->moveToLeft(st.iconLeft, st.padding.top());
				inner->resize(
					size.width()
						- rect::m::sum::h(st.padding)
						- price->width(),
					buttonHeight);
			}, button->lifetime());
			button->setClickedCallback([=] {
				const auto invoice = Payments::InvoiceCredits{
					.session = &controller->session(),
					.randomId = UniqueIdFromOption(option),
					.credits = option.credits,
					.product = option.product,
					.currency = option.currency,
					.amount = option.amount,
					.extended = option.extended,
				};

				const auto weak = Ui::MakeWeak(button);
				const auto done = [=](Payments::CheckoutResult result) {
					if (const auto strong = weak.data()) {
						strong->window()->setFocus();
						if (result == Payments::CheckoutResult::Paid) {
							if (paid) {
								paid();
							}
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
				rpl::combine(
					tr::lng_credits_summary_options_about_link(),
					tr::lng_credits_summary_options_about_url()
				) | rpl::map([](const QString &text, const QString &url) {
					return Ui::Text::Link(text, url);
				}),
				Ui::Text::RichLangValue);
			Ui::AddSkip(content);
			Ui::AddDividerText(content, std::move(text));
		}

		content->resizeToWidth(container->width());
	};

	using ApiOptions = Api::CreditsTopupOptions;
	const auto apiCredits = content->lifetime().make_state<ApiOptions>(
		controller->session().user());

	if (controller->session().premiumPossible()) {
		apiCredits->request(
		) | rpl::start_with_error_done([=](const QString &error) {
			controller->showToast(error);
		}, [=] {
			fill(apiCredits->options());
		}, content->lifetime());
	}

	controller->session().premiumPossibleValue(
	) | rpl::start_with_next([=](bool premiumPossible) {
		if (!premiumPossible) {
			fill({});
		}
	}, content->lifetime());
}

not_null<Ui::RpWidget*> AddBalanceWidget(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<uint64> balanceValue,
		bool rightAlign) {
	const auto balance = Ui::CreateChild<Balance>(parent);
	const auto balanceStar = balance->lifetime().make_state<QImage>(
		GenerateStars(st::creditsBalanceStarHeight, 1));
	const auto starSize = balanceStar->size() / style::DevicePixelRatio();
	const auto label = balance->lifetime().make_state<Ui::Text::String>(
		st::defaultTextStyle,
		tr::lng_credits_summary_balance(tr::now));
	const auto count = balance->lifetime().make_state<Ui::Text::String>(
		st::semiboldTextStyle,
		tr::lng_contacts_loading(tr::now));
	const auto diffBetweenStarAndCount = count->style()->font->spacew;
	const auto resize = [=] {
		balance->resize(
			std::max(
				label->maxWidth(),
				count->maxWidth()
					+ starSize.width()
					+ diffBetweenStarAndCount),
			label->style()->font->height + starSize.height());
	};
	std::move(balanceValue) | rpl::start_with_next([=](uint64 value) {
		count->setText(
			st::semiboldTextStyle,
			Lang::FormatCountToShort(value).string);
		balance->setBalance(value);
		resize();
	}, balance->lifetime());
	balance->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(balance);

		p.setPen(st::boxTextFg);

		label->draw(p, {
			.position = QPoint(
				rightAlign ? (balance->width() - label->maxWidth()) : 0,
				0),
			.availableWidth = balance->width(),
		});
		count->draw(p, {
			.position = QPoint(
				balance->width() - count->maxWidth(),
				label->minHeight()
					+ (starSize.height() - count->minHeight()) / 2),
			.availableWidth = balance->width(),
		});
		p.drawImage(
			balance->width()
				- count->maxWidth()
				- starSize.width()
				- diffBetweenStarAndCount,
			label->minHeight(),
			*balanceStar);
	}, balance->lifetime());
	return balance;
}

void ReceiptCreditsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		PeerData *premiumBot,
		const Data::CreditsHistoryEntry &e) {
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto star = GenerateStars(st::creditsTopupButton.height, 1);

	const auto content = box->verticalLayout();
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	using Type = Data::CreditsHistoryEntry::PeerType;

	const auto &stUser = st::boostReplaceUserpic;
	const auto peer = (e.peerType == Type::PremiumBot)
		? premiumBot
		: e.bareId
		? controller->session().data().peer(PeerId(e.bareId)).get()
		: nullptr;
	const auto photo = e.photoId
		? controller->session().data().photo(e.photoId).get()
		: nullptr;
	if (photo) {
		content->add(object_ptr<Ui::CenterWrap<>>(
			content,
			HistoryEntryPhoto(content, photo, stUser.photoSize)));
	} else if (peer) {
		content->add(object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::UserpicButton>(content, peer, stUser)));
	} else {
		const auto widget = content->add(
			object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::RpWidget>(content)))->entity();
		using Draw = Fn<void(Painter &, int, int, int, int)>;
		const auto draw = widget->lifetime().make_state<Draw>(
			Ui::GenerateCreditsPaintUserpicCallback(e));
		widget->resize(Size(stUser.photoSize));
		widget->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = Painter(widget);
			(*draw)(p, 0, 0, stUser.photoSize, stUser.photoSize);
		}, widget->lifetime());
	}

	Ui::AddSkip(content);
	Ui::AddSkip(content);


	box->addRow(object_ptr<Ui::CenterWrap<>>(
		box,
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(
				!e.title.isEmpty()
				? e.title
				: peer
				? peer->name()
				: Ui::GenerateEntryName(e).text),
			st::creditsBoxAboutTitle)));

	Ui::AddSkip(content);

	{
		constexpr auto kMinus = QChar(0x2212);
		auto &lifetime = content->lifetime();
		const auto text = lifetime.make_state<Ui::Text::String>(
			st::semiboldTextStyle,
			((!e.bareId || e.refunded) ? QChar('+') : kMinus)
				+ Lang::FormatCountDecimal(std::abs(int64(e.credits))));
		const auto refundedText = tr::lng_channel_earn_history_return(
			tr::now);
		const auto refunded = e.refunded
			? lifetime.make_state<Ui::Text::String>(
				st::defaultTextStyle,
				refundedText)
			: (Ui::Text::String*)(nullptr);

		const auto amount = content->add(
			object_ptr<Ui::FixedHeightWidget>(
				content,
				star.height() / style::DevicePixelRatio()));
		const auto font = text->style()->font;
		const auto refundedFont = st::defaultTextStyle.font;
		const auto starWidth = star.width()
			/ style::DevicePixelRatio();
		const auto refundedSkip = refundedFont->spacew * 2;
		const auto refundedWidth = refunded
			? refundedFont->width(refundedText)
				+ refundedSkip
				+ refundedFont->height
			: 0;
		const auto fullWidth = text->maxWidth()
			+ font->spacew * 1
			+ starWidth
			+ refundedWidth;
		amount->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = Painter(amount);
			p.setPen((!e.bareId || e.refunded)
				? st::boxTextFgGood
				: st::menuIconAttentionColor);
			const auto x = (amount->width() - fullWidth) / 2;
			text->draw(p, Ui::Text::PaintContext{
				.position = QPoint(
					x,
					(amount->height() - font->height) / 2),
				.outerWidth = amount->width(),
				.availableWidth = amount->width(),
			});
			p.drawImage(
				x + fullWidth - starWidth - refundedWidth,
				0,
				star);

			if (refunded) {
				const auto refundedLeft = fullWidth
					+ x
					- refundedWidth
					+ refundedSkip;
				const auto pen = p.pen();
				auto color = pen.color();
				color.setAlphaF(color.alphaF() * 0.15);
				p.setPen(Qt::NoPen);
				p.setBrush(color);
				{
					auto hq = PainterHighQualityEnabler(p);
					p.drawRoundedRect(
						refundedLeft,
						(amount->height() - refundedFont->height) / 2,
						refundedWidth - refundedSkip,
						refundedFont->height,
						refundedFont->height / 2,
						refundedFont->height / 2);
				}
				p.setPen(pen);
				refunded->draw(p, Ui::Text::PaintContext{
					.position = QPoint(
						refundedLeft + refundedFont->height / 2,
						(amount->height() - refundedFont->height) / 2),
					.outerWidth = refundedWidth,
					.availableWidth = refundedWidth,
				});
			}
		}, amount->lifetime());
	}

	if (!e.description.isEmpty()) {
		Ui::AddSkip(content);
		box->addRow(object_ptr<Ui::CenterWrap<>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(e.description),
				st::defaultFlatLabel)));
	}

	Ui::AddSkip(content);
	Ui::AddSkip(content);

	AddCreditsHistoryEntryTable(
		controller,
		box->verticalLayout(),
		e);

	Ui::AddSkip(content);

	box->addRow(object_ptr<Ui::CenterWrap<>>(
		box,
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_credits_box_out_about(
				lt_link,
				tr::lng_payments_terms_link(
				) | rpl::map([](const QString &t) {
					using namespace Ui::Text;
					return Link(t, u"https://telegram.org/tos"_q);
				}),
				Ui::Text::WithEntities),
			st::creditsBoxAboutDivider)));

	Ui::AddSkip(content);

	const auto button = box->addButton(tr::lng_box_ok(), [=] {
		box->closeBox();
	});
	const auto buttonWidth = st::boxWidth
		- rect::m::sum::h(st::giveawayGiftCodeBox.buttonPadding);
	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != buttonWidth);
	}) | rpl::start_with_next([=] {
		button->resizeToWidth(buttonWidth);
	}, button->lifetime());
}

object_ptr<Ui::RpWidget> HistoryEntryPhoto(
		not_null<Ui::RpWidget*> parent,
		not_null<PhotoData*> photo,
		int photoSize) {
	auto owned = object_ptr<Ui::RpWidget>(parent);
	const auto widget = owned.data();
	widget->resize(Size(photoSize));

	const auto draw = Ui::GenerateCreditsPaintEntryCallback(
		photo,
		[=] { widget->update(); });

	widget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = Painter(widget);
		draw(p, 0, 0, photoSize, photoSize);
	}, widget->lifetime());

	return owned;
}

void SmallBalanceBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		int creditsNeeded,
		UserId botId,
		Fn<void()> paid) {
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	const auto done = [=] {
		box->closeBox();
		paid();
	};

	const auto bot = controller->session().data().user(botId).get();

	const auto content = [&]() -> Ui::Premium::TopBarAbstract* {
		const auto weak = base::make_weak(controller);
		const auto clickContextOther = [=] {
			return QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = weak,
				.botStartAutoSubmit = true,
			});
		};
		return box->setPinnedToTopContent(object_ptr<Ui::Premium::TopBar>(
			box,
			st::creditsLowBalancePremiumCover,
			Ui::Premium::TopBarDescriptor{
				.clickContextOther = clickContextOther,
				.title = tr::lng_credits_small_balance_title(
					lt_count,
					rpl::single(creditsNeeded) | tr::to_count()),
				.about = tr::lng_credits_small_balance_about(
					lt_bot,
					rpl::single(TextWithEntities{ bot->name() }),
					Ui::Text::RichLangValue),
				.light = true,
				.gradientStops = Ui::Premium::CreditsIconGradientStops(),
			}));
	}();

	FillCreditOptions(controller, box->verticalLayout(), creditsNeeded, done);

	content->setMaximumHeight(st::creditsLowBalancePremiumCoverHeight);
	content->setMinimumHeight(st::infoLayerTopBarHeight);

	content->resize(content->width(), content->maximumHeight());
	content->additionalHeight(
	) | rpl::start_with_next([=](int additionalHeight) {
		const auto wasMax = (content->height() == content->maximumHeight());
		content->setMaximumHeight(st::creditsLowBalancePremiumCoverHeight
			+ additionalHeight);
		if (wasMax) {
			content->resize(content->width(), content->maximumHeight());
		}
	}, content->lifetime());

	{
		const auto balance = AddBalanceWidget(
			content,
			controller->session().creditsValue(),
			true);
		const auto api = balance->lifetime().make_state<Api::CreditsStatus>(
			controller->session().user());
		api->request({}, [=](Data::CreditsStatusSlice slice) {
			controller->session().setCredits(slice.balance);
		});
		rpl::combine(
			balance->sizeValue(),
			content->sizeValue()
		) | rpl::start_with_next([=](const QSize &, const QSize &) {
			balance->moveToRight(
				st::creditsHistoryRightSkip * 2,
				st::creditsHistoryRightSkip);
			balance->update();
		}, balance->lifetime());
	}
}

} // namespace Settings
