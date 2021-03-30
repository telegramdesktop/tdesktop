/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_form_summary.h"

#include "payments/ui/payments_panel_delegate.h"
#include "settings/settings_common.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/text/format_values.h"
#include "data/data_countries.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "styles/style_payments.h"
#include "styles/style_passport.h"

namespace Payments::Ui {

using namespace ::Ui;

class PanelDelegate;

FormSummary::FormSummary(
	QWidget *parent,
	const Invoice &invoice,
	const RequestedInformation &current,
	const PaymentMethodDetails &method,
	const ShippingOptions &options,
	not_null<PanelDelegate*> delegate)
: _delegate(delegate)
, _invoice(invoice)
, _method(method)
, _options(options)
, _information(current)
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _submit(
	this,
	(_invoice.receipt.paid
		? tr::lng_about_done()
		: tr::lng_payments_pay_amount(
			lt_amount,
			rpl::single(formatAmount(computeTotalAmount())))),
	(_invoice.receipt.paid
		? st::passportPanelSaveValue
		: st::paymentsPanelSubmit)) {
	setupControls();
}

void FormSummary::updateThumbnail(const QImage &thumbnail) {
	_invoice.cover.thumbnail = thumbnail;
	_thumbnails.fire_copy(thumbnail);
}

QString FormSummary::formatAmount(int64 amount) const {
	const auto base = FillAmountAndCurrency(
		std::abs(amount),
		_invoice.currency);
	return (amount < 0) ? (QString::fromUtf8("\xe2\x88\x92") + base) : base;
}

int64 FormSummary::computeTotalAmount() const {
	const auto total = ranges::accumulate(
		_invoice.prices,
		int64(0),
		std::plus<>(),
		&LabeledPrice::price);
	const auto selected = ranges::find(
		_options.list,
		_options.selectedId,
		&ShippingOption::id);
	const auto shipping = (selected != end(_options.list))
		? ranges::accumulate(
			selected->prices,
			int64(0),
			std::plus<>(),
			&LabeledPrice::price)
		: int64(0);
	return total + shipping;
}

void FormSummary::setupControls() {
	const auto inner = setupContent();

	_submit->addClickHandler([=] {
		_delegate->panelSubmit();
	});
	if (!_invoice) {
		_submit->hide();
	}

	using namespace rpl::mappers;

	_topShadow->toggleOn(
		_scroll->scrollTopValue() | rpl::map(_1 > 0));
	_bottomShadow->toggleOn(rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		inner->heightValue(),
		_1 + _2 < _3));
}

void FormSummary::setupCover(not_null<VerticalLayout*> layout) {
	struct State {
		QImage thumbnail;
		FlatLabel *title = nullptr;
		FlatLabel *description = nullptr;
		FlatLabel *seller = nullptr;
	};

	const auto cover = layout->add(object_ptr<RpWidget>(layout));
	const auto state = cover->lifetime().make_state<State>();
	state->title = CreateChild<FlatLabel>(
		cover,
		_invoice.cover.title,
		st::paymentsTitle);
	state->description = CreateChild<FlatLabel>(
		cover,
		_invoice.cover.description,
		st::paymentsDescription);
	state->seller = CreateChild<FlatLabel>(
		cover,
		_invoice.cover.seller,
		st::paymentsSeller);
	cover->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		if (state->thumbnail.isNull()) {
			return;
		}
		const auto &padding = st::paymentsCoverPadding;
		const auto thumbnailSkip = st::paymentsThumbnailSize.width()
			+ st::paymentsThumbnailSkip;
		const auto left = padding.left();
		const auto top = padding.top();
		const auto rect = QRect(
			QPoint(left, top),
			state->thumbnail.size() / state->thumbnail.devicePixelRatio());
		if (rect.intersects(clip)) {
			QPainter(cover).drawImage(rect, state->thumbnail);
		}
	}, cover->lifetime());
	rpl::combine(
		cover->widthValue(),
		_thumbnails.events_starting_with_copy(_invoice.cover.thumbnail)
	) | rpl::start_with_next([=](int width, QImage &&thumbnail) {
		const auto &padding = st::paymentsCoverPadding;
		const auto thumbnailSkip = st::paymentsThumbnailSize.width()
			+ st::paymentsThumbnailSkip;
		const auto left = padding.left()
			+ (thumbnail.isNull() ? 0 : thumbnailSkip);
		const auto available = width
			- padding.left()
			- padding.right()
			- (thumbnail.isNull() ? 0 : thumbnailSkip);
		state->title->resizeToNaturalWidth(available);
		state->title->moveToLeft(
			left,
			padding.top() + st::paymentsTitleTop);
		state->description->resizeToNaturalWidth(available);
		state->description->moveToLeft(
			left,
			(state->title->y()
				+ state->title->height()
				+ st::paymentsDescriptionTop));
		state->seller->resizeToNaturalWidth(available);
		state->seller->moveToLeft(
			left,
			(state->description->y()
				+ state->description->height()
				+ st::paymentsSellerTop));
		const auto thumbnailHeight = padding.top()
			+ (thumbnail.isNull()
				? 0
				: int(thumbnail.height() / thumbnail.devicePixelRatio()))
			+ padding.bottom();
		const auto height = state->seller->y()
			+ state->seller->height()
			+ padding.bottom();
		cover->resize(width, std::max(thumbnailHeight, height));
		state->thumbnail = std::move(thumbnail);
		cover->update();
	}, cover->lifetime());
}

void FormSummary::setupPrices(not_null<VerticalLayout*> layout) {
	const auto addRow = [&](
			const QString &label,
			const QString &value,
			bool full = false) {
		const auto &st = full
			? st::paymentsFullPriceAmount
			: st::paymentsPriceAmount;
		const auto right = CreateChild<FlatLabel>(layout.get(), value, st);
		const auto &padding = st::paymentsPricePadding;
		const auto left = layout->add(
			object_ptr<FlatLabel>(
				layout,
				label,
				(full
					? st::paymentsFullPriceLabel
					: st::paymentsPriceLabel)),
			style::margins(
				padding.left(),
				padding.top(),
				(padding.right()
					+ right->naturalWidth()
					+ 2 * st.style.font->spacew),
				padding.bottom()));
		rpl::combine(
			left->topValue(),
			layout->widthValue()
		) | rpl::start_with_next([=](int top, int width) {
			right->moveToRight(st::paymentsPricePadding.right(), top, width);
		}, right->lifetime());
	};

	Settings::AddSkip(layout, st::paymentsPricesTopSkip);
	if (_invoice.receipt) {
		addRow(
			tr::lng_payments_date_label(tr::now),
			langDateTime(base::unixtime::parse(_invoice.receipt.date)),
			true);
		Settings::AddSkip(layout, st::paymentsPricesBottomSkip);
		Settings::AddDivider(layout);
		Settings::AddSkip(layout, st::paymentsPricesBottomSkip);
	}

	const auto add = [&](
			const QString &label,
			int64 amount,
			bool full = false) {
		addRow(label, formatAmount(amount), full);
	};
	for (const auto &price : _invoice.prices) {
		add(price.label, price.price);
	}
	const auto selected = ranges::find(
		_options.list,
		_options.selectedId,
		&ShippingOption::id);
	if (selected != end(_options.list)) {
		for (const auto &price : selected->prices) {
			add(price.label, price.price);
		}
	}
	add(tr::lng_payments_total_label(tr::now), computeTotalAmount(), true);
	Settings::AddSkip(layout, st::paymentsPricesBottomSkip);
}

void FormSummary::setupSections(not_null<VerticalLayout*> layout) {
	Settings::AddSkip(layout, st::paymentsSectionsTopSkip);

	const auto add = [&](
			rpl::producer<QString> title,
			const QString &label,
			const style::icon *icon,
			Fn<void()> handler) {
		const auto button = Settings::AddButtonWithLabel(
			layout,
			std::move(title),
			rpl::single(label),
			st::paymentsSectionButton,
			icon);
		button->addClickHandler(std::move(handler));
		if (_invoice.receipt) {
			button->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
	};
	add(
		tr::lng_payments_payment_method(),
		_method.title,
		&st::paymentsIconPaymentMethod,
		[=] { _delegate->panelEditPaymentMethod(); });
	if (_invoice.isShippingAddressRequested) {
		auto list = QStringList();
		const auto push = [&](const QString &value) {
			if (!value.isEmpty()) {
				list.push_back(value);
			}
		};
		push(_information.shippingAddress.address1);
		push(_information.shippingAddress.address2);
		push(_information.shippingAddress.city);
		push(_information.shippingAddress.state);
		push(Data::CountryNameByISO2(
			_information.shippingAddress.countryIso2));
		push(_information.shippingAddress.postcode);
		add(
			tr::lng_payments_shipping_address(),
			list.join(", "),
			&st::paymentsIconShippingAddress,
			[=] { _delegate->panelEditShippingInformation(); });
	}
	if (!_options.list.empty()) {
		const auto selected = ranges::find(
			_options.list,
			_options.selectedId,
			&ShippingOption::id);
		add(
			tr::lng_payments_shipping_method(),
			(selected != end(_options.list)) ? selected->title : QString(),
			&st::paymentsIconShippingMethod,
			[=] { _delegate->panelChooseShippingOption(); });
	}
	if (_invoice.isNameRequested) {
		add(
			tr::lng_payments_info_name(),
			_information.name,
			&st::paymentsIconName,
			[=] { _delegate->panelEditName(); });
	}
	if (_invoice.isEmailRequested) {
		add(
			tr::lng_payments_info_email(),
			_information.email,
			&st::paymentsIconEmail,
			[=] { _delegate->panelEditEmail(); });
	}
	if (_invoice.isPhoneRequested) {
		add(
			tr::lng_payments_info_phone(),
			_information.phone,
			&st::paymentsIconPhone,
			[=] { _delegate->panelEditPhone(); });
	}
	Settings::AddSkip(layout, st::paymentsSectionsTopSkip);
}

not_null<RpWidget*> FormSummary::setupContent() {
	const auto inner = _scroll->setOwnedWidget(
		object_ptr<VerticalLayout>(this));

	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	setupCover(inner);
	if (_invoice) {
		Settings::AddDivider(inner);
		setupPrices(inner);
		Settings::AddDivider(inner);
		setupSections(inner);
	}

	return inner;
}

void FormSummary::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void FormSummary::updateControlsGeometry() {
	const auto submitTop = height() - _submit->height();
	_scroll->setGeometry(0, 0, width(), submitTop);
	_topShadow->resizeToWidth(width());
	_topShadow->moveToLeft(0, 0);
	_bottomShadow->resizeToWidth(width());
	_bottomShadow->moveToLeft(0, submitTop - st::lineWidth);
	_submit->setFullWidth(width());
	_submit->moveToLeft(0, submitTop);

	_scroll->updateBars();
}

} // namespace Payments::Ui
