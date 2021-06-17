/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_edit_card.h"

#include "payments/ui/payments_panel_delegate.h"
#include "payments/ui/payments_field.h"
#include "stripe/stripe_card_validator.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "lang/lang_keys.h"
#include "styles/style_payments.h"
#include "styles/style_passport.h"

#include <QtCore/QRegularExpression>

namespace Payments::Ui {
namespace {

struct SimpleFieldState {
	QString value;
	int position = 0;
};

[[nodiscard]] uint32 ExtractYear(const QString &value) {
	return value.split('/').value(1).toInt() + 2000;
}

[[nodiscard]] uint32 ExtractMonth(const QString &value) {
	return value.split('/').value(0).toInt();
}

[[nodiscard]] QString RemoveNonNumbers(QString value) {
	return value.replace(QRegularExpression("[^0-9]"), QString());
}

[[nodiscard]] SimpleFieldState NumbersOnlyState(SimpleFieldState state) {
	return {
		.value = RemoveNonNumbers(state.value),
		.position = RemoveNonNumbers(
			state.value.mid(0, state.position)).size(),
	};
}

[[nodiscard]] SimpleFieldState PostprocessCardValidateResult(
		SimpleFieldState result) {
	const auto groups = Stripe::CardNumberFormat(result.value);
	auto position = 0;
	for (const auto length : groups) {
		position += length;
		if (position >= result.value.size()) {
			break;
		}
		result.value.insert(position, QChar(' '));
		if (result.position >= position) {
			++result.position;
		}
		++position;
	}
	return result;
}

[[nodiscard]] SimpleFieldState PostprocessExpireDateValidateResult(
		SimpleFieldState result) {
	if (result.value.isEmpty()) {
		return result;
	} else if (result.value[0] == '1' && result.value[1] > '2') {
		result.value = result.value.mid(0, 2);
		return result;
	} else if (result.value[0] > '1') {
		result.value = '0' + result.value;
		++result.position;
	}
	if (result.value.size() > 1) {
		if (result.value.size() > 4) {
			result.value = result.value.mid(0, 4);
		}
		result.value.insert(2, '/');
		if (result.position >= 2) {
			++result.position;
		}
	}
	return result;
}

[[nodiscard]] bool IsBackspace(const FieldValidateRequest &request) {
	return (request.wasAnchor == request.wasPosition)
		&& (request.wasPosition == request.nowPosition + 1)
		&& (request.wasValue.midRef(0, request.wasPosition - 1)
			== request.nowValue.midRef(0, request.nowPosition))
		&& (request.wasValue.midRef(request.wasPosition)
			== request.nowValue.midRef(request.nowPosition));
}

[[nodiscard]] bool IsDelete(const FieldValidateRequest &request) {
	return (request.wasAnchor == request.wasPosition)
		&& (request.wasPosition == request.nowPosition)
		&& (request.wasValue.midRef(0, request.wasPosition)
			== request.nowValue.midRef(0, request.nowPosition))
		&& (request.wasValue.midRef(request.wasPosition + 1)
			== request.nowValue.midRef(request.nowPosition));
}

template <
	typename ValueValidator,
	typename ValueValidateResult = decltype(
		std::declval<ValueValidator>()(QString()))>
[[nodiscard]] auto ComplexNumberValidator(
		ValueValidator valueValidator,
		Fn<SimpleFieldState(SimpleFieldState)> postprocess) {
	using namespace Stripe;
	return [=](FieldValidateRequest request) {
		const auto realNowState = [&] {
			const auto backspaced = IsBackspace(request);
			const auto deleted = IsDelete(request);
			if (!backspaced && !deleted) {
				return NumbersOnlyState({
					.value = request.nowValue,
					.position = request.nowPosition,
				});
			}
			const auto realWasState = NumbersOnlyState({
				.value = request.wasValue,
				.position = request.wasPosition,
			});
			const auto changedValue = deleted
				? (realWasState.value.mid(0, realWasState.position)
					+ realWasState.value.mid(realWasState.position + 1))
				: (realWasState.position > 1)
				? (realWasState.value.mid(0, realWasState.position - 1)
					+ realWasState.value.mid(realWasState.position))
				: realWasState.value.mid(realWasState.position);
			return SimpleFieldState{
				.value = changedValue,
				.position = (deleted
					? realWasState.position
					: std::max(realWasState.position - 1, 0))
			};
		}();
		const auto result = valueValidator(realNowState.value);
		const auto postprocessed = postprocess(realNowState);
		return FieldValidateResult{
			.value = postprocessed.value,
			.position = postprocessed.position,
			.invalid = (result.state == ValidationState::Invalid),
			.finished = result.finished,
		};
	};

}

[[nodiscard]] auto CardNumberValidator() {
	return ComplexNumberValidator(
		Stripe::ValidateCard,
		PostprocessCardValidateResult);
}

[[nodiscard]] auto ExpireDateValidator() {
	return ComplexNumberValidator(
		Stripe::ValidateExpireDate,
		PostprocessExpireDateValidateResult);
}

[[nodiscard]] auto CvcValidator(Fn<QString()> number) {
	using namespace Stripe;
	return [=](FieldValidateRequest request) {
		const auto realNowState = NumbersOnlyState({
			.value = request.nowValue,
			.position = request.nowPosition,
		});
		const auto result = ValidateCvc(number(), realNowState.value);

		return FieldValidateResult{
			.value = realNowState.value,
			.position = realNowState.position,
			.invalid = (result.state == ValidationState::Invalid),
			.finished = result.finished,
		};
	};
}

[[nodiscard]] auto CardHolderNameValidator() {
	return [=](FieldValidateRequest request) {
		return FieldValidateResult{
			.value = request.nowValue.toUpper(),
			.position = request.nowPosition,
			.invalid = request.nowValue.isEmpty(),
		};
	};
}

} // namespace

EditCard::EditCard(
	QWidget *parent,
	const NativeMethodDetails &native,
	CardField field,
	not_null<PanelDelegate*> delegate)
: _delegate(delegate)
, _native(native)
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _submit(
	this,
	tr::lng_about_done(),
	st::paymentsPanelButton)
, _cancel(
		this,
		tr::lng_cancel(),
		st::paymentsPanelButton) {
	setupControls();
}

void EditCard::setFocus(CardField field) {
	_focusField = field;
	if (const auto control = lookupField(field)) {
		_scroll->ensureWidgetVisible(control->widget());
		control->setFocus();
	}
}

void EditCard::setFocusFast(CardField field) {
	_focusField = field;
	if (const auto control = lookupField(field)) {
		_scroll->ensureWidgetVisible(control->widget());
		control->setFocusFast();
	}
}

void EditCard::showError(CardField field) {
	if (const auto control = lookupField(field)) {
		_scroll->ensureWidgetVisible(control->widget());
		control->showError();
	}
}

void EditCard::setupControls() {
	const auto inner = setupContent();

	_submit->addClickHandler([=] {
		_delegate->panelValidateCard(collect(), (_save && _save->checked()));
	});
	_cancel->addClickHandler([=] {
		_delegate->panelCancelEdit();
	});

	using namespace rpl::mappers;

	_topShadow->toggleOn(
		_scroll->scrollTopValue() | rpl::map(_1 > 0));
	_bottomShadow->toggleOn(rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		inner->heightValue(),
		_1 + _2 < _3));
}

not_null<RpWidget*> EditCard::setupContent() {
	const auto inner = _scroll->setOwnedWidget(
		object_ptr<VerticalLayout>(this));

	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	const auto showBox = [=](object_ptr<BoxContent> box) {
		_delegate->panelShowBox(std::move(box));
	};
	auto last = (Field*)nullptr;
	const auto make = [&](QWidget *parent, FieldConfig &&config) {
		auto result = std::make_unique<Field>(parent, std::move(config));
		if (last) {
			last->setNextField(result.get());
			result->setPreviousField(last);
		}
		last = result.get();
		return result;
	};
	const auto add = [&](FieldConfig &&config) {
		auto result = make(inner, std::move(config));
		inner->add(result->ownedWidget(), st::paymentsFieldPadding);
		return result;
	};
	_number = add({
		.type = FieldType::CardNumber,
		.placeholder = tr::lng_payments_card_number(),
		.validator = CardNumberValidator(),
	});
	auto container = inner->add(
		object_ptr<FixedHeightWidget>(
			inner,
			_number->widget()->height()),
		st::paymentsFieldPadding);
	_expire = make(container, {
		.type = FieldType::CardExpireDate,
		.placeholder = tr::lng_payments_card_expire_date(),
		.validator = ExpireDateValidator(),
	});
	_cvc = make(container, {
		.type = FieldType::CardCVC,
		.placeholder = tr::lng_payments_card_cvc(),
		.validator = CvcValidator([=] { return _number->value(); }),
	});
	container->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto left = (width - st::paymentsExpireCvcSkip) / 2;
		const auto right = width - st::paymentsExpireCvcSkip - left;
		_expire->widget()->resizeToWidth(left);
		_cvc->widget()->resizeToWidth(right);
		_expire->widget()->moveToLeft(0, 0, width);
		_cvc->widget()->moveToRight(0, 0, width);
	}, container->lifetime());

	if (_native.needCardholderName) {
		_name = add({
			.type = FieldType::Text,
			.placeholder = tr::lng_payments_card_holder(),
			.validator = CardHolderNameValidator(),
		});
	}

	if (_native.needCountry || _native.needZip) {
		inner->add(
			object_ptr<Ui::FlatLabel>(
				inner,
				tr::lng_payments_billing_address(),
				st::paymentsBillingInformationTitle),
			st::paymentsBillingInformationTitlePadding);
	}
	if (_native.needCountry) {
		_country = add({
			.type = FieldType::Country,
			.placeholder = tr::lng_payments_billing_country(),
			.validator = RequiredFinishedValidator(),
			.showBox = showBox,
			.defaultCountry = _native.defaultCountry,
		});
	}
	if (_native.needZip) {
		_zip = add({
			.type = FieldType::Text,
			.placeholder = tr::lng_payments_billing_zip_code(),
			.validator = RequiredValidator(),
		});
		if (_country) {
			_country->finished(
			) | rpl::start_with_next([=] {
				_zip->setFocus();
			}, lifetime());
		}
	}
	if (_native.canSaveInformation) {
		_save = inner->add(
			object_ptr<Ui::Checkbox>(
				inner,
				tr::lng_payments_save_information(tr::now),
				false),
			st::paymentsSaveCheckboxPadding);
	}

	last->submitted(
	) | rpl::start_with_next([=] {
		_delegate->panelValidateCard(collect(), _save && _save->checked());
	}, lifetime());

	return inner;
}

void EditCard::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void EditCard::focusInEvent(QFocusEvent *e) {
	if (const auto control = lookupField(_focusField)) {
		control->setFocusFast();
	}
}

void EditCard::updateControlsGeometry() {
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
	_submit->moveToRight(right, buttonsTop + padding.top());
	right += _submit->width() + padding.left();
	_cancel->moveToRight(right, buttonsTop + padding.top());

	_scroll->updateBars();
}

auto EditCard::lookupField(CardField field) const -> Field* {
	switch (field) {
	case CardField::Number: return _number.get();
	case CardField::Cvc: return _cvc.get();
	case CardField::ExpireDate: return _expire.get();
	case CardField::Name: return _name.get();
	case CardField::AddressCountry: return _country.get();
	case CardField::AddressZip: return _zip.get();
	}
	Unexpected("Unknown field in EditCard::controlForField.");
}

UncheckedCardDetails EditCard::collect() const {
	return {
		.number = _number ? _number->value() : QString(),
		.cvc = _cvc ? _cvc->value() : QString(),
		.expireYear = _expire ? ExtractYear(_expire->value()) : 0,
		.expireMonth = _expire ? ExtractMonth(_expire->value()) : 0,
		.cardholderName = _name ? _name->value() : QString(),
		.addressCountry = _country ? _country->value() : QString(),
		.addressZip = _zip ? _zip->value() : QString(),
	};
}

} // namespace Payments::Ui
