/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_panel.h"

#include "payments/ui/payments_form_summary.h"
#include "payments/ui/payments_edit_information.h"
#include "payments/ui/payments_edit_card.h"
#include "payments/ui/payments_panel_delegate.h"
#include "payments/ui/payments_field.h"
#include "ui/widgets/separate_panel.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/text/format_values.h"
#include "lang/lang_keys.h"
#include "webview/webview_embed.h"
#include "styles/style_payments.h"
#include "styles/style_passport.h"
#include "styles/style_layers.h"

namespace Payments::Ui {

Panel::Panel(not_null<PanelDelegate*> delegate)
: _delegate(delegate)
, _widget(std::make_unique<SeparatePanel>()) {
	_widget->setInnerSize(st::passportPanelSize);
	_widget->setWindowFlag(Qt::WindowStaysOnTopHint, false);

	_widget->closeRequests(
	) | rpl::start_with_next([=] {
		_delegate->panelRequestClose();
	}, _widget->lifetime());

	_widget->closeEvents(
	) | rpl::start_with_next([=] {
		_delegate->panelCloseSure();
	}, _widget->lifetime());
}

Panel::~Panel() {
	// Destroy _widget before _webview.
	_widget = nullptr;
}

void Panel::requestActivate() {
	_widget->showAndActivate();
}

void Panel::showForm(
		const Invoice &invoice,
		const RequestedInformation &current,
		const PaymentMethodDetails &method,
		const ShippingOptions &options) {
	_widget->setTitle(invoice.receipt
		? tr::lng_payments_receipt_title()
		: tr::lng_payments_checkout_title());
	auto form = base::make_unique_q<FormSummary>(
		_widget.get(),
		invoice,
		current,
		method,
		options,
		_delegate);
	_weakFormSummary = form.get();
	_widget->showInner(std::move(form));
	_widget->setBackAllowed(false);
}

void Panel::updateFormThumbnail(const QImage &thumbnail) {
	if (_weakFormSummary) {
		_weakFormSummary->updateThumbnail(thumbnail);
	}
}

void Panel::showEditInformation(
		const Invoice &invoice,
		const RequestedInformation &current,
		InformationField field) {
	_widget->setTitle(tr::lng_payments_shipping_address_title());
	auto edit = base::make_unique_q<EditInformation>(
		_widget.get(),
		invoice,
		current,
		field,
		_delegate);
	_weakEditInformation = edit.get();
	_widget->showInner(std::move(edit));
	_widget->setBackAllowed(true);
	_weakEditInformation->setFocusFast(field);
}

void Panel::showInformationError(
		const Invoice &invoice,
		const RequestedInformation &current,
		InformationField field) {
	if (_weakEditInformation) {
		_weakEditInformation->showError(field);
	} else {
		showEditInformation(invoice, current, field);
		if (_weakEditInformation
			&& field == InformationField::ShippingCountry) {
			_weakEditInformation->showError(field);
		}
	}
}

void Panel::chooseShippingOption(const ShippingOptions &options) {
	showBox(Box([=](not_null<Ui::GenericBox*> box) {
		const auto i = ranges::find(
			options.list,
			options.selectedId,
			&ShippingOption::id);
		const auto index = (i != end(options.list))
			? (i - begin(options.list))
			: -1;
		const auto group = std::make_shared<Ui::RadiobuttonGroup>(index);

		const auto layout = box->verticalLayout();
		auto counter = 0;
		for (const auto &option : options.list) {
			const auto index = counter++;
			const auto button = layout->add(
				object_ptr<Ui::Radiobutton>(
					layout,
					group,
					index,
					QString(),
					st::defaultBoxCheckbox,
					st::defaultRadio),
				st::paymentsShippingMargin);
			const auto label = CreateChild<FlatLabel>(
				layout.get(),
				option.title,
				st::paymentsShippingLabel);
			const auto total = ranges::accumulate(
				option.prices,
				int64(0),
				std::plus<>(),
				&LabeledPrice::price);
			const auto price = CreateChild<FlatLabel>(
				layout.get(),
				FillAmountAndCurrency(total, options.currency),
				st::paymentsShippingPrice);
			const auto area = CreateChild<AbstractButton>(layout.get());
			area->setClickedCallback([=] { group->setValue(index); });
			button->geometryValue(
			) | rpl::start_with_next([=](QRect geometry) {
				label->move(
					geometry.topLeft() + st::paymentsShippingLabelPosition);
				price->move(
					geometry.topLeft() + st::paymentsShippingPricePosition);
				const auto right = geometry.x()
					+ st::paymentsShippingLabelPosition.x();
				area->setGeometry(
					right,
					geometry.y(),
					std::max(
						label->x() + label->width() - right,
						price->x() + price->width() - right),
					price->y() + price->height() - geometry.y());
			}, button->lifetime());
		}

		box->setTitle(tr::lng_payments_shipping_method());
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		group->setChangedCallback([=](int index) {
			if (index >= 0) {
				_delegate->panelChangeShippingOption(
					options.list[index].id);
				box->closeBox();
			}
		});
	}));
}

void Panel::chooseTips(const Invoice &invoice) {
	const auto max = invoice.tipsMax;
	const auto now = invoice.tipsSelected;
	const auto currency = invoice.currency;
	showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_payments_tips_box_title());
		const auto row = box->lifetime().make_state<Field>(
			box,
			FieldConfig{
				.type = FieldType::Money,
				.value = QString::number(now),
				.currency = ([&]() -> QString {
					static auto counter = 0;
					switch (++counter % 9) {
					case 0: return "USD";
					case 1: return "EUR";
					case 2: return "IRR";
					case 3: return "BRL";
					case 4: return "ALL";
					case 5: return "AZN";
					case 6: return "CHF";
					case 7: return "DKK";
					case 8: return "KZT";
					}
					return currency;
				})(), // #TODO payments currency,
			});
		box->setFocusCallback([=] {
			row->setFocusFast();
		});
		box->addRow(row->ownedWidget());
		const auto errorWrap = box->addRow(
			object_ptr<FadeWrap<FlatLabel>>(
				box,
				object_ptr<FlatLabel>(
					box,
					tr::lng_payments_tips_max(
						lt_amount,
						rpl::single(FillAmountAndCurrency(max, currency))),
					st::paymentTipsErrorLabel)),
			st::paymentTipsErrorPadding);
		errorWrap->hide(anim::type::instant);
		box->addButton(tr::lng_settings_save(), [=] {
			const auto value = row->value().toLongLong();
			if (value > max) {
				row->showError();
				errorWrap->show(anim::type::normal);
			} else {
				_delegate->panelChangeTips(value);
				box->closeBox();
			}
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void Panel::showEditPaymentMethod(const PaymentMethodDetails &method) {
	auto bottomText = method.canSaveInformation
		? rpl::producer<QString>()
		: tr::lng_payments_processed_by(
			lt_provider,
			rpl::single(method.provider));
	_widget->setTitle(tr::lng_payments_card_title());
	if (method.native.supported) {
		showEditCard(method.native, CardField::Number);
	} else if (!showWebview(method.url, true, std::move(bottomText))) {
		// #TODO payments errors not supported
	} else if (method.canSaveInformation) {
		const auto &padding = st::paymentsPanelPadding;
		_saveWebviewInformation = CreateChild<Checkbox>(
			_webviewBottom.get(),
			tr::lng_payments_save_information(tr::now),
			false);
		const auto height = padding.top()
			+ _saveWebviewInformation->heightNoMargins()
			+ padding.bottom();
		_saveWebviewInformation->moveToLeft(padding.right(), padding.top());
		_saveWebviewInformation->show();
		_webviewBottom->resize(_webviewBottom->width(), height);
	}
}

bool Panel::showWebview(
		const QString &url,
		bool allowBack,
		rpl::producer<QString> bottomText) {
	if (!_webview && !createWebview()) {
		return false;
	}
	_webview->navigate(url);
	_widget->setBackAllowed(allowBack);
	if (bottomText) {
		const auto &padding = st::paymentsPanelPadding;
		const auto label = CreateChild<FlatLabel>(
			_webviewBottom.get(),
			std::move(bottomText),
			st::paymentsWebviewBottom);
		const auto height = padding.top()
			+ label->heightNoMargins()
			+ padding.bottom();
		rpl::combine(
			_webviewBottom->widthValue(),
			label->widthValue()
		) | rpl::start_with_next([=](int outerWidth, int width) {
			label->move((outerWidth - width) / 2, padding.top());
		}, label->lifetime());
		label->show();
		_webviewBottom->resize(_webviewBottom->width(), height);
	}
	return true;
}

bool Panel::createWebview() {
	auto container = base::make_unique_q<RpWidget>(_widget.get());

	_webviewBottom = std::make_unique<RpWidget>(_widget.get());
	const auto bottom = _webviewBottom.get();
	bottom->show();

	bottom->heightValue(
	) | rpl::start_with_next([=, raw = container.get()](int height) {
		const auto inner = _widget->innerGeometry();
		bottom->move(inner.x(), inner.y() + inner.height() - height);
		raw->resize(inner.width(), inner.height() - height);
		bottom->resizeToWidth(inner.width());
	}, bottom->lifetime());
	container->show();

	_webview = std::make_unique<Webview::Window>(
		container.get(),
		Webview::WindowConfig{
			.userDataPath = _delegate->panelWebviewDataPath(),
		});
	const auto raw = _webview.get();
	QObject::connect(container.get(), &QObject::destroyed, [=] {
		if (_webview.get() == raw) {
			_webview = nullptr;
		}
		if (_webviewBottom.get() == bottom) {
			_webviewBottom = nullptr;
		}
	});
	if (!raw->widget()) {
		return false;
	}

	container->geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		raw->widget()->setGeometry(geometry);
	}, container->lifetime());

	raw->setMessageHandler([=](const QJsonDocument &message) {
		const auto save = _saveWebviewInformation
			&& _saveWebviewInformation->checked();
		_delegate->panelWebviewMessage(message, save);
	});

	raw->setNavigationHandler([=](const QString &uri) {
		return _delegate->panelWebviewNavigationAttempt(uri);
	});

	raw->init(R"(
window.TelegramWebviewProxy = {
postEvent: function(eventType, eventData) {
	if (window.external && window.external.invoke) {
		window.external.invoke(JSON.stringify([eventType, eventData]));
	}
}
};)");

	_widget->showInner(std::move(container));
	return true;
}

void Panel::choosePaymentMethod(const PaymentMethodDetails &method) {
	if (!method.ready) {
		showEditPaymentMethod(method);
		return;
	}
	showBox(Box([=](not_null<Ui::GenericBox*> box) {
		const auto save = [=](int option) {
			if (option) {
				showEditPaymentMethod(method);
			}
		};
		SingleChoiceBox(box, {
			.title = tr::lng_payments_payment_method(),
			.options = { method.title, tr::lng_payments_new_card(tr::now) },
			.initialSelection = 0,
			.callback = save,
		});
	}));
}

void Panel::askSetPassword() {
	showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box.get(),
				tr::lng_payments_need_password(),
				st::boxLabel),
			st::boxPadding);
		box->addButton(tr::lng_continue(), [=] {
			_delegate->panelSetPassword();
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void Panel::showEditCard(
		const NativeMethodDetails &native,
		CardField field) {
	Expects(native.supported);

	auto edit = base::make_unique_q<EditCard>(
		_widget.get(),
		native,
		field,
		_delegate);
	_weakEditCard = edit.get();
	_widget->showInner(std::move(edit));
	_widget->setBackAllowed(true);
	_weakEditCard->setFocusFast(field);
}

void Panel::showCardError(
		const NativeMethodDetails &native,
		CardField field) {
	if (_weakEditCard) {
		_weakEditCard->showError(field);
	} else {
		// We cancelled card edit already.
		//showEditCard(native, field);
		//if (_weakEditCard
		//	&& field == CardField::AddressCountry) {
		//	_weakEditCard->showError(field);
		//}
	}
}

rpl::producer<> Panel::backRequests() const {
	return _widget->backRequests();
}

void Panel::showBox(object_ptr<Ui::BoxContent> box) {
	_widget->showBox(
		std::move(box),
		Ui::LayerOption::KeepOther,
		anim::type::normal);
}

void Panel::showToast(const TextWithEntities &text) {
	_widget->showToast(text);
}

rpl::lifetime &Panel::lifetime() {
	return _widget->lifetime();
}

} // namespace Payments::Ui
