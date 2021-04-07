/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_field.h"

#include "ui/widgets/input_fields.h"
#include "ui/boxes/country_select_box.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "ui/special_fields.h"
#include "data/data_countries.h"
#include "base/platform/base_platform_info.h"
#include "base/event_filter.h"
#include "styles/style_payments.h"

#include <QtCore/QRegularExpression>

namespace Payments::Ui {
namespace {

struct SimpleFieldState {
	QString value;
	int position = 0;
};

[[nodiscard]] char FieldThousandsSeparator(const CurrencyRule &rule) {
	return (rule.thousands == '.' || rule.thousands == ',')
		? ' '
		: rule.thousands;
}

[[nodiscard]] QString RemoveNonNumbers(QString value) {
	return value.replace(QRegularExpression("[^0-9]"), QString());
}

[[nodiscard]] SimpleFieldState CleanMoneyState(
		const CurrencyRule &rule,
		SimpleFieldState state) {
	const auto withDecimal = state.value.replace(
		QChar('.'),
		rule.decimal
	).replace(
		QChar(','),
		rule.decimal
	);
	const auto digitsLimit = 16 - rule.exponent;
	const auto beforePosition = state.value.mid(0, state.position);
	auto decimalPosition = withDecimal.lastIndexOf(rule.decimal);
	if (decimalPosition < 0) {
		state = {
			.value = RemoveNonNumbers(state.value),
			.position = RemoveNonNumbers(beforePosition).size(),
		};
	} else {
		const auto onlyNumbersBeforeDecimal = RemoveNonNumbers(
			state.value.mid(0, decimalPosition));
		state = {
			.value = (onlyNumbersBeforeDecimal
				+ QChar(rule.decimal)
				+ RemoveNonNumbers(state.value.mid(decimalPosition + 1))),
			.position = (RemoveNonNumbers(beforePosition).size()
				+ (state.position > decimalPosition ? 1 : 0)),
		};
		decimalPosition = onlyNumbersBeforeDecimal.size();
		const auto maxLength = decimalPosition + 1 + rule.exponent;
		if (state.value.size() > maxLength) {
			state = {
				.value = state.value.mid(0, maxLength),
				.position = std::min(state.position, maxLength),
			};
		}
	}
	if (!state.value.isEmpty() && state.value[0] == QChar(rule.decimal)) {
		state = {
			.value = QChar('0') + state.value,
			.position = state.position + 1,
		};
		if (decimalPosition >= 0) {
			++decimalPosition;
		}
	}
	auto skip = 0;
	while (state.value.size() > skip + 1
		&& state.value[skip] == QChar('0')
		&& state.value[skip + 1] != QChar(rule.decimal)) {
		++skip;
	}
	state = {
		.value = state.value.mid(skip),
		.position = std::max(state.position - skip, 0),
	};
	if (decimalPosition >= 0) {
		Assert(decimalPosition >= skip);
		decimalPosition -= skip;
	}
	if (decimalPosition > digitsLimit) {
		state = {
			.value = (state.value.mid(0, digitsLimit)
				+ state.value.mid(decimalPosition)),
			.position = (state.position > digitsLimit
				? std::max(
					state.position - (decimalPosition - digitsLimit),
					digitsLimit)
				: state.position),
		};
	}
	return state;
}

[[nodiscard]] SimpleFieldState PostprocessMoneyResult(
		const CurrencyRule &rule,
		SimpleFieldState result) {
	const auto position = result.value.indexOf(rule.decimal);
	const auto from = (position >= 0) ? position : result.value.size();
	for (auto insertAt = from - 3; insertAt > 0; insertAt -= 3) {
		result.value.insert(insertAt, QChar(FieldThousandsSeparator(rule)));
		if (result.position >= insertAt) {
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

[[nodiscard]] auto MoneyValidator(const CurrencyRule &rule) {
	return [=](FieldValidateRequest request) {
		const auto realNowState = [&] {
			const auto backspaced = IsBackspace(request);
			const auto deleted = IsDelete(request);
			if (!backspaced && !deleted) {
				return CleanMoneyState(rule, {
					.value = request.nowValue,
					.position = request.nowPosition,
				});
			}
			const auto realWasState = CleanMoneyState(rule, {
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
		const auto postprocessed = PostprocessMoneyResult(
			rule,
			realNowState);
		return FieldValidateResult{
			.value = postprocessed.value,
			.position = postprocessed.position,
		};
	};
}

[[nodiscard]] QString Parse(const FieldConfig &config) {
	if (config.type == FieldType::Country) {
		return Data::CountryNameByISO2(config.value);
	} else if (config.type == FieldType::Money) {
		const auto amount = config.value.toLongLong();
		if (!amount) {
			return QString();
		}
		const auto rule = LookupCurrencyRule(config.currency);
		const auto value = std::abs(amount) / std::pow(10., rule.exponent);
		const auto precision = (!rule.stripDotZero
			|| std::floor(value) != value)
			? rule.exponent
			: 0;
		return FormatWithSeparators(
			value,
			precision,
			rule.decimal,
			FieldThousandsSeparator(rule));
	}
	return config.value;
}

[[nodiscard]] QString Format(
		const FieldConfig &config,
		const QString &parsed,
		const QString &countryIso2) {
	if (config.type == FieldType::Country) {
		return countryIso2;
	} else if (config.type == FieldType::Money) {
		const auto rule = LookupCurrencyRule(config.currency);
		const auto real = QString(parsed).replace(
			QChar(rule.decimal),
			QChar('.')
		).replace(
			QChar(','),
			QChar('.')
		).replace(
			QRegularExpression("[^0-9\\.]"),
			QString()
		).toDouble();
		return QString::number(
			int64(std::round(real * std::pow(10., rule.exponent))));
	} else if (config.type == FieldType::CardNumber
		|| config.type == FieldType::CardCVC) {
		return QString(parsed).replace(
			QRegularExpression("[^0-9\\.]"),
			QString());
	}
	return parsed;
}

[[nodiscard]] bool UseMaskedField(FieldType type) {
	switch (type) {
	case FieldType::Text:
	case FieldType::Email:
		return false;
	case FieldType::CardNumber:
	case FieldType::CardExpireDate:
	case FieldType::CardCVC:
	case FieldType::Country:
	case FieldType::Phone:
	case FieldType::Money:
		return true;
	}
	Unexpected("FieldType in Payments::Ui::UseMaskedField.");
}

[[nodiscard]] base::unique_qptr<RpWidget> CreateWrap(
		QWidget *parent,
		FieldConfig &config) {
	switch (config.type) {
	case FieldType::Text:
	case FieldType::Email:
		return base::make_unique_q<InputField>(
			parent,
			st::paymentsField,
			std::move(config.placeholder),
			Parse(config));
	case FieldType::CardNumber:
	case FieldType::CardExpireDate:
	case FieldType::CardCVC:
	case FieldType::Country:
	case FieldType::Phone:
	case FieldType::Money:
		return base::make_unique_q<RpWidget>(parent);
	}
	Unexpected("FieldType in Payments::Ui::CreateWrap.");
}

[[nodiscard]] InputField *LookupInputField(
		not_null<RpWidget*> wrap,
		FieldConfig &config) {
	return UseMaskedField(config.type)
		? nullptr
		: static_cast<InputField*>(wrap.get());
}

[[nodiscard]] MaskedInputField *CreateMoneyField(
		not_null<RpWidget*> wrap,
		FieldConfig &config,
		rpl::producer<> textPossiblyChanged) {
	struct State {
		CurrencyRule rule;
		style::InputField st;
		QString currencyText;
		int currencySkip = 0;
		FlatLabel *left = nullptr;
		FlatLabel *right = nullptr;
	};
	const auto state = wrap->lifetime().make_state<State>(State{
		.rule = LookupCurrencyRule(config.currency),
		.st = st::paymentsMoneyField,
	});
	const auto &rule = state->rule;
	state->currencySkip = rule.space ? state->st.font->spacew : 0;
	state->currencyText = ((!rule.left && rule.space)
		? QString(QChar(' '))
		: QString()) + (*rule.international
			? QString(rule.international)
			: config.currency) + ((rule.left && rule.space)
				? QString(QChar(' '))
				: QString());
	if (rule.left) {
		state->left = CreateChild<FlatLabel>(
			wrap.get(),
			state->currencyText,
			st::paymentsFieldAdditional);
	}
	state->right = CreateChild<FlatLabel>(
		wrap.get(),
		QString(),
		st::paymentsFieldAdditional);
	const auto leftSkip = state->left
		? (state->left->naturalWidth() + state->currencySkip)
		: 0;
	const auto rightSkip = st::paymentsFieldAdditional.style.font->width(
		QString(QChar(rule.decimal))
		+ QString(QChar('0')).repeated(rule.exponent)
		+ (rule.left ? QString() : state->currencyText));
	state->st.textMargins += QMargins(leftSkip, 0, rightSkip, 0);
	state->st.placeholderMargins -= QMargins(leftSkip, 0, rightSkip, 0);
	const auto result = CreateChild<MaskedInputField>(
		wrap.get(),
		state->st,
		std::move(config.placeholder),
		Parse(config));
	result->setPlaceholderHidden(true);
	if (state->left) {
		state->left->move(0, state->st.textMargins.top());
	}
	const auto updateRight = [=] {
		const auto text = result->getLastText();
		const auto width = state->st.font->width(text);
		const auto rect = result->getTextRect();
		const auto &rule = state->rule;
		const auto symbol = QChar(rule.decimal);
		const auto decimal = text.indexOf(symbol);
		const auto zeros = (decimal >= 0)
			? std::max(rule.exponent - (text.size() - decimal - 1), 0)
			: rule.stripDotZero
			? 0
			: rule.exponent;
		const auto valueDecimalSeparator = (decimal >= 0 || !zeros)
			? QString()
			: QString(symbol);
		const auto zeroString = QString(QChar('0'));
		const auto valueRightPart = (text.isEmpty() ? zeroString : QString())
			+ valueDecimalSeparator
			+ zeroString.repeated(zeros);
		const auto right = valueRightPart
			+ (rule.left ? QString() : state->currencyText);
		state->right->setText(right);
		state->right->setTextColorOverride(valueRightPart.isEmpty()
			? std::nullopt
			: std::make_optional(st::windowSubTextFg->c));
		state->right->move(
			(state->st.textMargins.left()
				+ width
				+ ((rule.left || !valueRightPart.isEmpty())
					? 0
					: state->currencySkip)),
			state->st.textMargins.top());
	};
	std::move(
		textPossiblyChanged
	) | rpl::start_with_next(updateRight, result->lifetime());
	if (state->left) {
		state->left->raise();
	}
	state->right->raise();
	return result;
}

[[nodiscard]] MaskedInputField *LookupMaskedField(
		not_null<RpWidget*> wrap,
		FieldConfig &config,
		rpl::producer<> textPossiblyChanged) {
	if (!UseMaskedField(config.type)) {
		return nullptr;
	}
	switch (config.type) {
	case FieldType::Text:
	case FieldType::Email:
		return nullptr;
	case FieldType::CardNumber:
	case FieldType::CardExpireDate:
	case FieldType::CardCVC:
	case FieldType::Country:
		return CreateChild<MaskedInputField>(
			wrap.get(),
			st::paymentsField,
			std::move(config.placeholder),
			Parse(config));
	case FieldType::Phone:
		return CreateChild<PhoneInput>(
			wrap.get(),
			st::paymentsField,
			std::move(config.placeholder),
			ExtractPhonePrefix(config.defaultPhone),
			Parse(config));
	case FieldType::Money:
		return CreateMoneyField(
			wrap,
			config,
			std::move(textPossiblyChanged));
	}
	Unexpected("FieldType in Payments::Ui::LookupMaskedField.");
}

} // namespace

Field::Field(QWidget *parent, FieldConfig &&config)
: _config(config)
, _wrap(CreateWrap(parent, config))
, _input(LookupInputField(_wrap.get(), config))
, _masked(LookupMaskedField(
	_wrap.get(),
	config,
	_textPossiblyChanged.events_starting_with({})))
, _countryIso2(config.value) {
	if (_masked) {
		setupMaskedGeometry();
	}
	if (_config.type == FieldType::Country) {
		setupCountry();
	}
	if (const auto &validator = config.validator) {
		setupValidator(validator);
	} else if (config.type == FieldType::Money) {
		setupValidator(MoneyValidator(LookupCurrencyRule(config.currency)));
	}
	setupFrontBackspace();
	setupSubmit();
}

RpWidget *Field::widget() const {
	return _wrap.get();
}

object_ptr<RpWidget> Field::ownedWidget() const {
	return object_ptr<RpWidget>::fromRaw(_wrap.get());
}

QString Field::value() const {
	return Format(
		_config,
		_input ? _input->getLastText() : _masked->getLastText(),
		_countryIso2);
}

rpl::producer<> Field::frontBackspace() const {
	return _frontBackspace.events();
}

rpl::producer<> Field::finished() const {
	return _finished.events();
}

rpl::producer<> Field::submitted() const {
	return _submitted.events();
}

void Field::setupMaskedGeometry() {
	Expects(_masked != nullptr);

	_wrap->resize(_masked->size());
	_wrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		_masked->resize(width, _masked->height());
	}, _masked->lifetime());
	_masked->heightValue(
	) | rpl::start_with_next([=](int height) {
		_wrap->resize(_wrap->width(), height);
	}, _masked->lifetime());
}

void Field::setupCountry() {
	Expects(_config.type == FieldType::Country);
	Expects(_masked != nullptr);

	QObject::connect(_masked, &MaskedInputField::focused, [=] {
		setFocus();

		const auto name = Data::CountryNameByISO2(_countryIso2);
		const auto country = !name.isEmpty()
			? _countryIso2
			: !_config.defaultCountry.isEmpty()
			? _config.defaultCountry
			: Platform::SystemCountry();
		auto box = Box<CountrySelectBox>(
			country,
			CountrySelectBox::Type::Countries);
		const auto raw = box.data();
		raw->countryChosen(
		) | rpl::start_with_next([=](QString iso2) {
			_countryIso2 = iso2;
			_masked->setText(Data::CountryNameByISO2(iso2));
			_masked->hideError();
			raw->closeBox();
			if (!iso2.isEmpty()) {
				if (_nextField) {
					_nextField->activate();
				} else {
					_submitted.fire({});
				}
			}
		}, _masked->lifetime());
		raw->boxClosing() | rpl::start_with_next([=] {
			setFocus();
		}, _masked->lifetime());
		_config.showBox(std::move(box));
	});
}

void Field::setupValidator(Fn<ValidateResult(ValidateRequest)> validator) {
	Expects(validator != nullptr);

	const auto state = [=]() -> State {
		if (_masked) {
			const auto position = _masked->cursorPosition();
			const auto selectionStart = _masked->selectionStart();
			const auto selectionEnd = _masked->selectionEnd();
			return {
				.value = _masked->getLastText(),
				.position = position,
				.anchor = (selectionStart == selectionEnd
					? position
					: (selectionStart == position)
					? selectionEnd
					: selectionStart),
			};
		}
		const auto cursor = _input->textCursor();
		return {
			.value = _input->getLastText(),
			.position = cursor.position(),
			.anchor = cursor.anchor(),
		};
	};
	const auto save = [=] {
		_was = state();
	};
	const auto setText = [=](const QString &text) {
		if (_masked) {
			_masked->setText(text);
		} else {
			_input->setText(text);
		}
	};
	const auto setPosition = [=](int position) {
		if (_masked) {
			_masked->setCursorPosition(position);
		} else {
			auto cursor = _input->textCursor();
			cursor.setPosition(position);
			_input->setTextCursor(cursor);
		}
	};
	const auto validate = [=] {
		if (_validating) {
			return;
		}
		_validating = true;
		const auto guard = gsl::finally([&] {
			_validating = false;
			save();
			_textPossiblyChanged.fire({});
		});

		const auto now = state();
		const auto result = validator(ValidateRequest{
			.wasValue = _was.value,
			.wasPosition = _was.position,
			.wasAnchor = _was.anchor,
			.nowValue = now.value,
			.nowPosition = now.position,
		});
		_valid = result.finished || !result.invalid;

		const auto changed = (result.value != now.value);
		if (changed) {
			setText(result.value);
		}
		if (changed || result.position != now.position) {
			setPosition(result.position);
		}
		if (result.finished) {
			_finished.fire({});
		} else if (result.invalid) {
			Ui::PostponeCall(
				_masked ? (QWidget*)_masked : _input,
				[=] { showErrorNoFocus(); });
		}
	};
	if (_masked) {
		QObject::connect(_masked, &QLineEdit::cursorPositionChanged, save);
		QObject::connect(_masked, &MaskedInputField::changed, validate);
	} else {
		const auto raw = _input->rawTextEdit();
		QObject::connect(raw, &QTextEdit::cursorPositionChanged, save);
		QObject::connect(_input, &InputField::changed, validate);
	}
}

void Field::setupFrontBackspace() {
	const auto filter = [=](not_null<QEvent*> e) {
		const auto frontBackspace = (e->type() == QEvent::KeyPress)
			&& (static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Backspace)
			&& (_masked
				? (_masked->cursorPosition() == 0
					&& _masked->selectionLength() == 0)
				: (_input->textCursor().position() == 0
					&& _input->textCursor().anchor() == 0));
		if (frontBackspace) {
			_frontBackspace.fire({});
		}
		return base::EventFilterResult::Continue;
	};
	if (_masked) {
		base::install_event_filter(_masked, filter);
	} else {
		base::install_event_filter(_input->rawTextEdit(), filter);
	}
}

void Field::setupSubmit() {
	const auto submitted = [=] {
		if (!_valid) {
			showError();
		} else if (_nextField) {
			_nextField->activate();
		} else {
			_submitted.fire({});
		}
	};
	if (_masked) {
		QObject::connect(_masked, &MaskedInputField::submitted, submitted);
	} else {
		QObject::connect(_input, &InputField::submitted, submitted);
	}
}

void Field::setNextField(not_null<Field*> field) {
	_nextField = field;

	finished() | rpl::start_with_next([=] {
		field->setFocus();
	}, _masked ? _masked->lifetime() : _input->lifetime());
}

void Field::setPreviousField(not_null<Field*> field) {
	frontBackspace(
	) | rpl::start_with_next([=] {
		field->setFocus();
	}, _masked ? _masked->lifetime() : _input->lifetime());
}

void Field::activate() {
	if (_input) {
		_input->setFocus();
	} else {
		_masked->setFocus();
	}
}

void Field::setFocus() {
	if (_config.type == FieldType::Country) {
		_wrap->setFocus();
	} else {
		activate();
	}
}

void Field::setFocusFast() {
	if (_config.type == FieldType::Country) {
		setFocus();
	} else if (_input) {
		_input->setFocusFast();
	} else {
		_masked->setFocusFast();
	}
}

void Field::showError() {
	if (_config.type == FieldType::Country) {
		setFocus();
		_masked->showErrorNoFocus();
	} else if (_input) {
		_input->showError();
	} else {
		_masked->showError();
	}
}

void Field::showErrorNoFocus() {
	if (_input) {
		_input->showErrorNoFocus();
	} else {
		_masked->showErrorNoFocus();
	}
}

} // namespace Payments::Ui
