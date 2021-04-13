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
#include "ui/text/text_utilities.h"
#include "data/data_countries.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "styles/style_payments.h"
#include "styles/style_passport.h"

namespace App {
QString formatPhone(QString phone); // #TODO
} // namespace App

namespace Payments::Ui {
namespace {

constexpr auto kLightOpacity = 0.1;
constexpr auto kLightRippleOpacity = 0.11;
constexpr auto kChosenOpacity = 0.8;
constexpr auto kChosenRippleOpacity = 0.5;

[[nodiscard]] Fn<QColor()> TransparentColor(
		const style::color &c,
		float64 opacity) {
	return [&c, opacity] {
		return QColor(
			c->c.red(),
			c->c.green(),
			c->c.blue(),
			c->c.alpha() * opacity);
	};
}

[[nodiscard]] style::RoundButton TipButtonStyle(
		const style::RoundButton &original,
		const style::color &light,
		const style::color &ripple) {
	auto result = original;
	result.textBg = light;
	result.ripple.color = ripple;
	return result;
}

} // namespace

using namespace ::Ui;

class PanelDelegate;

FormSummary::FormSummary(
	QWidget *parent,
	const Invoice &invoice,
	const RequestedInformation &current,
	const PaymentMethodDetails &method,
	const ShippingOptions &options,
	not_null<PanelDelegate*> delegate,
	int scrollTop)
: _delegate(delegate)
, _invoice(invoice)
, _method(method)
, _options(options)
, _information(current)
, _scroll(this, st::passportPanelScroll)
, _layout(_scroll->setOwnedWidget(object_ptr<VerticalLayout>(this)))
, _topShadow(this)
, _bottomShadow(this)
, _submit(_invoice.receipt.paid
	? object_ptr<RoundButton>(nullptr)
	: object_ptr<RoundButton>(
		this,
		tr::lng_payments_pay_amount(
			lt_amount,
			rpl::single(formatAmount(computeTotalAmount()))),
		st::paymentsPanelSubmit))
, _cancel(
	this,
	(_invoice.receipt.paid
		? tr::lng_about_done()
		: tr::lng_cancel()),
	st::paymentsPanelButton)
, _tipLightBg(TransparentColor(st::paymentsTipActive, kLightOpacity))
, _tipLightRipple(
	TransparentColor(st::paymentsTipActive, kLightRippleOpacity))
, _tipChosenBg(TransparentColor(st::paymentsTipActive, kChosenOpacity))
, _tipChosenRipple(
	TransparentColor(st::paymentsTipActive, kChosenRippleOpacity))
, _tipButton(TipButtonStyle(
	st::paymentsTipButton,
	_tipLightBg.color(),
	_tipLightRipple.color()))
, _tipChosen(TipButtonStyle(
	st::paymentsTipChosen,
	_tipChosenBg.color(),
	_tipChosenRipple.color()))
, _initialScrollTop(scrollTop) {
	setupControls();
}

rpl::producer<int> FormSummary::scrollTopValue() const {
	return _scroll->scrollTopValue();
}

bool FormSummary::showCriticalError(const TextWithEntities &text) {
	if (_invoice
		|| (_scroll->height() - _layout->height()
			< st::paymentsPanelSize.height() / 2)) {
		return false;
	}
	Settings::AddSkip(_layout.get(), st::paymentsPricesTopSkip);
	_layout->add(object_ptr<FlatLabel>(
		_layout.get(),
		rpl::single(text),
		st::paymentsCriticalError));
	return true;
}

int FormSummary::contentHeight() const {
	return _invoice ? _scroll->height() : _layout->height();
}

void FormSummary::updateThumbnail(const QImage &thumbnail) {
	_invoice.cover.thumbnail = thumbnail;
	_thumbnails.fire_copy(thumbnail);
}

QString FormSummary::formatAmount(
		int64 amount,
		bool forceStripDotZero) const {
	return FillAmountAndCurrency(
		amount,
		_invoice.currency,
		forceStripDotZero);
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
	return total + shipping + _invoice.tipsSelected;
}

void FormSummary::setupControls() {
	setupContent(_layout.get());

	if (_submit) {
		_submit->addClickHandler([=] {
			_delegate->panelSubmit();
		});
	}
	_cancel->addClickHandler([=] {
		_delegate->panelRequestClose();
	});
	if (!_invoice) {
		if (_submit) {
			_submit->hide();
		}
		_cancel->hide();
	}

	using namespace rpl::mappers;

	_topShadow->toggleOn(
		_scroll->scrollTopValue() | rpl::map(_1 > 0));
	_bottomShadow->toggleOn(rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		_layout->heightValue(),
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
			const TextWithEntities &value,
			bool full = false) {
		const auto &st = full
			? st::paymentsFullPriceAmount
			: st::paymentsPriceAmount;
		const auto right = CreateChild<FlatLabel>(
			layout.get(),
			rpl::single(value),
			st);
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
		return right;
	};

	Settings::AddSkip(layout, st::paymentsPricesTopSkip);
	if (_invoice.receipt) {
		addRow(
			tr::lng_payments_date_label(tr::now),
			{ langDateTime(base::unixtime::parse(_invoice.receipt.date)) },
			true);
		Settings::AddSkip(layout, st::paymentsPricesBottomSkip);
		Settings::AddDivider(layout);
		Settings::AddSkip(layout, st::paymentsPricesBottomSkip);
	}

	const auto add = [&](
			const QString &label,
			int64 amount,
			bool full = false) {
		addRow(label, { formatAmount(amount) }, full);
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

	const auto computedTotal = computeTotalAmount();
	const auto total = _invoice.receipt.paid
		? _invoice.receipt.totalAmount
		: computedTotal;
	if (_invoice.receipt.paid) {
		if (const auto tips = total - computedTotal) {
			add(tr::lng_payments_tips_label(tr::now), tips);
		}
	} else if (_invoice.tipsMax > 0) {
		const auto text = formatAmount(_invoice.tipsSelected);
		const auto label = addRow(
			tr::lng_payments_tips_label(tr::now),
			Ui::Text::Link(text, "internal:edit_tips"));
		label->setClickHandlerFilter([=](auto&&...) {
			_delegate->panelChooseTips();
			return false;
		});
		setupSuggestedTips(layout);
	}

	add(tr::lng_payments_total_label(tr::now), total, true);
	Settings::AddSkip(layout, st::paymentsPricesBottomSkip);
}

void FormSummary::setupSuggestedTips(not_null<VerticalLayout*> layout) {
	if (_invoice.suggestedTips.empty()) {
		return;
	}
	struct Button {
		RoundButton *widget = nullptr;
		int minWidth = 0;
	};
	struct State {
		std::vector<Button> buttons;
		int maxWidth = 0;
	};
	const auto outer = layout->add(
		object_ptr<RpWidget>(layout),
		st::paymentsTipButtonsPadding);
	const auto state = outer->lifetime().make_state<State>();
	for (const auto amount : _invoice.suggestedTips) {
		const auto text = formatAmount(amount, true);
		const auto selected = (amount == _invoice.tipsSelected);
		const auto &st = selected
			? _tipChosen
			: _tipButton;
		state->buttons.push_back(Button{
			.widget = CreateChild<RoundButton>(
				outer,
				rpl::single(formatAmount(amount, true)),
				st),
		});
		auto &button = state->buttons.back();
		button.widget->show();
		button.widget->setClickedCallback([=] {
			_delegate->panelChangeTips(selected ? 0 : amount);
		});
		button.minWidth = button.widget->width();
		state->maxWidth = std::max(state->maxWidth, button.minWidth);
	}
	outer->widthValue(
	) | rpl::filter([=](int outerWidth) {
		return outerWidth >= state->maxWidth;
	}) | rpl::start_with_next([=](int outerWidth) {
		const auto skip = st::paymentsTipSkip;
		const auto &buttons = state->buttons;
		auto left = outerWidth;
		auto height = 0;
		auto rowStart = 0;
		auto rowEnd = 0;
		auto buttonWidths = std::vector<float64>();
		const auto layoutRow = [&] {
			const auto count = rowEnd - rowStart;
			if (!count) {
				return;
			}
			buttonWidths.resize(count);
			ranges::fill(buttonWidths, 0.);
			auto available = float64(outerWidth - (count - 1) * skip);
			auto zeros = count;
			do {
				const auto started = zeros;
				const auto average = available / zeros;
				for (auto i = 0; i != count; ++i) {
					if (buttonWidths[i] > 0.) {
						continue;
					}
					const auto min = buttons[rowStart + i].minWidth;
					if (min > average) {
						buttonWidths[i] = min;
						available -= min;
						--zeros;
					}
				}
				if (started == zeros) {
					for (auto i = 0; i != count; ++i) {
						if (!buttonWidths[i]) {
							buttonWidths[i] = average;
						}
					}
					break;
				}
			} while (zeros > 0);
			auto x = 0.;
			for (auto i = 0; i != count; ++i) {
				const auto button = buttons[rowStart + i].widget;
				auto right = x + buttonWidths[i];
				button->setFullWidth(int(std::round(right) - std::round(x)));
				button->moveToLeft(int(std::round(x)), height, outerWidth);
				x = right + skip;
			}
			height += buttons[0].widget->height() + skip;
		};
		for (const auto button : buttons) {
			if (button.minWidth <= left) {
				left -= button.minWidth + skip;
				++rowEnd;
				continue;
			}
			layoutRow();
			rowStart = rowEnd++;
			left = outerWidth - button.minWidth - skip;
		}
		layoutRow();
		outer->resize(outerWidth, height - skip);
	}, outer->lifetime());
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
			(_information.phone.isEmpty()
				? QString()
				: App::formatPhone(_information.phone)),
			&st::paymentsIconPhone,
			[=] { _delegate->panelEditPhone(); });
	}
	Settings::AddSkip(layout, st::paymentsSectionsTopSkip);
}

void FormSummary::setupContent(not_null<VerticalLayout*> layout) {
	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		layout->resizeToWidth(width);
	}, layout->lifetime());

	setupCover(layout);
	if (_invoice) {
		Settings::AddDivider(layout);
		setupPrices(layout);
		Settings::AddDivider(layout);
		setupSections(layout);
	}
}

void FormSummary::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void FormSummary::updateControlsGeometry() {
	const auto &padding = st::paymentsPanelPadding;
	const auto buttonsHeight = padding.top()
		+ _cancel->height()
		+ padding.bottom();
	const auto buttonsTop = height() - buttonsHeight;
	_scroll->setGeometry(0, 0, width(), buttonsTop);
	_topShadow->resizeToWidth(width());
	_topShadow->moveToLeft(0, 0);
	_bottomShadow->resizeToWidth(width());
	_bottomShadow->moveToLeft(0, buttonsTop - st::lineWidth);
	auto right = padding.right();
	if (_submit) {
		_submit->moveToRight(right, buttonsTop + padding.top());
		right += _submit->width() + padding.left();
	}
	_cancel->moveToRight(right, buttonsTop + padding.top());

	_scroll->updateBars();

	if (buttonsTop > 0 && width() > 0) {
		if (const auto top = base::take(_initialScrollTop)) {
			_scroll->scrollToY(top);
		}
	}
}

} // namespace Payments::Ui
