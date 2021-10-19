/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "countries/countries_instance.h"

#include "base/qt_adapters.h"

namespace Countries {
namespace {

auto SingleInstance = CountriesInstance();

const std::array<Info, 231> FallbackList = { {
	{ "Andorra", "AD", "", { CallingCodeInfo{ "376", {}, { "XX XX XX" } } }, false },
	{ "United Arab Emirates", "AE", "", { CallingCodeInfo{ "971", {}, { "XX XXX XXXX" } } }, false },
	{ "Afghanistan", "AF", "", { CallingCodeInfo{ "93", {}, { "XXX XXX XXX" } } }, false },
	{ "Antigua & Barbuda", "AG", "", { CallingCodeInfo{ "1268", {}, { "XXX XXXX" } } }, false },
	{ "Anguilla", "AI", "", { CallingCodeInfo{ "1264", {}, { "XXX XXXX" } } }, false },
	{ "Albania", "AL", "", { CallingCodeInfo{ "355", {}, { "XX XXX XXXX" } } }, false },
	{ "Armenia", "AM", "", { CallingCodeInfo{ "374", {}, { "XX XXX XXX" } } }, false },
	{ "Angola", "AO", "", { CallingCodeInfo{ "244", {}, { "XXX XXX XXX" } } }, false },
	{ "Argentina", "AR", "", { CallingCodeInfo{ "54", {}, {} } }, false },
	{ "American Samoa", "AS", "", { CallingCodeInfo{ "1684", {}, { "XXX XXXX" } } }, false },
	{ "Austria", "AT", "", { CallingCodeInfo{ "43", {}, { "X XXXXXXXX" } } }, false },
	{ "Australia", "AU", "", { CallingCodeInfo{ "61", {}, { "X XXXX XXXX" } } }, false },
	{ "Aruba", "AW", "", { CallingCodeInfo{ "297", {}, { "XXX XXXX" } } }, false },
	{ "Azerbaijan", "AZ", "", { CallingCodeInfo{ "994", {}, { "XX XXX XXXX" } } }, false },
	{ "Bosnia & Herzegovina", "BA", "", { CallingCodeInfo{ "387", {}, { "XX XXX XXX" } } }, false },
	{ "Barbados", "BB", "", { CallingCodeInfo{ "1246", {}, { "XXX XXXX" } } }, false },
	{ "Bangladesh", "BD", "", { CallingCodeInfo{ "880", {}, { "XX XXX XXX" } } }, false },
	{ "Belgium", "BE", "", { CallingCodeInfo{ "32", {}, { "XXX XX XX XX" } } }, false },
	{ "Burkina Faso", "BF", "", { CallingCodeInfo{ "226", {}, { "XX XX XX XX" } } }, false },
	{ "Bulgaria", "BG", "", { CallingCodeInfo{ "359", {}, {} } }, false },
	{ "Bahrain", "BH", "", { CallingCodeInfo{ "973", {}, { "XXXX XXXX" } } }, false },
	{ "Burundi", "BI", "", { CallingCodeInfo{ "257", {}, { "XX XX XXXX" } } }, false },
	{ "Benin", "BJ", "", { CallingCodeInfo{ "229", {}, { "XX XXX XXX" } } }, false },
	{ "Bermuda", "BM", "", { CallingCodeInfo{ "1441", {}, { "XXX XXXX" } } }, false },
	{ "Brunei Darussalam", "BN", "", { CallingCodeInfo{ "673", {}, { "XXX XXXX" } } }, false },
	{ "Bolivia", "BO", "", { CallingCodeInfo{ "591", {}, { "X XXX XXXX" } } }, false },
	{ "Bonaire, Sint Eustatius & Saba", "BQ", "", { CallingCodeInfo{ "599", {}, {} } }, false },
	{ "Brazil", "BR", "", { CallingCodeInfo{ "55", {}, { "XX XXXXX XXXX" } } }, false },
	{ "Bahamas", "BS", "", { CallingCodeInfo{ "1242", {}, { "XXX XXXX" } } }, false },
	{ "Bhutan", "BT", "", { CallingCodeInfo{ "975", {}, { "XX XXX XXX" } } }, false },
	{ "Botswana", "BW", "", { CallingCodeInfo{ "267", {}, { "XX XXX XXX" } } }, false },
	{ "Belarus", "BY", "", { CallingCodeInfo{ "375", {}, { "XX XXX XXXX" } } }, false },
	{ "Belize", "BZ", "", { CallingCodeInfo{ "501", {}, {} } }, false },
	{ "Canada", "CA", "", { CallingCodeInfo{ "1", { "403" }, { "XXX XXX XXXX" } } }, false },
	{ "Congo (Dem. Rep.)", "CD", "", { CallingCodeInfo{ "243", {}, { "XX XXX XXXX" } } }, false },
	{ "Central African Rep.", "CF", "", { CallingCodeInfo{ "236", {}, { "XX XX XX XX" } } }, false },
	{ "Congo (Rep.)", "CG", "", { CallingCodeInfo{ "242", {}, { "XX XXX XXXX" } } }, false },
	{ "Switzerland", "CH", "", { CallingCodeInfo{ "41", {}, { "XX XXX XXXX" } } }, false },
	{ "Côte d'Ivoire", "CI", "", { CallingCodeInfo{ "225", {}, { "XX XX XX XXXX" } } }, false },
	{ "Cook Islands", "CK", "", { CallingCodeInfo{ "682", {}, {} } }, false },
	{ "Chile", "CL", "", { CallingCodeInfo{ "56", {}, { "X XXXX XXXX" } } }, false },
	{ "Cameroon", "CM", "", { CallingCodeInfo{ "237", {}, { "XXXX XXXX" } } }, false },
	{ "China", "CN", "", { CallingCodeInfo{ "86", {}, { "XXX XXXX XXXX" } } }, false },
	{ "Colombia", "CO", "", { CallingCodeInfo{ "57", {}, { "XXX XXX XXXX" } } }, false },
	{ "Costa Rica", "CR", "", { CallingCodeInfo{ "506", {}, { "XXXX XXXX" } } }, false },
	{ "Cuba", "CU", "", { CallingCodeInfo{ "53", {}, { "X XXX XXXX" } } }, false },
	{ "Cape Verde", "CV", "", { CallingCodeInfo{ "238", {}, { "XXX XXXX" } } }, false },
	{ "Curaçao", "CW", "", { CallingCodeInfo{ "599", { "9" }, {} } }, false },
	{ "Cyprus", "CY", "", { CallingCodeInfo{ "357", {}, { "XXXX XXXX" } } }, false },
	{ "Czech Republic", "CZ", "", { CallingCodeInfo{ "420", {}, { "XXX XXX XXX" } } }, false },
	{ "Germany", "DE", "", { CallingCodeInfo{ "49", {}, { "XXXX XXXXXXX" } } }, false },
	{ "Djibouti", "DJ", "", { CallingCodeInfo{ "253", {}, { "XX XX XX XX" } } }, false },
	{ "Denmark", "DK", "", { CallingCodeInfo{ "45", {}, { "XXXX XXXX" } } }, false },
	{ "Dominica", "DM", "", { CallingCodeInfo{ "1767", {}, { "XXX XXXX" } } }, false },
	{ "Dominican Rep.", "DO", "", { CallingCodeInfo{ "1809", {}, { "XXX XXXX" } } }, false },
	{ "Algeria", "DZ", "", { CallingCodeInfo{ "213", {}, { "XXX XX XX XX" } } }, false },
	{ "Ecuador", "EC", "", { CallingCodeInfo{ "593", {}, { "XX XXX XXXX" } } }, false },
	{ "Estonia", "EE", "", { CallingCodeInfo{ "372", {}, { "XXXX XXXX" } } }, false },
	{ "Egypt", "EG", "", { CallingCodeInfo{ "20", {}, { "XX XXXX XXXX" } } }, false },
	{ "Eritrea", "ER", "", { CallingCodeInfo{ "291", {}, { "X XXX XXX" } } }, false },
	{ "Spain", "ES", "", { CallingCodeInfo{ "34", {}, { "XXX XXX XXX" } } }, false },
	{ "Ethiopia", "ET", "", { CallingCodeInfo{ "251", {}, { "XX XXX XXXX" } } }, false },
	{ "Finland", "FI", "", { CallingCodeInfo{ "358", {}, {} } }, false },
	{ "Fiji", "FJ", "", { CallingCodeInfo{ "679", {}, { "XXX XXXX" } } }, false },
	{ "Falkland Islands", "FK", "", { CallingCodeInfo{ "500", {}, {} } }, false },
	{ "Micronesia", "FM", "", { CallingCodeInfo{ "691", {}, {} } }, false },
	{ "Faroe Islands", "FO", "", { CallingCodeInfo{ "298", {}, { "XXX XXX" } } }, false },
	{ "France", "FR", "", { CallingCodeInfo{ "33", {}, { "X XX XX XX XX" } } }, false },
	{ "Gabon", "GA", "", { CallingCodeInfo{ "241", {}, { "X XX XX XX" } } }, false },
	{ "United Kingdom", "GB", "", { CallingCodeInfo{ "44", {}, { "XXXX XXXXXX" } } }, false },
	{ "Grenada", "GD", "", { CallingCodeInfo{ "1473", {}, { "XXX XXXX" } } }, false },
	{ "Georgia", "GE", "", { CallingCodeInfo{ "995", {}, { "XXX XXX XXX" } } }, false },
	{ "French Guiana", "GF", "", { CallingCodeInfo{ "594", {}, {} } }, false },
	{ "Ghana", "GH", "", { CallingCodeInfo{ "233", {}, { "XX XXX XXXX" } } }, false },
	{ "Gibraltar", "GI", "", { CallingCodeInfo{ "350", {}, { "XXXX XXXX" } } }, false },
	{ "Greenland", "GL", "", { CallingCodeInfo{ "299", {}, { "XXX XXX" } } }, false },
	{ "Gambia", "GM", "", { CallingCodeInfo{ "220", {}, { "XXX XXXX" } } }, false },
	{ "Guinea", "GN", "", { CallingCodeInfo{ "224", {}, { "XXX XXX XXX" } } }, false },
	{ "Guadeloupe", "GP", "", { CallingCodeInfo{ "590", {}, { "XXX XX XX XX" } } }, false },
	{ "Equatorial Guinea", "GQ", "", { CallingCodeInfo{ "240", {}, { "XXX XXX XXX" } } }, false },
	{ "Greece", "GR", "", { CallingCodeInfo{ "30", {}, { "XXX XXX XXXX" } } }, false },
	{ "Guatemala", "GT", "", { CallingCodeInfo{ "502", {}, { "X XXX XXXX" } } }, false },
	{ "Guam", "GU", "", { CallingCodeInfo{ "1671", {}, { "XXX XXXX" } } }, false },
	{ "Guinea-Bissau", "GW", "", { CallingCodeInfo{ "245", {}, { "XXX XXXX" } } }, false },
	{ "Guyana", "GY", "", { CallingCodeInfo{ "592", {}, {} } }, false },
	{ "Hong Kong", "HK", "", { CallingCodeInfo{ "852", {}, { "X XXX XXXX" } } }, false },
	{ "Honduras", "HN", "", { CallingCodeInfo{ "504", {}, { "XXXX XXXX" } } }, false },
	{ "Croatia", "HR", "", { CallingCodeInfo{ "385", {}, { "XX XXX XXX" } } }, false },
	{ "Haiti", "HT", "", { CallingCodeInfo{ "509", {}, { "XXXX XXXX" } } }, false },
	{ "Hungary", "HU", "", { CallingCodeInfo{ "36", {}, { "XXX XXX XXX" } } }, false },
	{ "Indonesia", "ID", "", { CallingCodeInfo{ "62", {}, { "XXX XXXXXX" } } }, false },
	{ "Ireland", "IE", "", { CallingCodeInfo{ "353", {}, { "XX XXX XXXX" } } }, false },
	{ "Israel", "IL", "", { CallingCodeInfo{ "972", {}, { "XX XXX XXXX" } } }, false },
	{ "India", "IN", "", { CallingCodeInfo{ "91", {}, { "XXXXX XXXXX" } } }, false },
	{ "Diego Garcia", "IO", "", { CallingCodeInfo{ "246", {}, { "XXX XXXX" } } }, false },
	{ "Iraq", "IQ", "", { CallingCodeInfo{ "964", {}, { "XXX XXX XXXX" } } }, false },
	{ "Iran", "IR", "", { CallingCodeInfo{ "98", {}, { "XXX XXX XXXX" } } }, false },
	{ "Iceland", "IS", "", { CallingCodeInfo{ "354", {}, { "XXX XXXX" } } }, false },
	{ "Italy", "IT", "", { CallingCodeInfo{ "39", {}, { "XXX XXX XXX" } } }, false },
	{ "Jamaica", "JM", "", { CallingCodeInfo{ "1876", {}, { "XXX XXXX" } } }, false },
	{ "Jordan", "JO", "", { CallingCodeInfo{ "962", {}, { "X XXXX XXXX" } } }, false },
	{ "Japan", "JP", "", { CallingCodeInfo{ "81", {}, { "XX XXXX XXXX" } } }, false },
	{ "Kenya", "KE", "", { CallingCodeInfo{ "254", {}, { "XXX XXX XXX" } } }, false },
	{ "Kyrgyzstan", "KG", "", { CallingCodeInfo{ "996", {}, { "XXX XXXXXX" } } }, false },
	{ "Cambodia", "KH", "", { CallingCodeInfo{ "855", {}, { "XX XXX XXX" } } }, false },
	{ "Kiribati", "KI", "", { CallingCodeInfo{ "686", {}, { "XXXX XXXX" } } }, false },
	{ "Comoros", "KM", "", { CallingCodeInfo{ "269", {}, { "XXX XXXX" } } }, false },
	{ "Saint Kitts & Nevis", "KN", "", { CallingCodeInfo{ "1869", {}, { "XXX XXXX" } } }, false },
	{ "North Korea", "KP", "", { CallingCodeInfo{ "850", {}, {} } }, false },
	{ "South Korea", "KR", "", { CallingCodeInfo{ "82", {}, { "XX XXXX XXX" } } }, false },
	{ "Kuwait", "KW", "", { CallingCodeInfo{ "965", {}, { "XXXX XXXX" } } }, false },
	{ "Cayman Islands", "KY", "", { CallingCodeInfo{ "1345", {}, { "XXX XXXX" } } }, false },
	{ "Kazakhstan", "KZ", "", { CallingCodeInfo{ "7", { "6" }, { "XXX XXX XX XX" } } }, false },
	{ "Laos", "LA", "", { CallingCodeInfo{ "856", {}, { "XX XX XXX XXX" } } }, false },
	{ "Lebanon", "LB", "", { CallingCodeInfo{ "961", {}, { "XX XXX XXX" } } }, false },
	{ "Saint Lucia", "LC", "", { CallingCodeInfo{ "1758", {}, { "XXX XXXX" } } }, false },
	{ "Liechtenstein", "LI", "", { CallingCodeInfo{ "423", {}, { "XXX XXXX" } } }, false },
	{ "Sri Lanka", "LK", "", { CallingCodeInfo{ "94", {}, { "XX XXX XXXX" } } }, false },
	{ "Liberia", "LR", "", { CallingCodeInfo{ "231", {}, { "XX XXX XXXX" } } }, false },
	{ "Lesotho", "LS", "", { CallingCodeInfo{ "266", {}, { "XX XXX XXX" } } }, false },
	{ "Lithuania", "LT", "", { CallingCodeInfo{ "370", {}, { "XXX XXXXX" } } }, false },
	{ "Luxembourg", "LU", "", { CallingCodeInfo{ "352", {}, { "XXX XXX XXX" } } }, false },
	{ "Latvia", "LV", "", { CallingCodeInfo{ "371", {}, { "XXX XXXXX" } } }, false },
	{ "Libya", "LY", "", { CallingCodeInfo{ "218", {}, { "XX XXX XXXX" } } }, false },
	{ "Morocco", "MA", "", { CallingCodeInfo{ "212", {}, { "XX XXX XXXX" } } }, false },
	{ "Monaco", "MC", "", { CallingCodeInfo{ "377", {}, { "XXXX XXXX" } } }, false },
	{ "Moldova", "MD", "", { CallingCodeInfo{ "373", {}, { "XX XXX XXX" } } }, false },
	{ "Montenegro", "ME", "", { CallingCodeInfo{ "382", {}, {} } }, false },
	{ "Madagascar", "MG", "", { CallingCodeInfo{ "261", {}, { "XX XX XXX XX" } } }, false },
	{ "Marshall Islands", "MH", "", { CallingCodeInfo{ "692", {}, {} } }, false },
	{ "North Macedonia", "MK", "", { CallingCodeInfo{ "389", {}, { "XX XXX XXX" } } }, false },
	{ "Mali", "ML", "", { CallingCodeInfo{ "223", {}, { "XXXX XXXX" } } }, false },
	{ "Myanmar", "MM", "", { CallingCodeInfo{ "95", {}, {} } }, false },
	{ "Mongolia", "MN", "", { CallingCodeInfo{ "976", {}, { "XX XX XXXX" } } }, false },
	{ "Macau", "MO", "", { CallingCodeInfo{ "853", {}, { "XXXX XXXX" } } }, false },
	{ "Northern Mariana Islands", "MP", "", { CallingCodeInfo{ "1670", {}, { "XXX XXXX" } } }, false },
	{ "Martinique", "MQ", "", { CallingCodeInfo{ "596", {}, {} } }, false },
	{ "Mauritania", "MR", "", { CallingCodeInfo{ "222", {}, { "XXXX XXXX" } } }, false },
	{ "Montserrat", "MS", "", { CallingCodeInfo{ "1664", {}, { "XXX XXXX" } } }, false },
	{ "Malta", "MT", "", { CallingCodeInfo{ "356", {}, { "XX XX XX XX" } } }, false },
	{ "Mauritius", "MU", "", { CallingCodeInfo{ "230", {}, { "XXXX XXXX" } } }, false },
	{ "Maldives", "MV", "", { CallingCodeInfo{ "960", {}, { "XXX XXXX" } } }, false },
	{ "Malawi", "MW", "", { CallingCodeInfo{ "265", {}, { "XX XXX XXXX" } } }, false },
	{ "Mexico", "MX", "", { CallingCodeInfo{ "52", {}, {} } }, false },
	{ "Malaysia", "MY", "", { CallingCodeInfo{ "60", {}, { "XX XXXX XXXX" } } }, false },
	{ "Mozambique", "MZ", "", { CallingCodeInfo{ "258", {}, { "XX XXX XXXX" } } }, false },
	{ "Namibia", "NA", "", { CallingCodeInfo{ "264", {}, { "XX XXX XXXX" } } }, false },
	{ "New Caledonia", "NC", "", { CallingCodeInfo{ "687", {}, {} } }, false },
	{ "Niger", "NE", "", { CallingCodeInfo{ "227", {}, { "XX XX XX XX" } } }, false },
	{ "Norfolk Island", "NF", "", { CallingCodeInfo{ "672", {}, {} } }, false },
	{ "Nigeria", "NG", "", { CallingCodeInfo{ "234", {}, { "XX XXXX XXXX" } } }, false },
	{ "Nicaragua", "NI", "", { CallingCodeInfo{ "505", {}, { "XXXX XXXX" } } }, false },
	{ "Netherlands", "NL", "", { CallingCodeInfo{ "31", {}, { "X XX XX XX XX" } } }, false },
	{ "Norway", "NO", "", { CallingCodeInfo{ "47", {}, { "XXXX XXXX" } } }, false },
	{ "Nepal", "NP", "", { CallingCodeInfo{ "977", {}, { "XX XXXX XXXX" } } }, false },
	{ "Nauru", "NR", "", { CallingCodeInfo{ "674", {}, {} } }, false },
	{ "Niue", "NU", "", { CallingCodeInfo{ "683", {}, {} } }, false },
	{ "New Zealand", "NZ", "", { CallingCodeInfo{ "64", {}, { "XXXX XXXX" } } }, false },
	{ "Oman", "OM", "", { CallingCodeInfo{ "968", {}, { "XXXX XXXX" } } }, false },
	{ "Panama", "PA", "", { CallingCodeInfo{ "507", {}, { "XXXX XXXX" } } }, false },
	{ "Peru", "PE", "", { CallingCodeInfo{ "51", {}, { "XXX XXX XXX" } } }, false },
	{ "French Polynesia", "PF", "", { CallingCodeInfo{ "689", {}, {} } }, false },
	{ "Papua New Guinea", "PG", "", { CallingCodeInfo{ "675", {}, {} } }, false },
	{ "Philippines", "PH", "", { CallingCodeInfo{ "63", {}, { "XXX XXX XXXX" } } }, false },
	{ "Pakistan", "PK", "", { CallingCodeInfo{ "92", {}, { "XXX XXX XXXX" } } }, false },
	{ "Poland", "PL", "", { CallingCodeInfo{ "48", {}, { "XXX XXX XXX" } } }, false },
	{ "Saint Pierre & Miquelon", "PM", "", { CallingCodeInfo{ "508", {}, {} } }, false },
	{ "Puerto Rico", "PR", "", { CallingCodeInfo{ "1787", {}, { "XXX XXXX" } } }, false },
	{ "Palestine", "PS", "", { CallingCodeInfo{ "970", {}, { "XXX XX XXXX" } } }, false },
	{ "Portugal", "PT", "", { CallingCodeInfo{ "351", {}, { "XXX XXX XXX" } } }, false },
	{ "Palau", "PW", "", { CallingCodeInfo{ "680", {}, {} } }, false },
	{ "Paraguay", "PY", "", { CallingCodeInfo{ "595", {}, { "XXX XXX XXX" } } }, false },
	{ "Qatar", "QA", "", { CallingCodeInfo{ "974", {}, { "XX XXX XXX" } } }, false },
	{ "Réunion", "RE", "", { CallingCodeInfo{ "262", {}, { "XXX XXX XXX" } } }, false },
	{ "Romania", "RO", "", { CallingCodeInfo{ "40", {}, { "XXX XXX XXX" } } }, false },
	{ "Serbia", "RS", "", { CallingCodeInfo{ "381", {}, { "XX XXX XXXX" } } }, false },
	{ "Russian Federation", "RU", "", { CallingCodeInfo{ "7", {}, { "XXX XXX XXXX" } } }, false },
	{ "Rwanda", "RW", "", { CallingCodeInfo{ "250", {}, { "XXX XXX XXX" } } }, false },
	{ "Saudi Arabia", "SA", "", { CallingCodeInfo{ "966", {}, { "XX XXX XXXX" } } }, false },
	{ "Solomon Islands", "SB", "", { CallingCodeInfo{ "677", {}, {} } }, false },
	{ "Seychelles", "SC", "", { CallingCodeInfo{ "248", {}, { "X XX XX XX" } } }, false },
	{ "Sudan", "SD", "", { CallingCodeInfo{ "249", {}, { "XX XXX XXXX" } } }, false },
	{ "Sweden", "SE", "", { CallingCodeInfo{ "46", {}, { "XX XXX XXXX" } } }, false },
	{ "Singapore", "SG", "", { CallingCodeInfo{ "65", {}, { "XXXX XXXX" } } }, false },
	{ "Saint Helena", "SH", "", { CallingCodeInfo{ "247", {}, {} } }, false },
	{ "Slovenia", "SI", "", { CallingCodeInfo{ "386", {}, { "XX XXX XXX" } } }, false },
	{ "Slovakia", "SK", "", { CallingCodeInfo{ "421", {}, { "XXX XXX XXX" } } }, false },
	{ "Sierra Leone", "SL", "", { CallingCodeInfo{ "232", {}, { "XX XXX XXX" } } }, false },
	{ "San Marino", "SM", "", { CallingCodeInfo{ "378", {}, { "XXX XXX XXXX" } } }, false },
	{ "Senegal", "SN", "", { CallingCodeInfo{ "221", {}, { "XX XXX XXXX" } } }, false },
	{ "Somalia", "SO", "", { CallingCodeInfo{ "252", {}, { "XX XXX XXX" } } }, false },
	{ "Suriname", "SR", "", { CallingCodeInfo{ "597", {}, { "XXX XXXX" } } }, false },
	{ "South Sudan", "SS", "", { CallingCodeInfo{ "211", {}, { "XX XXX XXXX" } } }, false },
	{ "São Tomé & Príncipe", "ST", "", { CallingCodeInfo{ "239", {}, { "XX XXXXX" } } }, false },
	{ "El Salvador", "SV", "", { CallingCodeInfo{ "503", {}, { "XXXX XXXX" } } }, false },
	{ "Sint Maarten", "SX", "", { CallingCodeInfo{ "1721", {}, { "XXX XXXX" } } }, false },
	{ "Syria", "SY", "", { CallingCodeInfo{ "963", {}, { "XXX XXX XXX" } } }, false },
	{ "Eswatini", "SZ", "", { CallingCodeInfo{ "268", {}, { "XXXX XXXX" } } }, false },
	{ "Turks & Caicos Islands", "TC", "", { CallingCodeInfo{ "1649", {}, { "XXX XXXX" } } }, false },
	{ "Chad", "TD", "", { CallingCodeInfo{ "235", {}, { "XX XX XX XX" } } }, false },
	{ "Togo", "TG", "", { CallingCodeInfo{ "228", {}, { "XX XXX XXX" } } }, false },
	{ "Thailand", "TH", "", { CallingCodeInfo{ "66", {}, { "X XXXX XXXX" } } }, false },
	{ "Tajikistan", "TJ", "", { CallingCodeInfo{ "992", {}, { "XX XXX XXXX" } } }, false },
	{ "Tokelau", "TK", "", { CallingCodeInfo{ "690", {}, {} } }, false },
	{ "Timor-Leste", "TL", "", { CallingCodeInfo{ "670", {}, {} } }, false },
	{ "Turkmenistan", "TM", "", { CallingCodeInfo{ "993", {}, { "XX XXXXXX" } } }, false },
	{ "Tunisia", "TN", "", { CallingCodeInfo{ "216", {}, { "XX XXX XXX" } } }, false },
	{ "Tonga", "TO", "", { CallingCodeInfo{ "676", {}, {} } }, false },
	{ "Turkey", "TR", "", { CallingCodeInfo{ "90", {}, { "XXX XXX XXXX" } } }, false },
	{ "Trinidad & Tobago", "TT", "", { CallingCodeInfo{ "1868", {}, { "XXX XXXX" } } }, false },
	{ "Tuvalu", "TV", "", { CallingCodeInfo{ "688", {}, {} } }, false },
	{ "Taiwan", "TW", "", { CallingCodeInfo{ "886", {}, { "XXX XXX XXX" } } }, false },
	{ "Tanzania", "TZ", "", { CallingCodeInfo{ "255", {}, { "XX XXX XXXX" } } }, false },
	{ "Ukraine", "UA", "", { CallingCodeInfo{ "380", {}, { "XX XXX XX XX" } } }, false },
	{ "Uganda", "UG", "", { CallingCodeInfo{ "256", {}, { "XX XXX XXXX" } } }, false },
	{ "USA", "US", "United States of America", { CallingCodeInfo{ "1", {}, { "XXX XXX XXXX" } } }, false },
	{ "Uruguay", "UY", "", { CallingCodeInfo{ "598", {}, { "X XXX XXXX" } } }, false },
	{ "Uzbekistan", "UZ", "", { CallingCodeInfo{ "998", {}, { "XX XXX XX XX" } } }, false },
	{ "Saint Vincent & the Grenadines", "VC", "", { CallingCodeInfo{ "1784", {}, { "XXX XXXX" } } }, false },
	{ "Venezuela", "VE", "", { CallingCodeInfo{ "58", {}, { "XXX XXX XXXX" } } }, false },
	{ "British Virgin Islands", "VG", "", { CallingCodeInfo{ "1284", {}, { "XXX XXXX" } } }, false },
	{ "US Virgin Islands", "VI", "", { CallingCodeInfo{ "1340", {}, { "XXX XXXX" } } }, false },
	{ "Vietnam", "VN", "", { CallingCodeInfo{ "84", {}, {} } }, false },
	{ "Vanuatu", "VU", "", { CallingCodeInfo{ "678", {}, {} } }, false },
	{ "Wallis & Futuna", "WF", "", { CallingCodeInfo{ "681", {}, {} } }, false },
	{ "Samoa", "WS", "", { CallingCodeInfo{ "685", {}, {} } }, false },
	{ "Kosovo", "XK", "", { CallingCodeInfo{ "383", {}, { "XXXX XXXX" } } }, false },
	{ "Yemen", "YE", "", { CallingCodeInfo{ "967", {}, { "XXX XXX XXX" } } }, false },
	{ "South Africa", "ZA", "", { CallingCodeInfo{ "27", {}, { "XX XXX XXXX" } } }, false },
	{ "Zambia", "ZM", "", { CallingCodeInfo{ "260", {}, { "XX XXX XXXX" } } }, false },
	{ "Zimbabwe", "ZW", "", { CallingCodeInfo{ "263", {}, { "XX XXX XXXX" } } }, false },
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
	_byCode.clear();
	_byISO2.clear();
	_updated.fire({});
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
	[[maybe_unused]] auto isPrefix = false;
	for (const auto &country : list()) {
		for (auto &callingCode : country.codes) {
			if (phoneNumber.startsWith(callingCode.callingCode)) {
				const auto codeSize = callingCode.callingCode.size();
				for (const auto &prefix : callingCode.prefixes) {
					if (prefix.startsWith(base::StringViewMid(phoneNumber, codeSize))) {
						isPrefix = true;
					}
					if ((codeSize + prefix.size()) > bestLength &&
							base::StringViewMid(phoneNumber, codeSize).startsWith(prefix)) {
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
	if (args.onlyCode) {
		return FormatResult{ .code = bestCallingCodePtr->callingCode };
	}

	const auto codeSize = int(bestCallingCodePtr->callingCode.size());

	if (args.onlyGroups && args.incomplete) {
		auto groups = args.skipCode
			? QVector<int>()
			: QVector<int>{ codeSize };
		auto groupSize = 0;
		if (bestCallingCodePtr->patterns.empty()) {
			return FormatResult{ .groups = std::move(groups) };
		}
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
			// Don't add an extra space to the end.
			// if (!args.onlyGroups && (currentPatternPos == pattern.size())) {
			// 	result += ' ';
			// }
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

rpl::producer<> CountriesInstance::updated() const {
	return _updated.events();
}

CountriesInstance &Instance() {
	return SingleInstance;
}

QString ExtractPhoneCode(const QString &phone) {
	return Instance().format({ .phone = phone, .onlyCode = true }).code;
}

} // namespace Countries
