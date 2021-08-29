/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "countries/countries_instance.h"

namespace Countries {
namespace {

auto SingleInstance = CountriesInstance();

const std::array<Info, 231> FallbackList = { {
	{ "Afghanistan", "AF" },
	{ "Albania", "AL" },
	{ "Algeria", "DZ" },
	{ "American Samoa", "AS" },
	{ "Andorra", "AD" },
	{ "Angola", "AO" },
	{ "Anguilla", "AI" },
	{ "Antigua & Barbuda", "AG" },
	{ "Argentina", "AR" },
	{ "Armenia", "AM" },
	{ "Aruba", "AW" },
	{ "Australia", "AU" },
	{ "Austria", "AT" },
	{ "Azerbaijan", "AZ" },
	{ "Bahamas", "BS" },
	{ "Bahrain", "BH" },
	{ "Bangladesh", "BD" },
	{ "Barbados", "BB" },
	{ "Belarus", "BY" },
	{ "Belgium", "BE" },
	{ "Belize", "BZ" },
	{ "Benin", "BJ" },
	{ "Bermuda", "BM" },
	{ "Bhutan", "BT" },
	{ "Bolivia", "BO" },
	{ "Bonaire, Sint Eustatius & Saba", "BQ" },
	{ "Bosnia & Herzegovina", "BA" },
	{ "Botswana", "BW" },
	{ "Brazil", "BR" },
	{ "British Virgin Islands", "VG" },
	{ "Brunei Darussalam", "BN" },
	{ "Bulgaria", "BG" },
	{ "Burkina Faso", "BF" },
	{ "Burundi", "BI" },
	{ "Cambodia", "KH" },
	{ "Cameroon", "CM" },
	{ "Canada", "CA" },
	{ "Cape Verde", "CV" },
	{ "Cayman Islands", "KY" },
	{ "Central African Republic", "CF" },
	{ "Chad", "TD" },
	{ "Chile", "CL" },
	{ "China", "CN" },
	{ "Colombia", "CO" },
	{ "Comoros", "KM" },
	{ "Congo", "CG" },
	{ "Congo, Democratic Republic", "CD" },
	{ "Cook Islands", "CK" },
	{ "Costa Rica", "CR" },
	{ "Croatia", "HR" },
	{ "Cuba", "CU" },
	{ "Curaçao", "CW" },
	{ "Cyprus", "CY" },
	{ "Czech Republic", "CZ" },
	{ "Côte d`Ivoire", "CI" },
	{ "Denmark", "DK" },
	{ "Diego Garcia", "IO" },
	{ "Djibouti", "DJ" },
	{ "Dominica", "DM" },
	{ "Dominican Republic", "DO" },
	{ "Ecuador", "EC" },
	{ "Egypt", "EG" },
	{ "El Salvador", "SV" },
	{ "Equatorial Guinea", "GQ" },
	{ "Eritrea", "ER" },
	{ "Estonia", "EE" },
	{ "Ethiopia", "ET" },
	{ "Falkland Islands", "FK" },
	{ "Faroe Islands", "FO" },
	{ "Fiji", "FJ" },
	{ "Finland", "FI" },
	{ "France", "FR" },
	{ "French Guiana", "GF" },
	{ "French Polynesia", "PF" },
	{ "Gabon", "GA" },
	{ "Gambia", "GM" },
	{ "Georgia", "GE" },
	{ "Germany", "DE" },
	{ "Ghana", "GH" },
	{ "Gibraltar", "GI" },
	{ "Greece", "GR" },
	{ "Greenland", "GL" },
	{ "Grenada", "GD" },
	{ "Guadeloupe", "GP" },
	{ "Guam", "GU" },
	{ "Guatemala", "GT" },
	{ "Guinea", "GN" },
	{ "Guinea-Bissau", "GW" },
	{ "Guyana", "GY" },
	{ "Haiti", "HT" },
	{ "Honduras", "HN" },
	{ "Hong Kong", "HK" },
	{ "Hungary", "HU" },
	{ "Iceland", "IS" },
	{ "India", "IN" },
	{ "Indonesia", "ID" },
	{ "Iran", "IR" },
	{ "Iraq", "IQ" },
	{ "Ireland", "IE" },
	{ "Israel", "IL" },
	{ "Italy", "IT" },
	{ "Jamaica", "JM" },
	{ "Japan", "JP" },
	{ "Jordan", "JO" },
	{ "Kazakhstan", "KZ" },
	{ "Kenya", "KE" },
	{ "Kiribati", "KI" },
	{ "Kuwait", "KW" },
	{ "Kyrgyzstan", "KG" },
	{ "Laos", "LA" },
	{ "Latvia", "LV" },
	{ "Lebanon", "LB" },
	{ "Lesotho", "LS" },
	{ "Liberia", "LR" },
	{ "Libya", "LY" },
	{ "Liechtenstein", "LI" },
	{ "Lithuania", "LT" },
	{ "Luxembourg", "LU" },
	{ "Macau", "MO" },
	{ "Macedonia", "MK" },
	{ "Madagascar", "MG" },
	{ "Malawi", "MW" },
	{ "Malaysia", "MY" },
	{ "Maldives", "MV" },
	{ "Mali", "ML" },
	{ "Malta", "MT" },
	{ "Marshall Islands", "MH" },
	{ "Martinique", "MQ" },
	{ "Mauritania", "MR" },
	{ "Mauritius", "MU" },
	{ "Mexico", "MX" },
	{ "Micronesia", "FM" },
	{ "Moldova", "MD" },
	{ "Monaco", "MC" },
	{ "Mongolia", "MN" },
	{ "Montenegro", "ME" },
	{ "Montserrat", "MS" },
	{ "Morocco", "MA" },
	{ "Mozambique", "MZ" },
	{ "Myanmar", "MM" },
	{ "Namibia", "NA" },
	{ "Nauru", "NR" },
	{ "Nepal", "NP" },
	{ "Netherlands", "NL" },
	{ "New Caledonia", "NC" },
	{ "New Zealand", "NZ" },
	{ "Nicaragua", "NI" },
	{ "Niger", "NE" },
	{ "Nigeria", "NG" },
	{ "Niue", "NU" },
	{ "Norfolk Island", "NF" },
	{ "North Korea", "KP" },
	{ "Northern Mariana Islands", "MP" },
	{ "Norway", "NO" },
	{ "Oman", "OM" },
	{ "Pakistan", "PK" },
	{ "Palau", "PW" },
	{ "Palestine", "PS" },
	{ "Panama", "PA" },
	{ "Papua New Guinea", "PG" },
	{ "Paraguay", "PY" },
	{ "Peru", "PE" },
	{ "Philippines", "PH" },
	{ "Poland", "PL" },
	{ "Portugal", "PT" },
	{ "Puerto Rico", "PR" },
	{ "Qatar", "QA" },
	{ "Romania", "RO" },
	{ "Russian Federation", "RU" },
	{ "Rwanda", "RW" },
	{ "Réunion", "RE" },
	{ "Saint Helena", "SH" },
	{ "Saint Helena", "SH2" },
	{ "Saint Kitts & Nevis", "KN" },
	{ "Saint Lucia", "LC" },
	{ "Saint Pierre & Miquelon", "PM" },
	{ "Saint Vincent & the Grenadines", "VC" },
	{ "Samoa", "WS" },
	{ "San Marino", "SM" },
	{ "Saudi Arabia", "SA" },
	{ "Senegal", "SN" },
	{ "Serbia", "RS" },
	{ "Seychelles", "SC" },
	{ "Sierra Leone", "SL" },
	{ "Singapore", "SG" },
	{ "Sint Maarten", "SX" },
	{ "Slovakia", "SK" },
	{ "Slovenia", "SI" },
	{ "Solomon Islands", "SB" },
	{ "Somalia", "SO" },
	{ "South Africa", "ZA" },
	{ "South Korea", "KR" },
	{ "South Sudan", "SS" },
	{ "Spain", "ES" },
	{ "Sri Lanka", "LK" },
	{ "Sudan", "SD" },
	{ "Suriname", "SR" },
	{ "Swaziland", "SZ" },
	{ "Sweden", "SE" },
	{ "Switzerland", "CH" },
	{ "Syrian Arab Republic", "SY" },
	{ "São Tomé & Príncipe", "ST" },
	{ "Taiwan", "TW" },
	{ "Tajikistan", "TJ" },
	{ "Tanzania", "TZ" },
	{ "Thailand", "TH" },
	{ "Timor-Leste", "TL" },
	{ "Togo", "TG" },
	{ "Tokelau", "TK" },
	{ "Tonga", "TO" },
	{ "Trinidad & Tobago", "TT" },
	{ "Tunisia", "TN" },
	{ "Turkey", "TR" },
	{ "Turkmenistan", "TM" },
	{ "Turks & Caicos Islands", "TC" },
	{ "Tuvalu", "TV" },
	{ "US Virgin Islands", "VI" },
	{ "USA", "US", "United States of America" },
	{ "Uganda", "UG" },
	{ "Ukraine", "UA" },
	{ "United Arab Emirates", "AE" },
	{ "United Kingdom", "GB" },
	{ "Uruguay", "UY" },
	{ "Uzbekistan", "UZ" },
	{ "Vanuatu", "VU" },
	{ "Venezuela", "VE" },
	{ "Vietnam", "VN" },
	{ "Wallis & Futuna", "WF" },
	{ "Yemen", "YE" },
	{ "Zambia", "ZM" },
	{ "Zimbabwe", "ZW" },
} };

} // namespace

CountriesInstance::CountriesInstance() {
}

const std::vector<Info> &CountriesInstance::list() {
	if (_list.empty()) {
		_list = (FallbackList | ranges::to_vector);
	}
	return _list;
}

void CountriesInstance::setList(std::vector<Info> &&infos) {
	_list = std::move(infos);
}

const CountriesInstance::Map &CountriesInstance::byCode() {
	if (_byCode.empty()) {
		_byCode.reserve(list().size());
		for (const auto &entry : list()) {
			for (const auto &code : entry.codes) {
				_byCode.insert(code.callingCode, &entry);
			}
		}
	}
	return _byCode;
}

const CountriesInstance::Map &CountriesInstance::byISO2() {
	if (_byISO2.empty()) {
		_byISO2.reserve(list().size());
		for (const auto &entry : list()) {
			_byISO2.insert(entry.iso2, &entry);
		}
	}
	return _byISO2;
}

QString CountriesInstance::validPhoneCode(QString fullCode) {
	const auto &listByCode = byCode();
	while (fullCode.length()) {
		const auto i = listByCode.constFind(fullCode);
		if (i != listByCode.cend()) {
			return fullCode;
		}
		fullCode.chop(1);
	}
	return QString();
}

QString CountriesInstance::countryNameByISO2(const QString &iso) {
	const auto &listByISO2 = byISO2();
	const auto i = listByISO2.constFind(iso);
	return (i != listByISO2.cend()) ? (*i)->name : QString();
}

QString CountriesInstance::countryISO2ByPhone(const QString &phone) {
	const auto &listByCode = byCode();
	const auto code = validPhoneCode(phone);
	const auto i = listByCode.find(code);
	return (i != listByCode.cend()) ? (*i)->iso2 : QString();
}

FormatResult CountriesInstance::format(FormatArgs args) {
	// Ported from TDLib.
	if (args.phone.isEmpty()) {
		return FormatResult();
	}
	const auto &phoneNumber = args.phone;

	const Info *bestCountryPtr = nullptr;
	const CallingCodeInfo *bestCallingCodePtr = nullptr;
	auto bestLength = size_t(0);
	auto isPrefix = false;
	for (const auto &country : list()) {
		for (auto &callingCode : country.codes) {
			if (phoneNumber.startsWith(callingCode.callingCode)) {
				const auto codeSize = callingCode.callingCode.size();
				for (const auto &prefix : callingCode.prefixes) {
					if (prefix.startsWith(phoneNumber.midRef(codeSize))) {
						isPrefix = true;
					}
					if ((codeSize + prefix.size()) > bestLength &&
							phoneNumber.midRef(codeSize).startsWith(prefix)) {
						bestCountryPtr = &country;
						bestCallingCodePtr = &callingCode;
						bestLength = codeSize + prefix.size();
					}
				}
			}
			if (callingCode.callingCode.startsWith(phoneNumber)) {
				isPrefix = true;
			}
		}
	}
	if (bestCountryPtr == nullptr) {
		return FormatResult{ .formatted = phoneNumber };
	}

	const auto codeSize = int(bestCallingCodePtr->callingCode.size());

	if (args.onlyGroups && args.incomplete) {
		auto groups = args.skipCode
			? QVector<int>()
			: QVector<int>{ codeSize };
		auto groupSize = 0;
		for (const auto &c : bestCallingCodePtr->patterns.front()) {
			if (c == ' ') {
				groups.push_back(base::take(groupSize));
			} else {
				groupSize++;
			}
		}
		if (groupSize) {
			groups.push_back(base::take(groupSize));
		}
		return FormatResult{ .groups = std::move(groups) };
	}

	const auto formattedPart = phoneNumber.mid(codeSize);
	auto formattedResult = formattedPart;
	auto groups = QVector<int>();
	auto maxMatchedDigits = size_t(0);
	for (auto &pattern : bestCallingCodePtr->patterns) {
		auto resultGroups = QVector<int>();
		auto result = QString();
		auto currentPatternPos = int(0);
		auto isFailedMatch = false;
		auto matchedDigits = size_t(0);
		auto groupSize = 0;
		for (const auto &c : formattedPart) {
			while ((currentPatternPos < pattern.size())
				&& (pattern[currentPatternPos] != 'X')
				&& !pattern[currentPatternPos].isDigit()) {
				if (args.onlyGroups) {
					resultGroups.push_back(groupSize);
					groupSize = 0;
				} else {
					result += pattern[currentPatternPos];
				}
				currentPatternPos++;
			}
			if (!args.onlyGroups && (currentPatternPos == pattern.size())) {
				result += ' ';
			}
			if ((currentPatternPos >= pattern.size())
				|| (pattern[currentPatternPos] == 'X')) {
				currentPatternPos++;
				if (args.onlyGroups) {
					groupSize++;
				} else {
					result += c;
				}
			} else {
				if (c == pattern[currentPatternPos]) {
					matchedDigits++;
					currentPatternPos++;
					if (args.onlyGroups) {
						groupSize++;
					} else {
						result += c;
					}
				} else {
					isFailedMatch = true;
					break;
				}
			}
		}
		if (groupSize) {
			resultGroups.push_back(groupSize);
		}
		if (!isFailedMatch && matchedDigits >= maxMatchedDigits) {
			maxMatchedDigits = matchedDigits;
			if (args.onlyGroups) {
				groups = std::move(resultGroups);
			} else {
				formattedResult = std::move(result);
			}
		}
	}

	if (!args.skipCode) {
		if (args.onlyGroups) {
			groups.push_front(codeSize);
		} else {
			formattedResult = '+'
				+ bestCallingCodePtr->callingCode
				+ ' '
				+ std::move(formattedResult);
		}
	}

	return FormatResult{
		.formatted = (args.onlyGroups
			? QString()
			: std::move(formattedResult)),
		.groups = std::move(groups),
	};
}

CountriesInstance &Instance() {
	return SingleInstance;
}

} // namespace Countries
