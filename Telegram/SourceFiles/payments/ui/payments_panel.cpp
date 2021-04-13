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
#include "ui/text/text_utilities.h"
#include "ui/effects/radial_animation.h"
#include "lang/lang_keys.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "styles/style_payments.h"
#include "styles/style_layers.h"

namespace Payments::Ui {
namespace {

constexpr auto kProgressDuration = crl::time(200);
constexpr auto kProgressOpacity = 0.3;

} // namespace

struct Panel::Progress {
	Progress(QWidget *parent, Fn<QRect()> rect);

	RpWidget widget;
	InfiniteRadialAnimation animation;
	Animations::Simple shownAnimation;
	bool shown = true;
	rpl::lifetime geometryLifetime;
};

Panel::Progress::Progress(QWidget *parent, Fn<QRect()> rect)
: widget(parent)
, animation(
	[=] { if (!anim::Disabled()) widget.update(rect()); },
	st::paymentsLoading) {
}

Panel::Panel(not_null<PanelDelegate*> delegate)
: _delegate(delegate)
, _widget(std::make_unique<SeparatePanel>()) {
	_widget->setInnerSize(st::paymentsPanelSize);
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
	_progress = nullptr;
	_widget = nullptr;
}

void Panel::requestActivate() {
	_widget->showAndActivate();
}

void Panel::toggleProgress(bool shown) {
	if (!_progress) {
		if (!shown) {
			return;
		}
		_progress = std::make_unique<Progress>(
			_widget.get(),
			[=] { return progressRect(); });
		_progress->widget.paintRequest(
		) | rpl::start_with_next([=](QRect clip) {
			auto p = QPainter(&_progress->widget);
			p.setOpacity(
				_progress->shownAnimation.value(_progress->shown ? 1. : 0.));
			auto thickness = st::paymentsLoading.thickness;
			if (progressWithBackground()) {
				auto color = st::windowBg->c;
				color.setAlphaF(kProgressOpacity);
				p.fillRect(clip, color);
			}
			const auto rect = progressRect().marginsRemoved(
				{ thickness, thickness, thickness, thickness });
			InfiniteRadialAnimation::Draw(
				p,
				_progress->animation.computeState(),
				rect.topLeft(),
				rect.size() - QSize(),
				_progress->widget.width(),
				st::paymentsLoading.color,
				thickness);
		}, _progress->widget.lifetime());
		_progress->widget.show();
		_progress->animation.start();
	} else if (_progress->shown == shown) {
		return;
	}
	const auto callback = [=] {
		if (!_progress->shownAnimation.animating() && !_progress->shown) {
			_progress = nullptr;
		} else {
			_progress->widget.update();
		}
	};
	_progress->shown = shown;
	_progress->shownAnimation.start(
		callback,
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		kProgressDuration);
	if (shown) {
		setupProgressGeometry();
	}
}

bool Panel::progressWithBackground() const {
	return (_progress->widget.width() == _widget->innerGeometry().width());
}

QRect Panel::progressRect() const {
	const auto rect = _progress->widget.rect();
	if (!progressWithBackground()) {
		return rect;
	}
	const auto size = st::defaultBoxButton.height;
	return QRect(
		rect.x() + (rect.width() - size) / 2,
		rect.y() + (rect.height() - size) / 2,
		size,
		size);
}

void Panel::setupProgressGeometry() {
	if (!_progress || !_progress->shown) {
		return;
	}
	_progress->geometryLifetime.destroy();
	if (_webviewBottom) {
		_webviewBottom->geometryValue(
		) | rpl::start_with_next([=](QRect bottom) {
			const auto height = bottom.height();
			const auto size = st::paymentsLoading.size;
			const auto skip = (height - size.height()) / 2;
			const auto inner = _widget->innerGeometry();
			const auto right = inner.x() + inner.width();
			const auto top = inner.y() + inner.height() - height;
			// This doesn't work, because first we get the correct bottom
			// geometry and after that we get the previous event (which
			// triggered the 'fire' of correct geometry before getting here).
			//const auto right = bottom.x() + bottom.width();
			//const auto top = bottom.y();
			_progress->widget.setGeometry(QRect{
				QPoint(right - skip - size.width(), top + skip),
				size });
		}, _progress->geometryLifetime);
	} else if (_weakFormSummary) {
		_weakFormSummary->sizeValue(
		) | rpl::start_with_next([=](QSize form) {
			const auto full = _widget->innerGeometry();
			const auto size = st::defaultBoxButton.height;
			const auto inner = _weakFormSummary->contentHeight();
			const auto left = full.height() - inner;
			if (left >= 2 * size) {
				_progress->widget.setGeometry(
					full.x() + (full.width() - size) / 2,
					full.y() + inner + (left - size) / 2,
					size,
					size);
			} else {
				_progress->widget.setGeometry(full);
			}
		}, _progress->geometryLifetime);
	} else if (_weakEditInformation) {
		_weakEditInformation->geometryValue(
		) | rpl::start_with_next([=] {
			_progress->widget.setGeometry(_widget->innerGeometry());
		}, _progress->geometryLifetime);
	} else if (_weakEditCard) {
		_weakEditCard->geometryValue(
		) | rpl::start_with_next([=] {
			_progress->widget.setGeometry(_widget->innerGeometry());
		}, _progress->geometryLifetime);
	}
	_progress->widget.show();
	_progress->widget.raise();
	if (_progress->shown) {
		_progress->widget.setFocus();
	}
}

void Panel::showForm(
		const Invoice &invoice,
		const RequestedInformation &current,
		const PaymentMethodDetails &method,
		const ShippingOptions &options) {
	if (invoice && !method.ready && !method.native.supported) {
		const auto available = Webview::Availability();
		if (available.error != Webview::Available::Error::None) {
			showWebviewError(
				tr::lng_payments_webview_no_use(tr::now),
				available);
			return;
		}
	}

	_testMode = invoice.isTest;
	setTitle(invoice.receipt
		? tr::lng_payments_receipt_title()
		: tr::lng_payments_checkout_title());
	auto form = base::make_unique_q<FormSummary>(
		_widget.get(),
		invoice,
		current,
		method,
		options,
		_delegate,
		_formScrollTop.current());
	_weakFormSummary = form.get();
	_widget->showInner(std::move(form));
	_widget->setBackAllowed(false);
	_formScrollTop = _weakFormSummary->scrollTopValue();
	setupProgressGeometry();
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
	setTitle(tr::lng_payments_shipping_address_title());
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
	setupProgressGeometry();
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
	showBox(Box([=](not_null<GenericBox*> box) {
		const auto i = ranges::find(
			options.list,
			options.selectedId,
			&ShippingOption::id);
		const auto index = (i != end(options.list))
			? int(i - begin(options.list))
			: -1;
		const auto group = std::make_shared<RadiobuttonGroup>(index);

		const auto layout = box->verticalLayout();
		auto counter = 0;
		for (const auto &option : options.list) {
			const auto index = counter++;
			const auto button = layout->add(
				object_ptr<Radiobutton>(
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
	showBox(Box([=](not_null<GenericBox*> box) {
		box->setTitle(tr::lng_payments_tips_box_title());
		const auto row = box->lifetime().make_state<Field>(
			box,
			FieldConfig{
				.type = FieldType::Money,
				.value = QString::number(now),
				.currency = currency,
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
		const auto submit = [=] {
			const auto value = row->value().toLongLong();
			if (value > max) {
				row->showError();
				errorWrap->show(anim::type::normal);
			} else {
				_delegate->panelChangeTips(value);
				box->closeBox();
			}
		};
		row->submitted(
		) | rpl::start_with_next(submit, box->lifetime());
		box->addButton(tr::lng_settings_save(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void Panel::showEditPaymentMethod(const PaymentMethodDetails &method) {
	auto bottomText = method.canSaveInformation
		? rpl::producer<QString>()
		: tr::lng_payments_processed_by(
			lt_provider,
			rpl::single(method.provider));
	setTitle(tr::lng_payments_card_title());
	if (method.native.supported) {
		showEditCard(method.native, CardField::Number);
	} else if (!showWebview(method.url, true, std::move(bottomText))) {
		const auto available = Webview::Availability();
		if (available.error != Webview::Available::Error::None) {
			showWebviewError(
				tr::lng_payments_webview_no_card(tr::now),
				available);
		} else {
			showCriticalError({ "Error: Could not initialize WebView." });
		}
		_widget->setBackAllowed(true);
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

void Panel::showWebviewProgress() {
	if (_webviewProgress && _progress && _progress->shown) {
		return;
	}
	_webviewProgress = true;
	toggleProgress(true);
}

void Panel::hideWebviewProgress() {
	if (!_webviewProgress) {
		return;
	}
	_webviewProgress = false;
	toggleProgress(false);
}

bool Panel::showWebview(
		const QString &url,
		bool allowBack,
		rpl::producer<QString> bottomText) {
	if (!_webview && !createWebview()) {
		return false;
	}
	showWebviewProgress();
	_widget->destroyLayer();
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
			if (_webviewProgress) {
				hideWebviewProgress();
				if (_progress && !_progress->shown) {
					_progress = nullptr;
				}
			}
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

	raw->setNavigationStartHandler([=](const QString &uri) {
		if (!_delegate->panelWebviewNavigationAttempt(uri)) {
			return false;
		}
		showWebviewProgress();
		return true;
	});
	raw->setNavigationDoneHandler([=](bool success) {
		hideWebviewProgress();
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

	setupProgressGeometry();

	return true;
}

void Panel::choosePaymentMethod(const PaymentMethodDetails &method) {
	if (!method.ready) {
		showEditPaymentMethod(method);
		return;
	}
	showBox(Box([=](not_null<GenericBox*> box) {
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
	showBox(Box([=](not_null<GenericBox*> box) {
		box->addRow(
			object_ptr<FlatLabel>(
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

void Panel::showCloseConfirm() {
	showBox(Box([=](not_null<GenericBox*> box) {
		box->addRow(
			object_ptr<FlatLabel>(
				box.get(),
				tr::lng_payments_sure_close(),
				st::boxLabel),
			st::boxPadding);
		box->addButton(tr::lng_close(), [=] {
			_delegate->panelCloseSure();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void Panel::showWarning(const QString &bot, const QString &provider) {
	showBox(Box([=](not_null<GenericBox*> box) {
		box->setTitle(tr::lng_payments_warning_title());
		box->addRow(object_ptr<FlatLabel>(
			box.get(),
			tr::lng_payments_warning_body(
				lt_bot1,
				rpl::single(bot),
				lt_provider,
				rpl::single(provider),
				lt_bot2,
				rpl::single(bot),
				lt_bot3,
				rpl::single(bot)),
			st::boxLabel));
		box->addButton(tr::lng_continue(), [=] {
			_delegate->panelTrustAndSubmit();
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
	setupProgressGeometry();
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

void Panel::setTitle(rpl::producer<QString> title) {
	using namespace rpl::mappers;
	if (_testMode) {
		_widget->setTitle(std::move(title) | rpl::map(_1 + " (Test)"));
	} else {
		_widget->setTitle(std::move(title));
	}
}

rpl::producer<> Panel::backRequests() const {
	return _widget->backRequests();
}

void Panel::showBox(object_ptr<BoxContent> box) {
	_widget->showBox(
		std::move(box),
		LayerOption::KeepOther,
		anim::type::normal);
}

void Panel::showToast(const TextWithEntities &text) {
	_widget->showToast(text);
}

void Panel::showCriticalError(const TextWithEntities &text) {
	_progress = nullptr;
	_webviewProgress = false;
	if (!_weakFormSummary || !_weakFormSummary->showCriticalError(text)) {
		auto error = base::make_unique_q<PaddingWrap<FlatLabel>>(
			_widget.get(),
			object_ptr<FlatLabel>(
				_widget.get(),
				rpl::single(text),
				st::paymentsCriticalError),
			st::paymentsCriticalErrorPadding);
		error->entity()->setClickHandlerFilter([=](
				const ClickHandlerPtr &handler,
				Qt::MouseButton) {
			const auto entity = handler->getTextEntity();
			if (entity.type != EntityType::CustomUrl) {
				return true;
			}
			_delegate->panelOpenUrl(entity.data);
			return false;
		});
		_widget->showInner(std::move(error));
	}
}

void Panel::showWebviewError(
		const QString &text,
		const Webview::Available &information) {
	using Error = Webview::Available::Error;
	Expects(information.error != Error::None);

	auto rich = TextWithEntities{ text };
	rich.append("\n\n");
	switch (information.error) {
	case Error::NoWebview2: {
		const auto command = QString(QChar(TextCommand));
		const auto text = tr::lng_payments_webview_install_edge(
			tr::now,
			lt_link,
			command);
		const auto parts = text.split(command);
		rich.append(parts.value(0))
			.append(Text::Link(
				"Microsoft Edge WebView2 Runtime",
				"https://go.microsoft.com/fwlink/p/?LinkId=2124703"))
			.append(parts.value(1));
	} break;
	case Error::NoGtkOrWebkit2Gtk:
		rich.append(tr::lng_payments_webview_install_webkit(tr::now));
		break;
	case Error::MutterWM:
		rich.append(tr::lng_payments_webview_switch_mutter(tr::now));
		break;
	case Error::Wayland:
		rich.append(tr::lng_payments_webview_switch_wayland(tr::now));
		break;
	default:
		rich.append(QString::fromStdString(information.details));
		break;
	}
	showCriticalError(rich);
}

rpl::lifetime &Panel::lifetime() {
	return _widget->lifetime();
}

} // namespace Payments::Ui
