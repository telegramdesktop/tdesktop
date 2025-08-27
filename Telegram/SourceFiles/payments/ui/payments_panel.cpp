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
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/radial_animation.h"
#include "ui/click_handler.h"
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

struct Panel::WebviewWithLifetime {
	WebviewWithLifetime(
		QWidget *parent = nullptr,
		Webview::WindowConfig config = Webview::WindowConfig());

	Webview::Window window;
	QPointer<RpWidget> lastHidingBox;
	rpl::lifetime lifetime;
};

Panel::WebviewWithLifetime::WebviewWithLifetime(
	QWidget *parent,
	Webview::WindowConfig config)
: window(parent, std::move(config)) {
}

Panel::Progress::Progress(QWidget *parent, Fn<QRect()> rect)
: widget(parent)
, animation(
	[=] { if (!anim::Disabled()) widget.update(rect()); },
	st::paymentsLoading) {
}

Panel::Panel(not_null<PanelDelegate*> delegate)
: _delegate(delegate)
, _widget(std::make_unique<SeparatePanel>()) {
	_widget->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	_widget->setInnerSize(st::paymentsPanelSize);

	_widget->closeRequests(
	) | rpl::start_with_next([=] {
		_delegate->panelRequestClose();
	}, _widget->lifetime());

	_widget->closeEvents(
	) | rpl::start_with_next([=] {
		_delegate->panelCloseSure();
	}, _widget->lifetime());

	style::PaletteChanged(
	) | rpl::filter([=] {
		return !_themeUpdateScheduled;
	}) | rpl::start_with_next([=] {
		_themeUpdateScheduled = true;
		crl::on_main(_widget.get(), [=] {
			_themeUpdateScheduled = false;
			updateThemeParams(_delegate->panelWebviewThemeParams());
		});
	}, lifetime());
}

Panel::~Panel() {
	base::take(_webview);
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
	if (invoice
		&& method.savedMethods.empty()
		&& !method.native.supported) {
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
	setTitle(tr::lng_payments_card_title());
	if (method.native.supported) {
		showEditCard(method.native, CardField::Number);
	} else {
		showEditCardByUrl(
			method.url,
			method.provider,
			method.canSaveInformation);
	}
}

void Panel::showEditCardByUrl(
		const QString &url,
		const QString &provider,
		bool canSaveInformation) {
	auto bottomText = canSaveInformation
		? rpl::producer<QString>()
		: tr::lng_payments_processed_by(lt_provider, rpl::single(provider));
	if (!showWebview(url, true, std::move(bottomText))) {
		const auto available = Webview::Availability();
		if (available.error != Webview::Available::Error::None) {
			showWebviewError(
				tr::lng_payments_webview_no_use(tr::now),
				available);
		} else {
			showCriticalError({ "Error: Could not initialize WebView." });
		}
		_widget->setBackAllowed(true);
	} else if (canSaveInformation) {
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

void Panel::showAdditionalMethod(
		const PaymentMethodAdditional &method,
		const QString &provider,
		bool canSaveInformation) {
	setTitle(rpl::single(method.title));
	showEditCardByUrl(method.url, provider, canSaveInformation);
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
	const auto params = _delegate->panelWebviewThemeParams();
	if (!_webview && !createWebview(params)) {
		return false;
	}
	showWebviewProgress();
	_widget->hideLayer(anim::type::instant);
	updateThemeParams(params);
	_webview->window.navigate(url);
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

bool Panel::createWebview(const Webview::ThemeParams &params) {
	auto outer = base::make_unique_q<RpWidget>(_widget.get());
	const auto container = outer.get();
	_widget->showInner(std::move(outer));
	const auto webviewParent = QPointer<RpWidget>(container);

	_webviewBottom = std::make_unique<RpWidget>(_widget.get());
	const auto bottom = _webviewBottom.get();
	bottom->show();

	rpl::combine(
		container->geometryValue() | rpl::map([=] {
			return _widget->innerGeometry();
		}),
		bottom->heightValue()
	) | rpl::start_with_next([=](QRect inner, int height) {
		bottom->move(inner.x(), inner.y() + inner.height() - height);
		bottom->resizeToWidth(inner.width());
		_footerHeight = bottom->height();
	}, bottom->lifetime());
	container->show();

	_webview = std::make_unique<WebviewWithLifetime>(
		container,
		Webview::WindowConfig{
			.opaqueBg = params.bodyBg,
			.storageId = _delegate->panelWebviewStorageId(),
		});

	const auto raw = &_webview->window;
	QObject::connect(container, &QObject::destroyed, [=] {
		if (_webview && &_webview->window == raw) {
			base::take(_webview);
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
	QObject::connect(raw->widget(), &QObject::destroyed, [=] {
		const auto parent = webviewParent.data();
		if (!_webview
			|| &_webview->window != raw
			|| !parent
			|| _widget->inner() != parent) {
			// If we destroyed _webview ourselves,
			// or if we changed _widget->inner ourselves,
			// we don't show any message, nothing crashed.
			return;
		}
		crl::on_main(this, [=] {
			showCriticalError({ "Error: WebView has crashed." });
		});
	});

	rpl::combine(
		container->geometryValue(),
		_footerHeight.value()
	) | rpl::start_with_next([=](QRect geometry, int footer) {
		if (const auto view = raw->widget()) {
			view->setGeometry(geometry.marginsRemoved({ 0, 0, 0, footer }));
		}
	}, _webview->lifetime);

	raw->setMessageHandler([=](const QJsonDocument &message) {
		const auto save = _saveWebviewInformation
			&& _saveWebviewInformation->checked();
		_delegate->panelWebviewMessage(message, save);
	});

	raw->setNavigationStartHandler([=](const QString &uri, bool newWindow) {
		if (!_delegate->panelWebviewNavigationAttempt(uri)) {
			return false;
		} else if (newWindow) {
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

	if (!_webview) {
		return false;
	}

	setupProgressGeometry();

	return true;
}

void Panel::choosePaymentMethod(const PaymentMethodDetails &method) {
	if (method.savedMethods.empty() && method.additionalMethods.empty()) {
		showEditPaymentMethod(method);
		return;
	}
	showBox(Box([=](not_null<GenericBox*> box) {
		const auto save = [=](int option) {
			const auto saved = int(method.savedMethods.size());
			if (!option) {
				showEditPaymentMethod(method);
			} else if (option > saved) {
				const auto index = option - saved - 1;
				Assert(index < method.additionalMethods.size());
				showAdditionalMethod(
					method.additionalMethods[index],
					method.provider,
					method.canSaveInformation);
			} else {
				const auto index = option - 1;
				_savedMethodChosen.fire_copy(method.savedMethods[index].id);
			}
		};
		auto options = std::vector{
			tr::lng_payments_new_card(tr::now),
		};
		for (const auto &saved : method.savedMethods) {
			options.push_back(saved.title);
		}
		for (const auto &additional : method.additionalMethods) {
			options.push_back(additional.title);
		}
		SingleChoiceBox(box, {
			.title = tr::lng_payments_payment_method(),
			.options = std::move(options),
			.initialSelection = (method.savedMethods.empty()
				? -1
				: (method.savedMethodIndex + 1)),
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

void Panel::requestTermsAcceptance(
		const QString &username,
		const QString &url,
		bool recurring) {
	showBox(Box([=](not_null<GenericBox*> box) {
		box->setTitle(tr::lng_payments_terms_title());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box.get(),
			(recurring
				? tr::lng_payments_terms_text
				: tr::lng_payments_terms_text_once)(
					lt_bot,
					rpl::single(Ui::Text::Bold('@' + username)),
					Ui::Text::WithEntities),
			st::boxLabel));
		const auto update = std::make_shared<Fn<void()>>();
		auto checkView = std::make_unique<Ui::CheckView>(
			st::defaultCheck,
			false,
			[=] { if (*update) { (*update)(); } });
		const auto check = checkView.get();
		const auto row = box->addRow(
			object_ptr<Ui::Checkbox>(
				box.get(),
				tr::lng_payments_terms_agree(
					lt_link,
					rpl::single(Ui::Text::Link(
						tr::lng_payments_terms_link(tr::now),
						url)),
					Ui::Text::WithEntities),
				st::defaultBoxCheckbox,
				std::move(checkView)),
			{
				st::boxRowPadding.left(),
				st::boxRowPadding.left(),
				st::boxRowPadding.right(),
				st::defaultBoxCheckbox.margin.bottom(),
			});
		row->setAllowTextLines(5);
		row->setClickHandlerFilter([=](
				const ClickHandlerPtr &link,
				Qt::MouseButton button) {
			ActivateClickHandler(_widget.get(), link, ClickContext{
				.button = button,
				.other = _delegate->panelClickHandlerContext(),
			});
			return false;
		});

		(*update) = [=] { row->update(); };

		const auto showError = Ui::CheckView::PrepareNonToggledError(
			check,
			box->lifetime());

		box->addButton(tr::lng_payments_terms_accept(), [=] {
			if (check->checked()) {
				_delegate->panelAcceptTermsAndSubmit();
				box->closeBox();
			} else {
				showError();
			}
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

rpl::producer<QString> Panel::savedMethodChosen() const {
	return _savedMethodChosen.events();
}

void Panel::showBox(object_ptr<BoxContent> box) {
	if (const auto widget = _webview ? _webview->window.widget() : nullptr) {
		const auto hideNow = !widget->isHidden();
		if (hideNow || _webview->lastHidingBox) {
			const auto raw = _webview->lastHidingBox = box.data();
			box->boxClosing(
			) | rpl::start_with_next([=] {
				const auto widget = _webview
					? _webview->window.widget()
					: nullptr;
				if (widget
					&& widget->isHidden()
					&& _webview->lastHidingBox == raw) {
					widget->show();
				}
			}, _webview->lifetime);
			if (hideNow) {
				widget->hide();
			}
		}
	}
	_widget->showBox(
		std::move(box),
		LayerOption::KeepOther,
		anim::type::normal);
}

void Panel::showToast(TextWithEntities &&text) {
	_widget->showToast(std::move(text));
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

std::shared_ptr<Show> Panel::uiShow() {
	return _widget->uiShow();
}

void Panel::showWebviewError(
		const QString &text,
		const Webview::Available &information) {
	showCriticalError(TextWithEntities{ text }.append(
		"\n\n"
	).append(BotWebView::ErrorText(information)));
}

void Panel::updateThemeParams(const Webview::ThemeParams &params) {
	if (!_webview || !_webview->window.widget()) {
		return;
	}
	_webview->window.updateTheme(
		params.bodyBg,
		params.scrollBg,
		params.scrollBgOver,
		params.scrollBarBg,
		params.scrollBarBgOver);
	_webview->window.eval(R"(
if (window.TelegramGameProxy) {
	window.TelegramGameProxy.receiveEvent(
		"theme_changed",
		{ "theme_params": )" + params.json + R"( });
}
)");
}

rpl::lifetime &Panel::lifetime() {
	return _widget->lifetime();
}

} // namespace Payments::Ui
