/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_form_view_controller.h"

#include "passport/passport_form_controller.h"

namespace Passport {
namespace {

std::map<Value::Type, Scope::Type> ScopeTypesMap() {
	return {
		{ Value::Type::PersonalDetails, Scope::Type::Identity },
		{ Value::Type::Passport, Scope::Type::Identity },
		{ Value::Type::DriverLicense, Scope::Type::Identity },
		{ Value::Type::IdentityCard, Scope::Type::Identity },
		{ Value::Type::Address, Scope::Type::Address },
		{ Value::Type::UtilityBill, Scope::Type::Address },
		{ Value::Type::BankStatement, Scope::Type::Address },
		{ Value::Type::RentalAgreement, Scope::Type::Address },
		{ Value::Type::Phone, Scope::Type::Phone },
		{ Value::Type::Email, Scope::Type::Email },
	};
}

Scope::Type ScopeTypeForValueType(Value::Type type) {
	static const auto map = ScopeTypesMap();
	const auto i = map.find(type);
	Assert(i != map.end());
	return i->second;
}

std::map<Scope::Type, Value::Type> ScopeFieldsMap() {
	return {
		{ Scope::Type::Identity, Value::Type::PersonalDetails },
		{ Scope::Type::Address, Value::Type::Address },
		{ Scope::Type::Phone, Value::Type::Phone },
		{ Scope::Type::Email, Value::Type::Email },
	};
}

Value::Type FieldsTypeForScopeType(Scope::Type type) {
	static const auto map = ScopeFieldsMap();
	const auto i = map.find(type);
	Assert(i != map.end());
	return i->second;
}

} // namespace

Scope::Scope(Type type, not_null<const Value*> fields)
: type(type)
, fields(fields) {
}

std::vector<Scope> ComputeScopes(not_null<FormController*> controller) {
	auto scopes = std::map<Scope::Type, Scope>();
	const auto &form = controller->form();
	const auto findValue = [&](const Value::Type type) {
		const auto i = form.values.find(type);
		Assert(i != form.values.end());
		return &i->second;
	};
	for (const auto type : form.request) {
		const auto scopeType = ScopeTypeForValueType(type);
		const auto fieldsType = FieldsTypeForScopeType(scopeType);
		const auto [i, ok] = scopes.emplace(
			scopeType,
			Scope(scopeType, findValue(fieldsType)));
		i->second.selfieRequired = (scopeType == Scope::Type::Identity)
			&& form.identitySelfieRequired;
		const auto alreadyIt = ranges::find(
			i->second.files,
			type,
			[](not_null<const Value*> value) { return value->type; });
		if (alreadyIt != end(i->second.files)) {
			LOG(("API Error: Value type %1 multiple times in request."
				).arg(int(type)));
			continue;
		} else if (type != fieldsType) {
			i->second.files.push_back(findValue(type));
		}
	}
	auto result = std::vector<Scope>();
	result.reserve(scopes.size());
	for (auto &[type, scope] : scopes) {
		result.push_back(std::move(scope));
	}
	return result;
}

} // namespace Passport
