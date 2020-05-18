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
	return ranges::any_of(
		request,
		[&](const std::vector<Value::Type> &types) {
			Expects(!types.empty());

			return (types[0] == details);
		}
	);
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

bool CanRequireSelfie(Value::Type type) {
	const auto scope = ScopeTypeForValueType(type);
	return (scope == Scope::Type::Address)
		|| (scope == Scope::Type::Identity);
}

bool CanRequireScans(Value::Type type) {
	const auto scope = ScopeTypeForValueType(type);
	return (scope == Scope::Type::Address);
}

bool CanRequireTranslation(Value::Type type) {
	const auto scope = ScopeTypeForValueType(type);
	return (scope == Scope::Type::Address)
		|| (scope == Scope::Type::Identity);
}

bool CanRequireNativeNames(Value::Type type) {
	return (type == Value::Type::PersonalDetails);
}

bool CanHaveErrors(Value::Type type) {
	return (type != Value::Type::Phone) && (type != Value::Type::Email);
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

	// Invalid errors should be skipped while parsing the form.
	for (const auto &[type, value] : form.values) {
		if (value.selfieRequired && !CanRequireSelfie(type)) {
			LOG(("API Error: Bad value requiring selfie."));
			return false;
		} else if (value.translationRequired
			&& !CanRequireTranslation(type)) {
			LOG(("API Error: Bad value requiring translation."));
			return false;
		} else if (value.nativeNames && !CanRequireNativeNames(type)) {
			LOG(("API Error: Bad value requiring native names."));
			return false;
		}
		if (!value.requiresScan(FileType::Scan)) {
			for (const auto &scan : value.files(FileType::Scan)) {
				Assert(scan.error.isEmpty());
			}
			Assert(value.fileMissingError(FileType::Scan).isEmpty());
		}
		if (!value.requiresScan(FileType::Translation)) {
			for (const auto &scan : value.files(FileType::Translation)) {
				Assert(scan.error.isEmpty());
			}
			Assert(value.fileMissingError(FileType::Translation).isEmpty());
		}
		for (const auto &[type, specialScan] : value.specialScans) {
			if (!value.requiresSpecialScan(type)) {
				Assert(specialScan.error.isEmpty());
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

ScopeRow DocumentRowByType(Value::Type type) {
	using Type = Value::Type;
	switch (type) {
	case Type::Passport:
		return {
			tr::lng_passport_identity_passport(tr::now),
			tr::lng_passport_identity_passport_upload(tr::now),
		};
	case Type::DriverLicense:
		return {
			tr::lng_passport_identity_license(tr::now),
			tr::lng_passport_identity_license_upload(tr::now),
		};
	case Type::IdentityCard:
		return {
			tr::lng_passport_identity_card(tr::now),
			tr::lng_passport_identity_card_upload(tr::now),
		};
	case Type::InternalPassport:
		return {
			tr::lng_passport_identity_internal(tr::now),
			tr::lng_passport_identity_internal_upload(tr::now),
		};
	case Type::BankStatement:
		return {
			tr::lng_passport_address_statement(tr::now),
			tr::lng_passport_address_statement_upload(tr::now),
		};
	case Type::UtilityBill:
		return {
			tr::lng_passport_address_bill(tr::now),
			tr::lng_passport_address_bill_upload(tr::now),
		};
	case Type::RentalAgreement:
		return {
			tr::lng_passport_address_agreement(tr::now),
			tr::lng_passport_address_agreement_upload(tr::now),
		};
	case Type::PassportRegistration:
		return {
			tr::lng_passport_address_registration(tr::now),
			tr::lng_passport_address_registration_upload(tr::now),
		};
	case Type::TemporaryRegistration:
		return {
			tr::lng_passport_address_temporary(tr::now),
			tr::lng_passport_address_temporary_upload(tr::now),
		};
	default: Unexpected("Value type in DocumentRowByType.");
	}
}

QString DocumentName(Value::Type type) {
	return DocumentRowByType(type).title;
}

ScopeRow DocumentsOneOfRow(
		const Scope &scope,
		const QString &severalTitle,
		const QString &severalDescription) {
	Expects(!scope.documents.empty());

	const auto &documents = scope.documents;
	if (documents.size() == 1) {
		const auto type = documents.front()->type;
		return DocumentRowByType(type);
	} else if (documents.size() == 2) {
		const auto type1 = documents.front()->type;
		const auto type2 = documents.back()->type;
		return {
			tr::lng_passport_or_title(
				tr::now,
				lt_document,
				DocumentName(type1),
				lt_second_document,
				DocumentName(type2)),
			severalDescription,
		};
	}
	return {
		severalTitle,
		severalDescription,
	};
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
				if (const auto data = value.trimmed(); !data.isEmpty()) {
					if (i->second.isEmpty()) {
						i->second = data;
					} else {
						i->second += ' ' + data;
					}
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
		if (!scope.documents.empty() && !document) {
			return QString();
		}
		if ((document && scope.documents.size() > 1)
			|| (!scope.details
				&& (ScopeTypeForValueType(document->type)
					== Scope::Type::Address))) {
			pushListValue("_type", DocumentName(document->type));
		}
		const auto scheme = GetDocumentScheme(
			scope.type,
			document ? base::make_optional(document->type) : std::nullopt,
			scope.details ? scope.details->nativeNames : false);
		using ValueClass = EditDocumentScheme::ValueClass;
		const auto skipAdditional = [&] {
			if (!fields) {
				return false;
			}
			for (const auto &row : scheme.rows) {
				if (row.valueClass == ValueClass::Additional) {
					const auto i = fields->find(row.key);
					const auto native = (i == end(*fields))
						? QString()
						: i->second.text;
					const auto j = fields->find(row.additionalFallbackKey);
					const auto latin = (j == end(*fields))
						? QString()
						: j->second.text;
					if (latin != native) {
						return false;
					}
				}
			}
			return true;
		}();
		for (const auto &row : scheme.rows) {
			const auto format = row.format;
			if (row.valueClass != ValueClass::Scans) {
				if (!fields) {
					continue;
				} else if (row.valueClass == ValueClass::Additional
					&& skipAdditional) {
					continue;
				}
				const auto i = fields->find(row.key);
				const auto text = (i == end(*fields))
					? QString()
					: i->second.text;
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
				const auto text = (i == end(document->data.parsed.fields))
					? QString()
					: i->second.text;
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
	const auto addReadyError = [&](
			ScopeRow &&row,
			QString titleFallback = QString()) {
		row.ready = ComputeScopeRowReadyString(scope);
		auto errors = QStringList();
		const auto addValueErrors = [&](not_null<const Value*> value) {
			if (!value->error.isEmpty()) {
				errors.push_back(value->error);
			}
			const auto addTypeErrors = [&](FileType type) {
				if (!value->fileMissingError(type).isEmpty()) {
					errors.push_back(value->fileMissingError(type));
				}
				for (const auto &scan : value->files(type)) {
					if (!scan.error.isEmpty()) {
						errors.push_back(scan.error);
					}
				}
			};
			addTypeErrors(FileType::Scan);
			addTypeErrors(FileType::Translation);
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
		} else if (row.title == row.ready && !titleFallback.isEmpty()) {
			row.title = titleFallback;
		}

		if (row.error.isEmpty()
			&& row.ready.isEmpty()
			&& !scope.documents.empty()) {
			if (document) {
				row.description = (scope.type == Scope::Type::Identity)
					? tr::lng_passport_personal_details_enter(tr::now)
					: tr::lng_passport_address_enter(tr::now);
			} else {
				const auto best = ranges::min(
					scope.documents,
					std::less<>(),
					[](not_null<const Value*> document) {
						return document->whatNotFilled();
					});
				const auto notFilled = best->whatNotFilled();
				if (notFilled & Value::kNoTranslationFilled) {
					row.description = tr::lng_passport_translation_needed(tr::now);
				} else if (notFilled & Value::kNoSelfieFilled) {
					row.description = tr::lng_passport_identity_selfie(tr::now);
				}
			}
		}
		return std::move(row);
	};
	switch (scope.type) {
	case Scope::Type::PersonalDetails:
		return addReadyError({
			tr::lng_passport_personal_details(tr::now),
			tr::lng_passport_personal_details_enter(tr::now),
		});
	case Scope::Type::Identity:
		return addReadyError(DocumentsOneOfRow(
			scope,
			tr::lng_passport_identity_title(tr::now),
			tr::lng_passport_identity_description(tr::now)));
	case Scope::Type::AddressDetails:
		return addReadyError({
			tr::lng_passport_address(tr::now),
			tr::lng_passport_address_enter(tr::now),
		});
	case Scope::Type::Address:
		return addReadyError(DocumentsOneOfRow(
			scope,
			tr::lng_passport_address_title(tr::now),
			tr::lng_passport_address_description(tr::now)));
	case Scope::Type::Phone:
		return addReadyError({
			tr::lng_passport_phone_title(tr::now),
			tr::lng_passport_phone_description(tr::now),
		});
	case Scope::Type::Email:
		return addReadyError({
			tr::lng_passport_email_title(tr::now),
			tr::lng_passport_email_description(tr::now),
		});
	default: Unexpected("Scope type in ComputeScopeRow.");
	}
}

} // namespace Passport
