/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_credits_graphics.h"

#include "api/api_chat_invite.h"
#include "api/api_credits.h"
#include "api/api_earn.h"
#include "api/api_premium.h"
#include "apiwrap.h"
#include "base/random.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/gift_premium_box.h"
#include "boxes/share_box.h"
#include "boxes/star_gift_box.h"
#include "boxes/transfer_gift_box.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/click_handler_types.h" // UrlClickHandler
#include "core/ui_integration.h"
#include "data/components/credits.h"
#include "data/data_boosts.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_emoji_statuses.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_subscriptions.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h" // HistoryServicePaymentRefund.
#include "info/bot/starref/info_bot_starref_common.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "info/channel_statistics/earn/info_channel_earn_widget.h" // Info::ChannelEarn::Make.
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "info/statistics/info_statistics_list_controllers.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "iv/iv_instance.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_single_player.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "payments/payments_checkout_process.h"
#include "payments/payments_form.h"
#include "payments/payments_non_panel_process.h"
#include "settings/settings_common_session.h"
#include "settings/settings_credits.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/effects/credits_graphics.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/effects/toggle_arrow.h"
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
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/label_with_custom_emoji.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_calls.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"

#include <xxhash.h> // XXH64.

#include <QtSvg/QSvgRenderer>

namespace Settings {
namespace {

const auto kTopUpPrefix = "cloud_lng_topup_purpose_";

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

[[nodiscard]] rpl::producer<TextWithEntities> DeepLinkBalanceAbout(
		const QString &purpose) {
	const auto phrase = Lang::GetNonDefaultValue(
		kTopUpPrefix + purpose.toUtf8());
	return phrase.isEmpty()
		? tr::lng_credits_small_balance_fallback(Ui::Text::RichLangValue)
		: rpl::single(Ui::Text::RichLangValue(phrase));
}

class Balance final
	: public Ui::RpWidget
	, public Ui::AbstractTooltipShower {
public:
	using Ui::RpWidget::RpWidget;

	void setBalance(StarsAmount balance) {
		_balance = balance;
		_tooltip = Lang::FormatStarsAmountDecimal(balance);
	}

	void enterEventHook(QEnterEvent *e) override {
		if (_balance >= StarsAmount(10'000)) {
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
	StarsAmount _balance;

};

[[nodiscard]] Data::SavedStarGiftId EntryToSavedStarGiftId(
		not_null<Main::Session*> session,
		const Data::CreditsHistoryEntry &entry) {
	return !entry.stargift
		? Data::SavedStarGiftId()
		: (entry.bareGiftListPeerId && entry.giftSavedId)
		? Data::SavedStarGiftId::Chat(
			session->data().peer(PeerId(entry.bareGiftListPeerId)),
			entry.giftSavedId)
		: Data::SavedStarGiftId::User(MsgId(entry.bareMsgId));
}

void ToggleStarGiftSaved(
		std::shared_ptr<ChatHelpers::Show> show,
		Data::SavedStarGiftId savedId,
		bool save,
		Fn<void(bool)> done) {
	using Flag = MTPpayments_SaveStarGift::Flag;
	const auto api = &show->session().api();
	const auto channelGift = savedId.chat();
	api->request(MTPpayments_SaveStarGift(
		MTP_flags(save ? Flag(0) : Flag::f_unsave),
		Api::InputSavedStarGiftId(savedId)
	)).done([=] {
		done(true);
		show->showToast((save
			? (channelGift
				? tr::lng_gift_display_done_channel
				: tr::lng_gift_display_done)
			: (channelGift
				? tr::lng_gift_display_done_hide_channel
				: tr::lng_gift_display_done_hide))(tr::now));
	}).fail([=](const MTP::Error &error) {
		done(false);
		show->showToast(error.type());
	}).send();
}

void ConfirmConvertStarGift(
		std::shared_ptr<Ui::Show> show,
		rpl::producer<TextWithEntities> confirmText,
		int stars,
		int daysLeft,
		Fn<void()> convert) {
	auto text = rpl::combine(
		std::move(confirmText),
		tr::lng_gift_convert_sure_limit(
			lt_count,
			rpl::single(daysLeft * 1.),
			Ui::Text::RichLangValue),
		tr::lng_gift_convert_sure_caution(Ui::Text::RichLangValue)
	) | rpl::map([](
			TextWithEntities &&a,
			TextWithEntities &&b,
			TextWithEntities &&c) {
		return a.append("\n\n").append(b).append("\n\n").append(c);
	});
	show->show(Ui::MakeConfirmBox({
		.text = std::move(text),
		.confirmed = [=](Fn<void()> close) { close(); convert(); },
		.confirmText = tr::lng_gift_convert_sure(),
		.title = tr::lng_gift_convert_sure_title(),
	}));
}

void ConvertStarGift(
		std::shared_ptr<ChatHelpers::Show> show,
		Data::SavedStarGiftId savedId,
		int stars,
		Fn<void(bool)> done) {
	const auto api = &show->session().api();
	api->request(MTPpayments_ConvertStarGift(
		Api::InputSavedStarGiftId(savedId)
	)).done([=] {
		if (const auto window = show->resolveWindow()) {
			if (const auto channel = savedId.chat()) {
				window->showSection(Info::ChannelEarn::Make(channel));
			} else {
				window->showSettings(Settings::CreditsId());
			}
		}
		show->showToast((savedId.chat()
			? tr::lng_gift_channel_got
			: tr::lng_gift_got_stars)(
				tr::now,
				lt_count,
				stars,
				Ui::Text::RichLangValue));
		done(true);
	}).fail([=](const MTP::Error &error) {
		show->showToast(error.type());
		done(false);
	}).send();
}

void AddViewMediaHandler(
		not_null<Ui::RpWidget*> thumb,
		std::shared_ptr<ChatHelpers::Show> show,
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
	const auto session = &show->session();
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
			const auto document = owner->document(item.id);
			const auto item = state->item;
			using MediaFile = Data::MediaFile;
			using Args = MediaFile::Args;
			fake.push_back(std::make_unique<MediaFile>(item, document, Args{
				.skipPremiumEffect = true,
			}));
		}
	}
	state->item->overrideMedia(std::make_unique<Data::MediaInvoice>(
		state->item,
		Data::Invoice{
			.amount = uint64(e.credits.abs().whole()),
			.currency = Ui::kCreditsCurrency,
			.extendedMedia = std::move(fake),
			.isPaidMedia = true,
		}));
	const auto showMedia = [=] {
		const auto window = show->resolveWindow();
		if (!window) {
			return;
		} else if (const auto media = state->item->media()) {
			if (const auto invoice = media->invoice()) {
				if (!invoice->extendedMedia.empty()) {
					const auto first = invoice->extendedMedia[0].get();
					if (const auto photo = first->photo()) {
						window->openPhoto(photo, {
							.id = state->item->fullId(),
						});
					} else if (const auto document = first->document()) {
						window->openDocument(document, true, {
							.id = state->item->fullId(),
						});
					}
				}
			}
		}
	};
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

void AddMiniStars(
		not_null<Ui::VerticalLayout*> content,
		not_null<Ui::RpWidget*> widget,
		int photoSize,
		int boxWidth,
		float64 heightRatio) {
	using ColoredMiniStars = Ui::Premium::ColoredMiniStars;
	const auto stars = widget->lifetime().make_state<ColoredMiniStars>(
		widget,
		false,
		Ui::Premium::MiniStars::Type::BiStars);
	stars->setColorOverride(Ui::Premium::CreditsIconGradientStops());
	widget->resize(boxWidth - photoSize, photoSize * heightRatio);
	content->sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		widget->moveToLeft(photoSize / 2, 0);
		const auto starsRect = Rect(widget->size());
		stars->setPosition(starsRect.topLeft());
		stars->setSize(starsRect.size());
		widget->lower();
	}, widget->lifetime());
	widget->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(widget);
		p.fillRect(r, Qt::transparent);
		stars->paint(p);
	}, widget->lifetime());
}

SubscriptionRightLabel PaintSubscriptionRightLabelCallback(
		not_null<Main::Session*> session,
		const style::PeerListItem &st,
		int amount) {
	const auto text = std::make_shared<Ui::Text::String>();
	text->setMarkedText(
		st::semiboldTextStyle,
		TextWithEntities()
			.append(session->data().customEmojiManager().creditsEmoji())
			.append(QChar::Space)
			.append(Lang::FormatCountDecimal(amount)),
		kMarkupTextOptions,
		Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [] {},
		});
	const auto &font = text->style()->font;
	const auto &statusFont = st::contactsStatusFont;
	const auto status = tr::lng_group_invite_joined_right(tr::now);
	const auto rightSkip = st::boxRowPadding.right();
	const auto statusWidth = statusFont->width(status);
	const auto size = QSize(
		std::max(text->maxWidth(), statusWidth) + rightSkip,
		font->height + statusFont->height);
	const auto statusX = size.width() - statusWidth;
	auto draw = [=](QPainter &p, int x, int y, int h) {
		p.setPen(st.statusFg);
		p.setFont(statusFont);
		const auto skip = y + (h - size.height()) / 2;
		p.drawText(
			x + statusX,
			font->height + statusFont->ascent + skip,
			status);

		p.setPen(st.nameFg);
		const auto textWidth = text->maxWidth();
		text->draw(p, Ui::Text::PaintContext{
			.position = QPoint(x + size.width() - textWidth, skip),
			.outerWidth = textWidth,
			.availableWidth = textWidth,
		});
	};
	return { std::move(draw), size };
}

void FillCreditOptions(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer,
		StarsAmount minimumCredits,
		Fn<void()> paid,
		rpl::producer<QString> subtitle,
		std::vector<Data::CreditTopupOption> preloadedTopupOptions) {
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
		if (subtitle) {
			Ui::AddSubsectionTitle(content, std::move(subtitle));
		}

		const auto buttons = content->add(
			object_ptr<Ui::VerticalLayout>(content));

		const auto showMoreWrap = content->add(
			object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
				content,
				object_ptr<Ui::SettingsButton>(
					content,
					tr::lng_credits_more_options(),
					st::statisticsShowMoreButton)));
		const auto showMore = showMoreWrap->entity();
		showMore->setClickedCallback([=] {
			showMoreWrap->toggle(false, anim::type::instant);
		});
		Ui::AddToggleUpDownArrowToMoreButton(showMore);

		const auto &st = st::creditsTopupButton;
		const auto diffBetweenTextAndStar = st.padding.left()
			- st.iconLeft
			- int(singleStarWidth * 1.5);
		const auto buttonHeight = st.height + rect::m::sum::v(st.padding);
		const auto minCredits = (!options.empty()
				&& (minimumCredits > StarsAmount(options.back().credits)))
			? StarsAmount()
			: minimumCredits;
		for (auto i = 0; i < options.size(); i++) {
			const auto &option = options[i];
			if (StarsAmount(option.credits) < minCredits) {
				continue;
			}
			const auto button = [&] {
				auto owned = object_ptr<Ui::SettingsButton>(
					buttons,
					rpl::never<QString>(),
					st);
				if (!option.extended) {
					return buttons->add(std::move(owned));
				}
				const auto wrap = buttons->add(
					object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
						buttons,
						std::move(owned)));
				wrap->toggle(false, anim::type::instant);
				showMore->clicks() | rpl::start_with_next([=] {
					wrap->toggle(true, anim::type::normal);
				}, wrap->lifetime());
				return wrap->entity();
			}();
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
			const auto textLeft = diffBetweenTextAndStar
				+ stars.width() / style::DevicePixelRatio();
			inner->paintRequest(
			) | rpl::start_with_next([=](const QRect &rect) {
				auto p = QPainter(inner);
				p.drawImage(0, 0, stars);
				p.setPen(st.textFg);
				text->draw(p, {
					.position = QPoint(textLeft, 0),
					.availableWidth = inner->width() - textLeft,
					.elisionLines = 1,
				});
			}, inner->lifetime());
			button->widthValue(
			) | rpl::start_with_next([=](int width) {
				price->moveToRight(st.padding.right(), st.padding.top());
				inner->moveToLeft(st.iconLeft, st.padding.top());
				inner->resize(
					width - price->width() - st.padding.left(),
					buttonHeight);
			}, button->lifetime());
			button->setClickedCallback([=] {
				const auto invoice = Payments::InvoiceCredits{
					.session = &show->session(),
					.randomId = UniqueIdFromOption(option),
					.credits = option.credits,
					.product = option.product,
					.currency = option.currency,
					.amount = option.amount,
					.extended = option.extended,
					.giftPeerId = PeerId(option.giftBarePeerId),
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
	const auto apiCredits = content->lifetime().make_state<ApiOptions>(peer);

	if (show->session().premiumPossible()) {
		if (preloadedTopupOptions.empty()) {
			apiCredits->request(
			) | rpl::start_with_error_done([=](const QString &error) {
				show->showToast(error);
			}, [=] {
				fill(apiCredits->options());
			}, content->lifetime());
		} else {
			fill(std::move(preloadedTopupOptions));
		}
	}

	show->session().premiumPossibleValue(
	) | rpl::start_with_next([=](bool premiumPossible) {
		if (!premiumPossible) {
			fill({});
		}
	}, content->lifetime());
}

not_null<Ui::RpWidget*> AddBalanceWidget(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<StarsAmount> balanceValue,
		bool rightAlign,
		rpl::producer<float64> opacityValue) {
	struct State final {
		QImage star;
		float64 opacity = 1.0;
		Ui::Text::String label;
		Ui::Text::String count;
	};
	const auto balance = Ui::CreateChild<Balance>(parent);
	const auto state = balance->lifetime().make_state<State>();
	state->star = QImage(Ui::GenerateStars(st::creditsBalanceStarHeight, 1));
	const auto starSize = state->star.size() / style::DevicePixelRatio();
	state->label = Ui::Text::String(
		st::defaultTextStyle,
		tr::lng_credits_summary_balance(tr::now));
	state->count = Ui::Text::String(
		st::semiboldTextStyle,
		tr::lng_contacts_loading(tr::now));
	if (opacityValue) {
		std::move(opacityValue) | rpl::start_with_next([=](float64 value) {
			state->opacity = value;
		}, balance->lifetime());
	}
	const auto diffBetweenStarAndCount = state->count.style()->font->spacew;
	const auto resize = [=] {
		balance->resize(
			std::max(
				state->label.maxWidth(),
				state->count.maxWidth()
					+ starSize.width()
					+ diffBetweenStarAndCount),
			state->label.style()->font->height + starSize.height());
	};
	std::move(
		balanceValue
	) | rpl::start_with_next([=](StarsAmount value) {
		state->count.setText(
			st::semiboldTextStyle,
			Lang::FormatStarsAmountToShort(value).string);
		balance->setBalance(value);
		resize();
	}, balance->lifetime());
	balance->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(balance);

		p.setOpacity(state->opacity);
		p.setPen(st::boxTextFg);

		state->label.draw(p, {
			.position = QPoint(
				rightAlign ? (balance->width() - state->label.maxWidth()) : 0,
				0),
			.availableWidth = balance->width(),
		});
		state->count.draw(p, {
			.position = QPoint(
				(rightAlign
					? (balance->width() - state->count.maxWidth())
					: (starSize.width() + diffBetweenStarAndCount)),
				state->label.minHeight()
					+ (starSize.height() - state->count.minHeight()) / 2),
			.availableWidth = balance->width(),
		});
		p.drawImage(
			(rightAlign
				? (balance->width()
					- state->count.maxWidth()
					- starSize.width()
					- diffBetweenStarAndCount)
				: 0),
			state->label.minHeight(),
			state->star);
	}, balance->lifetime());
	return balance;
}

void BoostCreditsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const Data::Boost &b) {
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto content = box->verticalLayout();
	const auto session = &controller->session();
	Ui::AddSkip(content);
	{
		const auto &stUser = st::premiumGiftsUserpicButton;
		const auto widget = content->add(object_ptr<Ui::RpWidget>(content));
		AddMiniStars(content, widget, stUser.photoSize, st::boxWidth, 1.3);
		const auto svg = std::make_shared<QSvgRenderer>(
			Ui::Premium::ColorizedSvg(
				Ui::Premium::CreditsIconGradientStops()));
		widget->paintRequest() | rpl::start_with_next([=](const QRect &r) {
			auto p = QPainter(widget);
			svg->render(
				&p,
				QRectF(
					(widget->width() - stUser.photoSize) / 2.,
					(widget->height() - stUser.photoSize) / 2.,
					stUser.photoSize,
					stUser.photoSize));
		}, widget->lifetime());
	}
	content->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_gift_stars_title(
					lt_count,
					rpl::single(float64(b.credits))),
				st::boxTitle)));
	Ui::AddSkip(content);
	if (b.multiplier) {
		const auto &st = st::statisticsDetailsBottomCaptionStyle;
		const auto badge = content->add(object_ptr<Ui::RpWidget>(content));
		badge->resize(badge->width(), st.font->height * 1.5);
		const auto text = badge->lifetime().make_state<Ui::Text::String>(
			st::boxWidth
				- st::boxRowPadding.left()
				- st::boxRowPadding.right());
		auto textWithEntities = TextWithEntities();
		textWithEntities.append(
			Ui::Text::SingleCustomEmoji(
				session->data().customEmojiManager().registerInternalEmoji(
					st::boostsListMiniIcon,
					{ st.font->descent * 2, st.font->descent / 2, 0, 0 },
					true)));
		textWithEntities.append(
			tr::lng_boosts_list_title(tr::now, lt_count, b.multiplier));
		text->setMarkedText(
			st,
			std::move(textWithEntities),
			kMarkupTextOptions,
			Core::MarkedTextContext{
				.session = session,
				.customEmojiRepaint = [=] { badge->update(); },
			});
		badge->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = QPainter(badge);
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = badge->height() / 2;
			const auto badgeWidth = text->maxWidth() + radius;
			p.setPen(Qt::NoPen);
			p.setBrush(st::premiumButtonBg2);
			p.drawRoundedRect(
				QRect(
					(badge->width() - badgeWidth) / 2,
					0,
					badgeWidth,
					badge->height()),
				radius,
				radius);
			p.setPen(st::premiumButtonFg);
			p.setBrush(Qt::NoBrush);
			text->draw(p, Ui::Text::PaintContext{
				.position = QPoint(
					(badge->width() - text->maxWidth() - radius) / 2,
					(badge->height() - text->minHeight()) / 2),
				.outerWidth = badge->width(),
				.availableWidth = badge->width(),
			});
		}, badge->lifetime());

		Ui::AddSkip(content);
	}
	AddCreditsBoostTable(controller->uiShow(), content, {}, b);
	Ui::AddSkip(content);

	box->addRow(object_ptr<Ui::CenterWrap<>>(
		box,
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_credits_box_out_about(
				lt_link,
				tr::lng_payments_terms_link(
				) | Ui::Text::ToLink(
					tr::lng_credits_box_out_about_link(tr::now)),
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

void ProcessReceivedSubscriptions(
		QPointer<Ui::GenericBox> weak,
		not_null<Main::Session*> session) {
	const auto rebuilder = session->data().activeCreditsSubsRebuilder();
	if (const auto strong = weak.data()) {
		if (!rebuilder) {
			return strong->closeBox();
		}
		const auto api
			= strong->lifetime().make_state<Api::CreditsHistory>(
				session->user(),
				true,
				true);
		api->requestSubscriptions({}, [=](Data::CreditsStatusSlice first) {
			rebuilder->fire(std::move(first));
			if (const auto strong = weak.data()) {
				strong->closeBox();
			}
		});
	}
}

void FillUniqueGiftMenu(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::PopupMenu*> menu,
		const Data::CreditsHistoryEntry &e,
		CreditsEntryBoxStyleOverrides st) {
	Expects(e.uniqueGift != nullptr);

	const auto unique = e.uniqueGift;
	const auto local = u"nft/"_q + unique->slug;
	const auto url = show->session().createInternalLinkFull(local);
	menu->addAction(tr::lng_context_copy_link(tr::now), [=] {
		TextUtilities::SetClipboardText({ url });
		show->showToast(tr::lng_channel_public_link_copied(tr::now));
	}, st.link ? st.link : &st::menuIconLink);

	const auto shareBoxSt = st.shareBox;
	menu->addAction(tr::lng_chat_link_share(tr::now), [=] {
		FastShareLink(
			show,
			url,
			shareBoxSt ? *shareBoxSt : ShareBoxStyleOverrides());
	}, st.share ? st.share : &st::menuIconShare);

	const auto savedId = EntryToSavedStarGiftId(&show->session(), e);
	const auto transfer = savedId
		&& (savedId.isUser() ? e.in : savedId.chat()->canTransferGifts())
		&& (unique->starsForTransfer >= 0);
	if (transfer) {
		menu->addAction(tr::lng_gift_transfer_button(tr::now), [=] {
			if (const auto window = show->resolveWindow()) {
				ShowTransferGiftBox(window, unique, savedId);
			}
		}, st.transfer ? st.transfer : &st::menuIconReplace);
	}
	const auto owner = show->session().data().peer(unique->ownerId);
	const auto wear = owner->isSelf()
		? e.in
		: (owner->isChannel() && owner->asChannel()->canEditEmoji());
	if (wear) {
		const auto name = UniqueGiftName(*unique);
		const auto now = owner->emojiStatusId().collectible;
		if (now && unique->slug == now->slug) {
			menu->addAction(tr::lng_gift_transfer_take_off(tr::now), [=] {
				show->session().data().emojiStatuses().set(owner, {});
			}, st.takeoff ? st.takeoff : &st::menuIconNftTakeOff);
		} else {
			menu->addAction(tr::lng_gift_transfer_wear(tr::now), [=] {
				ShowUniqueGiftWearBox(show, owner, *unique, st.giftWearBox
					? *st.giftWearBox
					: GiftWearBoxStyleOverride());
			}, st.wear ? st.wear : &st::menuIconNftWear);
		}
	}
}

GiftWearBoxStyleOverride DarkGiftWearBoxStyle() {
	return {
		.box = &st::darkUpgradeGiftBox,
		.title = &st::darkUpgradeGiftTitle,
		.subtitle = &st::darkUpgradeGiftSubtitle,
		.radiantIcon = &st::darkUpgradeGiftRadiant,
		.proofIcon = &st::darkUpgradeGiftProof,
		.infoTitle = &st::darkUpgradeGiftInfoTitle,
		.infoAbout = &st::darkUpgradeGiftInfoAbout,
	};
}

CreditsEntryBoxStyleOverrides DarkCreditsEntryBoxStyle() {
	return {
		.box = &st::darkGiftCodeBox,
		.menu = &st::mediaviewPopupMenu,
		.table = &st::darkGiftTable,
		.tableValueMultiline = &st::darkGiftTableValueMultiline,
		.tableValueMessage = &st::darkGiftTableMessage,
		.link = &st::darkGiftLink,
		.share = &st::darkGiftShare,
		.transfer = &st::darkGiftTransfer,
		.wear = &st::darkGiftNftWear,
		.takeoff = &st::darkGiftNftTakeOff,
		.shareBox = std::make_shared<ShareBoxStyleOverrides>(
			DarkShareBoxStyle()),
		.giftWearBox = std::make_shared<GiftWearBoxStyleOverride>(
			DarkGiftWearBoxStyle()),
	};
}

void GenericCreditsEntryBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		const Data::CreditsHistoryEntry &e,
		const Data::SubscriptionEntry &s,
		CreditsEntryBoxStyleOverrides st) {
	const auto session = &show->session();
	const auto selfPeerId = session->userPeerId().value;
	const auto owner = &session->data();
	const auto item = owner->message(
		PeerId(e.barePeerId),
		MsgId(e.bareMsgId));
	const auto isStarGift = e.stargift || e.soldOutInfo;
	const auto creditsHistoryStarGift = isStarGift && !e.id.isEmpty();
	const auto sentStarGift = creditsHistoryStarGift && !e.in;
	const auto convertedStarGift = creditsHistoryStarGift && e.converted;
	const auto giftToSelf = isStarGift
		&& (e.barePeerId == selfPeerId)
		&& (e.in || e.bareGiftOwnerId == selfPeerId);
	const auto giftChannel = (isStarGift && e.giftSavedId)
		? session->data().peer(
			PeerId(e.bareGiftListPeerId))->asChannel()
		: nullptr;
	const auto giftToChannel = (giftChannel != nullptr);
	const auto giftToChannelCanManage = giftToChannel
		&& giftChannel->canManageGifts();
	const auto giftToChannelCanTransfer = giftToChannel
		&& giftChannel->canTransferGifts();
	const auto starGiftCanManage = isStarGift
		&& !creditsHistoryStarGift
		&& (e.in || giftToChannelCanManage);
	const auto starGiftCanTransfer = isStarGift
		&& !creditsHistoryStarGift
		&& (e.in || giftToChannelCanTransfer);
	const auto starGiftSender = (isStarGift && item)
		? item->history()->peer->asUser()
		: (isStarGift && e.in)
		? owner->peer(PeerId(e.barePeerId))->asUser()
		: (isStarGift && e.bareActorId)
		? owner->peer(PeerId(e.bareActorId)).get()
		: nullptr;
	const auto convertLast = base::unixtime::serialize(e.date)
		+ session->appConfig().stargiftConvertPeriodMax();
	const auto timeLeft = int64(convertLast) - int64(base::unixtime::now());
	const auto timeExceeded = (timeLeft <= 0);
	const auto uniqueGift = e.uniqueGift.get();
	const auto forConvert = starGiftCanTransfer
		&& e.starsConverted
		&& !e.converted
		&& starGiftSender;
	const auto canConvert = forConvert && !timeExceeded;

	if (auto savedId = EntryToSavedStarGiftId(session, e)) {
		session->data().giftUpdates(
		) | rpl::start_with_next([=](const Data::GiftUpdate &update) {
			if (update.id == savedId) {
				box->closeBox();
			}
		}, box->lifetime());
	}

	box->setStyle(st.box ? *st.box : st::giveawayGiftCodeBox);
	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);

	const auto content = box->verticalLayout();
	if (!uniqueGift) {
		Ui::AddSkip(content);
		Ui::AddSkip(content);
		Ui::AddSkip(content);
	}

	using Type = Data::CreditsHistoryEntry::PeerType;

	const auto &stUser = st::boostReplaceUserpic;
	const auto isPrize = e.bareGiveawayMsgId > 0;
	const auto starGiftSticker = (isStarGift && e.bareGiftStickerId)
		? owner->document(e.bareGiftStickerId).get()
		: nullptr;
	const auto peer = isPrize
		? nullptr
		: (s.barePeerId)
		? owner->peer(PeerId(s.barePeerId)).get()
		: (e.peerType == Type::PremiumBot)
		? nullptr
		: e.bareActorId
		? owner->peer(PeerId(e.bareActorId)).get()
		: e.barePeerId
		? owner->peer(PeerId(e.barePeerId)).get()
		: nullptr;
	if (uniqueGift) {
		box->setNoContentMargin(true);

		AddUniqueGiftCover(content, rpl::single(*uniqueGift));

		AddSkip(content, st::defaultVerticalListSkip * 2);

		AddUniqueCloseButton(box, st, [=](not_null<Ui::PopupMenu*> menu) {
			FillUniqueGiftMenu(show, menu, e, st);
		});
	} else if (const auto callback = Ui::PaintPreviewCallback(session, e)) {
		const auto thumb = content->add(object_ptr<Ui::CenterWrap<>>(
			content,
			GenericEntryPhoto(content, callback, stUser.photoSize)));
		AddViewMediaHandler(thumb->entity(), show, e);
	} else if (s.photoId || (e.photoId && !e.subscriptionUntil.isNull())) {
		if (!(s.cancelled || s.expired || s.cancelledByBot)) {
			const auto widget = Ui::CreateChild<Ui::RpWidget>(content);
			const auto photoSize = stUser.photoSize;
			AddMiniStars(content, widget, photoSize, st::boxWideWidth, 1.5);
		}
		const auto photoId = s.photoId ? s.photoId : e.photoId;
		const auto callback = [=](Fn<void()> update) {
			return Ui::GenerateCreditsPaintEntryCallback(
				owner->photo(photoId),
				std::move(update));
		};
		content->add(object_ptr<Ui::CenterWrap<>>(
			content,
			GenericEntryPhoto(content, callback, stUser.photoSize)));
	} else if (peer && !e.gift) {
		if (e.subscriptionUntil.isNull() && s.until.isNull()) {
			content->add(object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::UserpicButton>(content, peer, stUser)));
		} else {
			content->add(object_ptr<Ui::CenterWrap<>>(
				content,
				SubscriptionUserpic(content, peer, stUser.photoSize)));
		}
	} else if (e.gift || isPrize) {
		struct State final {
			DocumentData *sticker = nullptr;
			std::shared_ptr<Data::DocumentMedia> media;
			std::unique_ptr<Lottie::SinglePlayer> lottie;
			rpl::lifetime downloadLifetime;
		};
		Ui::AddSkip(content, isStarGift
			? st::creditsHistoryEntryStarGiftSpace
			: st::creditsHistoryEntryGiftStickerSpace);
		const auto icon = Ui::CreateChild<Ui::RpWidget>(content);
		icon->resize(Size(isStarGift
			? st::creditsHistoryEntryStarGiftSize
			: st::creditsHistoryEntryGiftStickerSize));
		const auto state = icon->lifetime().make_state<State>();
		auto &packs = session->giftBoxStickersPacks();
		const auto document = starGiftSticker
			? starGiftSticker
			: packs.lookup(packs.monthsForStars(e.credits.whole()));
		if (document && document->sticker()) {
			state->sticker = document;
			state->media = document->createMediaView();
			state->media->thumbnailWanted(packs.origin());
			state->media->automaticLoad(packs.origin(), nullptr);
			rpl::single() | rpl::then(
				session->downloaderTaskFinished()
			) | rpl::filter([=] {
				return state->media->loaded();
			}) | rpl::start_with_next([=] {
				state->lottie = ChatHelpers::LottiePlayerFromDocument(
					state->media.get(),
					ChatHelpers::StickerLottieSize::MessageHistory,
					icon->size(),
					Lottie::Quality::High);
				state->lottie->updates() | rpl::start_with_next([=] {
					icon->update();
				}, icon->lifetime());
				state->downloadLifetime.destroy();
			}, state->downloadLifetime);
		}
		icon->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = Painter(icon);
			const auto &lottie = state->lottie;
			const auto factor = style::DevicePixelRatio();
			const auto request = Lottie::FrameRequest{
				.box = icon->size() * factor,
			};
			const auto frame = (lottie && lottie->ready())
				? lottie->frameInfo(request)
				: Lottie::Animation::FrameInfo();
			if (!frame.image.isNull()) {
				p.drawImage(
					QRect(QPoint(), frame.image.size() / factor),
					frame.image);
				if (lottie->frameIndex() < lottie->framesCount() - 1) {
					lottie->markFrameShown();
				}
			}
		}, icon->lifetime());
		content->sizeValue(
		) | rpl::start_with_next([=](const QSize &size) {
			icon->move((size.width() - icon->width()) / 2, isStarGift
				? st::creditsHistoryEntryStarGiftSkip
				: st::creditsHistoryEntryGiftStickerSkip);
		}, icon->lifetime());
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

	if (!uniqueGift) {
		Ui::AddSkip(content);
		Ui::AddSkip(content);

		box->addRow(object_ptr<Ui::CenterWrap<>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(!s.title.isEmpty()
					? s.title
					: !s.until.isNull()
					? tr::lng_credits_box_subscription_title(tr::now)
					: isPrize
					? tr::lng_credits_box_history_entry_giveaway_name(tr::now)
					: (!e.subscriptionUntil.isNull() && e.title.isEmpty())
					? tr::lng_credits_box_history_entry_subscription(tr::now)
					: !e.title.isEmpty()
					? e.title
					: e.starrefCommission
					? tr::lng_credits_commission(
						tr::now,
						lt_amount,
						Info::BotStarRef::FormatCommission(e.starrefCommission))
					: e.soldOutInfo
					? tr::lng_credits_box_history_entry_gift_unavailable(tr::now)
					: sentStarGift
					? tr::lng_credits_box_history_entry_gift_sent(tr::now)
					: convertedStarGift
					? tr::lng_credits_box_history_entry_gift_converted(tr::now)
					: (isStarGift && !starGiftCanManage)
					? tr::lng_gift_link_label_gift(tr::now)
					: giftToSelf
					? tr::lng_action_gift_self_subtitle(tr::now)
					: e.gift
					? tr::lng_credits_box_history_entry_gift_name(tr::now)
					: (peer && !e.reaction)
					? peer->name()
					: Ui::GenerateEntryName(e).text),
				st::creditsBoxAboutTitle)));

		Ui::AddSkip(content);
	}
	if (!isStarGift || creditsHistoryStarGift || e.soldOutInfo) {
		constexpr auto kMinus = QChar(0x2212);
		auto &lifetime = content->lifetime();
		const auto text = lifetime.make_state<Ui::Text::String>();
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
				st::defaultTextStyle.font->height));
		const auto context = Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [=] { amount->update(); },
		};
		if (e.soldOutInfo) {
			text->setText(
				st::defaultTextStyle,
				tr::lng_credits_box_history_entry_gift_sold_out(tr::now));
		} else if (s) {
			text->setMarkedText(
				st::defaultTextStyle,
				tr::lng_credits_subscription_subtitle(
					tr::now,
					lt_emoji,
					owner->customEmojiManager().creditsEmoji(),
					lt_cost,
					{ QString::number(s.subscription.credits) },
					Ui::Text::WithEntities),
				kMarkupTextOptions,
				context);
		} else {
			auto t = TextWithEntities()
				.append((e.in && (creditsHistoryStarGift || !isStarGift))
					? u"+"_q
					: (e.gift && !creditsHistoryStarGift)
					? QString()
					: QString(kMinus))
				.append(Lang::FormatStarsAmountDecimal(e.credits.abs()))
				.append(QChar(' '))
				.append(owner->customEmojiManager().creditsEmoji());
			text->setMarkedText(
				st::semiboldTextStyle,
				std::move(t),
				kMarkupTextOptions,
				context);
		}
		const auto font = text->style()->font;
		const auto roundedFont = st::defaultTextStyle.font;
		const auto roundedSkip = roundedFont->spacew * 2;
		const auto roundedWidth = rounded
			? roundedFont->width(roundedText)
				+ roundedSkip
				+ roundedFont->height
			: 0;
		const auto fullWidth = text->maxWidth() + roundedWidth;
		amount->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = Painter(amount);
			p.setPen(e.soldOutInfo
				? st::menuIconAttentionColor
				: s
				? st::windowSubTextFg
				: e.pending
				? st::creditsStroke
				: (e.in || (isStarGift && !creditsHistoryStarGift))
				? st::boxTextFgGood
				: (e.gift && !creditsHistoryStarGift)
				? st::windowBoldFg
				: st::menuIconAttentionColor);
			const auto x = (amount->width() - fullWidth) / 2;
			text->draw(p, Ui::Text::PaintContext{
				.position = QPoint(
					x,
					(amount->height() - font->height) / 2),
				.outerWidth = amount->width(),
				.availableWidth = amount->width(),
			});

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

	if (!isStarGift && !e.description.empty()) {
		Ui::AddSkip(content);
		box->addRow(object_ptr<Ui::CenterWrap<>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(e.description),
				st::creditsBoxAbout)));
	}
	if (!uniqueGift && starGiftCanManage) {
		Ui::AddSkip(content);
		const auto about = box->addRow(
			object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
				box,
				object_ptr<Ui::FlatLabel>(
					box,
					(e.giftRefunded
						? tr::lng_action_gift_refunded(
							Ui::Text::RichLangValue)
						: e.starsUpgradedBySender
						? tr::lng_action_gift_got_upgradable_text(
							Ui::Text::RichLangValue)
						: (e.starsToUpgrade
							&& giftToSelf
							&& !e.giftTransferred)
						? tr::lng_action_gift_self_about_unique(
							Ui::Text::WithEntities)
						: (e.starsToUpgrade
							&& giftToChannelCanManage
							&& !e.giftTransferred)
						? tr::lng_action_gift_channel_about_unique(
							Ui::Text::WithEntities)
						: ((canConvert || e.converted)
							? rpl::combine(
								(canConvert
									? (giftToSelf
										? tr::lng_action_gift_self_about
										: giftToChannelCanTransfer
										? tr::lng_action_gift_channel_about
										: tr::lng_action_gift_got_stars_text)
									: (giftToChannel
										? tr::lng_gift_channel_got
										: tr::lng_gift_got_stars))(
										lt_count,
										rpl::single(e.starsConverted * 1.),
										Ui::Text::RichLangValue),
								tr::lng_paid_about_link()
							) | rpl::map([](
									TextWithEntities text,
									QString link) {
								return text.append(' ').append(
									Ui::Text::Link(link));
							})
							: (e.savedToProfile
								? (giftToChannel
									? tr::lng_action_gift_can_remove_channel
									: tr::lng_action_gift_can_remove_text)
								: (giftToChannel
									? tr::lng_action_gift_got_gift_channel
									: tr::lng_action_gift_got_gift_text))(
										Ui::Text::WithEntities))),
					st::creditsBoxAbout)))->entity();
		about->setClickHandlerFilter([=](const auto &...) {
			Core::App().iv().openWithIvPreferred(
				session,
				tr::lng_paid_about_link_url(tr::now));
			return false;
		});
		if (e.giftRefunded) {
			about->setTextColorOverride(st::menuIconAttentionColor->c);
		}
	} else if (isStarGift) {
	} else if (e.gift || isPrize) {
		Ui::AddSkip(content);
		const auto arrow = Ui::Text::SingleCustomEmoji(
			owner->customEmojiManager().registerInternalEmoji(
				st::topicButtonArrow,
				st::channelEarnLearnArrowMargins,
				true));
		auto link = tr::lng_credits_box_history_entry_gift_about_link(
			lt_emoji,
			rpl::single(arrow),
			Ui::Text::RichLangValue
		) | rpl::map([](TextWithEntities text) {
			return Ui::Text::Link(
				std::move(text),
				u"internal:stars_examples"_q);
		});
		box->addRow(object_ptr<Ui::CenterWrap<>>(
			box,
			Ui::CreateLabelWithCustomEmoji(
				box,
				(!e.in && peer)
					? tr::lng_credits_box_history_entry_gift_out_about(
						lt_user,
						rpl::single(TextWithEntities{ peer->shortName() }),
						lt_link,
						std::move(link),
						Ui::Text::RichLangValue)
					: tr::lng_credits_box_history_entry_gift_in_about(
						lt_link,
						std::move(link),
						Ui::Text::RichLangValue),
				{ .session = session },
				st::creditsBoxAbout)));
	}

	Ui::AddSkip(content);
	Ui::AddSkip(content);

	struct State final {
		rpl::variable<bool> confirmButtonBusy;
		rpl::variable<bool> convertButtonBusy;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto canToggle = starGiftCanManage
		&& !e.giftTransferred
		&& !e.giftRefunded;
	const auto toggleVisibility = [=, weak = Ui::MakeWeak(box)](bool save) {
		const auto showSection = !e.fromGiftsList;
		const auto savedId = EntryToSavedStarGiftId(&show->session(), e);
		const auto done = [=](bool ok) {
			if (ok) {
				using GiftAction = Data::GiftUpdate::Action;
				show->session().data().notifyGiftUpdate({
					.id = savedId,
					.action = (save
						? GiftAction::Save
						: GiftAction::Unsave),
				});
				if (showSection) {
					if (const auto window = show->resolveWindow()) {
						window->showSection(
							std::make_shared<Info::Memento>(
								window->session().user(),
								Info::Section::Type::PeerGifts));
					}
				}
			}
			if (const auto strong = weak.data()) {
				if (ok) {
					strong->closeBox();
				} else {
					state->confirmButtonBusy = false;
				}
			}
		};
		ToggleStarGiftSaved(show, savedId, save, done);
	};

	const auto upgradeGuard = std::make_shared<bool>();
	const auto upgrade = [=] {
		const auto window = show->resolveWindow();
		if (!window || *upgradeGuard) {
			return;
		}
		*upgradeGuard = true;
		const auto savedId = EntryToSavedStarGiftId(&window->session(), e);
		const auto openWhenDone = giftToChannel
			? window->session().data().peer(PeerId(e.bareGiftOwnerId)).get()
			: starGiftSender;
		using namespace Ui;
		ShowStarGiftUpgradeBox({
			.controller = window,
			.stargiftId = e.stargiftId,
			.ready = [=](bool) { *upgradeGuard = false; },
			.peer = openWhenDone,
			.savedId = savedId,
			.cost = e.starsUpgradedBySender ? 0 : e.starsToUpgrade,
			.canAddSender = !giftToSelf && !e.anonymous,
			.canAddComment = (!giftToSelf
				&& !e.anonymous
				&& e.hasGiftComment),
			.canAddMyComment = (giftToSelf && e.hasGiftComment),
			.addDetailsDefault = (giftToSelf
				|| (e.starsUpgradedBySender && !e.anonymous)),
		});
	};
	const auto canUpgrade = e.stargiftId
		&& e.canUpgradeGift
		&& (e.in || giftToSelf || giftToChannelCanManage)
		&& !e.uniqueGift;
	const auto canUpgradeFree = canUpgrade && (e.starsUpgradedBySender > 0);

	if (isStarGift && e.id.isEmpty()) {
		const auto convert = [=, weak = Ui::MakeWeak(box)] {
			const auto stars = e.starsConverted;
			const auto days = canConvert ? ((timeLeft + 86399) / 86400) : 0;
			auto text = giftToChannelCanManage
				? tr::lng_gift_convert_sure_confirm_channel(
					lt_count,
					rpl::single(stars * 1.),
					lt_channel,
					rpl::single(Ui::Text::Bold(giftChannel->name())),
					Ui::Text::RichLangValue)
				: tr::lng_gift_convert_sure_confirm(
					lt_count,
					rpl::single(stars * 1.),
					lt_user,
					rpl::single(Ui::Text::Bold(starGiftSender->shortName())),
					Ui::Text::RichLangValue);
			ConfirmConvertStarGift(show, std::move(text), stars, days, [=] {
				if (state->convertButtonBusy.current()
					|| state->confirmButtonBusy.current()) {
					return;
				}
				state->convertButtonBusy = true;
				const auto savedId = EntryToSavedStarGiftId(
					&show->session(),
					e);
				if (stars) {
					const auto done = [=](bool ok) {
						if (ok) {
							using GiftAction = Data::GiftUpdate::Action;
							show->session().data().notifyGiftUpdate({
								.id = savedId,
								.action = GiftAction::Convert,
							});
						}
						if (const auto strong = weak.data()) {
							if (ok) {
								strong->closeBox();
							} else {
								state->convertButtonBusy = false;
							}
						}
					};
					ConvertStarGift(show, savedId, stars, done);
				}
			});
		};
		AddStarGiftTable(
			show,
			content,
			st,
			e,
			canConvert ? convert : Fn<void()>(),
			canUpgrade ? upgrade : Fn<void()>());
	} else {
		AddCreditsHistoryEntryTable(show, content, st, e);
		AddSubscriptionEntryTable(show, content, st, s);
	}

	Ui::AddSkip(content);

	if (!isStarGift) {
		box->addRow(object_ptr<Ui::CenterWrap<>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_credits_box_out_about(
					lt_link,
					tr::lng_payments_terms_link(
					) | Ui::Text::ToLink(
						tr::lng_credits_box_out_about_link(tr::now)),
					Ui::Text::WithEntities),
				st::creditsBoxAboutDivider)));
	} else if (starGiftCanManage) {
		const auto hiddenPhrase = giftToChannelCanManage
			? tr::lng_gift_hidden_hint_channel
			: uniqueGift
			? tr::lng_gift_hidden_unique
			: tr::lng_gift_hidden_hint;
		const auto visiblePhrase = giftToChannelCanManage
			? tr::lng_gift_visible_hint_channel
			: tr::lng_gift_visible_hint;
		auto withHide = rpl::combine(
			visiblePhrase(),
			tr::lng_gift_visible_hide()
		) | rpl::map([](QString &&hint, QString &&hide) {
			return TextWithEntities{ std::move(hint) }.append(' ').append(
				Ui::Text::Link(std::move(hide)));
		});
		auto text = !e.savedToProfile
			? hiddenPhrase(Ui::Text::WithEntities)
			: canToggle
			? std::move(withHide)
			: visiblePhrase(Ui::Text::WithEntities);
		if (e.anonymous && e.barePeerId) {
			text = rpl::combine(
				std::move(text),
				(giftToChannelCanManage
					? tr::lng_gift_anonymous_hint_channel
					: tr::lng_gift_anonymous_hint)()
			) | rpl::map([](TextWithEntities &&a, QString &&b) {
				return a.append("\n\n").append(b);
			});
		}
		const auto label = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(text),
				st::creditsBoxAboutDivider));
		label->setClickHandlerFilter([=](const auto &...) {
			toggleVisibility(!e.savedToProfile);
			return false;
		});
	} else if (uniqueGift && !uniqueGift->ownerAddress.isEmpty()) {
		const auto label = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_gift_in_blockchain(
					lt_link,
					tr::lng_gift_in_blockchain_link() | Ui::Text::ToLink(),
					Ui::Text::WithEntities),
				st::creditsBoxAboutDivider));
		label->setClickHandlerFilter([=](const auto &...) {
			UrlClickHandler::Open(
				TonAddressUrl(session, uniqueGift->ownerAddress));
			return false;
		});
	}
	if (s) {
		const auto user = peer ? peer->asUser() : nullptr;
		const auto bot = (user && !user->isSelf()) ? user : nullptr;
		const auto toCancel = !s.expired && !s.cancelled && !s.cancelledByBot;
		if (toCancel) {
			Ui::AddSkip(content);
		}
		Ui::AddSkip(content);
		auto label = object_ptr<Ui::FlatLabel>(
			box,
			((s.cancelledByBot && bot)
				? tr::lng_credits_subscription_off_by_bot_about(
					lt_bot,
					rpl::single(bot->name()))
				: toCancel
				? tr::lng_credits_subscription_on_button()
				: s.cancelled
				? tr::lng_credits_subscription_off_about()
				: tr::lng_credits_subscription_on_about(
					lt_date,
					rpl::single(langDayOfMonthFull(s.until.date())))),
			st::creditsBoxAboutDivider);
		if (toCancel) {
			label->setClickHandlerFilter([=](
					const auto &,
					Qt::MouseButton button) {
				if (button != Qt::LeftButton) {
					return false;
				}
				const auto done = [=, weak = Ui::MakeWeak(box)] {
					ProcessReceivedSubscriptions(weak, session);
				};
				const auto fail = [=, s = box->uiShow()](const QString &e) {
					s->showToast(e);
				};
				Api::EditCreditsSubscription(session, s.id, true, done, fail);
				return true;
			});
			label->setMarkedText(
				Ui::Text::Link(
					tr::lng_credits_subscription_on_button(tr::now),
					u"internal:"_q));
		} else if (s.cancelled || s.cancelledByBot) {
			label->setTextColorOverride(st::menuIconAttentionColor->c);
		}
		box->addRow(
			object_ptr<Ui::CenterWrap<>>(box, std::move(label)));
	}

	Ui::AddSkip(content);

	if (e.peerType == Data::CreditsHistoryEntry::PeerType::PremiumBot) {
		const auto widget = Ui::CreateChild<Ui::RpWidget>(content);
		AddMiniStars(content, widget, stUser.photoSize, st::boxWideWidth, 2);
	}

	const auto rejoinByApi = base::unixtime::serialize(s.until)
		> base::unixtime::now();
	const auto rejoinByInvite = !s.inviteHash.isEmpty();
	const auto rejoinBySlug = !s.slug.isEmpty();
	const auto toRenew = (s.cancelled || s.expired)
		&& (rejoinByApi || rejoinByInvite)
		&& !s.cancelledByBot;
	const auto toRejoin = (s.cancelled || s.expired)
		&& rejoinBySlug
		&& !s.cancelledByBot;
	auto confirmText = rpl::conditional(
		state->confirmButtonBusy.value(),
		rpl::single(QString()),
		(toRenew
			? tr::lng_credits_subscription_off_button()
			: toRejoin
			? tr::lng_credits_subscription_off_rejoin_button()
			: canUpgradeFree
			? tr::lng_gift_upgrade_free()
			: (canToggle && !e.savedToProfile)
			? (e.giftSavedId
				? tr::lng_gift_show_on_channel
				: tr::lng_gift_show_on_page)()
			: tr::lng_box_ok()));
	const auto send = [=, weak = Ui::MakeWeak(box)] {
		if (toRejoin) {
			if (const auto window = show->resolveWindow()) {
				const auto finish = [=](Payments::CheckoutResult&&) {
					ProcessReceivedSubscriptions(weak, session);
				};
				Payments::CheckoutProcess::Start(
					&window->session(),
					s.slug,
					[](auto) {},
					Payments::ProcessNonPanelPaymentFormFactory(
						window,
						finish));
			}
		} else if (toRenew && s.expired) {
			if (const auto window = show->resolveWindow()) {
				Api::CheckChatInvite(window, s.inviteHash, nullptr, [=] {
					ProcessReceivedSubscriptions(weak, session);
				});
			}
		} else {
			const auto done = [=] {
				ProcessReceivedSubscriptions(weak, session);
			};
			const auto fail = [=, show = box->uiShow()](const QString &e) {
				if (const auto strong = weak.data()) {
					state->confirmButtonBusy = false;
				}
				show->showToast(e);
			};
			Api::EditCreditsSubscription(session, s.id, false, done, fail);
		}
	};

	const auto willBusy = toRejoin || (peer && toRenew);
	if (willBusy) {
		const auto close = Ui::CreateChild<Ui::IconButton>(
			content,
			st::boxTitleClose);
		close->setClickedCallback([=] { box->closeBox(); });
		content->widthValue() | rpl::start_with_next([=](int) {
			close->moveToRight(0, 0);
		}, content->lifetime());
	}

	const auto button = box->addButton(std::move(confirmText), [=] {
		if (state->confirmButtonBusy.current()
			|| state->convertButtonBusy.current()) {
			return;
		}
		if (willBusy) {
			state->confirmButtonBusy = true;
			send();
		} else if (canUpgradeFree) {
			upgrade();
		} else if (canToggle && !e.savedToProfile) {
			toggleVisibility(true);
		} else {
			box->closeBox();
		}
	});
	{
		using namespace Info::Statistics;
		const auto loadingAnimation = InfiniteRadialAnimationWidget(
			button,
			button->height() / 2);
		AddChildToWidgetCenter(button, loadingAnimation);
		loadingAnimation->showOn(state->confirmButtonBusy.value());
	}
	const auto buttonWidth = st::boxWideWidth
		- rect::m::sum::h(st::giveawayGiftCodeBox.buttonPadding);

	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != buttonWidth);
	}) | rpl::start_with_next([=] {
		button->resizeToWidth(buttonWidth);
	}, button->lifetime());
}

void ReceiptCreditsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const Data::CreditsHistoryEntry &e,
		const Data::SubscriptionEntry &s) {
	GenericCreditsEntryBox(box, controller->uiShow(), e, s);
}

void GiftedCreditsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> from,
		not_null<PeerData*> to,
		int count,
		TimeId date) {
	const auto received = to->isSelf();
	const auto anonymous = from->isServiceUser();
	const auto peer = received ? from : to;
	using PeerType = Data::CreditsHistoryEntry::PeerType;
	Settings::ReceiptCreditsBox(box, controller, {
		.id = QString(),
		.title = (received
			? tr::lng_credits_box_history_entry_gift_name
			: tr::lng_credits_box_history_entry_gift_sent)(tr::now),
		.date = base::unixtime::parse(date),
		.credits = StarsAmount(count),
		.bareMsgId = uint64(),
		.barePeerId = (anonymous ? uint64() : peer->id.value),
		.peerType = (anonymous ? PeerType::Fragment : PeerType::Peer),
		.in = received,
		.gift = true,
	}, {});
}

void CreditsPrizeBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const Data::GiftCode &data,
		TimeId date) {
	using Type = Data::CreditsHistoryEntry::PeerType;
	Settings::ReceiptCreditsBox(
		box,
		controller,
		Data::CreditsHistoryEntry{
			.id = data.slug,
			.title = QString(),
			.description = TextWithEntities(),
			.date = base::unixtime::parse(date),
			.credits = StarsAmount(data.count),
			.barePeerId = data.channel
				? data.channel->id.value
				: 0,
			.bareGiveawayMsgId = uint64(data.giveawayMsgId.bare),
			.peerType = Type::Peer,
			.in = true,
		},
		Data::SubscriptionEntry());
}

void GlobalStarGiftBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		const Data::StarGift &data,
		CreditsEntryBoxStyleOverrides st) {
	const auto ownerId = data.unique ? data.unique->ownerId.value : 0;
	Settings::GenericCreditsEntryBox(
		box,
		show,
		Data::CreditsHistoryEntry{
			.credits = StarsAmount(data.stars),
			.bareGiftStickerId = data.document->id,
			.bareGiftOwnerId = ownerId,
			.stargiftId = data.id,
			.uniqueGift = data.unique,
			.peerType = Data::CreditsHistoryEntry::PeerType::Peer,
			.limitedCount = data.limitedCount,
			.limitedLeft = data.limitedLeft,
			.stargift = true,
			.fromGiftSlug = true,
			.in = (ownerId == show->session().userPeerId().value),
			.gift = true,
		},
		Data::SubscriptionEntry(),
		st);
}

void SavedStarGiftBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> owner,
		const Data::SavedStarGift &data) {
	const auto chatGiftPeer = data.manageId.chat();
	Settings::ReceiptCreditsBox(
		box,
		controller,
		Data::CreditsHistoryEntry{
			.description = data.message,
			.date = base::unixtime::parse(data.date),
			.credits = StarsAmount(data.info.stars),
			.bareMsgId = uint64(data.manageId.userMessageId().bare),
			.barePeerId = data.fromId.value,
			.bareGiftStickerId = data.info.document->id,
			.bareGiftOwnerId = owner->id.value,
			.bareActorId = data.fromId.value,
			.bareGiftListPeerId = chatGiftPeer ? chatGiftPeer->id.value : 0,
			.giftSavedId = data.manageId.chatSavedId(),
			.stargiftId = data.info.id,
			.uniqueGift = data.info.unique,
			.peerType = Data::CreditsHistoryEntry::PeerType::Peer,
			.limitedCount = data.info.limitedCount,
			.limitedLeft = data.info.limitedLeft,
			.starsConverted = int(data.starsConverted),
			.starsToUpgrade = int(data.info.starsToUpgrade),
			.starsUpgradedBySender = int(data.starsUpgradedBySender),
			.converted = false,
			.anonymous = data.anonymous,
			.stargift = true,
			.savedToProfile = !data.hidden,
			.fromGiftsList = true,
			.canUpgradeGift = data.upgradable,
			.in = data.mine,
			.gift = true,
		},
		Data::SubscriptionEntry());
}

void StarGiftViewBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const Data::GiftCode &data,
		not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const auto toChannel = peer->isServiceUser() && data.channel;
	const auto incoming = !toChannel
		&& (data.upgrade ? item->out() : !item->out());
	const auto fromId = incoming ? peer->id : peer->session().userPeerId();
	const auto toId = incoming ? peer->session().userPeerId() : peer->id;
	const auto entry = Data::CreditsHistoryEntry{
		.id = data.slug,
		.description = data.message,
		.date = base::unixtime::parse(item->date()),
		.credits = StarsAmount(data.count),
		.bareMsgId = uint64(item->id.bare),
		.barePeerId = fromId.value,
		.bareGiftStickerId = data.document ? data.document->id : 0,
		.bareGiftOwnerId = (data.unique
			? data.unique->ownerId.value
			: toId.value),
		.bareActorId = (toChannel ? data.channelFrom->id.value : 0),
		.bareGiftListPeerId = (toChannel ? data.channel->id.value : 0),
		.giftSavedId = data.channelSavedId,
		.stargiftId = data.stargiftId,
		.uniqueGift = data.unique,
		.peerType = Data::CreditsHistoryEntry::PeerType::Peer,
		.limitedCount = data.limitedCount,
		.limitedLeft = data.limitedLeft,
		.starsConverted = data.starsConverted,
		.starsToUpgrade = data.starsToUpgrade,
		.starsUpgradedBySender = data.starsUpgradedBySender,
		.converted = data.converted,
		.anonymous = data.anonymous,
		.stargift = true,
		.giftTransferred = data.transferred,
		.giftRefunded = data.refunded,
		.savedToProfile = data.saved,
		.canUpgradeGift = data.upgradable,
		.hasGiftComment = !data.message.empty(),
		.in = incoming,
		.gift = true,
	};
	Settings::ReceiptCreditsBox(
		box,
		controller,
		entry,
		Data::SubscriptionEntry());
}

void ShowRefundInfoBox(
		not_null<Window::SessionController*> controller,
		FullMsgId refundItemId) {
	const auto owner = &controller->session().data();
	const auto item = owner->message(refundItemId);
	const auto refund = item
		? item->Get<HistoryServicePaymentRefund>()
		: nullptr;
	if (!refund) {
		return;
	}
	Assert(refund->peer != nullptr);
	auto info = Data::CreditsHistoryEntry();
	info.id = refund->transactionId;
	info.date = base::unixtime::parse(item->date());
	info.credits = StarsAmount(refund->amount);
	info.barePeerId = refund->peer->id.value;
	info.peerType = Data::CreditsHistoryEntry::PeerType::Peer;
	info.refunded = true;
	info.in = true;
	controller->show(Box(
		::Settings::ReceiptCreditsBox,
		controller,
		info,
		Data::SubscriptionEntry{}));
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

object_ptr<Ui::RpWidget> SubscriptionUserpic(
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer,
		int photoSize) {
	auto widget = object_ptr<Ui::RpWidget>(parent);
	const auto raw = widget.data();
	widget->resize(photoSize, photoSize);
	const auto userpicMedia = Ui::MakeUserpicThumbnail(peer, false);
	userpicMedia->subscribeToUpdates([=] { raw->update(); });
	const auto creditsIconSize = photoSize / 3;
	const auto creditsIconCallback =
		Ui::PaintOutlinedColoredCreditsIconCallback(
			creditsIconSize,
			1.5);
	widget->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		p.fillRect(Rect(Size(photoSize)), Qt::transparent);
		auto image = userpicMedia->image(photoSize);
		{
			auto q = QPainter(&image);
			q.translate(photoSize, photoSize);
			q.translate(-creditsIconSize, -creditsIconSize);
			creditsIconCallback(q);
		}
		p.drawImage(0, 0, image);
	}, widget->lifetime());
	return widget;
}

void SmallBalanceBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		uint64 wholeCredits,
		SmallBalanceSource source,
		Fn<void()> paid) {
	Expects(show->session().credits().loaded());

	auto credits = StarsAmount(wholeCredits);

	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	const auto done = [=] {
		box->closeBox();
		paid();
	};

	const auto owner = &show->session().data();
	const auto name = v::match(source, [&](SmallBalanceBot value) {
		return value.botId
			? owner->peer(peerFromUser(value.botId))->name()
			: QString();
	}, [&](SmallBalanceReaction value) {
		return owner->peer(peerFromChannel(value.channelId))->name();
	}, [](SmallBalanceSubscription value) {
		return value.name;
	}, [](SmallBalanceDeepLink value) {
		return QString();
	}, [&](SmallBalanceStarGift value) {
		return owner->peer(value.recipientId)->shortName();
	});

	auto needed = show->session().credits().balanceValue(
	) | rpl::map([=](StarsAmount balance) {
		return (balance < credits) ? (credits - balance) : StarsAmount();
	});
	const auto content = [&]() -> Ui::Premium::TopBarAbstract* {
		return box->setPinnedToTopContent(object_ptr<Ui::Premium::TopBar>(
			box,
			st::creditsLowBalancePremiumCover,
			Ui::Premium::TopBarDescriptor{
				.title = tr::lng_credits_small_balance_title(
					lt_count,
					rpl::duplicate(
						needed
					) | rpl::filter(
						rpl::mappers::_1 > StarsAmount(0)
					) | rpl::map([](StarsAmount amount) {
						return amount.value();
					})),
				.about = (v::is<SmallBalanceSubscription>(source)
					? tr::lng_credits_small_balance_subscribe(
						lt_channel,
						rpl::single(Ui::Text::Bold(name)),
						Ui::Text::RichLangValue)
					: v::is<SmallBalanceReaction>(source)
					? tr::lng_credits_small_balance_reaction(
						lt_channel,
						rpl::single(Ui::Text::Bold(name)),
						Ui::Text::RichLangValue)
					: v::is<SmallBalanceDeepLink>(source)
					? DeepLinkBalanceAbout(
						v::get<SmallBalanceDeepLink>(source).purpose)
					: v::is<SmallBalanceStarGift>(source)
					? tr::lng_credits_small_balance_star_gift(
						lt_user,
						rpl::single(Ui::Text::Bold(name)),
						Ui::Text::RichLangValue)
					: name.isEmpty()
					? tr::lng_credits_small_balance_fallback(
						Ui::Text::RichLangValue)
					: tr::lng_credits_small_balance_about(
						lt_bot,
						rpl::single(TextWithEntities{ name }),
						Ui::Text::RichLangValue)),
				.light = true,
				.gradientStops = Ui::Premium::CreditsIconGradientStops(),
			}));
	}();

	FillCreditOptions(
		show,
		box->verticalLayout(),
		show->session().user(),
		credits - show->session().credits().balance(),
		[=] { show->session().credits().load(true); },
		tr::lng_credits_summary_options_subtitle(),
		{});

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
			show->session().credits().balanceValue(),
			true);
		show->session().credits().load(true);

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

	std::move(
		needed
	) | rpl::filter(
		!rpl::mappers::_1
	) | rpl::start_with_next(done, content->lifetime());
}

void AddWithdrawalWidget(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		rpl::producer<QString> secondButtonUrl,
		rpl::producer<StarsAmount> availableBalanceValue,
		rpl::producer<QDateTime> dateValue,
		bool withdrawalEnabled,
		rpl::producer<QString> usdValue) {
	Ui::AddSkip(container);

	const auto labels = container->add(
		object_ptr<Ui::CenterWrap<Ui::RpWidget>>(
			container,
			object_ptr<Ui::RpWidget>(container)))->entity();

	const auto majorLabel = Ui::CreateChild<Ui::FlatLabel>(
		labels,
		rpl::duplicate(
			availableBalanceValue
		) | rpl::map([](StarsAmount v) {
			return Lang::FormatStarsAmountDecimal(v);
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

	const auto withdrawalWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto input = Ui::AddInputFieldForCredits(
		withdrawalWrap->entity(),
		rpl::duplicate(availableBalanceValue));

	Ui::AddSkip(withdrawalWrap->entity());
	Ui::AddSkip(withdrawalWrap->entity());

	const auto &stButton = st::defaultActiveButton;
	const auto buttonsContainer = withdrawalWrap->entity()->add(
		Ui::CreateSkipWidget(withdrawalWrap->entity(), stButton.height),
		st::boxRowPadding);
	withdrawalWrap->toggle(withdrawalEnabled, anim::type::instant);

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

	auto lockedValue = rpl::duplicate(
		dateValue
	) | rpl::map([](const QDateTime &dt) { return !dt.isNull(); });

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
		using Balance = rpl::variable<StarsAmount>;
		const auto currentBalance = input->lifetime().make_state<Balance>(
			rpl::duplicate(availableBalanceValue));
		const auto process = [=] {
			const auto amount = input->getLastText().toDouble();
			if (amount >= currentBalance->current().value()) {
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
	const auto lockedLabel = Ui::CreateChild<Ui::RpWidget>(button);
	lockedLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	struct LockedState final {
		Ui::Text::String text;
		bool locked = false;
		bool dateIsNull = false;
		rpl::lifetime dateUpdateLifetime;
	};
	const auto state = lockedLabel->lifetime().make_state<LockedState>();
	rpl::combine(
		rpl::duplicate(lockedValue),
		button->sizeValue()
	) | rpl::start_with_next([=](bool locked, const QSize &s) {
		state->locked = locked;
		lockedLabel->resize(s);
	}, lockedLabel->lifetime());
	lockedLabel->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(lockedLabel);
		p.setPen(state->locked ? QPen(lockedColor) : stButton.textFg->p);
		if (state->dateIsNull && state->locked) {
			p.setFont(st::channelEarnSemiboldLabel.style.font);
			p.drawText(
				lockedLabel->rect(),
				style::al_center,
				tr::lng_bot_earn_balance_button_locked(tr::now));
			return;
		}
		state->text.draw(p, {
			.position = QPoint(
				0,
				(lockedLabel->height() - state->text.minHeight()) / 2),
			.outerWidth = lockedLabel->width(),
			.availableWidth = lockedLabel->width(),
			.align = style::al_center,
		});
	}, lockedLabel->lifetime());

	std::move(
		dateValue
	) | rpl::start_with_next([=](const QDateTime &dt) {
		state->dateUpdateLifetime.destroy();
		state->dateIsNull = dt.isNull();
		if (dt.isNull()) {
			return;
		}
		constexpr auto kDateUpdateInterval = crl::time(250);
		const auto was = base::unixtime::serialize(dt);

		const auto context = Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [=] { lockedLabel->update(); },
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
			state->text.setMarkedText(
				st::botEarnLockedButtonLabel.style,
				TextWithEntities()
					.append(tr::lng_bot_earn_balance_button_locked(tr::now))
					.append('\n')
					.append(emoji)
					.append(formatted),
				kMarkupTextOptions,
				context);
			lockedLabel->update();
		}, state->dateUpdateLifetime);
	}, lockedLabel->lifetime());

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
			true));
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

void MaybeRequestBalanceIncrease(
		std::shared_ptr<Main::SessionShow> show,
		uint64 credits,
		SmallBalanceSource source,
		Fn<void(SmallBalanceResult)> done) {
	struct State {
		rpl::lifetime lifetime;
		bool success = false;
	};
	const auto state = std::make_shared<State>();

	const auto session = &show->session();
	session->credits().load();
	session->credits().loadedValue(
	) | rpl::filter(rpl::mappers::_1) | rpl::start_with_next([=] {
		state->lifetime.destroy();

		const auto balance = session->credits().balance();
		if (StarsAmount(credits) <= balance) {
			if (const auto onstack = done) {
				onstack(SmallBalanceResult::Already);
			}
		} else if (show->session().premiumPossible()) {
			const auto success = [=] {
				state->success = true;
				if (const auto onstack = done) {
					onstack(SmallBalanceResult::Success);
				}
			};
			const auto box = show->show(Box(
				Settings::SmallBalanceBox,
				show,
				credits,
				source,
				success));
			box->boxClosing() | rpl::start_with_next([=] {
				crl::on_main([=] {
					if (!state->success) {
						if (const auto onstack = done) {
							onstack(SmallBalanceResult::Cancelled);
						}
					}
				});
			}, box->lifetime());
		} else {
			show->showToast(
				tr::lng_credits_purchase_blocked(tr::now));
			if (const auto onstack = done) {
				onstack(SmallBalanceResult::Blocked);
			}
		}
	}, state->lifetime);
}

} // namespace Settings
