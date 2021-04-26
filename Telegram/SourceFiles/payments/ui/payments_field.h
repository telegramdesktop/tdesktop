/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"

namespace Ui {
class RpWidget;
class InputField;
class MaskedInputField;
class BoxContent;
} // namespace Ui

namespace Payments::Ui {

using namespace ::Ui;

enum class FieldType {
	Text,
	CardNumber,
	CardExpireDate,
	CardCVC,
	Country,
	Phone,
	Email,
	Money,
};

struct FieldValidateRequest {
	QString wasValue;
	int wasPosition = 0;
	int wasAnchor = 0;
	QString nowValue;
	int nowPosition = 0;
};

struct FieldValidateResult {
	QString value;
	int position = 0;
	bool invalid = false;
	bool finished = false;
};

[[nodiscard]] inline auto RangeLengthValidator(int minLength, int maxLength) {
	return [=](FieldValidateRequest request) {
		return FieldValidateResult{
			.value = request.nowValue,
			.position = request.nowPosition,
			.invalid = (request.nowValue.size() < minLength
				|| request.nowValue.size() > maxLength),
		};
	};
}

[[nodiscard]] inline auto MaxLengthValidator(int maxLength) {
	return RangeLengthValidator(0, maxLength);
}

[[nodiscard]] inline auto RequiredValidator() {
	return RangeLengthValidator(1, std::numeric_limits<int>::max());
}

[[nodiscard]] inline auto RequiredFinishedValidator() {
	return [=](FieldValidateRequest request) {
		return FieldValidateResult{
			.value = request.nowValue,
			.position = request.nowPosition,
			.invalid = request.nowValue.isEmpty(),
			.finished = !request.nowValue.isEmpty(),
		};
	};
}

struct FieldConfig {
	FieldType type = FieldType::Text;
	rpl::producer<QString> placeholder;
	QString value;
	Fn<FieldValidateResult(FieldValidateRequest)> validator;
	Fn<void(object_ptr<BoxContent>)> showBox;
	QString currency;
	QString defaultPhone;
	QString defaultCountry;
};

class Field final {
public:
	Field(QWidget *parent, FieldConfig &&config);

	[[nodiscard]] RpWidget *widget() const;
	[[nodiscard]] object_ptr<RpWidget> ownedWidget() const;

	[[nodiscard]] QString value() const;
	[[nodiscard]] rpl::producer<> frontBackspace() const;
	[[nodiscard]] rpl::producer<> finished() const;
	[[nodiscard]] rpl::producer<> submitted() const;

	void activate();
	void setFocus();
	void setFocusFast();
	void showError();
	void showErrorNoFocus();

	void setNextField(not_null<Field*> field);
	void setPreviousField(not_null<Field*> field);

private:
	struct State {
		QString value;
		int position = 0;
		int anchor = 0;
	};
	using ValidateRequest = FieldValidateRequest;
	using ValidateResult = FieldValidateResult;

	void setupMaskedGeometry();
	void setupCountry();
	void setupValidator(Fn<ValidateResult(ValidateRequest)> validator);
	void setupFrontBackspace();
	void setupSubmit();

	const FieldConfig _config;
	const base::unique_qptr<RpWidget> _wrap;
	rpl::event_stream<> _frontBackspace;
	rpl::event_stream<> _finished;
	rpl::event_stream<> _submitted;
	rpl::event_stream<> _textPossiblyChanged; // Must be above _masked.
	InputField *_input = nullptr;
	MaskedInputField *_masked = nullptr;
	Field *_nextField = nullptr;
	QString _countryIso2;
	State _was;
	bool _validating = false;
	bool _valid = true;

};

} // namespace Payments::Ui
