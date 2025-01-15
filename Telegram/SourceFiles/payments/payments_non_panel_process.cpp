/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/payments_non_panel_process.h"

#include "api/api_credits.h"
#include "base/unixtime.h"
#include "boxes/send_credits_box.h"
#include "data/components/credits.h"
#include "data/data_credits.h"
#include "data/data_photo.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "payments/payments_checkout_process.h" // NonPanelPaymentForm.
#include "payments/payments_form.h"
#include "settings/settings_credits_graphics.h"
#include "ui/boxes/boost_box.h" // Ui::StartFireworks.
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Payments {

bool IsCreditsInvoice(not_null<HistoryItem*> item) {
	if (const auto payment = item->Get<HistoryServicePayment>()) {
		return payment->isCreditsCurrency;
	}
	const auto media = item->media();
	const auto invoice = media ? media->invoice() : nullptr;
	return invoice && (invoice->currency == Ui::kCreditsCurrency);
}

void ProcessCreditsPayment(
		std::shared_ptr<Main::SessionShow> show,
		QPointer<QWidget> fireworks,
		std::shared_ptr<CreditsFormData> form,
		Fn<void(CheckoutResult)> maybeReturnToBot) {
	const auto done = [=](Settings::SmallBalanceResult result) {
		if (result == Settings::SmallBalanceResult::Blocked) {
			if (const auto onstack = maybeReturnToBot) {
				onstack(CheckoutResult::Failed);
			}
			return;
		} else if (result == Settings::SmallBalanceResult::Cancelled) {
			if (const auto onstack = maybeReturnToBot) {
				onstack(CheckoutResult::Cancelled);
			}
			return;
		} else if (form->starGiftForm) {
			const auto done = [=](std::optional<QString> error) {
				const auto onstack = maybeReturnToBot;
				if (error) {
					if (*error == u"STARGIFT_USAGE_LIMITED"_q) {
						if (form->starGiftLimitedCount) {
							show->showToast({
								.title = tr::lng_gift_sold_out_title(
									tr::now),
								.text = tr::lng_gift_sold_out_text(
									tr::now,
									lt_count_decimal,
									form->starGiftLimitedCount,
									Ui::Text::RichLangValue),
							});
						} else {
							show->showToast(
								tr::lng_gift_sold_out_title(tr::now));
						}
					} else {
						show->showToast(*error);
					}
					if (onstack) {
						onstack(CheckoutResult::Failed);
					}
				} else if (onstack) {
					onstack(CheckoutResult::Paid);
				}
			};
			Ui::SendStarGift(&show->session(), form, done);
			return;
		}
		const auto unsuccessful = std::make_shared<bool>(true);
		const auto box = show->show(Box(
			Ui::SendCreditsBox,
			form,
			[=] {
				*unsuccessful = false;
				if (const auto widget = fireworks.data()) {
					Ui::StartFireworks(widget);
				}
				if (const auto onstack = maybeReturnToBot) {
					onstack(CheckoutResult::Paid);
				}
			}));
		box->boxClosing() | rpl::start_with_next([=] {
			crl::on_main([=] {
				if (*unsuccessful) {
					if (const auto onstack = maybeReturnToBot) {
						onstack(CheckoutResult::Cancelled);
					}
				}
			});
		}, box->lifetime());
	};
	using namespace Settings;
	const auto starGift = std::get_if<InvoiceStarGift>(&form->id.value);
	auto source = !starGift
		? SmallBalanceSource(SmallBalanceBot{ .botId = form->botId })
		: SmallBalanceSource(SmallBalanceStarGift{
			.recipientId = starGift->recipient->id,
		});
	MaybeRequestBalanceIncrease(show, form->invoice.credits, source, done);
}

void ProcessCreditsReceipt(
		not_null<Window::SessionController*> controller,
		std::shared_ptr<CreditsReceiptData> receipt,
		Fn<void(CheckoutResult)> maybeReturnToBot) {
	const auto entry = Data::CreditsHistoryEntry{
		.id = receipt->id,
		.title = receipt->title,
		.description = { receipt->description },
		.date = base::unixtime::parse(receipt->date),
		.photoId = receipt->photo ? receipt->photo->id : 0,
		.credits = receipt->credits,
		.bareMsgId = uint64(),
		.barePeerId = receipt->peerId.value,
		.peerType = Data::CreditsHistoryEntry::PeerType::Peer,
	};
	controller->uiShow()->show(Box(
		Settings::ReceiptCreditsBox,
		controller,
		entry,
		Data::SubscriptionEntry{}));
	controller->window().activate();
}

Fn<void(NonPanelPaymentForm)> ProcessNonPanelPaymentFormFactory(
		not_null<Window::SessionController*> controller,
		Fn<void(CheckoutResult)> maybeReturnToBot) {
	return [=](NonPanelPaymentForm form) {
		using CreditsFormDataPtr = std::shared_ptr<CreditsFormData>;
		using CreditsReceiptPtr = std::shared_ptr<CreditsReceiptData>;
		v::match(form, [&](const CreditsFormDataPtr &form) {
			ProcessCreditsPayment(
				controller->uiShow(),
				controller->content().get(),
				form,
				maybeReturnToBot);
			controller->window().activate();
		}, [&](const CreditsReceiptPtr &receipt) {
			ProcessCreditsReceipt(controller, receipt, maybeReturnToBot);
		}, [](RealFormPresentedNotification) {});
	};
}

Fn<void(NonPanelPaymentForm)> ProcessNonPanelPaymentFormFactory(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	return IsCreditsInvoice(item)
		? ProcessNonPanelPaymentFormFactory(controller)
		: nullptr;
}

} // namespace Payments
