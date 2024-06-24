/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_credits_graphics.h"

#include "api/api_credits.h"
#include "api/api_earn.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/gift_premium_box.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "core/click_handler_types.h" // UrlClickHandler
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "info/statistics/info_statistics_list_controllers.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "payments/payments_checkout_process.h"
#include "payments/payments_form.h"
#include "settings/settings_common_session.h"
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
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/label_with_custom_emoji.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
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

[[nodiscard]] int WithdrawalMin(not_null<Main::Session*> session) {
	const auto key = u"stars_revenue_withdrawal_min"_q;
	return session->appConfig().get<int>(key, 1000);
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

void AddViewMediaHandler(
		not_null<Ui::RpWidget*> thumb,
		not_null<Window::SessionController*> controller,
		const Data::CreditsHistoryEntry &e) {
	if (e.extended.empty()) {
		return;
	}
	thumb->setCursor(style::cur_pointer);

	struct State {
		~State() {
			if (item) {
				item->destroy();
			}
		}

		HistoryItem *item = nullptr;
		bool pressed = false;
		bool over = false;
	};
	const auto state = thumb->lifetime().make_state<State>();
	const auto session = &controller->session();
	const auto owner = &session->data();
	const auto peerId = e.barePeerId
		? PeerId(e.barePeerId)
		: session->userPeerId();
	const auto history = owner->history(session->user());
	state->item = history->makeMessage({
		.id = history->nextNonHistoryEntryId(),
		.flags = MessageFlag::HasFromId | MessageFlag::AdminLogEntry,
		.from = peerId,
		.date = base::unixtime::serialize(e.date),
	}, TextWithEntities(), MTP_messageMediaEmpty());
	auto fake = std::vector<std::unique_ptr<Data::Media>>();
	fake.reserve(e.extended.size());
	for (const auto &item : e.extended) {
		if (item.type == Data::CreditsHistoryMediaType::Photo) {
			fake.push_back(std::make_unique<Data::MediaPhoto>(
				state->item,
				owner->photo(item.id),
				false)); // spoiler
		} else {
			fake.push_back(std::make_unique<Data::MediaFile>(
				state->item,
				owner->document(item.id),
				true, // skipPremiumEffect
				false, // spoiler
				0)); // ttlSeconds
		}
	}
	state->item->overrideMedia(std::make_unique<Data::MediaInvoice>(
		state->item,
		Data::Invoice{
			.amount = uint64(std::abs(int64(e.credits))),
			.currency = Ui::kCreditsCurrency,
			.extendedMedia = std::move(fake),
			.isPaidMedia = true,
		}));
	const auto showMedia = crl::guard(controller, [=] {
		if (const auto media = state->item->media()) {
			if (const auto invoice = media->invoice()) {
				if (!invoice->extendedMedia.empty()) {
					const auto first = invoice->extendedMedia[0].get();
					if (const auto photo = first->photo()) {
						controller->openPhoto(photo, {
							.id = state->item->fullId(),
						});
					} else if (const auto document = first->document()) {
						controller->openDocument(document, true, {
							.id = state->item->fullId(),
						});
					}
				}
			}
		}
	});
	thumb->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::LeftButton) {
				state->over = true;
				state->pressed = true;
			}
		} else if (e->type() == QEvent::MouseButtonRelease
			&& state->over
			&& state->pressed) {
			showMedia();
		} else if (e->type() == QEvent::Enter) {
			state->over = true;
		} else if (e->type() == QEvent::Leave) {
			state->over = false;
		}
	}, thumb->lifetime());
}

} // namespace

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

	const auto singleStarWidth = Ui::GenerateStars(
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
			const auto stars = Ui::GenerateStars(st.height, (i + 1));
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
		Ui::GenerateStars(st::creditsBalanceStarHeight, 1));
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

	const auto star = Ui::GenerateStars(st::creditsTopupButton.height, 1);

	const auto content = box->verticalLayout();
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	using Type = Data::CreditsHistoryEntry::PeerType;

	const auto &stUser = st::boostReplaceUserpic;
	const auto session = &controller->session();
	const auto peer = (e.peerType == Type::PremiumBot)
		? premiumBot
		: e.barePeerId
		? session->data().peer(PeerId(e.barePeerId)).get()
		: nullptr;
	if (const auto callback = Ui::PaintPreviewCallback(session, e)) {
		const auto thumb = content->add(object_ptr<Ui::CenterWrap<>>(
			content,
			GenericEntryPhoto(content, callback, stUser.photoSize)));
		AddViewMediaHandler(thumb->entity(), controller, e);
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
			(e.in ? QChar('+') : kMinus)
				+ Lang::FormatCountDecimal(std::abs(int64(e.credits))));
		const auto roundedText = e.refunded
			? tr::lng_channel_earn_history_return(tr::now)
			: e.pending
			? tr::lng_channel_earn_history_pending(tr::now)
			: e.failed
			? tr::lng_channel_earn_history_failed(tr::now)
			: QString();
		const auto rounded = !roundedText.isEmpty()
			? lifetime.make_state<Ui::Text::String>(
				st::defaultTextStyle,
				roundedText)
			: (Ui::Text::String*)(nullptr);

		const auto amount = content->add(
			object_ptr<Ui::FixedHeightWidget>(
				content,
				star.height() / style::DevicePixelRatio()));
		const auto font = text->style()->font;
		const auto roundedFont = st::defaultTextStyle.font;
		const auto starWidth = star.width()
			/ style::DevicePixelRatio();
		const auto roundedSkip = roundedFont->spacew * 2;
		const auto roundedWidth = rounded
			? roundedFont->width(roundedText)
				+ roundedSkip
				+ roundedFont->height
			: 0;
		const auto fullWidth = text->maxWidth()
			+ font->spacew * 1
			+ starWidth
			+ roundedWidth;
		amount->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = Painter(amount);
			p.setPen(e.pending
				? st::creditsStroke
				: e.in
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
				x + fullWidth - starWidth - roundedWidth,
				0,
				star);

			if (rounded) {
				const auto roundedLeft = fullWidth
					+ x
					- roundedWidth
					+ roundedSkip;
				const auto pen = p.pen();
				auto color = pen.color();
				color.setAlphaF(color.alphaF() * 0.15);
				p.setPen(Qt::NoPen);
				p.setBrush(color);
				{
					auto hq = PainterHighQualityEnabler(p);
					p.drawRoundedRect(
						roundedLeft,
						(amount->height() - roundedFont->height) / 2,
						roundedWidth - roundedSkip,
						roundedFont->height,
						roundedFont->height / 2,
						roundedFont->height / 2);
				}
				p.setPen(pen);
				rounded->draw(p, Ui::Text::PaintContext{
					.position = QPoint(
						roundedLeft + roundedFont->height / 2,
						(amount->height() - roundedFont->height) / 2),
					.outerWidth = roundedWidth,
					.availableWidth = roundedWidth,
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

object_ptr<Ui::RpWidget> GenericEntryPhoto(
		not_null<Ui::RpWidget*> parent,
		Fn<Fn<void(Painter &, int, int, int, int)>(Fn<void()>)> callback,
		int photoSize) {
	auto owned = object_ptr<Ui::RpWidget>(parent);
	const auto widget = owned.data();
	widget->resize(Size(photoSize));

	const auto draw = callback(
		crl::guard(widget, [=] { widget->update(); }));
	widget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = Painter(widget);
		draw(p, 0, 0, photoSize, photoSize);
	}, widget->lifetime());

	return owned;
}

object_ptr<Ui::RpWidget> HistoryEntryPhoto(
		not_null<Ui::RpWidget*> parent,
		not_null<PhotoData*> photo,
		int photoSize) {
	return GenericEntryPhoto(
		parent,
		[=](Fn<void()> update) {
			return Ui::GenerateCreditsPaintEntryCallback(photo, update);
		},
		photoSize);
}

object_ptr<Ui::RpWidget> PaidMediaThumbnail(
		not_null<Ui::RpWidget*> parent,
		not_null<PhotoData*> photo,
		PhotoData *second,
		int totalCount,
		int photoSize) {
	return GenericEntryPhoto(
		parent,
		[=](Fn<void()> update) {
			return Ui::GeneratePaidMediaPaintCallback(
				photo,
				second,
				totalCount,
				update);
		},
		photoSize);
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

void AddWithdrawalWidget(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		rpl::producer<QString> secondButtonUrl,
		rpl::producer<uint64> availableBalanceValue,
		rpl::producer<QDateTime> dateValue,
		rpl::producer<bool> lockedValue,
		rpl::producer<QString> usdValue) {
	Ui::AddSkip(container);

	const auto labels = container->add(
		object_ptr<Ui::CenterWrap<Ui::RpWidget>>(
			container,
			object_ptr<Ui::RpWidget>(container)))->entity();

	const auto majorLabel = Ui::CreateChild<Ui::FlatLabel>(
		labels,
		rpl::duplicate(availableBalanceValue) | rpl::map([](uint64 v) {
			return Lang::FormatCountDecimal(v);
		}),
		st::channelEarnBalanceMajorLabel);
	const auto icon = Ui::CreateSingleStarWidget(
		labels,
		majorLabel->height());
	majorLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	majorLabel->sizeValue(
	) | rpl::start_with_next([=](const QSize &majorSize) {
		const auto skip = st::channelEarnBalanceMinorLabelSkip;
		labels->resize(
			majorSize.width() + icon->width() + skip,
			majorSize.height());
		majorLabel->moveToLeft(icon->width() + skip, 0);
	}, labels->lifetime());
	Ui::ToggleChildrenVisibility(labels, true);

	Ui::AddSkip(container);
	container->add(
		object_ptr<Ui::CenterWrap<>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(usdValue),
				st::channelEarnOverviewSubMinorLabel)));

	Ui::AddSkip(container);

	const auto input = Ui::AddInputFieldForCredits(
		container,
		rpl::duplicate(availableBalanceValue));

	Ui::AddSkip(container);
	Ui::AddSkip(container);

	const auto &stButton = st::defaultActiveButton;
	const auto buttonsContainer = container->add(
		Ui::CreateSkipWidget(container, stButton.height),
		st::boxRowPadding);

	const auto button = Ui::CreateChild<Ui::RoundButton>(
		buttonsContainer,
		rpl::never<QString>(),
		stButton);

	const auto buttonCredits = Ui::CreateChild<Ui::RoundButton>(
		buttonsContainer,
		tr::lng_bot_earn_balance_button_buy_ads(),
		stButton);
	buttonCredits->setTextTransform(
		Ui::RoundButton::TextTransform::NoTransform);

	Ui::ToggleChildrenVisibility(buttonsContainer, true);

	rpl::combine(
		std::move(secondButtonUrl),
		buttonsContainer->sizeValue()
	) | rpl::start_with_next([=](const QString &url, const QSize &size) {
		if (url.isEmpty()) {
			button->resize(size.width(), size.height());
			buttonCredits->resize(0, 0);
		} else {
			const auto w = size.width() - st::boxRowPadding.left() / 2;
			button->resize(w / 2, size.height());
			buttonCredits->resize(w / 2, size.height());
			buttonCredits->moveToRight(0, 0);
			buttonCredits->setClickedCallback([=] {
				UrlClickHandler::Open(url);
			});
		}
	}, buttonsContainer->lifetime());

	rpl::duplicate(
		lockedValue
	) | rpl::start_with_next([=](bool v) {
		button->setAttribute(Qt::WA_TransparentForMouseEvents, v);
	}, button->lifetime());

	const auto session = &controller->session();

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		tr::lng_channel_earn_balance_button(tr::now),
		st::channelEarnSemiboldLabel);
	const auto processInputChange = [&] {
		const auto buttonEmoji = Ui::Text::SingleCustomEmoji(
			session->data().customEmojiManager().registerInternalEmoji(
				st::settingsPremiumIconStar,
				{ 0, -st::moderateBoxExpandInnerSkip, 0, 0 },
				true));
		const auto context = Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [=] { label->update(); },
		};
		using Balance = rpl::variable<uint64>;
		const auto currentBalance = input->lifetime().make_state<Balance>(
			rpl::duplicate(availableBalanceValue));
		const auto process = [=] {
			const auto amount = input->getLastText().toDouble();
			if (amount >= currentBalance->current()) {
				label->setText(
					tr::lng_bot_earn_balance_button_all(tr::now));
			} else {
				label->setMarkedText(
					tr::lng_bot_earn_balance_button(
						tr::now,
						lt_count,
						amount,
						lt_emoji,
						buttonEmoji,
						Ui::Text::RichLangValue),
					context);
			}
		};
		QObject::connect(input, &Ui::MaskedInputField::changed, process);
		process();
		return process;
	}();
	label->setTextColorOverride(stButton.textFg->c);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		rpl::duplicate(lockedValue),
		button->sizeValue(),
		label->sizeValue()
	) | rpl::start_with_next([=](bool v, const QSize &b, const QSize &l) {
		label->moveToLeft(
			(b.width() - l.width()) / 2,
			(v ? -10 : 1) * (b.height() - l.height()) / 2);
	}, label->lifetime());

	const auto lockedColor = anim::with_alpha(stButton.textFg->c, .5);
	const auto lockedLabelTop = Ui::CreateChild<Ui::FlatLabel>(
		button,
		tr::lng_bot_earn_balance_button_locked(),
		st::botEarnLockedButtonLabel);
	lockedLabelTop->setTextColorOverride(lockedColor);
	lockedLabelTop->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto lockedLabelBottom = Ui::CreateChild<Ui::FlatLabel>(
		button,
		QString(),
		st::botEarnLockedButtonLabel);
	lockedLabelBottom->setTextColorOverride(lockedColor);
	lockedLabelBottom->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		rpl::duplicate(lockedValue),
		button->sizeValue(),
		lockedLabelTop->sizeValue(),
		lockedLabelBottom->sizeValue()
	) | rpl::start_with_next([=](
			bool locked,
			const QSize &b,
			const QSize &top,
			const QSize &bottom) {
		const auto factor = locked ? 1 : -10;
		const auto sumHeight = top.height() + bottom.height();
		lockedLabelTop->moveToLeft(
			(b.width() - top.width()) / 2,
			factor * (b.height() - sumHeight) / 2);
		lockedLabelBottom->moveToLeft(
			(b.width() - bottom.width()) / 2,
			factor * ((b.height() - sumHeight) / 2 + top.height()));
	}, lockedLabelTop->lifetime());

	const auto dateUpdateLifetime
		= lockedLabelBottom->lifetime().make_state<rpl::lifetime>();
	std::move(
		dateValue
	) | rpl::start_with_next([=](const QDateTime &dt) {
		dateUpdateLifetime->destroy();
		if (dt.isNull()) {
			return;
		}
		constexpr auto kDateUpdateInterval = crl::time(250);
		const auto was = base::unixtime::serialize(dt);

		const auto context = Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [=] { lockedLabelBottom->update(); },
		};
		const auto emoji = Ui::Text::SingleCustomEmoji(
			session->data().customEmojiManager().registerInternalEmoji(
				st::chatSimilarLockedIcon,
				st::botEarnButtonLockMargins,
				true));

		rpl::single(
			rpl::empty
		) | rpl::then(
			base::timer_each(kDateUpdateInterval)
		) | rpl::start_with_next([=] {
			const auto secondsDifference = std::max(
				was - base::unixtime::now() - 1,
				0);
			const auto hours = secondsDifference / 3600;
			const auto minutes = (secondsDifference % 3600) / 60;
			const auto seconds = secondsDifference % 60;
			constexpr auto kZero = QChar('0');
			const auto formatted = (hours > 0)
				? (u"%1:%2:%3"_q)
					.arg(hours, 2, 10, kZero)
					.arg(minutes, 2, 10, kZero)
					.arg(seconds, 2, 10, kZero)
				: (u"%1:%2"_q)
					.arg(minutes, 2, 10, kZero)
					.arg(seconds, 2, 10, kZero);
			lockedLabelBottom->setMarkedText(
				base::duplicate(emoji).append(formatted),
				context);
		}, *dateUpdateLifetime);
	}, lockedLabelBottom->lifetime());

	Api::HandleWithdrawalButton(
		Api::RewardReceiver{
			.creditsReceiver = peer,
			.creditsAmount = [=, show = controller->uiShow()] {
				const auto amount = input->getLastText().toULongLong();
				const auto min = float64(WithdrawalMin(session));
				if (amount < min) {
					auto text = tr::lng_bot_earn_credits_out_minimal(
						tr::now,
						lt_link,
						Ui::Text::Link(
							tr::lng_bot_earn_credits_out_minimal_link(
								tr::now,
								lt_count,
								min),
							u"internal:"_q),
						Ui::Text::RichLangValue);
					show->showToast(Ui::Toast::Config{
						.text = std::move(text),
						.filter = [=](const auto ...) {
							input->setText(QString::number(min));
							processInputChange();
							return true;
						},
					});
					return 0ULL;
				}
				return amount;
			},
		},
		button,
		controller->uiShow());
	Ui::ToggleChildrenVisibility(button, true);

	Ui::AddSkip(container);
	Ui::AddSkip(container);

	const auto arrow = Ui::Text::SingleCustomEmoji(
		session->data().customEmojiManager().registerInternalEmoji(
			st::topicButtonArrow,
			st::channelEarnLearnArrowMargins,
			false));
	auto about = Ui::CreateLabelWithCustomEmoji(
		container,
		tr::lng_bot_earn_learn_credits_out_about(
			lt_link,
			tr::lng_channel_earn_about_link(
				lt_emoji,
				rpl::single(arrow),
				Ui::Text::RichLangValue
			) | rpl::map([](TextWithEntities text) {
				return Ui::Text::Link(
					std::move(text),
					tr::lng_bot_earn_balance_about_url(tr::now));
			}),
			Ui::Text::RichLangValue),
		{ .session = session },
		st::boxDividerLabel);
	Ui::AddSkip(container);
	container->add(object_ptr<Ui::DividerLabel>(
		container,
		std::move(about),
		st::defaultBoxDividerLabelPadding,
		RectPart::Top | RectPart::Bottom));

	Ui::AddSkip(container);
}

} // namespace Settings
