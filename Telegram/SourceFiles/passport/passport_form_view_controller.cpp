/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_form_view_controller.h"

#include "passport/passport_form_controller.h"
#include "passport/passport_panel_edit_document.h"
#include "passport/passport_panel_edit_contact.h"
#include "passport/passport_panel_controller.h"
#include "lang/lang_keys.h"

namespace Passport {
namespace {

std::map<Value::Type, Scope::Type> ScopeTypesMap() {
	return {
		{ Value::Type::PersonalDetails, Scope::Type::PersonalDetails },
		{ Value::Type::Passport, Scope::Type::Identity },
		{ Value::Type::DriverLicense, Scope::Type::Identity },
		{ Value::Type::IdentityCard, Scope::Type::Identity },
		{ Value::Type::InternalPassport, Scope::Type::Identity },
		{ Value::Type::Address, Scope::Type::AddressDetails },
		{ Value::Type::UtilityBill, Scope::Type::Address },
		{ Value::Type::BankStatement, Scope::Type::Address },
		{ Value::Type::RentalAgreement, Scope::Type::Address },
		{ Value::Type::PassportRegistration, Scope::Type::Address },
		{ Value::Type::TemporaryRegistration, Scope::Type::Address },
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

std::map<Scope::Type, Value::Type> ScopeDetailsMap() {
	return {
		{ Scope::Type::PersonalDetails, Value::Type::PersonalDetails },
		{ Scope::Type::Identity, Value::Type::PersonalDetails },
		{ Scope::Type::AddressDetails, Value::Type::Address },
		{ Scope::Type::Address, Value::Type::Address },
		{ Scope::Type::Phone, Value::Type::Phone },
		{ Scope::Type::Email, Value::Type::Email },
	};
}

Value::Type DetailsTypeForScopeType(Scope::Type type) {
	static const auto map = ScopeDetailsMap();
	const auto i = map.find(type);
	Assert(i != map.end());
	return i->second;
}

bool InlineDetails(
		const Form::Request &request,
		Scope::Type into,
		Value::Type details) {
	const auto count = ranges::count_if(
		request,
		[&](const std::vector<Value::Type> &types) {
			Expects(!types.empty());

			return ScopeTypeForValueType(types[0]) == into;
		});
	if (count != 1) {
		return false;
	}
	const auto has = ranges::find_if(
		request,
		[&](const std::vector<Value::Type> &types) {
			Expects(!types.empty());

			return (types[0] == details);
		}
	) != end(request);
	return has;
}

bool InlineDetails(const Form::Request &request, Value::Type details) {
	if (details == Value::Type::PersonalDetails) {
		return InlineDetails(request, Scope::Type::Identity, details);
	} else if (details == Value::Type::Address) {
		return InlineDetails(request, Scope::Type::Address, details);
	}
	return false;
}

} // namespace

Scope::Scope(Type type) : type(type) {
}

bool ValidateForm(const Form &form) {
	base::flat_set<Value::Type> values;
	for (const auto &requested : form.request) {
		if (requested.empty()) {
			LOG(("API Error: Empty types list in authorization form row."));
			return false;
		}
		const auto scopeType = ScopeTypeForValueType(requested[0]);
		const auto ownsDetails = (scopeType != Scope::Type::Identity
			&& scopeType != Scope::Type::Address);
		if (ownsDetails && requested.size() != 1) {
			LOG(("API Error: Large types list in authorization form row."));
			return false;
		}
		for (const auto type : requested) {
			if (values.contains(type)) {
				LOG(("API Error: Value twice in authorization form row."));
				return false;
			}
			values.emplace(type);
		}
	}
	for (const auto &[type, value] : form.values) {
		if (!value.translationRequired) {
			for (const auto &scan : value.translations) {
				if (!scan.error.isEmpty()) {
					LOG(("API Error: "
						"Translation error in authorization form value."));
					return false;
				}
			}
			if (!value.translationMissingError.isEmpty()) {
				LOG(("API Error: "
					"Translations error in authorization form value."));
				return false;
			}
		}
		for (const auto &[type, specialScan] : value.specialScans) {
			if (!value.requiresSpecialScan(type)
				&& !specialScan.error.isEmpty()) {
				LOG(("API Error: "
					"Special scan error in authorization form value."));
				return false;
			}
		}
	}
	return true;
}

std::vector<Scope> ComputeScopes(const Form &form) {
	auto result = std::vector<Scope>();
	const auto findValue = [&](const Value::Type type) {
		const auto i = form.values.find(type);
		Assert(i != form.values.end());
		return &i->second;
	};
	for (const auto &requested : form.request) {
		Assert(!requested.empty());
		const auto scopeType = ScopeTypeForValueType(requested[0]);
		const auto detailsType = DetailsTypeForScopeType(scopeType);
		const auto ownsDetails = (scopeType != Scope::Type::Identity
			&& scopeType != Scope::Type::Address);
		const auto inlineDetails = InlineDetails(form.request, detailsType);
		if (ownsDetails && inlineDetails) {
			continue;
		}
		result.push_back(Scope(scopeType));
		auto &scope = result.back();
		scope.details = (ownsDetails || inlineDetails)
			? findValue(detailsType)
			: nullptr;
		if (ownsDetails) {
			Assert(requested.size() == 1);
		} else {
			for (const auto type : requested) {
				scope.documents.push_back(findValue(type));
			}
		}
	}
	return result;
}

QString JoinScopeRowReadyString(
		std::vector<std::pair<QString, QString>> &&values) {
	using Pair = std::pair<QString, QString>;

	if (values.empty()) {
		return QString();
	}
	auto result = QString();
	auto size = ranges::accumulate(
		values,
		0,
		ranges::plus(),
		[](const Pair &v) { return v.second.size(); });
	result.reserve(size + (values.size() - 1) * 2);
	for (const auto &pair : values) {
		if (pair.second.isEmpty()) {
			continue;
		}
		if (!result.isEmpty()) {
			result.append(", ");
		}
		result.append(pair.second);
	}
	return result;
}

QString ComputeScopeRowReadyString(const Scope &scope) {
	switch (scope.type) {
	case Scope::Type::PersonalDetails:
	case Scope::Type::Identity:
	case Scope::Type::AddressDetails:
	case Scope::Type::Address: {
		auto list = std::vector<std::pair<QString, QString>>();
		const auto pushListValue = [&](
				const QString &key,
				const QString &value,
				const QString &keyForAttachmentTo = QString()) {
			if (keyForAttachmentTo.isEmpty()) {
				list.push_back({ key, value.trimmed() });
			} else {
				const auto i = ranges::find(
					list,
					keyForAttachmentTo,
					[](const std::pair<QString, QString> &value) {
					return value.first;
				});
				Assert(i != end(list));
				if (i->second.isEmpty()) {
					i->second = value.trimmed();
				} else {
					i->second += ' ' + value.trimmed();
				}
			}
		};
		const auto fields = scope.details
			? &scope.details->data.parsed.fields
			: nullptr;
		const auto document = [&]() -> const Value* {
			for (const auto &document : scope.documents) {
				if (document->scansAreFilled()) {
					return document;
				}
			}
			return nullptr;
		}();
		if (document && scope.documents.size() > 1) {
			pushListValue("_type", [&] {
				using Type = Value::Type;
				switch (document->type) {
				case Type::Passport:
					return lang(lng_passport_identity_passport);
				case Type::DriverLicense:
					return lang(lng_passport_identity_license);
				case Type::IdentityCard:
					return lang(lng_passport_identity_card);
				case Type::InternalPassport:
					return lang(lng_passport_identity_internal);
				case Type::BankStatement:
					return lang(lng_passport_address_statement);
				case Type::UtilityBill:
					return lang(lng_passport_address_bill);
				case Type::RentalAgreement:
					return lang(lng_passport_address_agreement);
				case Type::PassportRegistration:
					return lang(lng_passport_address_registration);
				case Type::TemporaryRegistration:
					return lang(lng_passport_address_temporary);
				default: Unexpected("Files type in ComputeScopeRowReadyString.");
				}
			}());
		}
		if (!scope.documents.empty() && !document) {
			return QString();
		}
		const auto scheme = GetDocumentScheme(scope.type);
		for (const auto &row : scheme.rows) {
			const auto format = row.format;
			if (row.valueClass == EditDocumentScheme::ValueClass::Fields) {
				if (!fields) {
					continue;
				}
				const auto i = fields->find(row.key);
				if (i == end(*fields)) {
					return QString();
				}
				const auto text = i->second.text;
				if (row.error && row.error(text).has_value()) {
					return QString();
				}
				pushListValue(
					row.key,
					format ? format(text) : text,
					row.keyForAttachmentTo);
			} else if (scope.documents.empty()) {
				continue;
			} else {
				const auto i = document->data.parsed.fields.find(row.key);
				if (i == end(document->data.parsed.fields)) {
					return QString();
				}
				const auto text = i->second.text;
				if (row.error && row.error(text).has_value()) {
					return QString();
				}
				pushListValue(row.key, text, row.keyForAttachmentTo);
			}
		}
		return JoinScopeRowReadyString(std::move(list));
	} break;
	case Scope::Type::Phone:
	case Scope::Type::Email: {
		Assert(scope.details != nullptr);
		const auto format = GetContactScheme(scope.type).format;
		const auto &fields = scope.details->data.parsed.fields;
		const auto i = fields.find("value");
		return (i != end(fields))
			? (format ? format(i->second.text) : i->second.text)
			: QString();
	} break;
	}
	Unexpected("Scope type in ComputeScopeRowReadyString.");
}

ScopeRow ComputeScopeRow(const Scope &scope) {
	const auto addReadyError = [&](ScopeRow &&row) {
		const auto ready = ComputeScopeRowReadyString(scope);
		row.ready = ready;
		auto errors = QStringList();
		const auto addValueErrors = [&](not_null<const Value*> value) {
			if (!value->error.isEmpty()) {
				errors.push_back(value->error);
			}
			if (!value->scanMissingError.isEmpty()) {
				errors.push_back(value->scanMissingError);
			}
			if (!value->translationMissingError.isEmpty()) {
				errors.push_back(value->translationMissingError);
			}
			for (const auto &scan : value->scans) {
				if (!scan.error.isEmpty()) {
					errors.push_back(scan.error);
				}
			}
			for (const auto &scan : value->translations) {
				if (!scan.error.isEmpty()) {
					errors.push_back(scan.error);
				}
			}
			for (const auto &[type, scan] : value->specialScans) {
				if (!scan.error.isEmpty()) {
					errors.push_back(scan.error);
				}
			}
			for (const auto &[key, value] : value->data.parsed.fields) {
				if (!value.error.isEmpty()) {
					errors.push_back(value.error);
				}
			}
		};
		const auto document = [&]() -> const Value* {
			for (const auto &document : scope.documents) {
				if (document->scansAreFilled()) {
					return document;
				}
			}
			return nullptr;
		}();
		if (document) {
			addValueErrors(document);
		}
		if (scope.details) {
			addValueErrors(scope.details);
		}
		if (!errors.isEmpty()) {
			row.error = errors[0];// errors.join('\n');
		}
		// #TODO passport half-full value
		//if (row.error.isEmpty()
		//	&& row.ready.isEmpty()
		//	&& scope.type == Scope::Type::Identity
		//	&& scope.selfieRequired) {
		//	auto noSelfieScope = scope;
		//	noSelfieScope.selfieRequired = false;
		//	if (!ComputeScopeRowReadyString(noSelfieScope).isEmpty()) {
		//		// Only selfie is missing.
		//		row.description = lang(lng_passport_identity_selfie);
		//	}
		//}
		return row;
	};
	switch (scope.type) {
	case Scope::Type::PersonalDetails:
		return addReadyError({
			lang(lng_passport_personal_details),
			lang(lng_passport_personal_details_enter),
		});
	case Scope::Type::Identity:
		Assert(!scope.documents.empty());
		if (scope.documents.size() == 1) {
			switch (scope.documents.front()->type) {
			case Value::Type::Passport:
				return addReadyError({
					lang(lng_passport_identity_passport),
					lang(lng_passport_identity_passport_upload),
				});
			case Value::Type::IdentityCard:
				return addReadyError({
					lang(lng_passport_identity_card),
					lang(lng_passport_identity_card_upload),
				});
			case Value::Type::DriverLicense:
				return addReadyError({
					lang(lng_passport_identity_license),
					lang(lng_passport_identity_license_upload),
				});
			case Value::Type::InternalPassport:
				return addReadyError({
					lang(lng_passport_identity_internal),
					lang(lng_passport_identity_internal_upload),
				});
			default: Unexpected("Identity type in ComputeScopeRow.");
			}
		}
		return addReadyError({
			lang(lng_passport_identity_title),
			lang(lng_passport_identity_description),
		});
	case Scope::Type::AddressDetails:
		return addReadyError({
			lang(lng_passport_address),
			lang(lng_passport_address_enter),
			});
	case Scope::Type::Address:
		Assert(!scope.documents.empty());
		if (scope.documents.size() == 1) {
			switch (scope.documents.front()->type) {
			case Value::Type::BankStatement:
				return addReadyError({
					lang(lng_passport_address_statement),
					lang(lng_passport_address_statement_upload),
				});
			case Value::Type::UtilityBill:
				return addReadyError({
					lang(lng_passport_address_bill),
					lang(lng_passport_address_bill_upload),
				});
			case Value::Type::RentalAgreement:
				return addReadyError({
					lang(lng_passport_address_agreement),
					lang(lng_passport_address_agreement_upload),
				});
			case Value::Type::PassportRegistration:
				return addReadyError({
					lang(lng_passport_address_registration),
					lang(lng_passport_address_registration_upload),
				});
			case Value::Type::TemporaryRegistration:
				return addReadyError({
					lang(lng_passport_address_temporary),
					lang(lng_passport_address_temporary_upload),
				});
			default: Unexpected("Address type in ComputeScopeRow.");
			}
		}
		return addReadyError({
			lang(lng_passport_address_title),
			lang(lng_passport_address_description),
		});
	case Scope::Type::Phone:
		return addReadyError({
			lang(lng_passport_phone_title),
			lang(lng_passport_phone_description),
		});
	case Scope::Type::Email:
		return addReadyError({
			lang(lng_passport_email_title),
			lang(lng_passport_email_description),
		});
	default: Unexpected("Scope type in ComputeScopeRow.");
	}
}

} // namespace Passport
