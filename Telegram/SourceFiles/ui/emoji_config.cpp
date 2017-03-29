/*
WARNING! All changes made in this file will be lost!
Created from 'empty' by 'codegen_emoji'

This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "emoji_config.h"

namespace Ui {
namespace Emoji {
namespace {

constexpr auto kCount = 2167;
auto WorkingIndex = -1;

std::vector<One> Items;

} // namespace

namespace internal {

EmojiPtr ByIndex(int index) {
	return (index >= 0 && index < Items.size()) ? &Items[index] : nullptr;
}

template <typename ...Args>
inline QString ComputeId(Args... args) {
	auto utf16 = { args... };
	auto result = QString();
	result.reserve(utf16.size());
	for (auto ch : utf16) {
		result.append(QChar(ch));
	}
	return result;
}

EmojiPtr FindReplace(const QChar *start, const QChar *end, int *outLength) {
	auto ch = start;

	if (ch != end) switch (ch->unicode()) {
	case 0x7d:
		++ch;
		if (ch != end && ch->unicode() == 0x3a) {
			++ch;
			if (ch != end && ch->unicode() == 0x29) {
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[75];
			}
		}
	break;
	case 0x78:
		++ch;
		if (ch != end && ch->unicode() == 0x44) {
			++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[4];
		}
	break;
	case 0x4f:
		++ch;
		if (ch != end && ch->unicode() == 0x3a) {
			++ch;
			if (ch != end && ch->unicode() == 0x29) {
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[10];
			}
		}
	break;
	case 0x42:
		++ch;
		if (ch != end && ch->unicode() == 0x2d) {
			++ch;
			if (ch != end && ch->unicode() == 0x29) {
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[27];
			}
		}
	break;
	case 0x3e:
		++ch;
		if (ch != end && ch->unicode() == 0x28) {
			++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0x28) {
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[44];
			}
			return &Items[43];
		}
	break;
	case 0x3c:
		++ch;
		if (ch != end && ch->unicode() == 0x33) {
			++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1643];
		}
	break;
	case 0x3b:
		++ch;
		if (ch != end) switch (ch->unicode()) {
		case 0x6f:
			++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[57];
		break;
		case 0x2d:
			++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0x50:
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[21];
			break;
			case 0x29:
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[13];
			break;
			}
		break;
		}
	break;
	case 0x3a:
		++ch;
		if (ch != end) switch (ch->unicode()) {
		case 0x7c:
			++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[46];
		break;
		case 0x76:
			++ch;
			if (ch != end && ch->unicode() == 0x3a) {
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[163];
			}
		break;
		case 0x75:
			++ch;
			if (ch != end && ch->unicode() == 0x70) {
				++ch;
				if (ch != end && ch->unicode() == 0x3a) {
					++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[205];
				}
			}
		break;
		case 0x6f:
			++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0x6b) {
				++ch;
				if (ch != end && ch->unicode() == 0x3a) {
					++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[175];
				}
			}
			return &Items[56];
		break;
		case 0x6c:
			++ch;
			if (ch != end && ch->unicode() == 0x69) {
				++ch;
				if (ch != end && ch->unicode() == 0x6b) {
					++ch;
					if (ch != end && ch->unicode() == 0x65) {
						++ch;
						if (ch != end && ch->unicode() == 0x3a) {
							++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[121];
						}
					}
				}
			}
		break;
		case 0x6b:
			++ch;
			if (ch != end && ch->unicode() == 0x69) {
				++ch;
				if (ch != end && ch->unicode() == 0x73) {
					++ch;
					if (ch != end && ch->unicode() == 0x73) {
						++ch;
						if (ch != end && ch->unicode() == 0x3a) {
							++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[279];
						}
					}
				}
			}
		break;
		case 0x6a:
			++ch;
			if (ch != end && ch->unicode() == 0x6f) {
				++ch;
				if (ch != end && ch->unicode() == 0x79) {
					++ch;
					if (ch != end && ch->unicode() == 0x3a) {
						++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[6];
					}
				}
			}
		break;
		case 0x67:
			++ch;
			if (ch != end && ch->unicode() == 0x72) {
				++ch;
				if (ch != end && ch->unicode() == 0x69) {
					++ch;
					if (ch != end && ch->unicode() == 0x6e) {
						++ch;
						if (ch != end && ch->unicode() == 0x3a) {
							++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[3];
						}
					}
				}
			}
		break;
		case 0x64:
			++ch;
			if (ch != end && ch->unicode() == 0x69) {
				++ch;
				if (ch != end && ch->unicode() == 0x73) {
					++ch;
					if (ch != end && ch->unicode() == 0x6c) {
						++ch;
						if (ch != end && ch->unicode() == 0x69) {
							++ch;
							if (ch != end && ch->unicode() == 0x6b) {
								++ch;
								if (ch != end && ch->unicode() == 0x65) {
									++ch;
									if (ch != end && ch->unicode() == 0x3a) {
										++ch;
										if (outLength) *outLength = (ch - start);
										return &Items[127];
									}
								}
							}
						}
					}
				}
			}
		break;
		case 0x5f:
			++ch;
			if (ch != end && ch->unicode() == 0x28) {
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[61];
			}
		break;
		case 0x5d:
			++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[30];
		break;
		case 0x58:
			++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[72];
		break;
		case 0x2d:
			++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0x70:
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[20];
			break;
			case 0x44:
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[1];
			break;
			case 0x2a:
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[19];
			break;
			case 0x29:
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[9];
			break;
			case 0x28:
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[32];
			break;
			}
		break;
		case 0x28:
			++ch;
			if (ch != end && ch->unicode() == 0x28) {
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[41];
			}
		break;
		case 0x27:
			++ch;
			if (ch != end && ch->unicode() == 0x28) {
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[58];
			}
		break;
		}
	break;
	case 0x38:
		++ch;
		if (ch != end) switch (ch->unicode()) {
		case 0x7c:
			++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[54];
		break;
		case 0x6f:
			++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[52];
		break;
		case 0x2d:
			++ch;
			if (ch != end && ch->unicode() == 0x29) {
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[15];
			}
		break;
		}
	break;
	case 0x33:
		++ch;
		if (ch != end) switch (ch->unicode()) {
		case 0x2d:
			++ch;
			if (ch != end && ch->unicode() == 0x29) {
				++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[14];
			}
		break;
		case 0x28:
			++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[33];
		break;
		}
	break;
	}

	return nullptr;
}

EmojiPtr Find(const QChar *start, const QChar *end, int *outLength) {
	auto ch = start;

	if (ch != end) switch (ch->unicode()) {
	case 0xd83e:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end) switch (ch->unicode()) {
		case 0xddc0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1084];
		break;
		case 0xdd91:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[944];
		break;
		case 0xdd90:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[946];
		break;
		case 0xdd8f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[964];
		break;
		case 0xdd8e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[941];
		break;
		case 0xdd8d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[965];
		break;
		case 0xdd8c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[960];
		break;
		case 0xdd8b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[932];
		break;
		case 0xdd8a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[901];
		break;
		case 0xdd89:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[924];
		break;
		case 0xdd88:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[951];
		break;
		case 0xdd87:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[925];
		break;
		case 0xdd86:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[922];
		break;
		case 0xdd85:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[923];
		break;
		case 0xdd84:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[929];
		break;
		case 0xdd83:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[975];
		break;
		case 0xdd82:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[942];
		break;
		case 0xdd81:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[906];
		break;
		case 0xdd80:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[943];
		break;
		case 0xdd5e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1088];
		break;
		case 0xdd5d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1068];
		break;
		case 0xdd5c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1079];
		break;
		case 0xdd5b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1125];
		break;
		case 0xdd5a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1085];
		break;
		case 0xdd59:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1096];
		break;
		case 0xdd58:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1100];
		break;
		case 0xdd57:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1099];
		break;
		case 0xdd56:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1083];
		break;
		case 0xdd55:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1073];
		break;
		case 0xdd54:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1076];
		break;
		case 0xdd53:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1087];
		break;
		case 0xdd52:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1072];
		break;
		case 0xdd51:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1069];
		break;
		case 0xdd50:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1081];
		break;
		case 0xdd4b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1159];
		break;
		case 0xdd4a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1158];
		break;
		case 0xdd49:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1310];
		break;
		case 0xdd48:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1309];
		break;
		case 0xdd47:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1308];
		break;
		case 0xdd45:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1151];
		break;
		case 0xdd44:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1138];
		break;
		case 0xdd43:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1134];
		break;
		case 0xdd42:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1132];
		break;
		case 0xdd41:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1336];
		break;
		case 0xdd40:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1003];
		break;
		case 0xdd3e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1214];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1208];
						break;
						}
					}
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1213];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1207];
						break;
						}
					}
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1212];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1206];
						break;
						}
					}
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1211];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1205];
						break;
						}
					}
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1210];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1204];
						break;
						}
					}
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0x2642:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1209];
				break;
				case 0x2640:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1203];
				break;
				}
			break;
			}
		break;
		case 0xdd3d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1262];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1256];
						break;
						}
					}
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1261];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1255];
						break;
						}
					}
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1260];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1254];
						break;
						}
					}
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1259];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1253];
						break;
						}
					}
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1258];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1252];
						break;
						}
					}
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0x2642:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1257];
				break;
				case 0x2640:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1251];
				break;
				}
			break;
			}
		break;
		case 0xdd3c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0x200d) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0x2642:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1178];
				break;
				case 0x2640:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1177];
				break;
				}
			}
		break;
		case 0xdd3a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1176];
		break;
		case 0xdd39:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1328];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1322];
						break;
						}
					}
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1327];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1321];
						break;
						}
					}
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1326];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1320];
						break;
						}
					}
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1325];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1319];
						break;
						}
					}
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1324];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1318];
						break;
						}
					}
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0x2642:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1323];
				break;
				case 0x2640:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1317];
				break;
				}
			break;
			}
		break;
		case 0xdd38:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1190];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1184];
						break;
						}
					}
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1189];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1183];
						break;
						}
					}
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1188];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1182];
						break;
						}
					}
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1187];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1181];
						break;
						}
					}
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1186];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1180];
						break;
						}
					}
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0x2642:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1185];
				break;
				case 0x2640:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1179];
				break;
				}
			break;
			}
		break;
		case 0xdd37:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[743];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[737];
						break;
						}
					}
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[742];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[736];
						break;
						}
					}
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[741];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[735];
						break;
						}
					}
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[740];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[734];
						break;
						}
					}
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[739];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[733];
						break;
						}
					}
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0x2642:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[738];
				break;
				case 0x2640:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[732];
				break;
				}
			break;
			}
		break;
		case 0xdd36:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[617];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[616];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[615];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[614];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[613];
				break;
				}
			}
			return &Items[612];
		break;
		case 0xdd35:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[647];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[646];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[645];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[644];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[643];
				break;
				}
			}
			return &Items[642];
		break;
		case 0xdd34:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[635];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[634];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[633];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[632];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[631];
				break;
				}
			}
			return &Items[630];
		break;
		case 0xdd33:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[270];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[269];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[268];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[267];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[266];
				break;
				}
			}
			return &Items[265];
		break;
		case 0xdd30:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[659];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[658];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[657];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[656];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[655];
				break;
				}
			}
			return &Items[654];
		break;
		case 0xdd27:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[71];
		break;
		case 0xdd26:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[731];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[725];
						break;
						}
					}
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[730];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[724];
						break;
						}
					}
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[729];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[723];
						break;
						}
					}
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[728];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[722];
						break;
						}
					}
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0x2642:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[727];
						break;
						case 0x2640:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[721];
						break;
						}
					}
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0x2642:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[726];
				break;
				case 0x2640:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[720];
				break;
				}
			break;
			}
		break;
		case 0xdd25:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[67];
		break;
		case 0xdd24:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[60];
		break;
		case 0xdd23:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[7];
		break;
		case 0xdd22:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[70];
		break;
		case 0xdd21:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[28];
		break;
		case 0xdd20:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[29];
		break;
		case 0xdd1e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[162];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[161];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[160];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[159];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[158];
				break;
				}
			}
			return &Items[157];
		break;
		case 0xdd1d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[120];
		break;
		case 0xdd1c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[156];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[155];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[154];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[153];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[152];
				break;
				}
			}
			return &Items[151];
		break;
		case 0xdd1b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[150];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[149];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[148];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[147];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[146];
				break;
				}
			}
			return &Items[145];
		break;
		case 0xdd1a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[222];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[221];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[220];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[219];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[218];
				break;
				}
			}
			return &Items[217];
		break;
		case 0xdd19:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[246];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[245];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[244];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[243];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[242];
				break;
				}
			}
			return &Items[241];
		break;
		case 0xdd18:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[174];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[173];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[172];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[171];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[170];
				break;
				}
			}
			return &Items[169];
		break;
		case 0xdd17:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[25];
		break;
		case 0xdd16:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[85];
		break;
		case 0xdd15:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[74];
		break;
		case 0xdd14:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[66];
		break;
		case 0xdd13:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[26];
		break;
		case 0xdd12:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[73];
		break;
		case 0xdd11:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[24];
		break;
		case 0xdd10:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[69];
		break;
		}
	break;
	case 0xd83d:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end) switch (ch->unicode()) {
		case 0xdef6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1391];
		break;
		case 0xdef5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1361];
		break;
		case 0xdef4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1359];
		break;
		case 0xdef3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1395];
		break;
		case 0xdef0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1389];
		break;
		case 0xdeec:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1387];
		break;
		case 0xdeeb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1386];
		break;
		case 0xdee9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1384];
		break;
		case 0xdee5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1393];
		break;
		case 0xdee4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1425];
		break;
		case 0xdee3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1426];
		break;
		case 0xdee2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1509];
		break;
		case 0xdee1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1532];
		break;
		case 0xdee0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1522];
		break;
		case 0xded2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1566];
		break;
		case 0xded1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1713];
		break;
		case 0xded0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1669];
		break;
		case 0xdecf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1562];
		break;
		case 0xdece:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1557];
		break;
		case 0xdecd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1565];
		break;
		case 0xdecc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1563];
		break;
		case 0xdecb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1561];
		break;
		case 0xdec5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1762];
		break;
		case 0xdec4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1761];
		break;
		case 0xdec3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1760];
		break;
		case 0xdec2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1759];
		break;
		case 0xdec1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1550];
		break;
		case 0xdec0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1556];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1555];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1554];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1553];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1552];
				break;
				}
			}
			return &Items[1551];
		break;
		case 0xdebf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1549];
		break;
		case 0xdebe:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1754];
		break;
		case 0xdebd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1547];
		break;
		case 0xdebc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1765];
		break;
		case 0xdebb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1766];
		break;
		case 0xdeba:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1764];
		break;
		case 0xdeb9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1763];
		break;
		case 0xdeb8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1737];
		break;
		case 0xdeb7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1720];
		break;
		case 0xdeb6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[817];
						}
					}
					return &Items[823];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[816];
						}
					}
					return &Items[822];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[815];
						}
					}
					return &Items[821];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[814];
						}
					}
					return &Items[820];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[813];
						}
					}
					return &Items[819];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[812];
				}
			break;
			}
			return &Items[818];
		break;
		case 0xdeb5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1298];
						}
					}
					return &Items[1304];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1297];
						}
					}
					return &Items[1303];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1296];
						}
					}
					return &Items[1302];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1295];
						}
					}
					return &Items[1301];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1294];
						}
					}
					return &Items[1300];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1293];
				}
			break;
			}
			return &Items[1299];
		break;
		case 0xdeb4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1286];
						}
					}
					return &Items[1292];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1285];
						}
					}
					return &Items[1291];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1284];
						}
					}
					return &Items[1290];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1283];
						}
					}
					return &Items[1289];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1282];
						}
					}
					return &Items[1288];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1281];
				}
			break;
			}
			return &Items[1287];
		break;
		case 0xdeb3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1722];
		break;
		case 0xdeb2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1360];
		break;
		case 0xdeb1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1723];
		break;
		case 0xdeb0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1548];
		break;
		case 0xdeaf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1721];
		break;
		case 0xdeae:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1767];
		break;
		case 0xdead:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1726];
		break;
		case 0xdeac:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1533];
		break;
		case 0xdeab:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1716];
		break;
		case 0xdeaa:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1560];
		break;
		case 0xdea9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1917];
		break;
		case 0xdea8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1363];
		break;
		case 0xdea7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1399];
		break;
		case 0xdea6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1402];
		break;
		case 0xdea5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1403];
		break;
		case 0xdea4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1394];
		break;
		case 0xdea3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1268];
						}
					}
					return &Items[1274];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1267];
						}
					}
					return &Items[1273];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1266];
						}
					}
					return &Items[1272];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1265];
						}
					}
					return &Items[1271];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1264];
						}
					}
					return &Items[1270];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1263];
				}
			break;
			}
			return &Items[1269];
		break;
		case 0xdea2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1397];
		break;
		case 0xdea1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1368];
		break;
		case 0xdea0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1369];
		break;
		case 0xde9f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1370];
		break;
		case 0xde9e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1373];
		break;
		case 0xde9d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1374];
		break;
		case 0xde9c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1358];
		break;
		case 0xde9b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1357];
		break;
		case 0xde9a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1356];
		break;
		case 0xde99:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1348];
		break;
		case 0xde98:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1366];
		break;
		case 0xde97:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1346];
		break;
		case 0xde96:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1367];
		break;
		case 0xde95:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1347];
		break;
		case 0xde94:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1364];
		break;
		case 0xde93:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1352];
		break;
		case 0xde92:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1354];
		break;
		case 0xde91:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1353];
		break;
		case 0xde90:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1355];
		break;
		case 0xde8f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1401];
		break;
		case 0xde8e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1350];
		break;
		case 0xde8d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1365];
		break;
		case 0xde8c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1349];
		break;
		case 0xde8b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1372];
		break;
		case 0xde8a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1381];
		break;
		case 0xde89:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1382];
		break;
		case 0xde88:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1377];
		break;
		case 0xde87:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1380];
		break;
		case 0xde86:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1379];
		break;
		case 0xde85:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1376];
		break;
		case 0xde84:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1375];
		break;
		case 0xde83:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1371];
		break;
		case 0xde82:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1378];
		break;
		case 0xde81:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1383];
		break;
		case 0xde80:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1388];
		break;
		case 0xde4f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[119];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[118];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[117];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[116];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[115];
				break;
				}
			}
			return &Items[114];
		break;
		case 0xde4e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[755];
						}
					}
					return &Items[749];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[754];
						}
					}
					return &Items[748];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[753];
						}
					}
					return &Items[747];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[752];
						}
					}
					return &Items[746];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[751];
						}
					}
					return &Items[745];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2642) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[750];
				}
			break;
			}
			return &Items[744];
		break;
		case 0xde4d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[767];
						}
					}
					return &Items[761];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[766];
						}
					}
					return &Items[760];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[765];
						}
					}
					return &Items[759];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[764];
						}
					}
					return &Items[758];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[763];
						}
					}
					return &Items[757];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2642) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[762];
				}
			break;
			}
			return &Items[756];
		break;
		case 0xde4c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[107];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[106];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[105];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[104];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[103];
				break;
				}
			}
			return &Items[102];
		break;
		case 0xde4b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[719];
						}
					}
					return &Items[713];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[718];
						}
					}
					return &Items[712];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[717];
						}
					}
					return &Items[711];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[716];
						}
					}
					return &Items[710];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[715];
						}
					}
					return &Items[709];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2642) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[714];
				}
			break;
			}
			return &Items[708];
		break;
		case 0xde4a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[914];
		break;
		case 0xde49:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[913];
		break;
		case 0xde48:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[912];
		break;
		case 0xde47:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[665];
						}
					}
					return &Items[671];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[664];
						}
					}
					return &Items[670];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[663];
						}
					}
					return &Items[669];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[662];
						}
					}
					return &Items[668];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[661];
						}
					}
					return &Items[667];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[660];
				}
			break;
			}
			return &Items[666];
		break;
		case 0xde46:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[707];
						}
					}
					return &Items[701];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[706];
						}
					}
					return &Items[700];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[705];
						}
					}
					return &Items[699];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[704];
						}
					}
					return &Items[698];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[703];
						}
					}
					return &Items[697];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2642) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[702];
				}
			break;
			}
			return &Items[696];
		break;
		case 0xde45:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[695];
						}
					}
					return &Items[689];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[694];
						}
					}
					return &Items[688];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[693];
						}
					}
					return &Items[687];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[692];
						}
					}
					return &Items[686];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[691];
						}
					}
					return &Items[685];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2642) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[690];
				}
			break;
			}
			return &Items[684];
		break;
		case 0xde44:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[65];
		break;
		case 0xde43:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[12];
		break;
		case 0xde42:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[11];
		break;
		case 0xde41:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[36];
		break;
		case 0xde40:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[93];
		break;
		case 0xde3f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[94];
		break;
		case 0xde3e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[95];
		break;
		case 0xde3d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[92];
		break;
		case 0xde3c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[91];
		break;
		case 0xde3b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[90];
		break;
		case 0xde3a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[87];
		break;
		case 0xde39:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[89];
		break;
		case 0xde38:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[88];
		break;
		case 0xde37:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[72];
		break;
		case 0xde36:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[45];
		break;
		case 0xde35:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[53];
		break;
		case 0xde34:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[64];
		break;
		case 0xde33:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[54];
		break;
		case 0xde32:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[52];
		break;
		case 0xde31:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[55];
		break;
		case 0xde30:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[57];
		break;
		case 0xde2f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[48];
		break;
		case 0xde2e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[51];
		break;
		case 0xde2d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[61];
		break;
		case 0xde2c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[68];
		break;
		case 0xde2b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[40];
		break;
		case 0xde2a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[63];
		break;
		case 0xde29:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[41];
		break;
		case 0xde28:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[56];
		break;
		case 0xde27:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[50];
		break;
		case 0xde26:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[49];
		break;
		case 0xde25:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[59];
		break;
		case 0xde24:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[42];
		break;
		case 0xde23:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[38];
		break;
		case 0xde22:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[58];
		break;
		case 0xde21:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[44];
		break;
		case 0xde20:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[43];
		break;
		case 0xde1f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[34];
		break;
		case 0xde1e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[32];
		break;
		case 0xde1d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[22];
		break;
		case 0xde1c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[21];
		break;
		case 0xde1b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[23];
		break;
		case 0xde1a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[19];
		break;
		case 0xde19:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[18];
		break;
		case 0xde18:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[16];
		break;
		case 0xde17:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[17];
		break;
		case 0xde16:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[39];
		break;
		case 0xde15:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[35];
		break;
		case 0xde14:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[33];
		break;
		case 0xde13:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[62];
		break;
		case 0xde12:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[31];
		break;
		case 0xde11:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[47];
		break;
		case 0xde10:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[46];
		break;
		case 0xde0f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[30];
		break;
		case 0xde0e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[27];
		break;
		case 0xde0d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[15];
		break;
		case 0xde0c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[14];
		break;
		case 0xde0b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[20];
		break;
		case 0xde0a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[9];
		break;
		case 0xde09:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[13];
		break;
		case 0xde08:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[75];
		break;
		case 0xde07:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[10];
		break;
		case 0xde06:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[4];
		break;
		case 0xde05:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[5];
		break;
		case 0xde04:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[2];
		break;
		case 0xde03:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1];
		break;
		case 0xde02:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[6];
		break;
		case 0xde01:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[3];
		break;
		case 0xde00:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[0];
		break;
		case 0xddff:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1405];
		break;
		case 0xddfe:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1450];
		break;
		case 0xddfd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1406];
		break;
		case 0xddfc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1408];
		break;
		case 0xddfb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1420];
		break;
		case 0xddfa:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1404];
		break;
		case 0xddf3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1604];
		break;
		case 0xddef:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1882];
		break;
		case 0xdde3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[297];
		break;
		case 0xdde1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1530];
		break;
		case 0xddde:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1610];
		break;
		case 0xdddd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1559];
		break;
		case 0xdddc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1475];
		break;
		case 0xddd3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1599];
		break;
		case 0xddd2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1598];
		break;
		case 0xddd1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1508];
		break;
		case 0xddc4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1605];
		break;
		case 0xddc3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1603];
		break;
		case 0xddc2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1609];
		break;
		case 0xddbc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1564];
		break;
		case 0xddb2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1473];
		break;
		case 0xddb1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1472];
		break;
		case 0xdda8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1471];
		break;
		case 0xdda5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1470];
		break;
		case 0xdda4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1648];
		break;
		case 0xdd96:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[234];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[233];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[232];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[231];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[230];
				break;
				}
			}
			return &Items[229];
		break;
		case 0xdd95:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[258];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[257];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[256];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[255];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[254];
				break;
				}
			}
			return &Items[253];
		break;
		case 0xdd90:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[228];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[227];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[226];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[225];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[224];
				break;
				}
			}
			return &Items[223];
		break;
		case 0xdd8d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1634];
		break;
		case 0xdd8c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1633];
		break;
		case 0xdd8b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1631];
		break;
		case 0xdd8a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1630];
		break;
		case 0xdd87:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1624];
		break;
		case 0xdd7a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[809];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[808];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[807];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[806];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[805];
				break;
				}
			}
			return &Items[804];
		break;
		case 0xdd79:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1474];
		break;
		case 0xdd78:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[938];
		break;
		case 0xdd77:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[937];
		break;
		case 0xdd76:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[893];
		break;
		case 0xdd75:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[413];
						}
					}
					return &Items[419];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[412];
						}
					}
					return &Items[418];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[411];
						}
					}
					return &Items[417];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[410];
						}
					}
					return &Items[416];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[409];
						}
					}
					return &Items[415];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[408];
				}
			break;
			}
			return &Items[414];
		break;
		case 0xdd74:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[797];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[796];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[795];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[794];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[793];
				break;
				}
			}
			return &Items[792];
		break;
		case 0xdd73:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1543];
		break;
		case 0xdd70:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1499];
		break;
		case 0xdd6f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1507];
		break;
		case 0xdd67:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1913];
		break;
		case 0xdd66:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1912];
		break;
		case 0xdd65:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1911];
		break;
		case 0xdd64:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1910];
		break;
		case 0xdd63:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1909];
		break;
		case 0xdd62:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1908];
		break;
		case 0xdd61:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1907];
		break;
		case 0xdd60:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1906];
		break;
		case 0xdd5f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1905];
		break;
		case 0xdd5e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1904];
		break;
		case 0xdd5d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1903];
		break;
		case 0xdd5c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1902];
		break;
		case 0xdd5b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1901];
		break;
		case 0xdd5a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1900];
		break;
		case 0xdd59:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1899];
		break;
		case 0xdd58:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1898];
		break;
		case 0xdd57:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1897];
		break;
		case 0xdd56:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1896];
		break;
		case 0xdd55:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1895];
		break;
		case 0xdd54:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1894];
		break;
		case 0xdd53:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1893];
		break;
		case 0xdd52:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1892];
		break;
		case 0xdd51:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1891];
		break;
		case 0xdd50:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1890];
		break;
		case 0xdd4e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1666];
		break;
		case 0xdd4d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1447];
		break;
		case 0xdd4c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1446];
		break;
		case 0xdd4b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1448];
		break;
		case 0xdd4a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[976];
		break;
		case 0xdd49:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1662];
		break;
		case 0xdd3d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1809];
		break;
		case 0xdd3c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1808];
		break;
		case 0xdd3b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1856];
		break;
		case 0xdd3a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1855];
		break;
		case 0xdd39:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1858];
		break;
		case 0xdd38:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1857];
		break;
		case 0xdd37:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1860];
		break;
		case 0xdd36:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1859];
		break;
		case 0xdd35:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1854];
		break;
		case 0xdd34:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1853];
		break;
		case 0xdd33:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1861];
		break;
		case 0xdd32:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1862];
		break;
		case 0xdd31:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1738];
		break;
		case 0xdd30:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1740];
		break;
		case 0xdd2f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1665];
		break;
		case 0xdd2e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1537];
		break;
		case 0xdd2d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1541];
		break;
		case 0xdd2c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1542];
		break;
		case 0xdd2b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1527];
		break;
		case 0xdd2a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1529];
		break;
		case 0xdd29:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1524];
		break;
		case 0xdd28:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1520];
		break;
		case 0xdd27:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1519];
		break;
		case 0xdd26:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1506];
		break;
		case 0xdd25:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1030];
		break;
		case 0xdd24:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1773];
		break;
		case 0xdd23:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1771];
		break;
		case 0xdd22:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1793];
		break;
		case 0xdd21:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1774];
		break;
		case 0xdd20:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1775];
		break;
		case 0xdd1f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1792];
		break;
		case 0xdd1e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1724];
		break;
		case 0xdd1d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1846];
		break;
		case 0xdd1c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1847];
		break;
		case 0xdd1b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1845];
		break;
		case 0xdd1a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1843];
		break;
		case 0xdd19:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1844];
		break;
		case 0xdd18:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1850];
		break;
		case 0xdd17:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1622];
		break;
		case 0xdd16:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1621];
		break;
		case 0xdd15:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1876];
		break;
		case 0xdd14:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1875];
		break;
		case 0xdd13:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1642];
		break;
		case 0xdd12:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1641];
		break;
		case 0xdd11:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1558];
		break;
		case 0xdd10:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1640];
		break;
		case 0xdd0f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1639];
		break;
		case 0xdd0e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1638];
		break;
		case 0xdd0d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1637];
		break;
		case 0xdd0c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1504];
		break;
		case 0xdd0b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1503];
		break;
		case 0xdd0a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1874];
		break;
		case 0xdd09:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1873];
		break;
		case 0xdd08:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1871];
		break;
		case 0xdd07:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1872];
		break;
		case 0xdd06:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1734];
		break;
		case 0xdd05:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1733];
		break;
		case 0xdd04:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1827];
		break;
		case 0xdd03:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1828];
		break;
		case 0xdd02:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1826];
		break;
		case 0xdd01:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1825];
		break;
		case 0xdd00:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1824];
		break;
		case 0xdcff:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1538];
		break;
		case 0xdcfd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1485];
		break;
		case 0xdcfc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1480];
		break;
		case 0xdcfb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1492];
		break;
		case 0xdcfa:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1491];
		break;
		case 0xdcf9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1483];
		break;
		case 0xdcf8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1482];
		break;
		case 0xdcf7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1481];
		break;
		case 0xdcf6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1769];
		break;
		case 0xdcf5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1725];
		break;
		case 0xdcf4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1688];
		break;
		case 0xdcf3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1689];
		break;
		case 0xdcf2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1467];
		break;
		case 0xdcf1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1466];
		break;
		case 0xdcf0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1611];
		break;
		case 0xdcef:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1590];
		break;
		case 0xdcee:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1589];
		break;
		case 0xdced:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1588];
		break;
		case 0xdcec:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1587];
		break;
		case 0xdceb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1586];
		break;
		case 0xdcea:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1585];
		break;
		case 0xdce9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1577];
		break;
		case 0xdce8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1578];
		break;
		case 0xdce7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1579];
		break;
		case 0xdce6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1583];
		break;
		case 0xdce5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1581];
		break;
		case 0xdce4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1582];
		break;
		case 0xdce3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1877];
		break;
		case 0xdce2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1878];
		break;
		case 0xdce1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1502];
		break;
		case 0xdce0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1490];
		break;
		case 0xdcdf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1489];
		break;
		case 0xdcde:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1487];
		break;
		case 0xdcdd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1635];
		break;
		case 0xdcdc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1591];
		break;
		case 0xdcdb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1715];
		break;
		case 0xdcda:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1619];
		break;
		case 0xdcd9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1618];
		break;
		case 0xdcd8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1617];
		break;
		case 0xdcd7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1616];
		break;
		case 0xdcd6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1620];
		break;
		case 0xdcd5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1615];
		break;
		case 0xdcd4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1613];
		break;
		case 0xdcd3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1612];
		break;
		case 0xdcd2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1614];
		break;
		case 0xdcd1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1594];
		break;
		case 0xdcd0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1625];
		break;
		case 0xdccf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1626];
		break;
		case 0xdcce:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1623];
		break;
		case 0xdccd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1628];
		break;
		case 0xdccc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1627];
		break;
		case 0xdccb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1606];
		break;
		case 0xdcca:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1595];
		break;
		case 0xdcc9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1597];
		break;
		case 0xdcc8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1596];
		break;
		case 0xdcc7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1602];
		break;
		case 0xdcc6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1600];
		break;
		case 0xdcc5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1601];
		break;
		case 0xdcc4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1593];
		break;
		case 0xdcc3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1592];
		break;
		case 0xdcc2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1608];
		break;
		case 0xdcc1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1607];
		break;
		case 0xdcc0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1479];
		break;
		case 0xdcbf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1478];
		break;
		case 0xdcbe:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1477];
		break;
		case 0xdcbd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1476];
		break;
		case 0xdcbc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[891];
		break;
		case 0xdcbb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1468];
		break;
		case 0xdcba:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1390];
		break;
		case 0xdcb9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1744];
		break;
		case 0xdcb8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1510];
		break;
		case 0xdcb7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1514];
		break;
		case 0xdcb6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1513];
		break;
		case 0xdcb5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1511];
		break;
		case 0xdcb4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1512];
		break;
		case 0xdcb3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1516];
		break;
		case 0xdcb2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1835];
		break;
		case 0xdcb1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1836];
		break;
		case 0xdcb0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1515];
		break;
		case 0xdcaf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1717];
		break;
		case 0xdcae:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1697];
		break;
		case 0xdcad:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1881];
		break;
		case 0xdcac:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1880];
		break;
		case 0xdcab:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1025];
		break;
		case 0xdcaa:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[252];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[251];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[250];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[249];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[248];
				break;
				}
			}
			return &Items[247];
		break;
		case 0xdca9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[79];
		break;
		case 0xdca8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1048];
		break;
		case 0xdca7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1052];
		break;
		case 0xdca6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1053];
		break;
		case 0xdca5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1031];
		break;
		case 0xdca4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1752];
		break;
		case 0xdca3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1528];
		break;
		case 0xdca2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1718];
		break;
		case 0xdca1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1505];
		break;
		case 0xdca0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1749];
		break;
		case 0xdc9f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1658];
		break;
		case 0xdc9e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1652];
		break;
		case 0xdc9d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1657];
		break;
		case 0xdc9c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1647];
		break;
		case 0xdc9b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1644];
		break;
		case 0xdc9a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1645];
		break;
		case 0xdc99:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1646];
		break;
		case 0xdc98:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1656];
		break;
		case 0xdc97:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1654];
		break;
		case 0xdc96:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1655];
		break;
		case 0xdc95:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1651];
		break;
		case 0xdc94:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1649];
		break;
		case 0xdc93:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1653];
		break;
		case 0xdc92:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1443];
		break;
		case 0xdc91:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[839];
		break;
		case 0xdc90:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1000];
		break;
		case 0xdc8f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[842];
		break;
		case 0xdc8e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1517];
		break;
		case 0xdc8d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[277];
		break;
		case 0xdc8c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1580];
		break;
		case 0xdc8b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[279];
		break;
		case 0xdc8a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1544];
		break;
		case 0xdc89:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1545];
		break;
		case 0xdc88:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1539];
		break;
		case 0xdc87:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[779];
						}
					}
					return &Items[773];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[778];
						}
					}
					return &Items[772];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[777];
						}
					}
					return &Items[771];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[776];
						}
					}
					return &Items[770];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[775];
						}
					}
					return &Items[769];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2642) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[774];
				}
			break;
			}
			return &Items[768];
		break;
		case 0xdc86:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[791];
						}
					}
					return &Items[785];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[790];
						}
					}
					return &Items[784];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[789];
						}
					}
					return &Items[783];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[788];
						}
					}
					return &Items[782];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[787];
						}
					}
					return &Items[781];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2642) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[786];
				}
			break;
			}
			return &Items[780];
		break;
		case 0xdc85:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[276];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[275];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[274];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[273];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[272];
				break;
				}
			}
			return &Items[271];
		break;
		case 0xdc84:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[278];
		break;
		case 0xdc83:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[803];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[802];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[801];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[800];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[799];
				break;
				}
			}
			return &Items[798];
		break;
		case 0xdc82:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[401];
						}
					}
					return &Items[407];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[400];
						}
					}
					return &Items[406];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[399];
						}
					}
					return &Items[405];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[398];
						}
					}
					return &Items[404];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[397];
						}
					}
					return &Items[403];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[396];
				}
			break;
			}
			return &Items[402];
		break;
		case 0xdc81:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[683];
						}
					}
					return &Items[677];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[682];
						}
					}
					return &Items[676];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[681];
						}
					}
					return &Items[675];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[680];
						}
					}
					return &Items[674];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2642) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[679];
						}
					}
					return &Items[673];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2642) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[678];
				}
			break;
			}
			return &Items[672];
		break;
		case 0xdc80:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[81];
		break;
		case 0xdc7f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[76];
		break;
		case 0xdc7e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[84];
		break;
		case 0xdc7d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[83];
		break;
		case 0xdc7c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[653];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[652];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[651];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[650];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[649];
				break;
				}
			}
			return &Items[648];
		break;
		case 0xdc7b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[80];
		break;
		case 0xdc7a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[78];
		break;
		case 0xdc79:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[77];
		break;
		case 0xdc78:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[629];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[628];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[627];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[626];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[625];
				break;
				}
			}
			return &Items[624];
		break;
		case 0xdc77:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[389];
						}
					}
					return &Items[395];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[388];
						}
					}
					return &Items[394];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[387];
						}
					}
					return &Items[393];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[386];
						}
					}
					return &Items[392];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[385];
						}
					}
					return &Items[391];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[384];
				}
			break;
			}
			return &Items[390];
		break;
		case 0xdc76:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[305];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[304];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[303];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[302];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[301];
				break;
				}
			}
			return &Items[300];
		break;
		case 0xdc75:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[353];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[352];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[351];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[350];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[349];
				break;
				}
			}
			return &Items[348];
		break;
		case 0xdc74:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[347];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[346];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[345];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[344];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[343];
				break;
				}
			}
			return &Items[342];
		break;
		case 0xdc73:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[365];
						}
					}
					return &Items[371];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[364];
						}
					}
					return &Items[370];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[363];
						}
					}
					return &Items[369];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[362];
						}
					}
					return &Items[368];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[361];
						}
					}
					return &Items[367];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[360];
				}
			break;
			}
			return &Items[366];
		break;
		case 0xdc72:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[359];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[358];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[357];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[356];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[355];
				break;
				}
			}
			return &Items[354];
		break;
		case 0xdc71:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[335];
						}
					}
					return &Items[341];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[334];
						}
					}
					return &Items[340];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[333];
						}
					}
					return &Items[339];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[332];
						}
					}
					return &Items[338];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[331];
						}
					}
					return &Items[337];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[330];
				}
			break;
			}
			return &Items[336];
		break;
		case 0xdc70:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[641];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[640];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[639];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[638];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[637];
				break;
				}
			}
			return &Items[636];
		break;
		case 0xdc6f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0x200d) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2642) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[811];
				}
			}
			return &Items[810];
		break;
		case 0xdc6e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[377];
						}
					}
					return &Items[383];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[376];
						}
					}
					return &Items[382];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[375];
						}
					}
					return &Items[381];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[374];
						}
					}
					return &Items[380];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[373];
						}
					}
					return &Items[379];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[372];
				}
			break;
			}
			return &Items[378];
		break;
		case 0xdc6d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[837];
		break;
		case 0xdc6c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[838];
		break;
		case 0xdc6b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[836];
		break;
		case 0xdc6a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[845];
		break;
		case 0xdc69:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0xd83d:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xde92:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[569];
							break;
							case 0xde80:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[593];
							break;
							case 0xdd2c:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[545];
							break;
							case 0xdd27:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[533];
							break;
							case 0xdcbc:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[521];
							break;
							case 0xdcbb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[509];
							break;
							}
						break;
						case 0xd83c:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdfed:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[497];
							break;
							case 0xdfeb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[485];
							break;
							case 0xdfa8:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[557];
							break;
							case 0xdfa4:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[473];
							break;
							case 0xdf93:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[461];
							break;
							case 0xdf73:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[449];
							break;
							case 0xdf3e:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[437];
							break;
							}
						break;
						case 0x2708:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[581];
						break;
						case 0x2696:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[605];
						break;
						case 0x2695:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[425];
						break;
						}
					}
					return &Items[329];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0xd83d:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xde92:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[568];
							break;
							case 0xde80:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[592];
							break;
							case 0xdd2c:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[544];
							break;
							case 0xdd27:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[532];
							break;
							case 0xdcbc:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[520];
							break;
							case 0xdcbb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[508];
							break;
							}
						break;
						case 0xd83c:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdfed:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[496];
							break;
							case 0xdfeb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[484];
							break;
							case 0xdfa8:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[556];
							break;
							case 0xdfa4:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[472];
							break;
							case 0xdf93:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[460];
							break;
							case 0xdf73:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[448];
							break;
							case 0xdf3e:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[436];
							break;
							}
						break;
						case 0x2708:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[580];
						break;
						case 0x2696:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[604];
						break;
						case 0x2695:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[424];
						break;
						}
					}
					return &Items[328];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0xd83d:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xde92:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[567];
							break;
							case 0xde80:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[591];
							break;
							case 0xdd2c:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[543];
							break;
							case 0xdd27:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[531];
							break;
							case 0xdcbc:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[519];
							break;
							case 0xdcbb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[507];
							break;
							}
						break;
						case 0xd83c:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdfed:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[495];
							break;
							case 0xdfeb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[483];
							break;
							case 0xdfa8:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[555];
							break;
							case 0xdfa4:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[471];
							break;
							case 0xdf93:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[459];
							break;
							case 0xdf73:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[447];
							break;
							case 0xdf3e:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[435];
							break;
							}
						break;
						case 0x2708:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[579];
						break;
						case 0x2696:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[603];
						break;
						case 0x2695:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[423];
						break;
						}
					}
					return &Items[327];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0xd83d:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xde92:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[566];
							break;
							case 0xde80:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[590];
							break;
							case 0xdd2c:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[542];
							break;
							case 0xdd27:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[530];
							break;
							case 0xdcbc:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[518];
							break;
							case 0xdcbb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[506];
							break;
							}
						break;
						case 0xd83c:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdfed:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[494];
							break;
							case 0xdfeb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[482];
							break;
							case 0xdfa8:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[554];
							break;
							case 0xdfa4:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[470];
							break;
							case 0xdf93:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[458];
							break;
							case 0xdf73:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[446];
							break;
							case 0xdf3e:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[434];
							break;
							}
						break;
						case 0x2708:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[578];
						break;
						case 0x2696:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[602];
						break;
						case 0x2695:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[422];
						break;
						}
					}
					return &Items[326];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0xd83d:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xde92:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[565];
							break;
							case 0xde80:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[589];
							break;
							case 0xdd2c:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[541];
							break;
							case 0xdd27:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[529];
							break;
							case 0xdcbc:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[517];
							break;
							case 0xdcbb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[505];
							break;
							}
						break;
						case 0xd83c:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdfed:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[493];
							break;
							case 0xdfeb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[481];
							break;
							case 0xdfa8:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[553];
							break;
							case 0xdfa4:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[469];
							break;
							case 0xdf93:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[457];
							break;
							case 0xdf73:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[445];
							break;
							case 0xdf3e:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[433];
							break;
							}
						break;
						case 0x2708:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[577];
						break;
						case 0x2696:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[601];
						break;
						case 0x2695:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[421];
						break;
						}
					}
					return &Items[325];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xd83d:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end) switch (ch->unicode()) {
					case 0xde92:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[564];
					break;
					case 0xde80:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[588];
					break;
					case 0xdd2c:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[540];
					break;
					case 0xdd27:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[528];
					break;
					case 0xdcbc:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[516];
					break;
					case 0xdcbb:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[504];
					break;
					case 0xdc69:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x200d) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end && ch->unicode() == 0xd83d) {
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (ch != end) switch (ch->unicode()) {
								case 0xdc67:
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (outLength) *outLength = (ch - start);
									if (ch != end && ch->unicode() == 0x200d) {
										if (++ch != end && ch->unicode() == kPostfix) ++ch;
										if (ch != end && ch->unicode() == 0xd83d) {
											if (++ch != end && ch->unicode() == kPostfix) ++ch;
											if (ch != end) switch (ch->unicode()) {
											case 0xdc67:
												if (++ch != end && ch->unicode() == kPostfix) ++ch;
												if (outLength) *outLength = (ch - start);
												return &Items[854];
											break;
											case 0xdc66:
												if (++ch != end && ch->unicode() == kPostfix) ++ch;
												if (outLength) *outLength = (ch - start);
												return &Items[852];
											break;
											}
										}
									}
									return &Items[851];
								break;
								case 0xdc66:
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (outLength) *outLength = (ch - start);
									if (ch != end && ch->unicode() == 0x200d) {
										if (++ch != end && ch->unicode() == kPostfix) ++ch;
										if (ch != end && ch->unicode() == 0xd83d) {
											if (++ch != end && ch->unicode() == kPostfix) ++ch;
											if (ch != end && ch->unicode() == 0xdc66) {
												if (++ch != end && ch->unicode() == kPostfix) ++ch;
												if (outLength) *outLength = (ch - start);
												return &Items[853];
											}
										}
									}
									return &Items[850];
								break;
								}
							}
						}
					break;
					case 0xdc67:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						if (ch != end && ch->unicode() == 0x200d) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end && ch->unicode() == 0xd83d) {
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (ch != end) switch (ch->unicode()) {
								case 0xdc67:
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (outLength) *outLength = (ch - start);
									return &Items[864];
								break;
								case 0xdc66:
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (outLength) *outLength = (ch - start);
									return &Items[862];
								break;
								}
							}
						}
						return &Items[861];
					break;
					case 0xdc66:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						if (ch != end && ch->unicode() == 0x200d) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end && ch->unicode() == 0xd83d) {
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (ch != end && ch->unicode() == 0xdc66) {
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (outLength) *outLength = (ch - start);
									return &Items[863];
								}
							}
						}
						return &Items[860];
					break;
					}
				break;
				case 0xd83c:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end) switch (ch->unicode()) {
					case 0xdfed:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[492];
					break;
					case 0xdfeb:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[480];
					break;
					case 0xdfa8:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[552];
					break;
					case 0xdfa4:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[468];
					break;
					case 0xdf93:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[456];
					break;
					case 0xdf73:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[444];
					break;
					case 0xdf3e:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[432];
					break;
					}
				break;
				case 0x2764:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0xd83d) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdc8b:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (ch != end && ch->unicode() == 0x200d) {
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (ch != end && ch->unicode() == 0xd83d) {
										if (++ch != end && ch->unicode() == kPostfix) ++ch;
										if (ch != end && ch->unicode() == 0xdc69) {
											if (++ch != end && ch->unicode() == kPostfix) ++ch;
											if (outLength) *outLength = (ch - start);
											return &Items[843];
										}
									}
								}
							break;
							case 0xdc69:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[840];
							break;
							}
						}
					}
				break;
				case 0x2708:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[576];
				break;
				case 0x2696:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[600];
				break;
				case 0x2695:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[420];
				break;
				}
			break;
			}
			return &Items[324];
		break;
		case 0xdc68:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0xd83d:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xde92:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[575];
							break;
							case 0xde80:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[599];
							break;
							case 0xdd2c:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[551];
							break;
							case 0xdd27:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[539];
							break;
							case 0xdcbc:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[527];
							break;
							case 0xdcbb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[515];
							break;
							}
						break;
						case 0xd83c:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdfed:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[503];
							break;
							case 0xdfeb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[491];
							break;
							case 0xdfa8:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[563];
							break;
							case 0xdfa4:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[479];
							break;
							case 0xdf93:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[467];
							break;
							case 0xdf73:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[455];
							break;
							case 0xdf3e:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[443];
							break;
							}
						break;
						case 0x2708:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[587];
						break;
						case 0x2696:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[611];
						break;
						case 0x2695:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[431];
						break;
						}
					}
					return &Items[323];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0xd83d:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xde92:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[574];
							break;
							case 0xde80:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[598];
							break;
							case 0xdd2c:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[550];
							break;
							case 0xdd27:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[538];
							break;
							case 0xdcbc:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[526];
							break;
							case 0xdcbb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[514];
							break;
							}
						break;
						case 0xd83c:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdfed:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[502];
							break;
							case 0xdfeb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[490];
							break;
							case 0xdfa8:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[562];
							break;
							case 0xdfa4:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[478];
							break;
							case 0xdf93:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[466];
							break;
							case 0xdf73:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[454];
							break;
							case 0xdf3e:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[442];
							break;
							}
						break;
						case 0x2708:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[586];
						break;
						case 0x2696:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[610];
						break;
						case 0x2695:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[430];
						break;
						}
					}
					return &Items[322];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0xd83d:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xde92:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[573];
							break;
							case 0xde80:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[597];
							break;
							case 0xdd2c:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[549];
							break;
							case 0xdd27:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[537];
							break;
							case 0xdcbc:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[525];
							break;
							case 0xdcbb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[513];
							break;
							}
						break;
						case 0xd83c:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdfed:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[501];
							break;
							case 0xdfeb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[489];
							break;
							case 0xdfa8:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[561];
							break;
							case 0xdfa4:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[477];
							break;
							case 0xdf93:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[465];
							break;
							case 0xdf73:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[453];
							break;
							case 0xdf3e:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[441];
							break;
							}
						break;
						case 0x2708:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[585];
						break;
						case 0x2696:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[609];
						break;
						case 0x2695:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[429];
						break;
						}
					}
					return &Items[321];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0xd83d:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xde92:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[572];
							break;
							case 0xde80:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[596];
							break;
							case 0xdd2c:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[548];
							break;
							case 0xdd27:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[536];
							break;
							case 0xdcbc:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[524];
							break;
							case 0xdcbb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[512];
							break;
							}
						break;
						case 0xd83c:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdfed:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[500];
							break;
							case 0xdfeb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[488];
							break;
							case 0xdfa8:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[560];
							break;
							case 0xdfa4:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[476];
							break;
							case 0xdf93:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[464];
							break;
							case 0xdf73:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[452];
							break;
							case 0xdf3e:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[440];
							break;
							}
						break;
						case 0x2708:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[584];
						break;
						case 0x2696:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[608];
						break;
						case 0x2695:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[428];
						break;
						}
					}
					return &Items[320];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end) switch (ch->unicode()) {
						case 0xd83d:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xde92:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[571];
							break;
							case 0xde80:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[595];
							break;
							case 0xdd2c:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[547];
							break;
							case 0xdd27:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[535];
							break;
							case 0xdcbc:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[523];
							break;
							case 0xdcbb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[511];
							break;
							}
						break;
						case 0xd83c:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdfed:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[499];
							break;
							case 0xdfeb:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[487];
							break;
							case 0xdfa8:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[559];
							break;
							case 0xdfa4:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[475];
							break;
							case 0xdf93:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[463];
							break;
							case 0xdf73:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[451];
							break;
							case 0xdf3e:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[439];
							break;
							}
						break;
						case 0x2708:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[583];
						break;
						case 0x2696:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[607];
						break;
						case 0x2695:
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[427];
						break;
						}
					}
					return &Items[319];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xd83d:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end) switch (ch->unicode()) {
					case 0xde92:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[570];
					break;
					case 0xde80:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[594];
					break;
					case 0xdd2c:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[546];
					break;
					case 0xdd27:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[534];
					break;
					case 0xdcbc:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[522];
					break;
					case 0xdcbb:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[510];
					break;
					case 0xdc69:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x200d) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end && ch->unicode() == 0xd83d) {
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (ch != end) switch (ch->unicode()) {
								case 0xdc67:
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (outLength) *outLength = (ch - start);
									if (ch != end && ch->unicode() == 0x200d) {
										if (++ch != end && ch->unicode() == kPostfix) ++ch;
										if (ch != end && ch->unicode() == 0xd83d) {
											if (++ch != end && ch->unicode() == kPostfix) ++ch;
											if (ch != end) switch (ch->unicode()) {
											case 0xdc67:
												if (++ch != end && ch->unicode() == kPostfix) ++ch;
												if (outLength) *outLength = (ch - start);
												return &Items[849];
											break;
											case 0xdc66:
												if (++ch != end && ch->unicode() == kPostfix) ++ch;
												if (outLength) *outLength = (ch - start);
												return &Items[847];
											break;
											}
										}
									}
									return &Items[846];
								break;
								case 0xdc66:
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (ch != end && ch->unicode() == 0x200d) {
										if (++ch != end && ch->unicode() == kPostfix) ++ch;
										if (ch != end && ch->unicode() == 0xd83d) {
											if (++ch != end && ch->unicode() == kPostfix) ++ch;
											if (ch != end && ch->unicode() == 0xdc66) {
												if (++ch != end && ch->unicode() == kPostfix) ++ch;
												if (outLength) *outLength = (ch - start);
												return &Items[848];
											}
										}
									}
								break;
								}
							}
						}
					break;
					case 0xdc68:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x200d) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end && ch->unicode() == 0xd83d) {
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (ch != end) switch (ch->unicode()) {
								case 0xdc67:
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (outLength) *outLength = (ch - start);
									if (ch != end && ch->unicode() == 0x200d) {
										if (++ch != end && ch->unicode() == kPostfix) ++ch;
										if (ch != end && ch->unicode() == 0xd83d) {
											if (++ch != end && ch->unicode() == kPostfix) ++ch;
											if (ch != end) switch (ch->unicode()) {
											case 0xdc67:
												if (++ch != end && ch->unicode() == kPostfix) ++ch;
												if (outLength) *outLength = (ch - start);
												return &Items[859];
											break;
											case 0xdc66:
												if (++ch != end && ch->unicode() == kPostfix) ++ch;
												if (outLength) *outLength = (ch - start);
												return &Items[857];
											break;
											}
										}
									}
									return &Items[856];
								break;
								case 0xdc66:
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (outLength) *outLength = (ch - start);
									if (ch != end && ch->unicode() == 0x200d) {
										if (++ch != end && ch->unicode() == kPostfix) ++ch;
										if (ch != end && ch->unicode() == 0xd83d) {
											if (++ch != end && ch->unicode() == kPostfix) ++ch;
											if (ch != end && ch->unicode() == 0xdc66) {
												if (++ch != end && ch->unicode() == kPostfix) ++ch;
												if (outLength) *outLength = (ch - start);
												return &Items[858];
											}
										}
									}
									return &Items[855];
								break;
								}
							}
						}
					break;
					case 0xdc67:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						if (ch != end && ch->unicode() == 0x200d) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end && ch->unicode() == 0xd83d) {
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (ch != end) switch (ch->unicode()) {
								case 0xdc67:
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (outLength) *outLength = (ch - start);
									return &Items[869];
								break;
								case 0xdc66:
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (outLength) *outLength = (ch - start);
									return &Items[867];
								break;
								}
							}
						}
						return &Items[866];
					break;
					case 0xdc66:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						if (ch != end && ch->unicode() == 0x200d) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end && ch->unicode() == 0xd83d) {
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (ch != end && ch->unicode() == 0xdc66) {
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (outLength) *outLength = (ch - start);
									return &Items[868];
								}
							}
						}
						return &Items[865];
					break;
					}
				break;
				case 0xd83c:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end) switch (ch->unicode()) {
					case 0xdfed:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[498];
					break;
					case 0xdfeb:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[486];
					break;
					case 0xdfa8:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[558];
					break;
					case 0xdfa4:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[474];
					break;
					case 0xdf93:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[462];
					break;
					case 0xdf73:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[450];
					break;
					case 0xdf3e:
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[438];
					break;
					}
				break;
				case 0x2764:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0xd83d) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (ch != end) switch (ch->unicode()) {
							case 0xdc8b:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (ch != end && ch->unicode() == 0x200d) {
									if (++ch != end && ch->unicode() == kPostfix) ++ch;
									if (ch != end && ch->unicode() == 0xd83d) {
										if (++ch != end && ch->unicode() == kPostfix) ++ch;
										if (ch != end && ch->unicode() == 0xdc68) {
											if (++ch != end && ch->unicode() == kPostfix) ++ch;
											if (outLength) *outLength = (ch - start);
											return &Items[844];
										}
									}
								}
							break;
							case 0xdc68:
								if (++ch != end && ch->unicode() == kPostfix) ++ch;
								if (outLength) *outLength = (ch - start);
								return &Items[841];
							break;
							}
						}
					}
				break;
				case 0x2708:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[582];
				break;
				case 0x2696:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[606];
				break;
				case 0x2695:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[426];
				break;
				}
			break;
			}
			return &Items[318];
		break;
		case 0xdc67:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[317];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[316];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[315];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[314];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[313];
				break;
				}
			}
			return &Items[312];
		break;
		case 0xdc66:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[311];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[310];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[309];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[308];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[307];
				break;
				}
			}
			return &Items[306];
		break;
		case 0xdc65:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[299];
		break;
		case 0xdc64:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[298];
		break;
		case 0xdc63:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[294];
		break;
		case 0xdc62:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[879];
		break;
		case 0xdc61:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[878];
		break;
		case 0xdc60:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[877];
		break;
		case 0xdc5f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[881];
		break;
		case 0xdc5e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[880];
		break;
		case 0xdc5d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[888];
		break;
		case 0xdc5c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[890];
		break;
		case 0xdc5b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[889];
		break;
		case 0xdc5a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[870];
		break;
		case 0xdc59:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[875];
		break;
		case 0xdc58:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[876];
		break;
		case 0xdc57:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[874];
		break;
		case 0xdc56:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[872];
		break;
		case 0xdc55:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[871];
		break;
		case 0xdc54:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[873];
		break;
		case 0xdc53:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[892];
		break;
		case 0xdc52:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[882];
		break;
		case 0xdc51:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[885];
		break;
		case 0xdc50:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[101];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[100];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[99];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[98];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[97];
				break;
				}
			}
			return &Items[96];
		break;
		case 0xdc4f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[113];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[112];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[111];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[110];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[109];
				break;
				}
			}
			return &Items[108];
		break;
		case 0xdc4e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[132];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[131];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[130];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[129];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[128];
				break;
				}
			}
			return &Items[127];
		break;
		case 0xdc4d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[126];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[125];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[124];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[123];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[122];
				break;
				}
			}
			return &Items[121];
		break;
		case 0xdc4c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[180];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[179];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[178];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[177];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[176];
				break;
				}
			}
			return &Items[175];
		break;
		case 0xdc4b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[240];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[239];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[238];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[237];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[236];
				break;
				}
			}
			return &Items[235];
		break;
		case 0xdc4a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[138];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[137];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[136];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[135];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[134];
				break;
				}
			}
			return &Items[133];
		break;
		case 0xdc49:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[192];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[191];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[190];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[189];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[188];
				break;
				}
			}
			return &Items[187];
		break;
		case 0xdc48:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[186];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[185];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[184];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[183];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[182];
				break;
				}
			}
			return &Items[181];
		break;
		case 0xdc47:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[204];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[203];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[202];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[201];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[200];
				break;
				}
			}
			return &Items[199];
		break;
		case 0xdc46:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[198];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[197];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[196];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[195];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[194];
				break;
				}
			}
			return &Items[193];
		break;
		case 0xdc45:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[281];
		break;
		case 0xdc44:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[280];
		break;
		case 0xdc43:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[293];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[292];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[291];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[290];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[289];
				break;
				}
			}
			return &Items[288];
		break;
		case 0xdc42:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[287];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[286];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[285];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[284];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[283];
				break;
				}
			}
			return &Items[282];
		break;
		case 0xdc41:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0x200d) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0xd83d) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0xdde8) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[1879];
					}
				}
			}
			return &Items[295];
		break;
		case 0xdc40:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[296];
		break;
		case 0xdc3f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[980];
		break;
		case 0xdc3e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[981];
		break;
		case 0xdc3d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[909];
		break;
		case 0xdc3c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[903];
		break;
		case 0xdc3b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[902];
		break;
		case 0xdc3a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[926];
		break;
		case 0xdc39:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[899];
		break;
		case 0xdc38:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[910];
		break;
		case 0xdc37:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[908];
		break;
		case 0xdc36:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[896];
		break;
		case 0xdc35:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[911];
		break;
		case 0xdc34:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[928];
		break;
		case 0xdc33:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[952];
		break;
		case 0xdc32:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[983];
		break;
		case 0xdc31:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[897];
		break;
		case 0xdc30:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[900];
		break;
		case 0xdc2f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[905];
		break;
		case 0xdc2e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[907];
		break;
		case 0xdc2d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[898];
		break;
		case 0xdc2c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[950];
		break;
		case 0xdc2b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[962];
		break;
		case 0xdc2a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[961];
		break;
		case 0xdc29:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[972];
		break;
		case 0xdc28:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[904];
		break;
		case 0xdc27:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[917];
		break;
		case 0xdc26:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[918];
		break;
		case 0xdc25:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[921];
		break;
		case 0xdc24:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[919];
		break;
		case 0xdc23:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[920];
		break;
		case 0xdc22:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[939];
		break;
		case 0xdc21:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[949];
		break;
		case 0xdc20:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[947];
		break;
		case 0xdc1f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[948];
		break;
		case 0xdc1e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[935];
		break;
		case 0xdc1d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[930];
		break;
		case 0xdc1c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[936];
		break;
		case 0xdc1b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[931];
		break;
		case 0xdc1a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[934];
		break;
		case 0xdc19:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[945];
		break;
		case 0xdc18:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[963];
		break;
		case 0xdc17:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[927];
		break;
		case 0xdc16:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[967];
		break;
		case 0xdc15:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[971];
		break;
		case 0xdc14:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[916];
		break;
		case 0xdc13:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[974];
		break;
		case 0xdc12:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[915];
		break;
		case 0xdc11:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[970];
		break;
		case 0xdc10:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[968];
		break;
		case 0xdc0f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[969];
		break;
		case 0xdc0e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[966];
		break;
		case 0xdc0d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[940];
		break;
		case 0xdc0c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[933];
		break;
		case 0xdc0b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[953];
		break;
		case 0xdc0a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[954];
		break;
		case 0xdc09:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[982];
		break;
		case 0xdc08:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[973];
		break;
		case 0xdc07:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[977];
		break;
		case 0xdc06:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[955];
		break;
		case 0xdc05:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[956];
		break;
		case 0xdc04:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[959];
		break;
		case 0xdc03:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[957];
		break;
		case 0xdc02:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[958];
		break;
		case 0xdc01:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[978];
		break;
		case 0xdc00:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[979];
		break;
		}
	break;
	case 0xd83c:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end) switch (ch->unicode()) {
		case 0xdffa:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1536];
		break;
		case 0xdff9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1156];
		break;
		case 0xdff8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1150];
		break;
		case 0xdff7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1584];
		break;
		case 0xdff5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1312];
		break;
		case 0xdff4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1915];
		break;
		case 0xdff3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0x200d) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0xd83c) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0xdf08) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[1918];
					}
				}
			}
			return &Items[1914];
		break;
		case 0xdff0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1409];
		break;
		case 0xdfef:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1410];
		break;
		case 0xdfee:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1574];
		break;
		case 0xdfed:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1428];
		break;
		case 0xdfec:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1434];
		break;
		case 0xdfeb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1441];
		break;
		case 0xdfea:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1440];
		break;
		case 0xdfe9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1442];
		break;
		case 0xdfe8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1439];
		break;
		case 0xdfe7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1753];
		break;
		case 0xdfe6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1438];
		break;
		case 0xdfe5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1437];
		break;
		case 0xdfe4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1436];
		break;
		case 0xdfe3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1435];
		break;
		case 0xdfe2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1433];
		break;
		case 0xdfe1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1430];
		break;
		case 0xdfe0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1429];
		break;
		case 0xdfdf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1411];
		break;
		case 0xdfde:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1452];
		break;
		case 0xdfdd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1417];
		break;
		case 0xdfdc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1422];
		break;
		case 0xdfdb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1444];
		break;
		case 0xdfda:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1432];
		break;
		case 0xdfd9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1460];
		break;
		case 0xdfd8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1431];
		break;
		case 0xdfd7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1427];
		break;
		case 0xdfd6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1416];
		break;
		case 0xdfd5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1423];
		break;
		case 0xdfd4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1419];
		break;
		case 0xdfd3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1149];
		break;
		case 0xdfd2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1152];
		break;
		case 0xdfd1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1153];
		break;
		case 0xdfd0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1146];
		break;
		case 0xdfcf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1154];
		break;
		case 0xdfce:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1351];
		break;
		case 0xdfcd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1362];
		break;
		case 0xdfcc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1220];
						}
					}
					return &Items[1226];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1219];
						}
					}
					return &Items[1225];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1218];
						}
					}
					return &Items[1224];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1217];
						}
					}
					return &Items[1223];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1216];
						}
					}
					return &Items[1222];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1215];
				}
			break;
			}
			return &Items[1221];
		break;
		case 0xdfcb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1169];
						}
					}
					return &Items[1175];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1168];
						}
					}
					return &Items[1174];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1167];
						}
					}
					return &Items[1173];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1166];
						}
					}
					return &Items[1172];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1165];
						}
					}
					return &Items[1171];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1164];
				}
			break;
			}
			return &Items[1170];
		break;
		case 0xdfca:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1244];
						}
					}
					return &Items[1250];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1243];
						}
					}
					return &Items[1249];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1242];
						}
					}
					return &Items[1248];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1241];
						}
					}
					return &Items[1247];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1240];
						}
					}
					return &Items[1246];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1239];
				}
			break;
			}
			return &Items[1245];
		break;
		case 0xdfc9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1147];
		break;
		case 0xdfc8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1143];
		break;
		case 0xdfc7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1280];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1279];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1278];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1277];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1276];
				break;
				}
			}
			return &Items[1275];
		break;
		case 0xdfc6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1311];
		break;
		case 0xdfc5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1306];
		break;
		case 0xdfc4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1232];
						}
					}
					return &Items[1238];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1231];
						}
					}
					return &Items[1237];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1230];
						}
					}
					return &Items[1236];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1229];
						}
					}
					return &Items[1235];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[1228];
						}
					}
					return &Items[1234];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1227];
				}
			break;
			}
			return &Items[1233];
		break;
		case 0xdfc3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end) switch (ch->unicode()) {
			case 0xd83c:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[829];
						}
					}
					return &Items[835];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[828];
						}
					}
					return &Items[834];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[827];
						}
					}
					return &Items[833];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[826];
						}
					}
					return &Items[832];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					if (ch != end && ch->unicode() == 0x200d) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (ch != end && ch->unicode() == 0x2640) {
							if (++ch != end && ch->unicode() == kPostfix) ++ch;
							if (outLength) *outLength = (ch - start);
							return &Items[825];
						}
					}
					return &Items[831];
				break;
				}
			break;
			case 0x200d:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0x2640) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[824];
				}
			break;
			}
			return &Items[830];
		break;
		case 0xdfc2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1163];
		break;
		case 0xdfc1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1916];
		break;
		case 0xdfc0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1142];
		break;
		case 0xdfbf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1161];
		break;
		case 0xdfbe:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1145];
		break;
		case 0xdfbd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1305];
		break;
		case 0xdfbc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1334];
		break;
		case 0xdfbb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1340];
		break;
		case 0xdfba:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1338];
		break;
		case 0xdfb9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1335];
		break;
		case 0xdfb8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1339];
		break;
		case 0xdfb7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1337];
		break;
		case 0xdfb6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1830];
		break;
		case 0xdfb5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1829];
		break;
		case 0xdfb4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1888];
		break;
		case 0xdfb3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1343];
		break;
		case 0xdfb2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1341];
		break;
		case 0xdfb1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1148];
		break;
		case 0xdfb0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1345];
		break;
		case 0xdfaf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1342];
		break;
		case 0xdfae:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1344];
		break;
		case 0xdfad:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1329];
		break;
		case 0xdfac:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1331];
		break;
		case 0xdfab:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1314];
		break;
		case 0xdfaa:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1316];
		break;
		case 0xdfa9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[883];
		break;
		case 0xdfa8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1330];
		break;
		case 0xdfa7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1333];
		break;
		case 0xdfa6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1768];
		break;
		case 0xdfa5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1484];
		break;
		case 0xdfa4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1332];
		break;
		case 0xdfa3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1157];
		break;
		case 0xdfa2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1413];
		break;
		case 0xdfa1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1412];
		break;
		case 0xdfa0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1414];
		break;
		case 0xdf9f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1315];
		break;
		case 0xdf9e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1486];
		break;
		case 0xdf9b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1495];
		break;
		case 0xdf9a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1494];
		break;
		case 0xdf99:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1493];
		break;
		case 0xdf97:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1313];
		break;
		case 0xdf96:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1307];
		break;
		case 0xdf93:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[884];
		break;
		case 0xdf92:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[887];
		break;
		case 0xdf91:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1451];
		break;
		case 0xdf90:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1575];
		break;
		case 0xdf8f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1569];
		break;
		case 0xdf8e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1573];
		break;
		case 0xdf8d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[993];
		break;
		case 0xdf8c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[2031];
		break;
		case 0xdf8b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[994];
		break;
		case 0xdf8a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1571];
		break;
		case 0xdf89:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1572];
		break;
		case 0xdf88:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1568];
		break;
		case 0xdf87:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1456];
		break;
		case 0xdf86:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1457];
		break;
		case 0xdf85:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xdfff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[623];
				break;
				case 0xdffe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[622];
				break;
				case 0xdffd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[621];
				break;
				case 0xdffc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[620];
				break;
				case 0xdffb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[619];
				break;
				}
			}
			return &Items[618];
		break;
		case 0xdf84:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[985];
		break;
		case 0xdf83:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[86];
		break;
		case 0xdf82:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1117];
		break;
		case 0xdf81:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1567];
		break;
		case 0xdf80:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1570];
		break;
		case 0xdf7f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1122];
		break;
		case 0xdf7e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1137];
		break;
		case 0xdf7d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1140];
		break;
		case 0xdf7c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1126];
		break;
		case 0xdf7b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1131];
		break;
		case 0xdf7a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1130];
		break;
		case 0xdf79:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1136];
		break;
		case 0xdf78:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1135];
		break;
		case 0xdf77:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1133];
		break;
		case 0xdf76:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1129];
		break;
		case 0xdf75:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1128];
		break;
		case 0xdf74:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1139];
		break;
		case 0xdf73:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1086];
		break;
		case 0xdf72:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1103];
		break;
		case 0xdf71:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1106];
		break;
		case 0xdf70:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1116];
		break;
		case 0xdf6f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1080];
		break;
		case 0xdf6e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1118];
		break;
		case 0xdf6d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1119];
		break;
		case 0xdf6c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1120];
		break;
		case 0xdf6b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1121];
		break;
		case 0xdf6a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1124];
		break;
		case 0xdf69:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1123];
		break;
		case 0xdf68:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1114];
		break;
		case 0xdf67:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1113];
		break;
		case 0xdf66:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1115];
		break;
		case 0xdf65:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1104];
		break;
		case 0xdf64:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1089];
		break;
		case 0xdf63:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1105];
		break;
		case 0xdf62:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1111];
		break;
		case 0xdf61:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1112];
		break;
		case 0xdf60:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1077];
		break;
		case 0xdf5f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1095];
		break;
		case 0xdf5e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1082];
		break;
		case 0xdf5d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1101];
		break;
		case 0xdf5c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1102];
		break;
		case 0xdf5b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1107];
		break;
		case 0xdf5a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1109];
		break;
		case 0xdf59:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1108];
		break;
		case 0xdf58:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1110];
		break;
		case 0xdf57:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1090];
		break;
		case 0xdf56:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1091];
		break;
		case 0xdf55:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1092];
		break;
		case 0xdf54:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1094];
		break;
		case 0xdf53:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1063];
		break;
		case 0xdf52:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1065];
		break;
		case 0xdf51:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1066];
		break;
		case 0xdf50:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1057];
		break;
		case 0xdf4f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1055];
		break;
		case 0xdf4e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1056];
		break;
		case 0xdf4d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1067];
		break;
		case 0xdf4c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1060];
		break;
		case 0xdf4b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1059];
		break;
		case 0xdf4a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1058];
		break;
		case 0xdf49:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1061];
		break;
		case 0xdf48:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1064];
		break;
		case 0xdf47:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1062];
		break;
		case 0xdf46:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1071];
		break;
		case 0xdf45:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1070];
		break;
		case 0xdf44:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[998];
		break;
		case 0xdf43:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[995];
		break;
		case 0xdf42:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[996];
		break;
		case 0xdf41:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[997];
		break;
		case 0xdf40:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[992];
		break;
		case 0xdf3f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[990];
		break;
		case 0xdf3e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[999];
		break;
		case 0xdf3d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1074];
		break;
		case 0xdf3c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1005];
		break;
		case 0xdf3b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1004];
		break;
		case 0xdf3a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1007];
		break;
		case 0xdf39:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1002];
		break;
		case 0xdf38:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1006];
		break;
		case 0xdf37:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1001];
		break;
		case 0xdf36:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1075];
		break;
		case 0xdf35:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[984];
		break;
		case 0xdf34:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[988];
		break;
		case 0xdf33:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[987];
		break;
		case 0xdf32:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[986];
		break;
		case 0xdf31:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[989];
		break;
		case 0xdf30:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1078];
		break;
		case 0xdf2f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1098];
		break;
		case 0xdf2e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1097];
		break;
		case 0xdf2d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1093];
		break;
		case 0xdf2c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1047];
		break;
		case 0xdf2b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1050];
		break;
		case 0xdf2a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1049];
		break;
		case 0xdf29:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1042];
		break;
		case 0xdf28:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1043];
		break;
		case 0xdf27:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1040];
		break;
		case 0xdf26:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1037];
		break;
		case 0xdf25:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1036];
		break;
		case 0xdf24:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1034];
		break;
		case 0xdf21:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1546];
		break;
		case 0xdf20:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1455];
		break;
		case 0xdf1f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1027];
		break;
		case 0xdf1e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1021];
		break;
		case 0xdf1d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1020];
		break;
		case 0xdf1c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1023];
		break;
		case 0xdf1b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1022];
		break;
		case 0xdf1a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1019];
		break;
		case 0xdf19:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1024];
		break;
		case 0xdf18:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1014];
		break;
		case 0xdf17:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1013];
		break;
		case 0xdf16:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1012];
		break;
		case 0xdf15:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1011];
		break;
		case 0xdf14:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1018];
		break;
		case 0xdf13:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1017];
		break;
		case 0xdf12:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1016];
		break;
		case 0xdf11:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1015];
		break;
		case 0xdf10:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1748];
		break;
		case 0xdf0f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1010];
		break;
		case 0xdf0e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1008];
		break;
		case 0xdf0d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1009];
		break;
		case 0xdf0c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1462];
		break;
		case 0xdf0b:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1421];
		break;
		case 0xdf0a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1051];
		break;
		case 0xdf09:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1463];
		break;
		case 0xdf08:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1038];
		break;
		case 0xdf07:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1458];
		break;
		case 0xdf06:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1459];
		break;
		case 0xdf05:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1453];
		break;
		case 0xdf04:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1454];
		break;
		case 0xdf03:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1461];
		break;
		case 0xdf02:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[894];
		break;
		case 0xdf01:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1464];
		break;
		case 0xdf00:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1751];
		break;
		case 0xde51:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1685];
		break;
		case 0xde50:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1698];
		break;
		case 0xde3a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1693];
		break;
		case 0xde39:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1703];
		break;
		case 0xde38:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1692];
		break;
		case 0xde37:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1694];
		break;
		case 0xde36:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1690];
		break;
		case 0xde35:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1702];
		break;
		case 0xde34:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1701];
		break;
		case 0xde33:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1757];
		break;
		case 0xde32:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1704];
		break;
		case 0xde2f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1743];
		break;
		case 0xde1a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1691];
		break;
		case 0xde02:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1758];
		break;
		case 0xde01:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1770];
		break;
		case 0xddff:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddfc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2166];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2165];
				break;
				case 0xdde6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2119];
				break;
				}
			}
		break;
		case 0xddfe:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2061];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2164];
				break;
				}
			}
		break;
		case 0xddfd:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0xddf0) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2037];
				}
			}
		break;
		case 0xddfc:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2104];
				break;
				case 0xddeb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2162];
				break;
				}
			}
		break;
		case 0xddfb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddfa:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2158];
				break;
				case 0xddf3:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2161];
				break;
				case 0xddee:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2150];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1950];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2160];
				break;
				case 0xdde8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2129];
				break;
				case 0xdde6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2159];
				break;
				}
			}
		break;
		case 0xddfa:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2157];
				break;
				case 0xddfe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2156];
				break;
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2155];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2151];
				break;
				case 0xdde6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2152];
				break;
				}
			}
		break;
		case 0xddf9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2138];
				break;
				case 0xddfc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2136];
				break;
				case 0xddfb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2149];
				break;
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2144];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2146];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2143];
				break;
				case 0xddf3:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2145];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2147];
				break;
				case 0xddf1:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2140];
				break;
				case 0xddf0:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2142];
				break;
				case 0xddef:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2137];
				break;
				case 0xdded:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2139];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2141];
				break;
				case 0xddeb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1999];
				break;
				case 0xdde9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1963];
				break;
				case 0xdde8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2148];
				break;
				}
			}
		break;
		case 0xddf8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2132];
				break;
				case 0xddfe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2135];
				break;
				case 0xddfd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2113];
				break;
				case 0xddfb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1986];
				break;
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2106];
				break;
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2121];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2131];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2118];
				break;
				case 0xddf3:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2108];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2105];
				break;
				case 0xddf1:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2111];
				break;
				case 0xddf0:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2114];
				break;
				case 0xddee:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2115];
				break;
				case 0xdded:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2125];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2112];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2133];
				break;
				case 0xdde9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2130];
				break;
				case 0xdde8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2110];
				break;
				case 0xdde7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2117];
				break;
				case 0xdde6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2107];
				break;
				}
			}
		break;
		case 0xddf7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddfc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2103];
				break;
				case 0xddfa:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2102];
				break;
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2109];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2101];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2100];
				break;
				}
			}
		break;
		case 0xddf6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0xdde6) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2099];
				}
			}
		break;
		case 0xddf5:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddfe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2092];
				break;
				case 0xddfc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2088];
				break;
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2097];
				break;
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2089];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2098];
				break;
				case 0xddf3:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2095];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2128];
				break;
				case 0xddf1:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2096];
				break;
				case 0xddf0:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2087];
				break;
				case 0xdded:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2094];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2091];
				break;
				case 0xddeb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1998];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2093];
				break;
				case 0xdde6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2090];
				break;
				}
			}
		break;
		case 0xddf4:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end && ch->unicode() == 0xddf2) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2086];
				}
			}
		break;
		case 0xddf3:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2077];
				break;
				case 0xddfa:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2081];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2073];
				break;
				case 0xddf5:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2074];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2085];
				break;
				case 0xddf1:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2075];
				break;
				case 0xddee:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2078];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2080];
				break;
				case 0xddeb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2082];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2079];
				break;
				case 0xdde8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2076];
				break;
				case 0xdde6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2072];
				break;
				}
			}
		break;
		case 0xddf2:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2070];
				break;
				case 0xddfe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2053];
				break;
				case 0xddfd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2062];
				break;
				case 0xddfc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2052];
				break;
				case 0xddfb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2054];
				break;
				case 0xddfa:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2060];
				break;
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2056];
				break;
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2068];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2059];
				break;
				case 0xddf6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2058];
				break;
				case 0xddf5:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2084];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2049];
				break;
				case 0xddf3:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2066];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2071];
				break;
				case 0xddf1:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2055];
				break;
				case 0xddf0:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2050];
				break;
				case 0xdded:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2057];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2051];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2067];
				break;
				case 0xdde9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2064];
				break;
				case 0xdde8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2065];
				break;
				case 0xdde6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2069];
				break;
				}
			}
		break;
		case 0xddf1:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddfe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2045];
				break;
				case 0xddfb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2041];
				break;
				case 0xddfa:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2048];
				break;
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2047];
				break;
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2043];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2044];
				break;
				case 0xddf0:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2123];
				break;
				case 0xddee:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2046];
				break;
				case 0xdde8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2127];
				break;
				case 0xdde7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2042];
				break;
				case 0xdde6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2040];
				break;
				}
			}
		break;
		case 0xddf0:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2034];
				break;
				case 0xddfe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1961];
				break;
				case 0xddfc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2038];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2120];
				break;
				case 0xddf5:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2083];
				break;
				case 0xddf3:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2126];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1969];
				break;
				case 0xddee:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2036];
				break;
				case 0xdded:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1955];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2039];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2035];
				break;
				}
			}
		break;
		case 0xddef:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddf5:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2030];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2033];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2029];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2032];
				break;
				}
			}
		break;
		case 0xddee:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2028];
				break;
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2020];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2023];
				break;
				case 0xddf6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2024];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1949];
				break;
				case 0xddf3:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2021];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2026];
				break;
				case 0xddf1:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2027];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2025];
				break;
				case 0xdde9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2022];
				break;
				case 0xdde8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1958];
				break;
				}
			}
		break;
		case 0xdded:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddfa:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2019];
				break;
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2016];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1975];
				break;
				case 0xddf3:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2017];
				break;
				case 0xddf0:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2018];
				break;
				}
			}
		break;
		case 0xddec:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddfe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2015];
				break;
				case 0xddfc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2014];
				break;
				case 0xddfa:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2010];
				break;
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2011];
				break;
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2116];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2006];
				break;
				case 0xddf6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1987];
				break;
				case 0xddf5:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2009];
				break;
				case 0xddf3:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2013];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2001];
				break;
				case 0xddf1:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2007];
				break;
				case 0xddee:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2005];
				break;
				case 0xdded:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2004];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2012];
				break;
				case 0xddeb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1997];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2002];
				break;
				case 0xdde9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2008];
				break;
				case 0xdde7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2154];
				break;
				case 0xdde6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2000];
				break;
				}
			}
		break;
		case 0xddeb:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1996];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1993];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2063];
				break;
				case 0xddf0:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1992];
				break;
				case 0xddef:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1994];
				break;
				case 0xddee:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1995];
				break;
				}
			}
		break;
		case 0xddea:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddfa:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1991];
				break;
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1990];
				break;
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2122];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1988];
				break;
				case 0xdded:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2163];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1985];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1989];
				break;
				case 0xdde8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1984];
				break;
				}
			}
		break;
		case 0xdde9:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1922];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1983];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1982];
				break;
				case 0xddf0:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1980];
				break;
				case 0xddef:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1981];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2003];
				break;
				}
			}
		break;
		case 0xdde8:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1979];
				break;
				case 0xddfe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1978];
				break;
				case 0xddfd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1966];
				break;
				case 0xddfc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1977];
				break;
				case 0xddfb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1959];
				break;
				case 0xddfa:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1976];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1973];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1968];
				break;
				case 0xddf3:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1965];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1956];
				break;
				case 0xddf1:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1964];
				break;
				case 0xddf0:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1972];
				break;
				case 0xddee:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1974];
				break;
				case 0xdded:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2134];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1970];
				break;
				case 0xddeb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1962];
				break;
				case 0xdde9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1971];
				break;
				case 0xdde8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1967];
				break;
				case 0xdde6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1957];
				break;
				}
			}
		break;
		case 0xdde7:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1941];
				break;
				case 0xddfe:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1939];
				break;
				case 0xddfc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1947];
				break;
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1944];
				break;
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1935];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1948];
				break;
				case 0xddf6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1960];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1945];
				break;
				case 0xddf3:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1951];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1943];
				break;
				case 0xddf1:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2124];
				break;
				case 0xddef:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1942];
				break;
				case 0xddee:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1954];
				break;
				case 0xdded:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1936];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1952];
				break;
				case 0xddeb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1953];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1940];
				break;
				case 0xdde9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1937];
				break;
				case 0xdde7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1938];
				break;
				case 0xdde6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1946];
				break;
				}
			}
		break;
		case 0xdde6:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0xd83c) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (ch != end) switch (ch->unicode()) {
				case 0xddff:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1934];
				break;
				case 0xddfd:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1920];
				break;
				case 0xddfc:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1931];
				break;
				case 0xddfa:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1932];
				break;
				case 0xddf9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1933];
				break;
				case 0xddf8:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1923];
				break;
				case 0xddf7:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1929];
				break;
				case 0xddf6:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1927];
				break;
				case 0xddf4:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1925];
				break;
				case 0xddf2:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1930];
				break;
				case 0xddf1:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1921];
				break;
				case 0xddee:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1926];
				break;
				case 0xddec:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1928];
				break;
				case 0xddeb:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1919];
				break;
				case 0xddea:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[2153];
				break;
				case 0xdde9:
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (outLength) *outLength = (ch - start);
					return &Items[1924];
				break;
				}
			}
		break;
		case 0xdd9a:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1696];
		break;
		case 0xdd99:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1778];
		break;
		case 0xdd98:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1710];
		break;
		case 0xdd97:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1777];
		break;
		case 0xdd96:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1776];
		break;
		case 0xdd95:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1780];
		break;
		case 0xdd94:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1683];
		break;
		case 0xdd93:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1781];
		break;
		case 0xdd92:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1779];
		break;
		case 0xdd91:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1708];
		break;
		case 0xdd8e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1707];
		break;
		case 0xdd7f:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1756];
		break;
		case 0xdd7e:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1709];
		break;
		case 0xdd71:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1706];
		break;
		case 0xdd70:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1705];
		break;
		case 0xdccf:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1887];
		break;
		case 0xdc04:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1889];
		break;
		}
	break;
	case 0x3299:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1699];
	break;
	case 0x3297:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1700];
	break;
	case 0x303d:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1735];
	break;
	case 0x3030:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1840];
	break;
	case 0x2b55:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1712];
	break;
	case 0x2b50:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1026];
	break;
	case 0x2b1c:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1870];
	break;
	case 0x2b1b:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1869];
	break;
	case 0x2b07:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1813];
	break;
	case 0x2b06:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1812];
	break;
	case 0x2b05:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1811];
	break;
	case 0x2935:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1823];
	break;
	case 0x2934:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1822];
	break;
	case 0x27bf:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1842];
	break;
	case 0x27b0:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1841];
	break;
	case 0x27a1:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1810];
	break;
	case 0x2797:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1833];
	break;
	case 0x2796:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1832];
	break;
	case 0x2795:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1831];
	break;
	case 0x2764:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1643];
	break;
	case 0x2763:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1650];
	break;
	case 0x2757:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1727];
	break;
	case 0x2755:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1728];
	break;
	case 0x2754:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1730];
	break;
	case 0x2753:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1729];
	break;
	case 0x274e:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1747];
	break;
	case 0x274c:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1711];
	break;
	case 0x2747:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1745];
	break;
	case 0x2744:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1046];
	break;
	case 0x2734:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1695];
	break;
	case 0x2733:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1746];
	break;
	case 0x2728:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1028];
	break;
	case 0x2721:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1664];
	break;
	case 0x271d:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1660];
	break;
	case 0x2716:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1834];
	break;
	case 0x2714:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1848];
	break;
	case 0x2712:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1632];
	break;
	case 0x270f:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1636];
	break;
	case 0x270d:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		if (ch != end && ch->unicode() == 0xd83c) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xdfff:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[264];
			break;
			case 0xdffe:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[263];
			break;
			case 0xdffd:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[262];
			break;
			case 0xdffc:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[261];
			break;
			case 0xdffb:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[260];
			break;
			}
		}
		return &Items[259];
	break;
	case 0x270c:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		if (ch != end && ch->unicode() == 0xd83c) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xdfff:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[168];
			break;
			case 0xdffe:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[167];
			break;
			case 0xdffd:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[166];
			break;
			case 0xdffc:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[165];
			break;
			case 0xdffb:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[164];
			break;
			}
		}
		return &Items[163];
	break;
	case 0x270b:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		if (ch != end && ch->unicode() == 0xd83c) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xdfff:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[216];
			break;
			case 0xdffe:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[215];
			break;
			case 0xdffd:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[214];
			break;
			case 0xdffc:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[213];
			break;
			case 0xdffb:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[212];
			break;
			}
		}
		return &Items[211];
	break;
	case 0x270a:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		if (ch != end && ch->unicode() == 0xd83c) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xdfff:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[144];
			break;
			case 0xdffe:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[143];
			break;
			case 0xdffd:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[142];
			break;
			case 0xdffc:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[141];
			break;
			case 0xdffb:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[140];
			break;
			}
		}
		return &Items[139];
	break;
	case 0x2709:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1576];
	break;
	case 0x2708:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1385];
	break;
	case 0x2705:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1742];
	break;
	case 0x2702:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1629];
	break;
	case 0x26fd:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1400];
	break;
	case 0x26fa:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1424];
	break;
	case 0x26f9:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		if (ch != end) switch (ch->unicode()) {
		case 0xd83c:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xdfff:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				if (ch != end && ch->unicode() == 0x200d) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x2640) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[1196];
					}
				}
				return &Items[1202];
			break;
			case 0xdffe:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				if (ch != end && ch->unicode() == 0x200d) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x2640) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[1195];
					}
				}
				return &Items[1201];
			break;
			case 0xdffd:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				if (ch != end && ch->unicode() == 0x200d) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x2640) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[1194];
					}
				}
				return &Items[1200];
			break;
			case 0xdffc:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				if (ch != end && ch->unicode() == 0x200d) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x2640) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[1193];
					}
				}
				return &Items[1199];
			break;
			case 0xdffb:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				if (ch != end && ch->unicode() == 0x200d) {
					if (++ch != end && ch->unicode() == kPostfix) ++ch;
					if (ch != end && ch->unicode() == 0x2640) {
						if (++ch != end && ch->unicode() == kPostfix) ++ch;
						if (outLength) *outLength = (ch - start);
						return &Items[1192];
					}
				}
				return &Items[1198];
			break;
			}
		break;
		case 0x200d:
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end && ch->unicode() == 0x2640) {
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[1191];
			}
		break;
		}
		return &Items[1197];
	break;
	case 0x26f8:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1160];
	break;
	case 0x26f7:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1162];
	break;
	case 0x26f5:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1392];
	break;
	case 0x26f4:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1396];
	break;
	case 0x26f3:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1155];
	break;
	case 0x26f2:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1407];
	break;
	case 0x26f1:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1415];
	break;
	case 0x26f0:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1418];
	break;
	case 0x26ea:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1445];
	break;
	case 0x26e9:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1449];
	break;
	case 0x26d4:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1714];
	break;
	case 0x26d3:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1526];
	break;
	case 0x26d1:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[886];
	break;
	case 0x26cf:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1523];
	break;
	case 0x26ce:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1670];
	break;
	case 0x26c8:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1041];
	break;
	case 0x26c5:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1035];
	break;
	case 0x26c4:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1045];
	break;
	case 0x26be:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1144];
	break;
	case 0x26bd:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1141];
	break;
	case 0x26b1:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1535];
	break;
	case 0x26b0:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1534];
	break;
	case 0x26ab:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1852];
	break;
	case 0x26aa:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1851];
	break;
	case 0x26a1:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1029];
	break;
	case 0x26a0:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1736];
	break;
	case 0x269c:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1739];
	break;
	case 0x269b:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1684];
	break;
	case 0x2699:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1525];
	break;
	case 0x2697:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1540];
	break;
	case 0x2696:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1518];
	break;
	case 0x2694:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1531];
	break;
	case 0x2693:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1398];
	break;
	case 0x2692:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1521];
	break;
	case 0x267f:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1755];
	break;
	case 0x267b:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1741];
	break;
	case 0x2668:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1719];
	break;
	case 0x2666:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1886];
	break;
	case 0x2665:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1885];
	break;
	case 0x2663:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1884];
	break;
	case 0x2660:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1883];
	break;
	case 0x2653:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1682];
	break;
	case 0x2652:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1681];
	break;
	case 0x2651:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1680];
	break;
	case 0x2650:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1679];
	break;
	case 0x264f:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1678];
	break;
	case 0x264e:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1677];
	break;
	case 0x264d:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1676];
	break;
	case 0x264c:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1675];
	break;
	case 0x264b:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1674];
	break;
	case 0x264a:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1673];
	break;
	case 0x2649:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1672];
	break;
	case 0x2648:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1671];
	break;
	case 0x263a:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[8];
	break;
	case 0x2639:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[37];
	break;
	case 0x2638:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1663];
	break;
	case 0x262f:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1667];
	break;
	case 0x262e:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1659];
	break;
	case 0x262a:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1661];
	break;
	case 0x2626:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1668];
	break;
	case 0x2623:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1687];
	break;
	case 0x2622:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1686];
	break;
	case 0x2620:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[82];
	break;
	case 0x261d:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		if (ch != end && ch->unicode() == 0xd83c) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (ch != end) switch (ch->unicode()) {
			case 0xdfff:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[210];
			break;
			case 0xdffe:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[209];
			break;
			case 0xdffd:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[208];
			break;
			case 0xdffc:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[207];
			break;
			case 0xdffb:
				if (++ch != end && ch->unicode() == kPostfix) ++ch;
				if (outLength) *outLength = (ch - start);
				return &Items[206];
			break;
			}
		}
		return &Items[205];
	break;
	case 0x2618:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[991];
	break;
	case 0x2615:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1127];
	break;
	case 0x2614:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1054];
	break;
	case 0x2611:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1849];
	break;
	case 0x260e:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1488];
	break;
	case 0x2604:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1032];
	break;
	case 0x2603:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1044];
	break;
	case 0x2602:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[895];
	break;
	case 0x2601:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1039];
	break;
	case 0x2600:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1033];
	break;
	case 0x25fe:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1865];
	break;
	case 0x25fd:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1866];
	break;
	case 0x25fc:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1867];
	break;
	case 0x25fb:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1868];
	break;
	case 0x25c0:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1807];
	break;
	case 0x25b6:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1796];
	break;
	case 0x25ab:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1864];
	break;
	case 0x25aa:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1863];
	break;
	case 0x24c2:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1750];
	break;
	case 0x23fa:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1800];
	break;
	case 0x23f9:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1799];
	break;
	case 0x23f8:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1797];
	break;
	case 0x23f3:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1501];
	break;
	case 0x23f2:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1497];
	break;
	case 0x23f1:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1496];
	break;
	case 0x23f0:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1498];
	break;
	case 0x23ef:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1798];
	break;
	case 0x23ee:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1802];
	break;
	case 0x23ed:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1801];
	break;
	case 0x23ec:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1806];
	break;
	case 0x23eb:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1805];
	break;
	case 0x23ea:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1804];
	break;
	case 0x23e9:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1803];
	break;
	case 0x2328:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1469];
	break;
	case 0x231b:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1500];
	break;
	case 0x231a:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1465];
	break;
	case 0x21aa:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1820];
	break;
	case 0x21a9:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1821];
	break;
	case 0x2199:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1816];
	break;
	case 0x2198:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1815];
	break;
	case 0x2197:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1814];
	break;
	case 0x2196:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1817];
	break;
	case 0x2195:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1818];
	break;
	case 0x2194:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1819];
	break;
	case 0x2139:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1772];
	break;
	case 0x2122:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1837];
	break;
	case 0x2049:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1732];
	break;
	case 0x203c:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1731];
	break;
	case 0xae:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1839];
	break;
	case 0xa9:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (outLength) *outLength = (ch - start);
		return &Items[1838];
	break;
	case 0x39:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1791];
		}
	break;
	case 0x38:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1790];
		}
	break;
	case 0x37:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1789];
		}
	break;
	case 0x36:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1788];
		}
	break;
	case 0x35:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1787];
		}
	break;
	case 0x34:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1786];
		}
	break;
	case 0x33:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1785];
		}
	break;
	case 0x32:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1784];
		}
	break;
	case 0x31:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1783];
		}
	break;
	case 0x30:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1782];
		}
	break;
	case 0x2a:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1795];
		}
	break;
	case 0x23:
		if (++ch != end && ch->unicode() == kPostfix) ++ch;
		if (ch != end && ch->unicode() == 0x20e3) {
			if (++ch != end && ch->unicode() == kPostfix) ++ch;
			if (outLength) *outLength = (ch - start);
			return &Items[1794];
		}
	break;
	}

	return nullptr;
}

} // namespace internal

void Init() {
	auto tag = One::CreationTag();
	auto scaleForEmoji = cRetina() ? dbisTwo : cScale();

	switch (scaleForEmoji) {
	case dbisOne: WorkingIndex = 0; break;
	case dbisOneAndQuarter: WorkingIndex = 1; break;
	case dbisOneAndHalf: WorkingIndex = 2; break;
	case dbisTwo: WorkingIndex = 3; break;
	};

	Items.reserve(kCount);

	Items.emplace_back(internal::ComputeId(0xd83d, 0xde00), 0, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde03), 1, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde04), 2, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde01), 3, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde06), 4, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde05), 5, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde02), 6, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd23), 7, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x263a), 8, 0, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde0a), 9, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde07), 10, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde42), 11, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde43), 12, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde09), 13, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde0c), 14, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde0d), 15, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde18), 16, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde17), 17, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde19), 18, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde1a), 19, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde0b), 20, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde1c), 21, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde1d), 22, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde1b), 23, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd11), 24, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd17), 25, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd13), 26, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde0e), 27, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd21), 28, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd20), 29, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde0f), 30, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde12), 31, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde1e), 32, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde14), 33, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde1f), 34, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde15), 35, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde41), 36, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2639), 37, 0, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde23), 38, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde16), 39, 0, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde2b), 0, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde29), 1, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde24), 2, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde20), 3, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde21), 4, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde36), 5, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde10), 6, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde11), 7, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde2f), 8, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde26), 9, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde27), 10, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde2e), 11, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde32), 12, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde35), 13, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde33), 14, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde31), 15, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde28), 16, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde30), 17, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde22), 18, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde25), 19, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd24), 20, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde2d), 21, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde13), 22, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde2a), 23, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde34), 24, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde44), 25, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd14), 26, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd25), 27, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde2c), 28, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd10), 29, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd22), 30, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd27), 31, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde37), 32, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd12), 33, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd15), 34, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde08), 35, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc7f), 36, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc79), 37, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc7a), 38, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdca9), 39, 1, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc7b), 0, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc80), 1, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2620), 2, 2, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc7d), 3, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc7e), 4, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd16), 5, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf83), 6, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde3a), 7, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde38), 8, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde39), 9, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde3b), 10, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde3c), 11, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde3d), 12, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde40), 13, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde3f), 14, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde3e), 15, 2, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc50), 16, 2, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc50, 0xd83c, 0xdffb), 17, 2, false, false, &Items[96], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc50, 0xd83c, 0xdffc), 18, 2, false, false, &Items[96], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc50, 0xd83c, 0xdffd), 19, 2, false, false, &Items[96], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc50, 0xd83c, 0xdffe), 20, 2, false, false, &Items[96], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc50, 0xd83c, 0xdfff), 21, 2, false, false, &Items[96], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4c), 22, 2, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4c, 0xd83c, 0xdffb), 23, 2, false, false, &Items[102], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4c, 0xd83c, 0xdffc), 24, 2, false, false, &Items[102], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4c, 0xd83c, 0xdffd), 25, 2, false, false, &Items[102], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4c, 0xd83c, 0xdffe), 26, 2, false, false, &Items[102], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4c, 0xd83c, 0xdfff), 27, 2, false, false, &Items[102], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4f), 28, 2, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4f, 0xd83c, 0xdffb), 29, 2, false, false, &Items[108], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4f, 0xd83c, 0xdffc), 30, 2, false, false, &Items[108], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4f, 0xd83c, 0xdffd), 31, 2, false, false, &Items[108], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4f, 0xd83c, 0xdffe), 32, 2, false, false, &Items[108], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4f, 0xd83c, 0xdfff), 33, 2, false, false, &Items[108], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4f), 34, 2, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4f, 0xd83c, 0xdffb), 35, 2, false, false, &Items[114], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4f, 0xd83c, 0xdffc), 36, 2, false, false, &Items[114], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4f, 0xd83c, 0xdffd), 37, 2, false, false, &Items[114], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4f, 0xd83c, 0xdffe), 38, 2, false, false, &Items[114], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4f, 0xd83c, 0xdfff), 39, 2, false, false, &Items[114], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1d), 0, 3, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4d), 1, 3, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4d, 0xd83c, 0xdffb), 2, 3, false, false, &Items[121], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4d, 0xd83c, 0xdffc), 3, 3, false, false, &Items[121], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4d, 0xd83c, 0xdffd), 4, 3, false, false, &Items[121], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4d, 0xd83c, 0xdffe), 5, 3, false, false, &Items[121], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4d, 0xd83c, 0xdfff), 6, 3, false, false, &Items[121], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4e), 7, 3, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4e, 0xd83c, 0xdffb), 8, 3, false, false, &Items[127], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4e, 0xd83c, 0xdffc), 9, 3, false, false, &Items[127], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4e, 0xd83c, 0xdffd), 10, 3, false, false, &Items[127], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4e, 0xd83c, 0xdffe), 11, 3, false, false, &Items[127], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4e, 0xd83c, 0xdfff), 12, 3, false, false, &Items[127], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4a), 13, 3, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4a, 0xd83c, 0xdffb), 14, 3, false, false, &Items[133], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4a, 0xd83c, 0xdffc), 15, 3, false, false, &Items[133], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4a, 0xd83c, 0xdffd), 16, 3, false, false, &Items[133], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4a, 0xd83c, 0xdffe), 17, 3, false, false, &Items[133], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4a, 0xd83c, 0xdfff), 18, 3, false, false, &Items[133], tag);
	Items.emplace_back(internal::ComputeId(0x270a), 19, 3, true, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x270a, 0xd83c, 0xdffb), 20, 3, false, false, &Items[139], tag);
	Items.emplace_back(internal::ComputeId(0x270a, 0xd83c, 0xdffc), 21, 3, false, false, &Items[139], tag);
	Items.emplace_back(internal::ComputeId(0x270a, 0xd83c, 0xdffd), 22, 3, false, false, &Items[139], tag);
	Items.emplace_back(internal::ComputeId(0x270a, 0xd83c, 0xdffe), 23, 3, false, false, &Items[139], tag);
	Items.emplace_back(internal::ComputeId(0x270a, 0xd83c, 0xdfff), 24, 3, false, false, &Items[139], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1b), 25, 3, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1b, 0xd83c, 0xdffb), 26, 3, false, false, &Items[145], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1b, 0xd83c, 0xdffc), 27, 3, false, false, &Items[145], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1b, 0xd83c, 0xdffd), 28, 3, false, false, &Items[145], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1b, 0xd83c, 0xdffe), 29, 3, false, false, &Items[145], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1b, 0xd83c, 0xdfff), 30, 3, false, false, &Items[145], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1c), 31, 3, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1c, 0xd83c, 0xdffb), 32, 3, false, false, &Items[151], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1c, 0xd83c, 0xdffc), 33, 3, false, false, &Items[151], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1c, 0xd83c, 0xdffd), 34, 3, false, false, &Items[151], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1c, 0xd83c, 0xdffe), 35, 3, false, false, &Items[151], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1c, 0xd83c, 0xdfff), 36, 3, false, false, &Items[151], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1e), 37, 3, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1e, 0xd83c, 0xdffb), 38, 3, false, false, &Items[157], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1e, 0xd83c, 0xdffc), 39, 3, false, false, &Items[157], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1e, 0xd83c, 0xdffd), 0, 4, false, false, &Items[157], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1e, 0xd83c, 0xdffe), 1, 4, false, false, &Items[157], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1e, 0xd83c, 0xdfff), 2, 4, false, false, &Items[157], tag);
	Items.emplace_back(internal::ComputeId(0x270c), 3, 4, true, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x270c, 0xd83c, 0xdffb), 4, 4, false, false, &Items[163], tag);
	Items.emplace_back(internal::ComputeId(0x270c, 0xd83c, 0xdffc), 5, 4, false, false, &Items[163], tag);
	Items.emplace_back(internal::ComputeId(0x270c, 0xd83c, 0xdffd), 6, 4, false, false, &Items[163], tag);
	Items.emplace_back(internal::ComputeId(0x270c, 0xd83c, 0xdffe), 7, 4, false, false, &Items[163], tag);
	Items.emplace_back(internal::ComputeId(0x270c, 0xd83c, 0xdfff), 8, 4, false, false, &Items[163], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd18), 9, 4, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd18, 0xd83c, 0xdffb), 10, 4, false, false, &Items[169], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd18, 0xd83c, 0xdffc), 11, 4, false, false, &Items[169], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd18, 0xd83c, 0xdffd), 12, 4, false, false, &Items[169], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd18, 0xd83c, 0xdffe), 13, 4, false, false, &Items[169], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd18, 0xd83c, 0xdfff), 14, 4, false, false, &Items[169], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4c), 15, 4, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4c, 0xd83c, 0xdffb), 16, 4, false, false, &Items[175], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4c, 0xd83c, 0xdffc), 17, 4, false, false, &Items[175], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4c, 0xd83c, 0xdffd), 18, 4, false, false, &Items[175], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4c, 0xd83c, 0xdffe), 19, 4, false, false, &Items[175], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4c, 0xd83c, 0xdfff), 20, 4, false, false, &Items[175], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc48), 21, 4, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc48, 0xd83c, 0xdffb), 22, 4, false, false, &Items[181], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc48, 0xd83c, 0xdffc), 23, 4, false, false, &Items[181], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc48, 0xd83c, 0xdffd), 24, 4, false, false, &Items[181], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc48, 0xd83c, 0xdffe), 25, 4, false, false, &Items[181], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc48, 0xd83c, 0xdfff), 26, 4, false, false, &Items[181], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc49), 27, 4, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc49, 0xd83c, 0xdffb), 28, 4, false, false, &Items[187], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc49, 0xd83c, 0xdffc), 29, 4, false, false, &Items[187], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc49, 0xd83c, 0xdffd), 30, 4, false, false, &Items[187], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc49, 0xd83c, 0xdffe), 31, 4, false, false, &Items[187], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc49, 0xd83c, 0xdfff), 32, 4, false, false, &Items[187], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc46), 33, 4, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc46, 0xd83c, 0xdffb), 34, 4, false, false, &Items[193], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc46, 0xd83c, 0xdffc), 35, 4, false, false, &Items[193], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc46, 0xd83c, 0xdffd), 36, 4, false, false, &Items[193], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc46, 0xd83c, 0xdffe), 37, 4, false, false, &Items[193], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc46, 0xd83c, 0xdfff), 38, 4, false, false, &Items[193], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc47), 39, 4, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc47, 0xd83c, 0xdffb), 0, 5, false, false, &Items[199], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc47, 0xd83c, 0xdffc), 1, 5, false, false, &Items[199], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc47, 0xd83c, 0xdffd), 2, 5, false, false, &Items[199], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc47, 0xd83c, 0xdffe), 3, 5, false, false, &Items[199], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc47, 0xd83c, 0xdfff), 4, 5, false, false, &Items[199], tag);
	Items.emplace_back(internal::ComputeId(0x261d), 5, 5, true, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x261d, 0xd83c, 0xdffb), 6, 5, false, false, &Items[205], tag);
	Items.emplace_back(internal::ComputeId(0x261d, 0xd83c, 0xdffc), 7, 5, false, false, &Items[205], tag);
	Items.emplace_back(internal::ComputeId(0x261d, 0xd83c, 0xdffd), 8, 5, false, false, &Items[205], tag);
	Items.emplace_back(internal::ComputeId(0x261d, 0xd83c, 0xdffe), 9, 5, false, false, &Items[205], tag);
	Items.emplace_back(internal::ComputeId(0x261d, 0xd83c, 0xdfff), 10, 5, false, false, &Items[205], tag);
	Items.emplace_back(internal::ComputeId(0x270b), 11, 5, true, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x270b, 0xd83c, 0xdffb), 12, 5, false, false, &Items[211], tag);
	Items.emplace_back(internal::ComputeId(0x270b, 0xd83c, 0xdffc), 13, 5, false, false, &Items[211], tag);
	Items.emplace_back(internal::ComputeId(0x270b, 0xd83c, 0xdffd), 14, 5, false, false, &Items[211], tag);
	Items.emplace_back(internal::ComputeId(0x270b, 0xd83c, 0xdffe), 15, 5, false, false, &Items[211], tag);
	Items.emplace_back(internal::ComputeId(0x270b, 0xd83c, 0xdfff), 16, 5, false, false, &Items[211], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1a), 17, 5, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1a, 0xd83c, 0xdffb), 18, 5, false, false, &Items[217], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1a, 0xd83c, 0xdffc), 19, 5, false, false, &Items[217], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1a, 0xd83c, 0xdffd), 20, 5, false, false, &Items[217], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1a, 0xd83c, 0xdffe), 21, 5, false, false, &Items[217], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd1a, 0xd83c, 0xdfff), 22, 5, false, false, &Items[217], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd90), 23, 5, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd90, 0xd83c, 0xdffb), 24, 5, false, false, &Items[223], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd90, 0xd83c, 0xdffc), 25, 5, false, false, &Items[223], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd90, 0xd83c, 0xdffd), 26, 5, false, false, &Items[223], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd90, 0xd83c, 0xdffe), 27, 5, false, false, &Items[223], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd90, 0xd83c, 0xdfff), 28, 5, false, false, &Items[223], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd96), 29, 5, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd96, 0xd83c, 0xdffb), 30, 5, false, false, &Items[229], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd96, 0xd83c, 0xdffc), 31, 5, false, false, &Items[229], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd96, 0xd83c, 0xdffd), 32, 5, false, false, &Items[229], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd96, 0xd83c, 0xdffe), 33, 5, false, false, &Items[229], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd96, 0xd83c, 0xdfff), 34, 5, false, false, &Items[229], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4b), 35, 5, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4b, 0xd83c, 0xdffb), 36, 5, false, false, &Items[235], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4b, 0xd83c, 0xdffc), 37, 5, false, false, &Items[235], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4b, 0xd83c, 0xdffd), 38, 5, false, false, &Items[235], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4b, 0xd83c, 0xdffe), 39, 5, false, false, &Items[235], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc4b, 0xd83c, 0xdfff), 0, 6, false, false, &Items[235], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd19), 1, 6, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd19, 0xd83c, 0xdffb), 2, 6, false, false, &Items[241], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd19, 0xd83c, 0xdffc), 3, 6, false, false, &Items[241], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd19, 0xd83c, 0xdffd), 4, 6, false, false, &Items[241], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd19, 0xd83c, 0xdffe), 5, 6, false, false, &Items[241], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd19, 0xd83c, 0xdfff), 6, 6, false, false, &Items[241], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcaa), 7, 6, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcaa, 0xd83c, 0xdffb), 8, 6, false, false, &Items[247], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcaa, 0xd83c, 0xdffc), 9, 6, false, false, &Items[247], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcaa, 0xd83c, 0xdffd), 10, 6, false, false, &Items[247], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcaa, 0xd83c, 0xdffe), 11, 6, false, false, &Items[247], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcaa, 0xd83c, 0xdfff), 12, 6, false, false, &Items[247], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd95), 13, 6, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd95, 0xd83c, 0xdffb), 14, 6, false, false, &Items[253], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd95, 0xd83c, 0xdffc), 15, 6, false, false, &Items[253], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd95, 0xd83c, 0xdffd), 16, 6, false, false, &Items[253], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd95, 0xd83c, 0xdffe), 17, 6, false, false, &Items[253], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd95, 0xd83c, 0xdfff), 18, 6, false, false, &Items[253], tag);
	Items.emplace_back(internal::ComputeId(0x270d), 19, 6, true, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x270d, 0xd83c, 0xdffb), 20, 6, false, false, &Items[259], tag);
	Items.emplace_back(internal::ComputeId(0x270d, 0xd83c, 0xdffc), 21, 6, false, false, &Items[259], tag);
	Items.emplace_back(internal::ComputeId(0x270d, 0xd83c, 0xdffd), 22, 6, false, false, &Items[259], tag);
	Items.emplace_back(internal::ComputeId(0x270d, 0xd83c, 0xdffe), 23, 6, false, false, &Items[259], tag);
	Items.emplace_back(internal::ComputeId(0x270d, 0xd83c, 0xdfff), 24, 6, false, false, &Items[259], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd33), 25, 6, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd33, 0xd83c, 0xdffb), 26, 6, false, false, &Items[265], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd33, 0xd83c, 0xdffc), 27, 6, false, false, &Items[265], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd33, 0xd83c, 0xdffd), 28, 6, false, false, &Items[265], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd33, 0xd83c, 0xdffe), 29, 6, false, false, &Items[265], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd33, 0xd83c, 0xdfff), 30, 6, false, false, &Items[265], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc85), 31, 6, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc85, 0xd83c, 0xdffb), 32, 6, false, false, &Items[271], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc85, 0xd83c, 0xdffc), 33, 6, false, false, &Items[271], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc85, 0xd83c, 0xdffd), 34, 6, false, false, &Items[271], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc85, 0xd83c, 0xdffe), 35, 6, false, false, &Items[271], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc85, 0xd83c, 0xdfff), 36, 6, false, false, &Items[271], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc8d), 37, 6, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc84), 38, 6, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc8b), 39, 6, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc44), 0, 7, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc45), 1, 7, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc42), 2, 7, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc42, 0xd83c, 0xdffb), 3, 7, false, false, &Items[282], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc42, 0xd83c, 0xdffc), 4, 7, false, false, &Items[282], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc42, 0xd83c, 0xdffd), 5, 7, false, false, &Items[282], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc42, 0xd83c, 0xdffe), 6, 7, false, false, &Items[282], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc42, 0xd83c, 0xdfff), 7, 7, false, false, &Items[282], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc43), 8, 7, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc43, 0xd83c, 0xdffb), 9, 7, false, false, &Items[288], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc43, 0xd83c, 0xdffc), 10, 7, false, false, &Items[288], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc43, 0xd83c, 0xdffd), 11, 7, false, false, &Items[288], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc43, 0xd83c, 0xdffe), 12, 7, false, false, &Items[288], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc43, 0xd83c, 0xdfff), 13, 7, false, false, &Items[288], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc63), 14, 7, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc41), 15, 7, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc40), 16, 7, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdde3), 17, 7, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc64), 18, 7, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc65), 19, 7, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc76), 20, 7, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc76, 0xd83c, 0xdffb), 21, 7, false, false, &Items[300], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc76, 0xd83c, 0xdffc), 22, 7, false, false, &Items[300], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc76, 0xd83c, 0xdffd), 23, 7, false, false, &Items[300], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc76, 0xd83c, 0xdffe), 24, 7, false, false, &Items[300], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc76, 0xd83c, 0xdfff), 25, 7, false, false, &Items[300], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc66), 26, 7, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc66, 0xd83c, 0xdffb), 27, 7, false, false, &Items[306], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc66, 0xd83c, 0xdffc), 28, 7, false, false, &Items[306], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc66, 0xd83c, 0xdffd), 29, 7, false, false, &Items[306], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc66, 0xd83c, 0xdffe), 30, 7, false, false, &Items[306], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc66, 0xd83c, 0xdfff), 31, 7, false, false, &Items[306], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc67), 32, 7, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc67, 0xd83c, 0xdffb), 33, 7, false, false, &Items[312], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc67, 0xd83c, 0xdffc), 34, 7, false, false, &Items[312], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc67, 0xd83c, 0xdffd), 35, 7, false, false, &Items[312], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc67, 0xd83c, 0xdffe), 36, 7, false, false, &Items[312], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc67, 0xd83c, 0xdfff), 37, 7, false, false, &Items[312], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68), 38, 7, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb), 39, 7, false, false, &Items[318], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc), 0, 8, false, false, &Items[318], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd), 1, 8, false, false, &Items[318], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe), 2, 8, false, false, &Items[318], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff), 3, 8, false, false, &Items[318], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69), 4, 8, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb), 5, 8, false, false, &Items[324], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc), 6, 8, false, false, &Items[324], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd), 7, 8, false, false, &Items[324], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe), 8, 8, false, false, &Items[324], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff), 9, 8, false, false, &Items[324], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71, 0x200d, 0x2640, 0xfe0f), 10, 8, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 11, 8, false, false, &Items[330], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 12, 8, false, false, &Items[330], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 13, 8, false, false, &Items[330], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 14, 8, false, false, &Items[330], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 15, 8, false, false, &Items[330], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71), 16, 8, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71, 0xd83c, 0xdffb), 17, 8, false, false, &Items[336], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71, 0xd83c, 0xdffc), 18, 8, false, false, &Items[336], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71, 0xd83c, 0xdffd), 19, 8, false, false, &Items[336], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71, 0xd83c, 0xdffe), 20, 8, false, false, &Items[336], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc71, 0xd83c, 0xdfff), 21, 8, false, false, &Items[336], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc74), 22, 8, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc74, 0xd83c, 0xdffb), 23, 8, false, false, &Items[342], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc74, 0xd83c, 0xdffc), 24, 8, false, false, &Items[342], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc74, 0xd83c, 0xdffd), 25, 8, false, false, &Items[342], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc74, 0xd83c, 0xdffe), 26, 8, false, false, &Items[342], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc74, 0xd83c, 0xdfff), 27, 8, false, false, &Items[342], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc75), 28, 8, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc75, 0xd83c, 0xdffb), 29, 8, false, false, &Items[348], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc75, 0xd83c, 0xdffc), 30, 8, false, false, &Items[348], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc75, 0xd83c, 0xdffd), 31, 8, false, false, &Items[348], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc75, 0xd83c, 0xdffe), 32, 8, false, false, &Items[348], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc75, 0xd83c, 0xdfff), 33, 8, false, false, &Items[348], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc72), 34, 8, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc72, 0xd83c, 0xdffb), 35, 8, false, false, &Items[354], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc72, 0xd83c, 0xdffc), 36, 8, false, false, &Items[354], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc72, 0xd83c, 0xdffd), 37, 8, false, false, &Items[354], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc72, 0xd83c, 0xdffe), 38, 8, false, false, &Items[354], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc72, 0xd83c, 0xdfff), 39, 8, false, false, &Items[354], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73, 0x200d, 0x2640, 0xfe0f), 0, 9, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 1, 9, false, false, &Items[360], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 2, 9, false, false, &Items[360], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 3, 9, false, false, &Items[360], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 4, 9, false, false, &Items[360], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 5, 9, false, false, &Items[360], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73), 6, 9, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73, 0xd83c, 0xdffb), 7, 9, false, false, &Items[366], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73, 0xd83c, 0xdffc), 8, 9, false, false, &Items[366], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73, 0xd83c, 0xdffd), 9, 9, false, false, &Items[366], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73, 0xd83c, 0xdffe), 10, 9, false, false, &Items[366], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc73, 0xd83c, 0xdfff), 11, 9, false, false, &Items[366], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e, 0x200d, 0x2640, 0xfe0f), 12, 9, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 13, 9, false, false, &Items[372], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 14, 9, false, false, &Items[372], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 15, 9, false, false, &Items[372], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 16, 9, false, false, &Items[372], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 17, 9, false, false, &Items[372], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e), 18, 9, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e, 0xd83c, 0xdffb), 19, 9, false, false, &Items[378], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e, 0xd83c, 0xdffc), 20, 9, false, false, &Items[378], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e, 0xd83c, 0xdffd), 21, 9, false, false, &Items[378], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e, 0xd83c, 0xdffe), 22, 9, false, false, &Items[378], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6e, 0xd83c, 0xdfff), 23, 9, false, false, &Items[378], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77, 0x200d, 0x2640, 0xfe0f), 24, 9, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 25, 9, false, false, &Items[384], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 26, 9, false, false, &Items[384], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 27, 9, false, false, &Items[384], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 28, 9, false, false, &Items[384], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 29, 9, false, false, &Items[384], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77), 30, 9, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77, 0xd83c, 0xdffb), 31, 9, false, false, &Items[390], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77, 0xd83c, 0xdffc), 32, 9, false, false, &Items[390], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77, 0xd83c, 0xdffd), 33, 9, false, false, &Items[390], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77, 0xd83c, 0xdffe), 34, 9, false, false, &Items[390], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc77, 0xd83c, 0xdfff), 35, 9, false, false, &Items[390], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82, 0x200d, 0x2640, 0xfe0f), 36, 9, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 37, 9, false, false, &Items[396], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 38, 9, false, false, &Items[396], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 39, 9, false, false, &Items[396], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 0, 10, false, false, &Items[396], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 1, 10, false, false, &Items[396], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82), 2, 10, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82, 0xd83c, 0xdffb), 3, 10, false, false, &Items[402], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82, 0xd83c, 0xdffc), 4, 10, false, false, &Items[402], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82, 0xd83c, 0xdffd), 5, 10, false, false, &Items[402], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82, 0xd83c, 0xdffe), 6, 10, false, false, &Items[402], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc82, 0xd83c, 0xdfff), 7, 10, false, false, &Items[402], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 8, 10, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75, 0xd83c, 0xdffb, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 9, 10, false, false, &Items[408], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75, 0xd83c, 0xdffc, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 10, 10, false, false, &Items[408], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75, 0xd83c, 0xdffd, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 11, 10, false, false, &Items[408], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75, 0xd83c, 0xdffe, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 12, 10, false, false, &Items[408], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75, 0xd83c, 0xdfff, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 13, 10, false, false, &Items[408], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75), 14, 10, true, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75, 0xd83c, 0xdffb), 15, 10, false, false, &Items[414], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75, 0xd83c, 0xdffc), 16, 10, false, false, &Items[414], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75, 0xd83c, 0xdffd), 17, 10, false, false, &Items[414], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75, 0xd83c, 0xdffe), 18, 10, false, false, &Items[414], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd75, 0xd83c, 0xdfff), 19, 10, false, false, &Items[414], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0x2695, 0xfe0f), 20, 10, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0x2695, 0xfe0f), 21, 10, false, false, &Items[420], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0x2695, 0xfe0f), 22, 10, false, false, &Items[420], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0x2695, 0xfe0f), 23, 10, false, false, &Items[420], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0x2695, 0xfe0f), 24, 10, false, false, &Items[420], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0x2695, 0xfe0f), 25, 10, false, false, &Items[420], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0x2695, 0xfe0f), 26, 10, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0x2695, 0xfe0f), 27, 10, false, false, &Items[426], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0x2695, 0xfe0f), 28, 10, false, false, &Items[426], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0x2695, 0xfe0f), 29, 10, false, false, &Items[426], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0x2695, 0xfe0f), 30, 10, false, false, &Items[426], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0x2695, 0xfe0f), 31, 10, false, false, &Items[426], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83c, 0xdf3e), 32, 10, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdf3e), 33, 10, false, false, &Items[432], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdf3e), 34, 10, false, false, &Items[432], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdf3e), 35, 10, false, false, &Items[432], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdf3e), 36, 10, false, false, &Items[432], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdf3e), 37, 10, false, false, &Items[432], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83c, 0xdf3e), 38, 10, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdf3e), 39, 10, false, false, &Items[438], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdf3e), 0, 11, false, false, &Items[438], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdf3e), 1, 11, false, false, &Items[438], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdf3e), 2, 11, false, false, &Items[438], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdf3e), 3, 11, false, false, &Items[438], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83c, 0xdf73), 4, 11, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdf73), 5, 11, false, false, &Items[444], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdf73), 6, 11, false, false, &Items[444], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdf73), 7, 11, false, false, &Items[444], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdf73), 8, 11, false, false, &Items[444], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdf73), 9, 11, false, false, &Items[444], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83c, 0xdf73), 10, 11, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdf73), 11, 11, false, false, &Items[450], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdf73), 12, 11, false, false, &Items[450], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdf73), 13, 11, false, false, &Items[450], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdf73), 14, 11, false, false, &Items[450], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdf73), 15, 11, false, false, &Items[450], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83c, 0xdf93), 16, 11, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdf93), 17, 11, false, false, &Items[456], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdf93), 18, 11, false, false, &Items[456], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdf93), 19, 11, false, false, &Items[456], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdf93), 20, 11, false, false, &Items[456], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdf93), 21, 11, false, false, &Items[456], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83c, 0xdf93), 22, 11, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdf93), 23, 11, false, false, &Items[462], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdf93), 24, 11, false, false, &Items[462], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdf93), 25, 11, false, false, &Items[462], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdf93), 26, 11, false, false, &Items[462], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdf93), 27, 11, false, false, &Items[462], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83c, 0xdfa4), 28, 11, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdfa4), 29, 11, false, false, &Items[468], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdfa4), 30, 11, false, false, &Items[468], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdfa4), 31, 11, false, false, &Items[468], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdfa4), 32, 11, false, false, &Items[468], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdfa4), 33, 11, false, false, &Items[468], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83c, 0xdfa4), 34, 11, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdfa4), 35, 11, false, false, &Items[474], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdfa4), 36, 11, false, false, &Items[474], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdfa4), 37, 11, false, false, &Items[474], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdfa4), 38, 11, false, false, &Items[474], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdfa4), 39, 11, false, false, &Items[474], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83c, 0xdfeb), 0, 12, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdfeb), 1, 12, false, false, &Items[480], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdfeb), 2, 12, false, false, &Items[480], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdfeb), 3, 12, false, false, &Items[480], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdfeb), 4, 12, false, false, &Items[480], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdfeb), 5, 12, false, false, &Items[480], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83c, 0xdfeb), 6, 12, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdfeb), 7, 12, false, false, &Items[486], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdfeb), 8, 12, false, false, &Items[486], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdfeb), 9, 12, false, false, &Items[486], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdfeb), 10, 12, false, false, &Items[486], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdfeb), 11, 12, false, false, &Items[486], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83c, 0xdfed), 12, 12, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdfed), 13, 12, false, false, &Items[492], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdfed), 14, 12, false, false, &Items[492], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdfed), 15, 12, false, false, &Items[492], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdfed), 16, 12, false, false, &Items[492], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdfed), 17, 12, false, false, &Items[492], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83c, 0xdfed), 18, 12, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdfed), 19, 12, false, false, &Items[498], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdfed), 20, 12, false, false, &Items[498], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdfed), 21, 12, false, false, &Items[498], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdfed), 22, 12, false, false, &Items[498], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdfed), 23, 12, false, false, &Items[498], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdcbb), 24, 12, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xdcbb), 25, 12, false, false, &Items[504], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xdcbb), 26, 12, false, false, &Items[504], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xdcbb), 27, 12, false, false, &Items[504], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xdcbb), 28, 12, false, false, &Items[504], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xdcbb), 29, 12, false, false, &Items[504], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdcbb), 30, 12, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xdcbb), 31, 12, false, false, &Items[510], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xdcbb), 32, 12, false, false, &Items[510], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xdcbb), 33, 12, false, false, &Items[510], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xdcbb), 34, 12, false, false, &Items[510], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xdcbb), 35, 12, false, false, &Items[510], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdcbc), 36, 12, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xdcbc), 37, 12, false, false, &Items[516], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xdcbc), 38, 12, false, false, &Items[516], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xdcbc), 39, 12, false, false, &Items[516], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xdcbc), 0, 13, false, false, &Items[516], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xdcbc), 1, 13, false, false, &Items[516], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdcbc), 2, 13, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xdcbc), 3, 13, false, false, &Items[522], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xdcbc), 4, 13, false, false, &Items[522], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xdcbc), 5, 13, false, false, &Items[522], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xdcbc), 6, 13, false, false, &Items[522], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xdcbc), 7, 13, false, false, &Items[522], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdd27), 8, 13, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xdd27), 9, 13, false, false, &Items[528], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xdd27), 10, 13, false, false, &Items[528], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xdd27), 11, 13, false, false, &Items[528], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xdd27), 12, 13, false, false, &Items[528], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xdd27), 13, 13, false, false, &Items[528], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdd27), 14, 13, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xdd27), 15, 13, false, false, &Items[534], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xdd27), 16, 13, false, false, &Items[534], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xdd27), 17, 13, false, false, &Items[534], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xdd27), 18, 13, false, false, &Items[534], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xdd27), 19, 13, false, false, &Items[534], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdd2c), 20, 13, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xdd2c), 21, 13, false, false, &Items[540], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xdd2c), 22, 13, false, false, &Items[540], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xdd2c), 23, 13, false, false, &Items[540], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xdd2c), 24, 13, false, false, &Items[540], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xdd2c), 25, 13, false, false, &Items[540], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdd2c), 26, 13, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xdd2c), 27, 13, false, false, &Items[546], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xdd2c), 28, 13, false, false, &Items[546], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xdd2c), 29, 13, false, false, &Items[546], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xdd2c), 30, 13, false, false, &Items[546], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xdd2c), 31, 13, false, false, &Items[546], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83c, 0xdfa8), 32, 13, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdfa8), 33, 13, false, false, &Items[552], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdfa8), 34, 13, false, false, &Items[552], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdfa8), 35, 13, false, false, &Items[552], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdfa8), 36, 13, false, false, &Items[552], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdfa8), 37, 13, false, false, &Items[552], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83c, 0xdfa8), 38, 13, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83c, 0xdfa8), 39, 13, false, false, &Items[558], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83c, 0xdfa8), 0, 14, false, false, &Items[558], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83c, 0xdfa8), 1, 14, false, false, &Items[558], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83c, 0xdfa8), 2, 14, false, false, &Items[558], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83c, 0xdfa8), 3, 14, false, false, &Items[558], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xde92), 4, 14, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xde92), 5, 14, false, false, &Items[564], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xde92), 6, 14, false, false, &Items[564], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xde92), 7, 14, false, false, &Items[564], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xde92), 8, 14, false, false, &Items[564], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xde92), 9, 14, false, false, &Items[564], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xde92), 10, 14, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xde92), 11, 14, false, false, &Items[570], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xde92), 12, 14, false, false, &Items[570], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xde92), 13, 14, false, false, &Items[570], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xde92), 14, 14, false, false, &Items[570], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xde92), 15, 14, false, false, &Items[570], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0x2708, 0xfe0f), 16, 14, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0x2708, 0xfe0f), 17, 14, false, false, &Items[576], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0x2708, 0xfe0f), 18, 14, false, false, &Items[576], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0x2708, 0xfe0f), 19, 14, false, false, &Items[576], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0x2708, 0xfe0f), 20, 14, false, false, &Items[576], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0x2708, 0xfe0f), 21, 14, false, false, &Items[576], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0x2708, 0xfe0f), 22, 14, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0x2708, 0xfe0f), 23, 14, false, false, &Items[582], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0x2708, 0xfe0f), 24, 14, false, false, &Items[582], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0x2708, 0xfe0f), 25, 14, false, false, &Items[582], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0x2708, 0xfe0f), 26, 14, false, false, &Items[582], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0x2708, 0xfe0f), 27, 14, false, false, &Items[582], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xde80), 28, 14, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xde80), 29, 14, false, false, &Items[588], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xde80), 30, 14, false, false, &Items[588], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xde80), 31, 14, false, false, &Items[588], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xde80), 32, 14, false, false, &Items[588], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xde80), 33, 14, false, false, &Items[588], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xde80), 34, 14, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0xd83d, 0xde80), 35, 14, false, false, &Items[594], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0xd83d, 0xde80), 36, 14, false, false, &Items[594], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0xd83d, 0xde80), 37, 14, false, false, &Items[594], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0xd83d, 0xde80), 38, 14, false, false, &Items[594], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0xd83d, 0xde80), 39, 14, false, false, &Items[594], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0x2696, 0xfe0f), 0, 15, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffb, 0x200d, 0x2696, 0xfe0f), 1, 15, false, false, &Items[600], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffc, 0x200d, 0x2696, 0xfe0f), 2, 15, false, false, &Items[600], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffd, 0x200d, 0x2696, 0xfe0f), 3, 15, false, false, &Items[600], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdffe, 0x200d, 0x2696, 0xfe0f), 4, 15, false, false, &Items[600], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0xd83c, 0xdfff, 0x200d, 0x2696, 0xfe0f), 5, 15, false, false, &Items[600], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0x2696, 0xfe0f), 6, 15, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffb, 0x200d, 0x2696, 0xfe0f), 7, 15, false, false, &Items[606], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffc, 0x200d, 0x2696, 0xfe0f), 8, 15, false, false, &Items[606], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffd, 0x200d, 0x2696, 0xfe0f), 9, 15, false, false, &Items[606], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdffe, 0x200d, 0x2696, 0xfe0f), 10, 15, false, false, &Items[606], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0xd83c, 0xdfff, 0x200d, 0x2696, 0xfe0f), 11, 15, false, false, &Items[606], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd36), 12, 15, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd36, 0xd83c, 0xdffb), 13, 15, false, false, &Items[612], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd36, 0xd83c, 0xdffc), 14, 15, false, false, &Items[612], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd36, 0xd83c, 0xdffd), 15, 15, false, false, &Items[612], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd36, 0xd83c, 0xdffe), 16, 15, false, false, &Items[612], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd36, 0xd83c, 0xdfff), 17, 15, false, false, &Items[612], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf85), 18, 15, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf85, 0xd83c, 0xdffb), 19, 15, false, false, &Items[618], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf85, 0xd83c, 0xdffc), 20, 15, false, false, &Items[618], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf85, 0xd83c, 0xdffd), 21, 15, false, false, &Items[618], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf85, 0xd83c, 0xdffe), 22, 15, false, false, &Items[618], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf85, 0xd83c, 0xdfff), 23, 15, false, false, &Items[618], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc78), 24, 15, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc78, 0xd83c, 0xdffb), 25, 15, false, false, &Items[624], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc78, 0xd83c, 0xdffc), 26, 15, false, false, &Items[624], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc78, 0xd83c, 0xdffd), 27, 15, false, false, &Items[624], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc78, 0xd83c, 0xdffe), 28, 15, false, false, &Items[624], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc78, 0xd83c, 0xdfff), 29, 15, false, false, &Items[624], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd34), 30, 15, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd34, 0xd83c, 0xdffb), 31, 15, false, false, &Items[630], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd34, 0xd83c, 0xdffc), 32, 15, false, false, &Items[630], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd34, 0xd83c, 0xdffd), 33, 15, false, false, &Items[630], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd34, 0xd83c, 0xdffe), 34, 15, false, false, &Items[630], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd34, 0xd83c, 0xdfff), 35, 15, false, false, &Items[630], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc70), 36, 15, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc70, 0xd83c, 0xdffb), 37, 15, false, false, &Items[636], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc70, 0xd83c, 0xdffc), 38, 15, false, false, &Items[636], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc70, 0xd83c, 0xdffd), 39, 15, false, false, &Items[636], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc70, 0xd83c, 0xdffe), 0, 16, false, false, &Items[636], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc70, 0xd83c, 0xdfff), 1, 16, false, false, &Items[636], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd35), 2, 16, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd35, 0xd83c, 0xdffb), 3, 16, false, false, &Items[642], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd35, 0xd83c, 0xdffc), 4, 16, false, false, &Items[642], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd35, 0xd83c, 0xdffd), 5, 16, false, false, &Items[642], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd35, 0xd83c, 0xdffe), 6, 16, false, false, &Items[642], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd35, 0xd83c, 0xdfff), 7, 16, false, false, &Items[642], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc7c), 8, 16, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc7c, 0xd83c, 0xdffb), 9, 16, false, false, &Items[648], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc7c, 0xd83c, 0xdffc), 10, 16, false, false, &Items[648], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc7c, 0xd83c, 0xdffd), 11, 16, false, false, &Items[648], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc7c, 0xd83c, 0xdffe), 12, 16, false, false, &Items[648], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc7c, 0xd83c, 0xdfff), 13, 16, false, false, &Items[648], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd30), 14, 16, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd30, 0xd83c, 0xdffb), 15, 16, false, false, &Items[654], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd30, 0xd83c, 0xdffc), 16, 16, false, false, &Items[654], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd30, 0xd83c, 0xdffd), 17, 16, false, false, &Items[654], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd30, 0xd83c, 0xdffe), 18, 16, false, false, &Items[654], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd30, 0xd83c, 0xdfff), 19, 16, false, false, &Items[654], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47, 0x200d, 0x2640, 0xfe0f), 20, 16, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 21, 16, false, false, &Items[660], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 22, 16, false, false, &Items[660], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 23, 16, false, false, &Items[660], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 24, 16, false, false, &Items[660], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 25, 16, false, false, &Items[660], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47), 26, 16, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47, 0xd83c, 0xdffb), 27, 16, false, false, &Items[666], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47, 0xd83c, 0xdffc), 28, 16, false, false, &Items[666], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47, 0xd83c, 0xdffd), 29, 16, false, false, &Items[666], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47, 0xd83c, 0xdffe), 30, 16, false, false, &Items[666], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde47, 0xd83c, 0xdfff), 31, 16, false, false, &Items[666], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81), 32, 16, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81, 0xd83c, 0xdffb), 33, 16, false, false, &Items[672], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81, 0xd83c, 0xdffc), 34, 16, false, false, &Items[672], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81, 0xd83c, 0xdffd), 35, 16, false, false, &Items[672], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81, 0xd83c, 0xdffe), 36, 16, false, false, &Items[672], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81, 0xd83c, 0xdfff), 37, 16, false, false, &Items[672], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81, 0x200d, 0x2642, 0xfe0f), 38, 16, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 39, 16, false, false, &Items[678], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 0, 17, false, false, &Items[678], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 1, 17, false, false, &Items[678], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 2, 17, false, false, &Items[678], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc81, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 3, 17, false, false, &Items[678], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45), 4, 17, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45, 0xd83c, 0xdffb), 5, 17, false, false, &Items[684], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45, 0xd83c, 0xdffc), 6, 17, false, false, &Items[684], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45, 0xd83c, 0xdffd), 7, 17, false, false, &Items[684], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45, 0xd83c, 0xdffe), 8, 17, false, false, &Items[684], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45, 0xd83c, 0xdfff), 9, 17, false, false, &Items[684], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45, 0x200d, 0x2642, 0xfe0f), 10, 17, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 11, 17, false, false, &Items[690], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 12, 17, false, false, &Items[690], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 13, 17, false, false, &Items[690], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 14, 17, false, false, &Items[690], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde45, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 15, 17, false, false, &Items[690], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46), 16, 17, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46, 0xd83c, 0xdffb), 17, 17, false, false, &Items[696], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46, 0xd83c, 0xdffc), 18, 17, false, false, &Items[696], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46, 0xd83c, 0xdffd), 19, 17, false, false, &Items[696], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46, 0xd83c, 0xdffe), 20, 17, false, false, &Items[696], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46, 0xd83c, 0xdfff), 21, 17, false, false, &Items[696], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46, 0x200d, 0x2642, 0xfe0f), 22, 17, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 23, 17, false, false, &Items[702], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 24, 17, false, false, &Items[702], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 25, 17, false, false, &Items[702], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 26, 17, false, false, &Items[702], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde46, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 27, 17, false, false, &Items[702], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b), 28, 17, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b, 0xd83c, 0xdffb), 29, 17, false, false, &Items[708], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b, 0xd83c, 0xdffc), 30, 17, false, false, &Items[708], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b, 0xd83c, 0xdffd), 31, 17, false, false, &Items[708], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b, 0xd83c, 0xdffe), 32, 17, false, false, &Items[708], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b, 0xd83c, 0xdfff), 33, 17, false, false, &Items[708], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b, 0x200d, 0x2642, 0xfe0f), 34, 17, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 35, 17, false, false, &Items[714], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 36, 17, false, false, &Items[714], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 37, 17, false, false, &Items[714], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 38, 17, false, false, &Items[714], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4b, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 39, 17, false, false, &Items[714], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0x200d, 0x2640, 0xfe0f), 0, 18, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 1, 18, false, false, &Items[720], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 2, 18, false, false, &Items[720], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 3, 18, false, false, &Items[720], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 4, 18, false, false, &Items[720], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 5, 18, false, false, &Items[720], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0x200d, 0x2642, 0xfe0f), 6, 18, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 7, 18, false, false, &Items[726], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 8, 18, false, false, &Items[726], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 9, 18, false, false, &Items[726], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 10, 18, false, false, &Items[726], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd26, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 11, 18, false, false, &Items[726], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0x200d, 0x2640, 0xfe0f), 12, 18, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 13, 18, false, false, &Items[732], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 14, 18, false, false, &Items[732], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 15, 18, false, false, &Items[732], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 16, 18, false, false, &Items[732], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 17, 18, false, false, &Items[732], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0x200d, 0x2642, 0xfe0f), 18, 18, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 19, 18, false, false, &Items[738], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 20, 18, false, false, &Items[738], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 21, 18, false, false, &Items[738], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 22, 18, false, false, &Items[738], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd37, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 23, 18, false, false, &Items[738], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e), 24, 18, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e, 0xd83c, 0xdffb), 25, 18, false, false, &Items[744], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e, 0xd83c, 0xdffc), 26, 18, false, false, &Items[744], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e, 0xd83c, 0xdffd), 27, 18, false, false, &Items[744], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e, 0xd83c, 0xdffe), 28, 18, false, false, &Items[744], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e, 0xd83c, 0xdfff), 29, 18, false, false, &Items[744], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e, 0x200d, 0x2642, 0xfe0f), 30, 18, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 31, 18, false, false, &Items[750], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 32, 18, false, false, &Items[750], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 33, 18, false, false, &Items[750], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 34, 18, false, false, &Items[750], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4e, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 35, 18, false, false, &Items[750], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d), 36, 18, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d, 0xd83c, 0xdffb), 37, 18, false, false, &Items[756], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d, 0xd83c, 0xdffc), 38, 18, false, false, &Items[756], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d, 0xd83c, 0xdffd), 39, 18, false, false, &Items[756], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d, 0xd83c, 0xdffe), 0, 19, false, false, &Items[756], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d, 0xd83c, 0xdfff), 1, 19, false, false, &Items[756], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d, 0x200d, 0x2642, 0xfe0f), 2, 19, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 3, 19, false, false, &Items[762], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 4, 19, false, false, &Items[762], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 5, 19, false, false, &Items[762], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 6, 19, false, false, &Items[762], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4d, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 7, 19, false, false, &Items[762], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87), 8, 19, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87, 0xd83c, 0xdffb), 9, 19, false, false, &Items[768], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87, 0xd83c, 0xdffc), 10, 19, false, false, &Items[768], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87, 0xd83c, 0xdffd), 11, 19, false, false, &Items[768], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87, 0xd83c, 0xdffe), 12, 19, false, false, &Items[768], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87, 0xd83c, 0xdfff), 13, 19, false, false, &Items[768], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87, 0x200d, 0x2642, 0xfe0f), 14, 19, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 15, 19, false, false, &Items[774], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 16, 19, false, false, &Items[774], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 17, 19, false, false, &Items[774], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 18, 19, false, false, &Items[774], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc87, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 19, 19, false, false, &Items[774], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86), 20, 19, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86, 0xd83c, 0xdffb), 21, 19, false, false, &Items[780], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86, 0xd83c, 0xdffc), 22, 19, false, false, &Items[780], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86, 0xd83c, 0xdffd), 23, 19, false, false, &Items[780], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86, 0xd83c, 0xdffe), 24, 19, false, false, &Items[780], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86, 0xd83c, 0xdfff), 25, 19, false, false, &Items[780], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86, 0x200d, 0x2642, 0xfe0f), 26, 19, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 27, 19, false, false, &Items[786], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 28, 19, false, false, &Items[786], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 29, 19, false, false, &Items[786], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 30, 19, false, false, &Items[786], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc86, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 31, 19, false, false, &Items[786], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd74), 32, 19, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd74, 0xd83c, 0xdffb), 33, 19, false, false, &Items[792], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd74, 0xd83c, 0xdffc), 34, 19, false, false, &Items[792], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd74, 0xd83c, 0xdffd), 35, 19, false, false, &Items[792], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd74, 0xd83c, 0xdffe), 36, 19, false, false, &Items[792], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd74, 0xd83c, 0xdfff), 37, 19, false, false, &Items[792], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc83), 38, 19, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc83, 0xd83c, 0xdffb), 39, 19, false, false, &Items[798], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc83, 0xd83c, 0xdffc), 0, 20, false, false, &Items[798], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc83, 0xd83c, 0xdffd), 1, 20, false, false, &Items[798], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc83, 0xd83c, 0xdffe), 2, 20, false, false, &Items[798], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc83, 0xd83c, 0xdfff), 3, 20, false, false, &Items[798], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd7a), 4, 20, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd7a, 0xd83c, 0xdffb), 5, 20, false, false, &Items[804], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd7a, 0xd83c, 0xdffc), 6, 20, false, false, &Items[804], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd7a, 0xd83c, 0xdffd), 7, 20, false, false, &Items[804], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd7a, 0xd83c, 0xdffe), 8, 20, false, false, &Items[804], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd7a, 0xd83c, 0xdfff), 9, 20, false, false, &Items[804], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6f), 10, 20, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6f, 0x200d, 0x2642, 0xfe0f), 11, 20, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6, 0x200d, 0x2640, 0xfe0f), 12, 20, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 13, 20, false, false, &Items[812], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 14, 20, false, false, &Items[812], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 15, 20, false, false, &Items[812], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 16, 20, false, false, &Items[812], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 17, 20, false, false, &Items[812], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6), 18, 20, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6, 0xd83c, 0xdffb), 19, 20, false, false, &Items[818], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6, 0xd83c, 0xdffc), 20, 20, false, false, &Items[818], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6, 0xd83c, 0xdffd), 21, 20, false, false, &Items[818], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6, 0xd83c, 0xdffe), 22, 20, false, false, &Items[818], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb6, 0xd83c, 0xdfff), 23, 20, false, false, &Items[818], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3, 0x200d, 0x2640, 0xfe0f), 24, 20, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 25, 20, false, false, &Items[824], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 26, 20, false, false, &Items[824], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 27, 20, false, false, &Items[824], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 28, 20, false, false, &Items[824], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 29, 20, false, false, &Items[824], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3), 30, 20, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3, 0xd83c, 0xdffb), 31, 20, false, false, &Items[830], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3, 0xd83c, 0xdffc), 32, 20, false, false, &Items[830], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3, 0xd83c, 0xdffd), 33, 20, false, false, &Items[830], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3, 0xd83c, 0xdffe), 34, 20, false, false, &Items[830], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc3, 0xd83c, 0xdfff), 35, 20, false, false, &Items[830], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6b), 36, 20, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6d), 37, 20, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6c), 38, 20, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc91), 39, 20, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0x2764, 0xfe0f, 0x200d, 0xd83d, 0xdc69), 0, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0x2764, 0xfe0f, 0x200d, 0xd83d, 0xdc68), 1, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc8f), 2, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0x2764, 0xfe0f, 0x200d, 0xd83d, 0xdc8b, 0x200d, 0xd83d, 0xdc69), 3, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0x2764, 0xfe0f, 0x200d, 0xd83d, 0xdc8b, 0x200d, 0xd83d, 0xdc68), 4, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc6a), 5, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc67), 6, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc67, 0x200d, 0xd83d, 0xdc66), 7, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc66, 0x200d, 0xd83d, 0xdc66), 8, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc67, 0x200d, 0xd83d, 0xdc67), 9, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc66), 10, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc67), 11, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc67, 0x200d, 0xd83d, 0xdc66), 12, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc66, 0x200d, 0xd83d, 0xdc66), 13, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc67, 0x200d, 0xd83d, 0xdc67), 14, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc66), 15, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc67), 16, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc67, 0x200d, 0xd83d, 0xdc66), 17, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc66, 0x200d, 0xd83d, 0xdc66), 18, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc67, 0x200d, 0xd83d, 0xdc67), 19, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc66), 20, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc67), 21, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc67, 0x200d, 0xd83d, 0xdc66), 22, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc66, 0x200d, 0xd83d, 0xdc66), 23, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdc67, 0x200d, 0xd83d, 0xdc67), 24, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc66), 25, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc67), 26, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc67, 0x200d, 0xd83d, 0xdc66), 27, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc66, 0x200d, 0xd83d, 0xdc66), 28, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc68, 0x200d, 0xd83d, 0xdc67, 0x200d, 0xd83d, 0xdc67), 29, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc5a), 30, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc55), 31, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc56), 32, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc54), 33, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc57), 34, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc59), 35, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc58), 36, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc60), 37, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc61), 38, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc62), 39, 21, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc5e), 0, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc5f), 1, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc52), 2, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfa9), 3, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf93), 4, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc51), 5, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26d1), 6, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf92), 7, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc5d), 8, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc5b), 9, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc5c), 10, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcbc), 11, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc53), 12, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd76), 13, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf02), 14, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2602), 15, 22, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc36), 16, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc31), 17, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc2d), 18, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc39), 19, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc30), 20, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd8a), 21, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc3b), 22, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc3c), 23, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc28), 24, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc2f), 25, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd81), 26, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc2e), 27, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc37), 28, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc3d), 29, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc38), 30, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc35), 31, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde48), 32, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde49), 33, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde4a), 34, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc12), 35, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc14), 36, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc27), 37, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc26), 38, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc24), 39, 22, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc23), 0, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc25), 1, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd86), 2, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd85), 3, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd89), 4, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd87), 5, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc3a), 6, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc17), 7, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc34), 8, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd84), 9, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc1d), 10, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc1b), 11, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd8b), 12, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc0c), 13, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc1a), 14, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc1e), 15, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc1c), 16, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd77), 17, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd78), 18, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc22), 19, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc0d), 20, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd8e), 21, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd82), 22, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd80), 23, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd91), 24, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc19), 25, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd90), 26, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc20), 27, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc1f), 28, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc21), 29, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc2c), 30, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd88), 31, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc33), 32, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc0b), 33, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc0a), 34, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc06), 35, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc05), 36, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc03), 37, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc02), 38, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc04), 39, 23, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd8c), 0, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc2a), 1, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc2b), 2, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc18), 3, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd8f), 4, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd8d), 5, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc0e), 6, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc16), 7, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc10), 8, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc0f), 9, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc11), 10, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc15), 11, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc29), 12, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc08), 13, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc13), 14, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd83), 15, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd4a), 16, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc07), 17, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc01), 18, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc00), 19, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc3f), 20, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc3e), 21, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc09), 22, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc32), 23, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf35), 24, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf84), 25, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf32), 26, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf33), 27, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf34), 28, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf31), 29, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf3f), 30, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2618), 31, 24, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf40), 32, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf8d), 33, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf8b), 34, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf43), 35, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf42), 36, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf41), 37, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf44), 38, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf3e), 39, 24, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc90), 0, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf37), 1, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf39), 2, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd40), 3, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf3b), 4, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf3c), 5, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf38), 6, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf3a), 7, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf0e), 8, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf0d), 9, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf0f), 10, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf15), 11, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf16), 12, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf17), 13, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf18), 14, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf11), 15, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf12), 16, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf13), 17, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf14), 18, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf1a), 19, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf1d), 20, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf1e), 21, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf1b), 22, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf1c), 23, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf19), 24, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcab), 25, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2b50), 26, 25, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf1f), 27, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2728), 28, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26a1), 29, 25, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd25), 30, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdca5), 31, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2604), 32, 25, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2600), 33, 25, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf24), 34, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26c5), 35, 25, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf25), 36, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf26), 37, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf08), 38, 25, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2601), 39, 25, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf27), 0, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26c8), 1, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf29), 2, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf28), 3, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2603), 4, 26, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26c4), 5, 26, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2744), 6, 26, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf2c), 7, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdca8), 8, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf2a), 9, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf2b), 10, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf0a), 11, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdca7), 12, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdca6), 13, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2614), 14, 26, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf4f), 15, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf4e), 16, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf50), 17, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf4a), 18, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf4b), 19, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf4c), 20, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf49), 21, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf47), 22, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf53), 23, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf48), 24, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf52), 25, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf51), 26, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf4d), 27, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd5d), 28, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd51), 29, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf45), 30, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf46), 31, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd52), 32, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd55), 33, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf3d), 34, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf36), 35, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd54), 36, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf60), 37, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf30), 38, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd5c), 39, 26, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf6f), 0, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd50), 1, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf5e), 2, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd56), 3, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xddc0), 4, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd5a), 5, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf73), 6, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd53), 7, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd5e), 8, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf64), 9, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf57), 10, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf56), 11, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf55), 12, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf2d), 13, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf54), 14, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf5f), 15, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd59), 16, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf2e), 17, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf2f), 18, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd57), 19, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd58), 20, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf5d), 21, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf5c), 22, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf72), 23, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf65), 24, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf63), 25, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf71), 26, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf5b), 27, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf59), 28, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf5a), 29, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf58), 30, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf62), 31, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf61), 32, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf67), 33, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf68), 34, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf66), 35, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf70), 36, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf82), 37, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf6e), 38, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf6d), 39, 27, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf6c), 0, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf6b), 1, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf7f), 2, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf69), 3, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf6a), 4, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd5b), 5, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf7c), 6, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2615), 7, 28, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf75), 8, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf76), 9, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf7a), 10, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf7b), 11, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd42), 12, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf77), 13, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd43), 14, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf78), 15, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf79), 16, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf7e), 17, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd44), 18, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf74), 19, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf7d), 20, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26bd), 21, 28, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc0), 22, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc8), 23, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26be), 24, 28, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfbe), 25, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfd0), 26, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc9), 27, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfb1), 28, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfd3), 29, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdff8), 30, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd45), 31, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfd2), 32, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfd1), 33, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcf), 34, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26f3), 35, 28, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdff9), 36, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfa3), 37, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd4a), 38, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd4b), 39, 28, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26f8), 0, 29, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfbf), 1, 29, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26f7), 2, 29, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc2), 3, 29, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 4, 29, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb, 0xd83c, 0xdffb, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 5, 29, false, false, &Items[1164], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb, 0xd83c, 0xdffc, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 6, 29, false, false, &Items[1164], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb, 0xd83c, 0xdffd, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 7, 29, false, false, &Items[1164], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb, 0xd83c, 0xdffe, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 8, 29, false, false, &Items[1164], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb, 0xd83c, 0xdfff, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 9, 29, false, false, &Items[1164], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb), 10, 29, true, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb, 0xd83c, 0xdffb), 11, 29, false, false, &Items[1170], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb, 0xd83c, 0xdffc), 12, 29, false, false, &Items[1170], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb, 0xd83c, 0xdffd), 13, 29, false, false, &Items[1170], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb, 0xd83c, 0xdffe), 14, 29, false, false, &Items[1170], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcb, 0xd83c, 0xdfff), 15, 29, false, false, &Items[1170], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3a), 16, 29, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3c, 0x200d, 0x2640, 0xfe0f), 17, 29, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3c, 0x200d, 0x2642, 0xfe0f), 18, 29, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0x200d, 0x2640, 0xfe0f), 19, 29, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 20, 29, false, false, &Items[1179], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 21, 29, false, false, &Items[1179], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 22, 29, false, false, &Items[1179], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 23, 29, false, false, &Items[1179], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 24, 29, false, false, &Items[1179], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0x200d, 0x2642, 0xfe0f), 25, 29, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 26, 29, false, false, &Items[1185], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 27, 29, false, false, &Items[1185], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 28, 29, false, false, &Items[1185], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 29, 29, false, false, &Items[1185], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd38, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 30, 29, false, false, &Items[1185], tag);
	Items.emplace_back(internal::ComputeId(0x26f9, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 31, 29, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26f9, 0xd83c, 0xdffb, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 32, 29, false, false, &Items[1191], tag);
	Items.emplace_back(internal::ComputeId(0x26f9, 0xd83c, 0xdffc, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 33, 29, false, false, &Items[1191], tag);
	Items.emplace_back(internal::ComputeId(0x26f9, 0xd83c, 0xdffd, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 34, 29, false, false, &Items[1191], tag);
	Items.emplace_back(internal::ComputeId(0x26f9, 0xd83c, 0xdffe, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 35, 29, false, false, &Items[1191], tag);
	Items.emplace_back(internal::ComputeId(0x26f9, 0xd83c, 0xdfff, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 36, 29, false, false, &Items[1191], tag);
	Items.emplace_back(internal::ComputeId(0x26f9), 37, 29, true, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26f9, 0xd83c, 0xdffb), 38, 29, false, false, &Items[1197], tag);
	Items.emplace_back(internal::ComputeId(0x26f9, 0xd83c, 0xdffc), 39, 29, false, false, &Items[1197], tag);
	Items.emplace_back(internal::ComputeId(0x26f9, 0xd83c, 0xdffd), 0, 30, false, false, &Items[1197], tag);
	Items.emplace_back(internal::ComputeId(0x26f9, 0xd83c, 0xdffe), 1, 30, false, false, &Items[1197], tag);
	Items.emplace_back(internal::ComputeId(0x26f9, 0xd83c, 0xdfff), 2, 30, false, false, &Items[1197], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0x200d, 0x2640, 0xfe0f), 3, 30, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 4, 30, false, false, &Items[1203], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 5, 30, false, false, &Items[1203], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 6, 30, false, false, &Items[1203], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 7, 30, false, false, &Items[1203], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 8, 30, false, false, &Items[1203], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0x200d, 0x2642, 0xfe0f), 9, 30, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 10, 30, false, false, &Items[1209], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 11, 30, false, false, &Items[1209], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 12, 30, false, false, &Items[1209], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 13, 30, false, false, &Items[1209], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3e, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 14, 30, false, false, &Items[1209], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 15, 30, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc, 0xd83c, 0xdffb, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 16, 30, false, false, &Items[1215], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc, 0xd83c, 0xdffc, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 17, 30, false, false, &Items[1215], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc, 0xd83c, 0xdffd, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 18, 30, false, false, &Items[1215], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc, 0xd83c, 0xdffe, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 19, 30, false, false, &Items[1215], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc, 0xd83c, 0xdfff, 0xfe0f, 0x200d, 0x2640, 0xfe0f), 20, 30, false, false, &Items[1215], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc), 21, 30, true, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc, 0xd83c, 0xdffb), 22, 30, false, false, &Items[1221], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc, 0xd83c, 0xdffc), 23, 30, false, false, &Items[1221], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc, 0xd83c, 0xdffd), 24, 30, false, false, &Items[1221], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc, 0xd83c, 0xdffe), 25, 30, false, false, &Items[1221], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcc, 0xd83c, 0xdfff), 26, 30, false, false, &Items[1221], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4, 0x200d, 0x2640, 0xfe0f), 27, 30, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 28, 30, false, false, &Items[1227], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 29, 30, false, false, &Items[1227], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 30, 30, false, false, &Items[1227], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 31, 30, false, false, &Items[1227], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 32, 30, false, false, &Items[1227], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4), 33, 30, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4, 0xd83c, 0xdffb), 34, 30, false, false, &Items[1233], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4, 0xd83c, 0xdffc), 35, 30, false, false, &Items[1233], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4, 0xd83c, 0xdffd), 36, 30, false, false, &Items[1233], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4, 0xd83c, 0xdffe), 37, 30, false, false, &Items[1233], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc4, 0xd83c, 0xdfff), 38, 30, false, false, &Items[1233], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca, 0x200d, 0x2640, 0xfe0f), 39, 30, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 0, 31, false, false, &Items[1239], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 1, 31, false, false, &Items[1239], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 2, 31, false, false, &Items[1239], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 3, 31, false, false, &Items[1239], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 4, 31, false, false, &Items[1239], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca), 5, 31, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca, 0xd83c, 0xdffb), 6, 31, false, false, &Items[1245], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca, 0xd83c, 0xdffc), 7, 31, false, false, &Items[1245], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca, 0xd83c, 0xdffd), 8, 31, false, false, &Items[1245], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca, 0xd83c, 0xdffe), 9, 31, false, false, &Items[1245], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfca, 0xd83c, 0xdfff), 10, 31, false, false, &Items[1245], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0x200d, 0x2640, 0xfe0f), 11, 31, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 12, 31, false, false, &Items[1251], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 13, 31, false, false, &Items[1251], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 14, 31, false, false, &Items[1251], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 15, 31, false, false, &Items[1251], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 16, 31, false, false, &Items[1251], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0x200d, 0x2642, 0xfe0f), 17, 31, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 18, 31, false, false, &Items[1257], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 19, 31, false, false, &Items[1257], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 20, 31, false, false, &Items[1257], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 21, 31, false, false, &Items[1257], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd3d, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 22, 31, false, false, &Items[1257], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3, 0x200d, 0x2640, 0xfe0f), 23, 31, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 24, 31, false, false, &Items[1263], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 25, 31, false, false, &Items[1263], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 26, 31, false, false, &Items[1263], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 27, 31, false, false, &Items[1263], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 28, 31, false, false, &Items[1263], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3), 29, 31, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3, 0xd83c, 0xdffb), 30, 31, false, false, &Items[1269], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3, 0xd83c, 0xdffc), 31, 31, false, false, &Items[1269], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3, 0xd83c, 0xdffd), 32, 31, false, false, &Items[1269], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3, 0xd83c, 0xdffe), 33, 31, false, false, &Items[1269], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea3, 0xd83c, 0xdfff), 34, 31, false, false, &Items[1269], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc7), 35, 31, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc7, 0xd83c, 0xdffb), 36, 31, false, false, &Items[1275], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc7, 0xd83c, 0xdffc), 37, 31, false, false, &Items[1275], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc7, 0xd83c, 0xdffd), 38, 31, false, false, &Items[1275], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc7, 0xd83c, 0xdffe), 39, 31, false, false, &Items[1275], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc7, 0xd83c, 0xdfff), 0, 32, false, false, &Items[1275], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4, 0x200d, 0x2640, 0xfe0f), 1, 32, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 2, 32, false, false, &Items[1281], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 3, 32, false, false, &Items[1281], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 4, 32, false, false, &Items[1281], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 5, 32, false, false, &Items[1281], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 6, 32, false, false, &Items[1281], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4), 7, 32, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4, 0xd83c, 0xdffb), 8, 32, false, false, &Items[1287], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4, 0xd83c, 0xdffc), 9, 32, false, false, &Items[1287], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4, 0xd83c, 0xdffd), 10, 32, false, false, &Items[1287], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4, 0xd83c, 0xdffe), 11, 32, false, false, &Items[1287], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb4, 0xd83c, 0xdfff), 12, 32, false, false, &Items[1287], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5, 0x200d, 0x2640, 0xfe0f), 13, 32, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 14, 32, false, false, &Items[1293], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 15, 32, false, false, &Items[1293], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 16, 32, false, false, &Items[1293], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 17, 32, false, false, &Items[1293], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 18, 32, false, false, &Items[1293], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5), 19, 32, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5, 0xd83c, 0xdffb), 20, 32, false, false, &Items[1299], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5, 0xd83c, 0xdffc), 21, 32, false, false, &Items[1299], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5, 0xd83c, 0xdffd), 22, 32, false, false, &Items[1299], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5, 0xd83c, 0xdffe), 23, 32, false, false, &Items[1299], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb5, 0xd83c, 0xdfff), 24, 32, false, false, &Items[1299], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfbd), 25, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc5), 26, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf96), 27, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd47), 28, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd48), 29, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd49), 30, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc6), 31, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdff5), 32, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf97), 33, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfab), 34, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf9f), 35, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfaa), 36, 32, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0x200d, 0x2640, 0xfe0f), 37, 32, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0xd83c, 0xdffb, 0x200d, 0x2640, 0xfe0f), 38, 32, false, false, &Items[1317], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0xd83c, 0xdffc, 0x200d, 0x2640, 0xfe0f), 39, 32, false, false, &Items[1317], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0xd83c, 0xdffd, 0x200d, 0x2640, 0xfe0f), 0, 33, false, false, &Items[1317], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0xd83c, 0xdffe, 0x200d, 0x2640, 0xfe0f), 1, 33, false, false, &Items[1317], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0xd83c, 0xdfff, 0x200d, 0x2640, 0xfe0f), 2, 33, false, false, &Items[1317], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0x200d, 0x2642, 0xfe0f), 3, 33, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0xd83c, 0xdffb, 0x200d, 0x2642, 0xfe0f), 4, 33, false, false, &Items[1323], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0xd83c, 0xdffc, 0x200d, 0x2642, 0xfe0f), 5, 33, false, false, &Items[1323], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0xd83c, 0xdffd, 0x200d, 0x2642, 0xfe0f), 6, 33, false, false, &Items[1323], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0xd83c, 0xdffe, 0x200d, 0x2642, 0xfe0f), 7, 33, false, false, &Items[1323], tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd39, 0xd83c, 0xdfff, 0x200d, 0x2642, 0xfe0f), 8, 33, false, false, &Items[1323], tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfad), 9, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfa8), 10, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfac), 11, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfa4), 12, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfa7), 13, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfbc), 14, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfb9), 15, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83e, 0xdd41), 16, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfb7), 17, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfba), 18, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfb8), 19, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfbb), 20, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfb2), 21, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfaf), 22, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfb3), 23, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfae), 24, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfb0), 25, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde97), 26, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde95), 27, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde99), 28, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde8c), 29, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde8e), 30, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfce), 31, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde93), 32, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde91), 33, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde92), 34, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde90), 35, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde9a), 36, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde9b), 37, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde9c), 38, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdef4), 39, 33, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb2), 0, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdef5), 1, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfcd), 2, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea8), 3, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde94), 4, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde8d), 5, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde98), 6, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde96), 7, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea1), 8, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea0), 9, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde9f), 10, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde83), 11, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde8b), 12, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde9e), 13, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde9d), 14, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde84), 15, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde85), 16, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde88), 17, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde82), 18, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde86), 19, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde87), 20, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde8a), 21, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde89), 22, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde81), 23, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdee9), 24, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2708), 25, 34, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeeb), 26, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeec), 27, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde80), 28, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdef0), 29, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcba), 30, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdef6), 31, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26f5), 32, 34, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdee5), 33, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea4), 34, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdef3), 35, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26f4), 36, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea2), 37, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2693), 38, 34, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea7), 39, 34, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26fd), 0, 35, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xde8f), 1, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea6), 2, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea5), 3, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddfa), 4, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddff), 5, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddfd), 6, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26f2), 7, 35, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddfc), 8, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdff0), 9, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfef), 10, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfdf), 11, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfa1), 12, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfa2), 13, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfa0), 14, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26f1), 15, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfd6), 16, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfdd), 17, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26f0), 18, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfd4), 19, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddfb), 20, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf0b), 21, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfdc), 22, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfd5), 23, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26fa), 24, 35, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdee4), 25, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdee3), 26, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfd7), 27, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfed), 28, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfe0), 29, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfe1), 30, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfd8), 31, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfda), 32, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfe2), 33, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfec), 34, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfe3), 35, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfe4), 36, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfe5), 37, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfe6), 38, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfe8), 39, 35, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfea), 0, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfeb), 1, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfe9), 2, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc92), 3, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfdb), 4, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26ea), 5, 36, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd4c), 6, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd4d), 7, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd4b), 8, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26e9), 9, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddfe), 10, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf91), 11, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfde), 12, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf05), 13, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf04), 14, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf20), 15, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf87), 16, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf86), 17, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf07), 18, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf06), 19, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfd9), 20, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf03), 21, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf0c), 22, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf09), 23, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf01), 24, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x231a), 25, 36, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcf1), 26, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcf2), 27, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcbb), 28, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2328), 29, 36, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdda5), 30, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdda8), 31, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddb1), 32, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddb2), 33, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd79), 34, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdddc), 35, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcbd), 36, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcbe), 37, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcbf), 38, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcc0), 39, 36, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcfc), 0, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcf7), 1, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcf8), 2, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcf9), 3, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfa5), 4, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcfd), 5, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf9e), 6, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcde), 7, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x260e), 8, 37, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcdf), 9, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdce0), 10, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcfa), 11, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcfb), 12, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf99), 13, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf9a), 14, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf9b), 15, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23f1), 16, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23f2), 17, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23f0), 18, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd70), 19, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x231b), 20, 37, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23f3), 21, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdce1), 22, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd0b), 23, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd0c), 24, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdca1), 25, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd26), 26, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd6f), 27, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddd1), 28, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdee2), 29, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcb8), 30, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcb5), 31, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcb4), 32, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcb6), 33, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcb7), 34, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcb0), 35, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcb3), 36, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc8e), 37, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2696), 38, 37, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd27), 39, 37, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd28), 0, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2692), 1, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdee0), 2, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26cf), 3, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd29), 4, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2699), 5, 38, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26d3), 6, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd2b), 7, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdca3), 8, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd2a), 9, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdde1), 10, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2694), 11, 38, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdee1), 12, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeac), 13, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26b0), 14, 38, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26b1), 15, 38, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdffa), 16, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd2e), 17, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcff), 18, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc88), 19, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2697), 20, 38, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd2d), 21, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd2c), 22, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd73), 23, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc8a), 24, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc89), 25, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf21), 26, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdebd), 27, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb0), 28, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdebf), 29, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdec1), 30, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdec0), 31, 38, false, true, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdec0, 0xd83c, 0xdffb), 32, 38, false, false, &Items[1551], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdec0, 0xd83c, 0xdffc), 33, 38, false, false, &Items[1551], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdec0, 0xd83c, 0xdffd), 34, 38, false, false, &Items[1551], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdec0, 0xd83c, 0xdffe), 35, 38, false, false, &Items[1551], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdec0, 0xd83c, 0xdfff), 36, 38, false, false, &Items[1551], tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdece), 37, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd11), 38, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdddd), 39, 38, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeaa), 0, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdecb), 1, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdecf), 2, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdecc), 3, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddbc), 4, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdecd), 5, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xded2), 6, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf81), 7, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf88), 8, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf8f), 9, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf80), 10, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf8a), 11, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf89), 12, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf8e), 13, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfee), 14, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf90), 15, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2709), 16, 39, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdce9), 17, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdce8), 18, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdce7), 19, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc8c), 20, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdce5), 21, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdce4), 22, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdce6), 23, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdff7), 24, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcea), 25, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdceb), 26, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcec), 27, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdced), 28, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcee), 29, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcef), 30, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcdc), 31, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcc3), 32, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcc4), 33, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcd1), 34, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcca), 35, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcc8), 36, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcc9), 37, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddd2), 38, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddd3), 39, 39, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcc6), 0, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcc5), 1, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcc7), 2, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddc3), 3, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddf3), 4, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddc4), 5, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdccb), 6, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcc1), 7, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcc2), 8, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddc2), 9, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddde), 10, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcf0), 11, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcd3), 12, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcd4), 13, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcd2), 14, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcd5), 15, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcd7), 16, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcd8), 17, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcd9), 18, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcda), 19, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcd6), 20, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd16), 21, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd17), 22, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcce), 23, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd87), 24, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcd0), 25, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdccf), 26, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdccc), 27, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdccd), 28, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2702), 29, 40, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd8a), 30, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd8b), 31, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2712), 32, 40, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd8c), 33, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd8d), 34, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcdd), 35, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x270f), 36, 40, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd0d), 37, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd0e), 38, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd0f), 39, 40, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd10), 0, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd12), 1, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd13), 2, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2764), 3, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc9b), 4, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc9a), 5, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc99), 6, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc9c), 7, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdda4), 8, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc94), 9, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2763), 10, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc95), 11, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc9e), 12, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc93), 13, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc97), 14, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc96), 15, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc98), 16, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc9d), 17, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc9f), 18, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x262e), 19, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x271d), 20, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x262a), 21, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd49), 22, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2638), 23, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2721), 24, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd2f), 25, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd4e), 26, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x262f), 27, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2626), 28, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xded0), 29, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26ce), 30, 41, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2648), 31, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2649), 32, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x264a), 33, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x264b), 34, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x264c), 35, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x264d), 36, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x264e), 37, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x264f), 38, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2650), 39, 41, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2651), 0, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2652), 1, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2653), 2, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd94), 3, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x269b), 4, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde51), 5, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2622), 6, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2623), 7, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcf4), 8, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcf3), 9, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde36), 10, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde1a), 11, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde38), 12, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde3a), 13, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde37), 14, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2734), 15, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd9a), 16, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcae), 17, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde50), 18, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x3299), 19, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x3297), 20, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde34), 21, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde35), 22, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde39), 23, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde32), 24, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd70), 25, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd71), 26, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd8e), 27, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd91), 28, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd7e), 29, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd98), 30, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x274c), 31, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2b55), 32, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xded1), 33, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26d4), 34, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcdb), 35, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeab), 36, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcaf), 37, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdca2), 38, 42, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2668), 39, 42, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb7), 0, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeaf), 1, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb3), 2, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb1), 3, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd1e), 4, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcf5), 5, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdead), 6, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2757), 7, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2755), 8, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2753), 9, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2754), 10, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x203c), 11, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2049), 12, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd05), 13, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd06), 14, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x303d), 15, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26a0), 16, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb8), 17, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd31), 18, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x269c), 19, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd30), 20, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x267b), 21, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2705), 22, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde2f), 23, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcb9), 24, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2747), 25, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2733), 26, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x274e), 27, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf10), 28, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdca0), 29, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x24c2), 30, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf00), 31, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdca4), 32, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfe7), 33, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdebe), 34, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x267f), 35, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd7f), 36, 43, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde33), 37, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde02), 38, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdec2), 39, 43, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdec3), 0, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdec4), 1, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdec5), 2, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeb9), 3, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeba), 4, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdebc), 5, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdebb), 6, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdeae), 7, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfa6), 8, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcf6), 9, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xde01), 10, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd23), 11, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2139), 12, 44, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd24), 13, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd21), 14, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd20), 15, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd96), 16, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd97), 17, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd99), 18, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd92), 19, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd95), 20, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdd93), 21, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x30, 0xfe0f, 0x20e3), 22, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x31, 0xfe0f, 0x20e3), 23, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x32, 0xfe0f, 0x20e3), 24, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x33, 0xfe0f, 0x20e3), 25, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x34, 0xfe0f, 0x20e3), 26, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x35, 0xfe0f, 0x20e3), 27, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x36, 0xfe0f, 0x20e3), 28, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x37, 0xfe0f, 0x20e3), 29, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x38, 0xfe0f, 0x20e3), 30, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x39, 0xfe0f, 0x20e3), 31, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd1f), 32, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd22), 33, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23, 0xfe0f, 0x20e3), 34, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2a, 0xfe0f, 0x20e3), 35, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x25b6), 36, 44, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23f8), 37, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23ef), 38, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23f9), 39, 44, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23fa), 0, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23ed), 1, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23ee), 2, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23e9), 3, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23ea), 4, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23eb), 5, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x23ec), 6, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x25c0), 7, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd3c), 8, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd3d), 9, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x27a1), 10, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2b05), 11, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2b06), 12, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2b07), 13, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2197), 14, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2198), 15, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2199), 16, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2196), 17, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2195), 18, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2194), 19, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x21aa), 20, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x21a9), 21, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2934), 22, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2935), 23, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd00), 24, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd01), 25, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd02), 26, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd04), 27, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd03), 28, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfb5), 29, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfb6), 30, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2795), 31, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2796), 32, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2797), 33, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2716), 34, 45, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcb2), 35, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcb1), 36, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2122), 37, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xa9), 38, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xae), 39, 45, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x3030), 0, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x27b0), 1, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x27bf), 2, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd1a), 3, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd19), 4, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd1b), 5, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd1d), 6, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd1c), 7, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2714), 8, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2611), 9, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd18), 10, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26aa), 11, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x26ab), 12, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd34), 13, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd35), 14, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd3a), 15, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd3b), 16, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd38), 17, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd39), 18, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd36), 19, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd37), 20, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd33), 21, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd32), 22, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x25aa), 23, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x25ab), 24, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x25fe), 25, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x25fd), 26, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x25fc), 27, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x25fb), 28, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2b1b), 29, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2b1c), 30, 46, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd08), 31, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd07), 32, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd09), 33, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd0a), 34, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd14), 35, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd15), 36, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdce3), 37, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdce2), 38, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdc41, 0x200d, 0xd83d, 0xdde8), 39, 46, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcac), 0, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdcad), 1, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xddef), 2, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2660), 3, 47, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2663), 4, 47, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2665), 5, 47, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0x2666), 6, 47, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdccf), 7, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfb4), 8, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdc04), 9, 47, true, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd50), 10, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd51), 11, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd52), 12, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd53), 13, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd54), 14, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd55), 15, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd56), 16, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd57), 17, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd58), 18, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd59), 19, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd5a), 20, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd5b), 21, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd5c), 22, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd5d), 23, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd5e), 24, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd5f), 25, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd60), 26, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd61), 27, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd62), 28, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd63), 29, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd64), 30, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd65), 31, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd66), 32, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdd67), 33, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdff3), 34, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdff4), 35, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdfc1), 36, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83d, 0xdea9), 37, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdff3, 0xfe0f, 0x200d, 0xd83c, 0xdf08), 38, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddeb), 39, 47, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddfd), 0, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddf1), 1, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde9, 0xd83c, 0xddff), 2, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddf8), 3, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xdde9), 4, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddf4), 5, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddee), 6, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddf6), 7, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddec), 8, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddf7), 9, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddf2), 10, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddfc), 11, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddfa), 12, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddf9), 13, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddff), 14, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddf8), 15, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xdded), 16, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xdde9), 17, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xdde7), 18, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddfe), 19, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddea), 20, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddff), 21, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddef), 22, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddf2), 23, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddf9), 24, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddf4), 25, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xdde6), 26, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddfc), 27, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddf7), 28, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddee, 0xd83c, 0xddf4), 29, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfb, 0xd83c, 0xddec), 30, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddf3), 31, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddec), 32, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddeb), 33, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddee), 34, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf0, 0xd83c, 0xdded), 35, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddf2), 36, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xdde6), 37, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddee, 0xd83c, 0xdde8), 38, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddfb), 39, 48, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddf6), 0, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf0, 0xd83c, 0xddfe), 1, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddeb), 2, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xdde9), 3, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddf1), 4, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddf3), 5, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddfd), 6, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xdde8), 7, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddf4), 8, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf0, 0xd83c, 0xddf2), 9, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddec), 10, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xdde9), 11, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddf0), 12, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddf7), 13, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddee), 14, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdded, 0xd83c, 0xddf7), 15, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddfa), 16, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddfc), 17, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddfe), 18, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xddff), 19, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde9, 0xd83c, 0xddf0), 20, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde9, 0xd83c, 0xddef), 21, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde9, 0xd83c, 0xddf2), 22, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde9, 0xd83c, 0xddf4), 23, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddea, 0xd83c, 0xdde8), 24, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddea, 0xd83c, 0xddec), 25, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddfb), 26, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddf6), 27, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddea, 0xd83c, 0xddf7), 28, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddea, 0xd83c, 0xddea), 29, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddea, 0xd83c, 0xddf9), 30, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddea, 0xd83c, 0xddfa), 31, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddeb, 0xd83c, 0xddf0), 32, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddeb, 0xd83c, 0xddf4), 33, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddeb, 0xd83c, 0xddef), 34, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddeb, 0xd83c, 0xddee), 35, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddeb, 0xd83c, 0xddf7), 36, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddeb), 37, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddeb), 38, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddeb), 39, 49, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xdde6), 0, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddf2), 1, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddea), 2, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde9, 0xd83c, 0xddea), 3, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xdded), 4, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddee), 5, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddf7), 6, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddf1), 7, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xdde9), 8, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddf5), 9, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddfa), 10, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddf9), 11, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddec), 12, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddf3), 13, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddfc), 14, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddfe), 15, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdded, 0xd83c, 0xddf9), 16, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdded, 0xd83c, 0xddf3), 17, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdded, 0xd83c, 0xddf0), 18, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdded, 0xd83c, 0xddfa), 19, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddee, 0xd83c, 0xddf8), 20, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddee, 0xd83c, 0xddf3), 21, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddee, 0xd83c, 0xdde9), 22, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddee, 0xd83c, 0xddf7), 23, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddee, 0xd83c, 0xddf6), 24, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddee, 0xd83c, 0xddea), 25, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddee, 0xd83c, 0xddf2), 26, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddee, 0xd83c, 0xddf1), 27, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddee, 0xd83c, 0xddf9), 28, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddef, 0xd83c, 0xddf2), 29, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddef, 0xd83c, 0xddf5), 30, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdf8c), 31, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddef, 0xd83c, 0xddea), 32, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddef, 0xd83c, 0xddf4), 33, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf0, 0xd83c, 0xddff), 34, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf0, 0xd83c, 0xddea), 35, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf0, 0xd83c, 0xddee), 36, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfd, 0xd83c, 0xddf0), 37, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf0, 0xd83c, 0xddfc), 38, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf0, 0xd83c, 0xddec), 39, 50, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf1, 0xd83c, 0xdde6), 0, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf1, 0xd83c, 0xddfb), 1, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf1, 0xd83c, 0xdde7), 2, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf1, 0xd83c, 0xddf8), 3, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf1, 0xd83c, 0xddf7), 4, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf1, 0xd83c, 0xddfe), 5, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf1, 0xd83c, 0xddee), 6, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf1, 0xd83c, 0xddf9), 7, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf1, 0xd83c, 0xddfa), 8, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddf4), 9, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddf0), 10, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddec), 11, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddfc), 12, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddfe), 13, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddfb), 14, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddf1), 15, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddf9), 16, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xdded), 17, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddf6), 18, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddf7), 19, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddfa), 20, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfe, 0xd83c, 0xddf9), 21, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddfd), 22, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddeb, 0xd83c, 0xddf2), 23, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xdde9), 24, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xdde8), 25, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddf3), 26, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddea), 27, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddf8), 28, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xdde6), 29, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddff), 30, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddf2), 31, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xdde6), 32, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xddf7), 33, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xddf5), 34, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xddf1), 35, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xdde8), 36, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xddff), 37, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xddee), 38, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xddea), 39, 51, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xddec), 0, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xddfa), 1, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xddeb), 2, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf0, 0xd83c, 0xddf5), 3, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf2, 0xd83c, 0xddf5), 4, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf3, 0xd83c, 0xddf4), 5, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf4, 0xd83c, 0xddf2), 6, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddf0), 7, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddfc), 8, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddf8), 9, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xdde6), 10, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddec), 11, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddfe), 12, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddea), 13, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xdded), 14, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddf3), 15, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddf1), 16, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddf9), 17, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddf7), 18, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf6, 0xd83c, 0xdde6), 19, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf7, 0xd83c, 0xddea), 20, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf7, 0xd83c, 0xddf4), 21, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf7, 0xd83c, 0xddfa), 22, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf7, 0xd83c, 0xddfc), 23, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfc, 0xd83c, 0xddf8), 24, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddf2), 25, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddf9), 26, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xdde6), 27, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddf3), 28, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf7, 0xd83c, 0xddf8), 29, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xdde8), 30, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddf1), 31, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddec), 32, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddfd), 33, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddf0), 34, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddee), 35, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xddf8), 36, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xdde7), 37, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddf4), 38, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddff, 0xd83c, 0xdde6), 39, 52, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf0, 0xd83c, 0xddf7), 0, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddf8), 1, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddea, 0xd83c, 0xddf8), 2, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf1, 0xd83c, 0xddf0), 3, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde7, 0xd83c, 0xddf1), 4, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xdded), 5, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf0, 0xd83c, 0xddf3), 6, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf1, 0xd83c, 0xdde8), 7, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf5, 0xd83c, 0xddf2), 8, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfb, 0xd83c, 0xdde8), 9, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xdde9), 10, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddf7), 11, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddff), 12, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddea), 13, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde8, 0xd83c, 0xdded), 14, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf8, 0xd83c, 0xddfe), 15, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddfc), 16, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddef), 17, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddff), 18, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xdded), 19, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddf1), 20, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddec), 21, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddf0), 22, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddf4), 23, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddf9), 24, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddf3), 25, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddf7), 26, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddf2), 27, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xdde8), 28, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddf9, 0xd83c, 0xddfb), 29, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfb, 0xd83c, 0xddee), 30, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfa, 0xd83c, 0xddec), 31, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfa, 0xd83c, 0xdde6), 32, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xdde6, 0xd83c, 0xddea), 33, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddec, 0xd83c, 0xdde7), 34, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfa, 0xd83c, 0xddf8), 35, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfa, 0xd83c, 0xddfe), 36, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfa, 0xd83c, 0xddff), 37, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfb, 0xd83c, 0xddfa), 38, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfb, 0xd83c, 0xdde6), 39, 53, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfb, 0xd83c, 0xddea), 0, 54, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfb, 0xd83c, 0xddf3), 1, 54, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfc, 0xd83c, 0xddeb), 2, 54, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddea, 0xd83c, 0xdded), 3, 54, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddfe, 0xd83c, 0xddea), 4, 54, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddff, 0xd83c, 0xddf2), 5, 54, false, false, nullptr, tag);
	Items.emplace_back(internal::ComputeId(0xd83c, 0xddff, 0xd83c, 0xddfc), 6, 54, false, false, nullptr, tag);
}

int GetSectionCount(Section section) {
	switch (section) {
	case Section::Recent: return GetRecent().size();
	case Section::People: return 291;
	case Section::Nature: return 159;
	case Section::Food: return 86;
	case Section::Activity: return 80;
	case Section::Travel: return 119;
	case Section::Objects: return 173;
	case Section::Symbols: return 524;
	}
	return 0;
}

EmojiPack GetSection(Section section) {
	switch (section) {
	case Section::Recent: {
		auto result = EmojiPack();
		result.reserve(GetRecent().size());
		for (auto &item : GetRecent()) {
			result.push_back(item.first);
		}
		return result;
	} break;

	case Section::People: {
		static auto result = EmojiPack();
		if (result.isEmpty()) {
			result.reserve(291);
			result.push_back(&Items[0]);
			result.push_back(&Items[1]);
			result.push_back(&Items[2]);
			result.push_back(&Items[3]);
			result.push_back(&Items[4]);
			result.push_back(&Items[5]);
			result.push_back(&Items[6]);
			result.push_back(&Items[7]);
			result.push_back(&Items[8]);
			result.push_back(&Items[9]);
			result.push_back(&Items[10]);
			result.push_back(&Items[11]);
			result.push_back(&Items[12]);
			result.push_back(&Items[13]);
			result.push_back(&Items[14]);
			result.push_back(&Items[15]);
			result.push_back(&Items[16]);
			result.push_back(&Items[17]);
			result.push_back(&Items[18]);
			result.push_back(&Items[19]);
			result.push_back(&Items[20]);
			result.push_back(&Items[21]);
			result.push_back(&Items[22]);
			result.push_back(&Items[23]);
			result.push_back(&Items[24]);
			result.push_back(&Items[25]);
			result.push_back(&Items[26]);
			result.push_back(&Items[27]);
			result.push_back(&Items[28]);
			result.push_back(&Items[29]);
			result.push_back(&Items[30]);
			result.push_back(&Items[31]);
			result.push_back(&Items[32]);
			result.push_back(&Items[33]);
			result.push_back(&Items[34]);
			result.push_back(&Items[35]);
			result.push_back(&Items[36]);
			result.push_back(&Items[37]);
			result.push_back(&Items[38]);
			result.push_back(&Items[39]);
			result.push_back(&Items[40]);
			result.push_back(&Items[41]);
			result.push_back(&Items[42]);
			result.push_back(&Items[43]);
			result.push_back(&Items[44]);
			result.push_back(&Items[45]);
			result.push_back(&Items[46]);
			result.push_back(&Items[47]);
			result.push_back(&Items[48]);
			result.push_back(&Items[49]);
			result.push_back(&Items[50]);
			result.push_back(&Items[51]);
			result.push_back(&Items[52]);
			result.push_back(&Items[53]);
			result.push_back(&Items[54]);
			result.push_back(&Items[55]);
			result.push_back(&Items[56]);
			result.push_back(&Items[57]);
			result.push_back(&Items[58]);
			result.push_back(&Items[59]);
			result.push_back(&Items[60]);
			result.push_back(&Items[61]);
			result.push_back(&Items[62]);
			result.push_back(&Items[63]);
			result.push_back(&Items[64]);
			result.push_back(&Items[65]);
			result.push_back(&Items[66]);
			result.push_back(&Items[67]);
			result.push_back(&Items[68]);
			result.push_back(&Items[69]);
			result.push_back(&Items[70]);
			result.push_back(&Items[71]);
			result.push_back(&Items[72]);
			result.push_back(&Items[73]);
			result.push_back(&Items[74]);
			result.push_back(&Items[75]);
			result.push_back(&Items[76]);
			result.push_back(&Items[77]);
			result.push_back(&Items[78]);
			result.push_back(&Items[79]);
			result.push_back(&Items[80]);
			result.push_back(&Items[81]);
			result.push_back(&Items[82]);
			result.push_back(&Items[83]);
			result.push_back(&Items[84]);
			result.push_back(&Items[85]);
			result.push_back(&Items[86]);
			result.push_back(&Items[87]);
			result.push_back(&Items[88]);
			result.push_back(&Items[89]);
			result.push_back(&Items[90]);
			result.push_back(&Items[91]);
			result.push_back(&Items[92]);
			result.push_back(&Items[93]);
			result.push_back(&Items[94]);
			result.push_back(&Items[95]);
			result.push_back(&Items[96]);
			result.push_back(&Items[102]);
			result.push_back(&Items[108]);
			result.push_back(&Items[114]);
			result.push_back(&Items[120]);
			result.push_back(&Items[121]);
			result.push_back(&Items[127]);
			result.push_back(&Items[133]);
			result.push_back(&Items[139]);
			result.push_back(&Items[145]);
			result.push_back(&Items[151]);
			result.push_back(&Items[157]);
			result.push_back(&Items[163]);
			result.push_back(&Items[169]);
			result.push_back(&Items[175]);
			result.push_back(&Items[181]);
			result.push_back(&Items[187]);
			result.push_back(&Items[193]);
			result.push_back(&Items[199]);
			result.push_back(&Items[205]);
			result.push_back(&Items[211]);
			result.push_back(&Items[217]);
			result.push_back(&Items[223]);
			result.push_back(&Items[229]);
			result.push_back(&Items[235]);
			result.push_back(&Items[241]);
			result.push_back(&Items[247]);
			result.push_back(&Items[253]);
			result.push_back(&Items[259]);
			result.push_back(&Items[265]);
			result.push_back(&Items[271]);
			result.push_back(&Items[277]);
			result.push_back(&Items[278]);
			result.push_back(&Items[279]);
			result.push_back(&Items[280]);
			result.push_back(&Items[281]);
			result.push_back(&Items[282]);
			result.push_back(&Items[288]);
			result.push_back(&Items[294]);
			result.push_back(&Items[295]);
			result.push_back(&Items[296]);
			result.push_back(&Items[297]);
			result.push_back(&Items[298]);
			result.push_back(&Items[299]);
			result.push_back(&Items[300]);
			result.push_back(&Items[306]);
			result.push_back(&Items[312]);
			result.push_back(&Items[318]);
			result.push_back(&Items[324]);
			result.push_back(&Items[330]);
			result.push_back(&Items[336]);
			result.push_back(&Items[342]);
			result.push_back(&Items[348]);
			result.push_back(&Items[354]);
			result.push_back(&Items[360]);
			result.push_back(&Items[366]);
			result.push_back(&Items[372]);
			result.push_back(&Items[378]);
			result.push_back(&Items[384]);
			result.push_back(&Items[390]);
			result.push_back(&Items[396]);
			result.push_back(&Items[402]);
			result.push_back(&Items[408]);
			result.push_back(&Items[414]);
			result.push_back(&Items[420]);
			result.push_back(&Items[426]);
			result.push_back(&Items[432]);
			result.push_back(&Items[438]);
			result.push_back(&Items[444]);
			result.push_back(&Items[450]);
			result.push_back(&Items[456]);
			result.push_back(&Items[462]);
			result.push_back(&Items[468]);
			result.push_back(&Items[474]);
			result.push_back(&Items[480]);
			result.push_back(&Items[486]);
			result.push_back(&Items[492]);
			result.push_back(&Items[498]);
			result.push_back(&Items[504]);
			result.push_back(&Items[510]);
			result.push_back(&Items[516]);
			result.push_back(&Items[522]);
			result.push_back(&Items[528]);
			result.push_back(&Items[534]);
			result.push_back(&Items[540]);
			result.push_back(&Items[546]);
			result.push_back(&Items[552]);
			result.push_back(&Items[558]);
			result.push_back(&Items[564]);
			result.push_back(&Items[570]);
			result.push_back(&Items[576]);
			result.push_back(&Items[582]);
			result.push_back(&Items[588]);
			result.push_back(&Items[594]);
			result.push_back(&Items[600]);
			result.push_back(&Items[606]);
			result.push_back(&Items[612]);
			result.push_back(&Items[618]);
			result.push_back(&Items[624]);
			result.push_back(&Items[630]);
			result.push_back(&Items[636]);
			result.push_back(&Items[642]);
			result.push_back(&Items[648]);
			result.push_back(&Items[654]);
			result.push_back(&Items[660]);
			result.push_back(&Items[666]);
			result.push_back(&Items[672]);
			result.push_back(&Items[678]);
			result.push_back(&Items[684]);
			result.push_back(&Items[690]);
			result.push_back(&Items[696]);
			result.push_back(&Items[702]);
			result.push_back(&Items[708]);
			result.push_back(&Items[714]);
			result.push_back(&Items[720]);
			result.push_back(&Items[726]);
			result.push_back(&Items[732]);
			result.push_back(&Items[738]);
			result.push_back(&Items[744]);
			result.push_back(&Items[750]);
			result.push_back(&Items[756]);
			result.push_back(&Items[762]);
			result.push_back(&Items[768]);
			result.push_back(&Items[774]);
			result.push_back(&Items[780]);
			result.push_back(&Items[786]);
			result.push_back(&Items[792]);
			result.push_back(&Items[798]);
			result.push_back(&Items[804]);
			result.push_back(&Items[810]);
			result.push_back(&Items[811]);
			result.push_back(&Items[812]);
			result.push_back(&Items[818]);
			result.push_back(&Items[824]);
			result.push_back(&Items[830]);
			result.push_back(&Items[836]);
			result.push_back(&Items[837]);
			result.push_back(&Items[838]);
			result.push_back(&Items[839]);
			result.push_back(&Items[840]);
			result.push_back(&Items[841]);
			result.push_back(&Items[842]);
			result.push_back(&Items[843]);
			result.push_back(&Items[844]);
			result.push_back(&Items[845]);
			result.push_back(&Items[846]);
			result.push_back(&Items[847]);
			result.push_back(&Items[848]);
			result.push_back(&Items[849]);
			result.push_back(&Items[850]);
			result.push_back(&Items[851]);
			result.push_back(&Items[852]);
			result.push_back(&Items[853]);
			result.push_back(&Items[854]);
			result.push_back(&Items[855]);
			result.push_back(&Items[856]);
			result.push_back(&Items[857]);
			result.push_back(&Items[858]);
			result.push_back(&Items[859]);
			result.push_back(&Items[860]);
			result.push_back(&Items[861]);
			result.push_back(&Items[862]);
			result.push_back(&Items[863]);
			result.push_back(&Items[864]);
			result.push_back(&Items[865]);
			result.push_back(&Items[866]);
			result.push_back(&Items[867]);
			result.push_back(&Items[868]);
			result.push_back(&Items[869]);
			result.push_back(&Items[870]);
			result.push_back(&Items[871]);
			result.push_back(&Items[872]);
			result.push_back(&Items[873]);
			result.push_back(&Items[874]);
			result.push_back(&Items[875]);
			result.push_back(&Items[876]);
			result.push_back(&Items[877]);
			result.push_back(&Items[878]);
			result.push_back(&Items[879]);
			result.push_back(&Items[880]);
			result.push_back(&Items[881]);
			result.push_back(&Items[882]);
			result.push_back(&Items[883]);
			result.push_back(&Items[884]);
			result.push_back(&Items[885]);
			result.push_back(&Items[886]);
			result.push_back(&Items[887]);
			result.push_back(&Items[888]);
			result.push_back(&Items[889]);
			result.push_back(&Items[890]);
			result.push_back(&Items[891]);
			result.push_back(&Items[892]);
			result.push_back(&Items[893]);
			result.push_back(&Items[894]);
			result.push_back(&Items[895]);
		}
		return result;
	} break;

	case Section::Nature: {
		static auto result = EmojiPack();
		if (result.isEmpty()) {
			result.reserve(159);
			result.push_back(&Items[896]);
			result.push_back(&Items[897]);
			result.push_back(&Items[898]);
			result.push_back(&Items[899]);
			result.push_back(&Items[900]);
			result.push_back(&Items[901]);
			result.push_back(&Items[902]);
			result.push_back(&Items[903]);
			result.push_back(&Items[904]);
			result.push_back(&Items[905]);
			result.push_back(&Items[906]);
			result.push_back(&Items[907]);
			result.push_back(&Items[908]);
			result.push_back(&Items[909]);
			result.push_back(&Items[910]);
			result.push_back(&Items[911]);
			result.push_back(&Items[912]);
			result.push_back(&Items[913]);
			result.push_back(&Items[914]);
			result.push_back(&Items[915]);
			result.push_back(&Items[916]);
			result.push_back(&Items[917]);
			result.push_back(&Items[918]);
			result.push_back(&Items[919]);
			result.push_back(&Items[920]);
			result.push_back(&Items[921]);
			result.push_back(&Items[922]);
			result.push_back(&Items[923]);
			result.push_back(&Items[924]);
			result.push_back(&Items[925]);
			result.push_back(&Items[926]);
			result.push_back(&Items[927]);
			result.push_back(&Items[928]);
			result.push_back(&Items[929]);
			result.push_back(&Items[930]);
			result.push_back(&Items[931]);
			result.push_back(&Items[932]);
			result.push_back(&Items[933]);
			result.push_back(&Items[934]);
			result.push_back(&Items[935]);
			result.push_back(&Items[936]);
			result.push_back(&Items[937]);
			result.push_back(&Items[938]);
			result.push_back(&Items[939]);
			result.push_back(&Items[940]);
			result.push_back(&Items[941]);
			result.push_back(&Items[942]);
			result.push_back(&Items[943]);
			result.push_back(&Items[944]);
			result.push_back(&Items[945]);
			result.push_back(&Items[946]);
			result.push_back(&Items[947]);
			result.push_back(&Items[948]);
			result.push_back(&Items[949]);
			result.push_back(&Items[950]);
			result.push_back(&Items[951]);
			result.push_back(&Items[952]);
			result.push_back(&Items[953]);
			result.push_back(&Items[954]);
			result.push_back(&Items[955]);
			result.push_back(&Items[956]);
			result.push_back(&Items[957]);
			result.push_back(&Items[958]);
			result.push_back(&Items[959]);
			result.push_back(&Items[960]);
			result.push_back(&Items[961]);
			result.push_back(&Items[962]);
			result.push_back(&Items[963]);
			result.push_back(&Items[964]);
			result.push_back(&Items[965]);
			result.push_back(&Items[966]);
			result.push_back(&Items[967]);
			result.push_back(&Items[968]);
			result.push_back(&Items[969]);
			result.push_back(&Items[970]);
			result.push_back(&Items[971]);
			result.push_back(&Items[972]);
			result.push_back(&Items[973]);
			result.push_back(&Items[974]);
			result.push_back(&Items[975]);
			result.push_back(&Items[976]);
			result.push_back(&Items[977]);
			result.push_back(&Items[978]);
			result.push_back(&Items[979]);
			result.push_back(&Items[980]);
			result.push_back(&Items[981]);
			result.push_back(&Items[982]);
			result.push_back(&Items[983]);
			result.push_back(&Items[984]);
			result.push_back(&Items[985]);
			result.push_back(&Items[986]);
			result.push_back(&Items[987]);
			result.push_back(&Items[988]);
			result.push_back(&Items[989]);
			result.push_back(&Items[990]);
			result.push_back(&Items[991]);
			result.push_back(&Items[992]);
			result.push_back(&Items[993]);
			result.push_back(&Items[994]);
			result.push_back(&Items[995]);
			result.push_back(&Items[996]);
			result.push_back(&Items[997]);
			result.push_back(&Items[998]);
			result.push_back(&Items[999]);
			result.push_back(&Items[1000]);
			result.push_back(&Items[1001]);
			result.push_back(&Items[1002]);
			result.push_back(&Items[1003]);
			result.push_back(&Items[1004]);
			result.push_back(&Items[1005]);
			result.push_back(&Items[1006]);
			result.push_back(&Items[1007]);
			result.push_back(&Items[1008]);
			result.push_back(&Items[1009]);
			result.push_back(&Items[1010]);
			result.push_back(&Items[1011]);
			result.push_back(&Items[1012]);
			result.push_back(&Items[1013]);
			result.push_back(&Items[1014]);
			result.push_back(&Items[1015]);
			result.push_back(&Items[1016]);
			result.push_back(&Items[1017]);
			result.push_back(&Items[1018]);
			result.push_back(&Items[1019]);
			result.push_back(&Items[1020]);
			result.push_back(&Items[1021]);
			result.push_back(&Items[1022]);
			result.push_back(&Items[1023]);
			result.push_back(&Items[1024]);
			result.push_back(&Items[1025]);
			result.push_back(&Items[1026]);
			result.push_back(&Items[1027]);
			result.push_back(&Items[1028]);
			result.push_back(&Items[1029]);
			result.push_back(&Items[1030]);
			result.push_back(&Items[1031]);
			result.push_back(&Items[1032]);
			result.push_back(&Items[1033]);
			result.push_back(&Items[1034]);
			result.push_back(&Items[1035]);
			result.push_back(&Items[1036]);
			result.push_back(&Items[1037]);
			result.push_back(&Items[1038]);
			result.push_back(&Items[1039]);
			result.push_back(&Items[1040]);
			result.push_back(&Items[1041]);
			result.push_back(&Items[1042]);
			result.push_back(&Items[1043]);
			result.push_back(&Items[1044]);
			result.push_back(&Items[1045]);
			result.push_back(&Items[1046]);
			result.push_back(&Items[1047]);
			result.push_back(&Items[1048]);
			result.push_back(&Items[1049]);
			result.push_back(&Items[1050]);
			result.push_back(&Items[1051]);
			result.push_back(&Items[1052]);
			result.push_back(&Items[1053]);
			result.push_back(&Items[1054]);
		}
		return result;
	} break;

	case Section::Food: {
		static auto result = EmojiPack();
		if (result.isEmpty()) {
			result.reserve(86);
			result.push_back(&Items[1055]);
			result.push_back(&Items[1056]);
			result.push_back(&Items[1057]);
			result.push_back(&Items[1058]);
			result.push_back(&Items[1059]);
			result.push_back(&Items[1060]);
			result.push_back(&Items[1061]);
			result.push_back(&Items[1062]);
			result.push_back(&Items[1063]);
			result.push_back(&Items[1064]);
			result.push_back(&Items[1065]);
			result.push_back(&Items[1066]);
			result.push_back(&Items[1067]);
			result.push_back(&Items[1068]);
			result.push_back(&Items[1069]);
			result.push_back(&Items[1070]);
			result.push_back(&Items[1071]);
			result.push_back(&Items[1072]);
			result.push_back(&Items[1073]);
			result.push_back(&Items[1074]);
			result.push_back(&Items[1075]);
			result.push_back(&Items[1076]);
			result.push_back(&Items[1077]);
			result.push_back(&Items[1078]);
			result.push_back(&Items[1079]);
			result.push_back(&Items[1080]);
			result.push_back(&Items[1081]);
			result.push_back(&Items[1082]);
			result.push_back(&Items[1083]);
			result.push_back(&Items[1084]);
			result.push_back(&Items[1085]);
			result.push_back(&Items[1086]);
			result.push_back(&Items[1087]);
			result.push_back(&Items[1088]);
			result.push_back(&Items[1089]);
			result.push_back(&Items[1090]);
			result.push_back(&Items[1091]);
			result.push_back(&Items[1092]);
			result.push_back(&Items[1093]);
			result.push_back(&Items[1094]);
			result.push_back(&Items[1095]);
			result.push_back(&Items[1096]);
			result.push_back(&Items[1097]);
			result.push_back(&Items[1098]);
			result.push_back(&Items[1099]);
			result.push_back(&Items[1100]);
			result.push_back(&Items[1101]);
			result.push_back(&Items[1102]);
			result.push_back(&Items[1103]);
			result.push_back(&Items[1104]);
			result.push_back(&Items[1105]);
			result.push_back(&Items[1106]);
			result.push_back(&Items[1107]);
			result.push_back(&Items[1108]);
			result.push_back(&Items[1109]);
			result.push_back(&Items[1110]);
			result.push_back(&Items[1111]);
			result.push_back(&Items[1112]);
			result.push_back(&Items[1113]);
			result.push_back(&Items[1114]);
			result.push_back(&Items[1115]);
			result.push_back(&Items[1116]);
			result.push_back(&Items[1117]);
			result.push_back(&Items[1118]);
			result.push_back(&Items[1119]);
			result.push_back(&Items[1120]);
			result.push_back(&Items[1121]);
			result.push_back(&Items[1122]);
			result.push_back(&Items[1123]);
			result.push_back(&Items[1124]);
			result.push_back(&Items[1125]);
			result.push_back(&Items[1126]);
			result.push_back(&Items[1127]);
			result.push_back(&Items[1128]);
			result.push_back(&Items[1129]);
			result.push_back(&Items[1130]);
			result.push_back(&Items[1131]);
			result.push_back(&Items[1132]);
			result.push_back(&Items[1133]);
			result.push_back(&Items[1134]);
			result.push_back(&Items[1135]);
			result.push_back(&Items[1136]);
			result.push_back(&Items[1137]);
			result.push_back(&Items[1138]);
			result.push_back(&Items[1139]);
			result.push_back(&Items[1140]);
		}
		return result;
	} break;

	case Section::Activity: {
		static auto result = EmojiPack();
		if (result.isEmpty()) {
			result.reserve(80);
			result.push_back(&Items[1141]);
			result.push_back(&Items[1142]);
			result.push_back(&Items[1143]);
			result.push_back(&Items[1144]);
			result.push_back(&Items[1145]);
			result.push_back(&Items[1146]);
			result.push_back(&Items[1147]);
			result.push_back(&Items[1148]);
			result.push_back(&Items[1149]);
			result.push_back(&Items[1150]);
			result.push_back(&Items[1151]);
			result.push_back(&Items[1152]);
			result.push_back(&Items[1153]);
			result.push_back(&Items[1154]);
			result.push_back(&Items[1155]);
			result.push_back(&Items[1156]);
			result.push_back(&Items[1157]);
			result.push_back(&Items[1158]);
			result.push_back(&Items[1159]);
			result.push_back(&Items[1160]);
			result.push_back(&Items[1161]);
			result.push_back(&Items[1162]);
			result.push_back(&Items[1163]);
			result.push_back(&Items[1164]);
			result.push_back(&Items[1170]);
			result.push_back(&Items[1176]);
			result.push_back(&Items[1177]);
			result.push_back(&Items[1178]);
			result.push_back(&Items[1179]);
			result.push_back(&Items[1185]);
			result.push_back(&Items[1191]);
			result.push_back(&Items[1197]);
			result.push_back(&Items[1203]);
			result.push_back(&Items[1209]);
			result.push_back(&Items[1215]);
			result.push_back(&Items[1221]);
			result.push_back(&Items[1227]);
			result.push_back(&Items[1233]);
			result.push_back(&Items[1239]);
			result.push_back(&Items[1245]);
			result.push_back(&Items[1251]);
			result.push_back(&Items[1257]);
			result.push_back(&Items[1263]);
			result.push_back(&Items[1269]);
			result.push_back(&Items[1275]);
			result.push_back(&Items[1281]);
			result.push_back(&Items[1287]);
			result.push_back(&Items[1293]);
			result.push_back(&Items[1299]);
			result.push_back(&Items[1305]);
			result.push_back(&Items[1306]);
			result.push_back(&Items[1307]);
			result.push_back(&Items[1308]);
			result.push_back(&Items[1309]);
			result.push_back(&Items[1310]);
			result.push_back(&Items[1311]);
			result.push_back(&Items[1312]);
			result.push_back(&Items[1313]);
			result.push_back(&Items[1314]);
			result.push_back(&Items[1315]);
			result.push_back(&Items[1316]);
			result.push_back(&Items[1317]);
			result.push_back(&Items[1323]);
			result.push_back(&Items[1329]);
			result.push_back(&Items[1330]);
			result.push_back(&Items[1331]);
			result.push_back(&Items[1332]);
			result.push_back(&Items[1333]);
			result.push_back(&Items[1334]);
			result.push_back(&Items[1335]);
			result.push_back(&Items[1336]);
			result.push_back(&Items[1337]);
			result.push_back(&Items[1338]);
			result.push_back(&Items[1339]);
			result.push_back(&Items[1340]);
			result.push_back(&Items[1341]);
			result.push_back(&Items[1342]);
			result.push_back(&Items[1343]);
			result.push_back(&Items[1344]);
			result.push_back(&Items[1345]);
		}
		return result;
	} break;

	case Section::Travel: {
		static auto result = EmojiPack();
		if (result.isEmpty()) {
			result.reserve(119);
			result.push_back(&Items[1346]);
			result.push_back(&Items[1347]);
			result.push_back(&Items[1348]);
			result.push_back(&Items[1349]);
			result.push_back(&Items[1350]);
			result.push_back(&Items[1351]);
			result.push_back(&Items[1352]);
			result.push_back(&Items[1353]);
			result.push_back(&Items[1354]);
			result.push_back(&Items[1355]);
			result.push_back(&Items[1356]);
			result.push_back(&Items[1357]);
			result.push_back(&Items[1358]);
			result.push_back(&Items[1359]);
			result.push_back(&Items[1360]);
			result.push_back(&Items[1361]);
			result.push_back(&Items[1362]);
			result.push_back(&Items[1363]);
			result.push_back(&Items[1364]);
			result.push_back(&Items[1365]);
			result.push_back(&Items[1366]);
			result.push_back(&Items[1367]);
			result.push_back(&Items[1368]);
			result.push_back(&Items[1369]);
			result.push_back(&Items[1370]);
			result.push_back(&Items[1371]);
			result.push_back(&Items[1372]);
			result.push_back(&Items[1373]);
			result.push_back(&Items[1374]);
			result.push_back(&Items[1375]);
			result.push_back(&Items[1376]);
			result.push_back(&Items[1377]);
			result.push_back(&Items[1378]);
			result.push_back(&Items[1379]);
			result.push_back(&Items[1380]);
			result.push_back(&Items[1381]);
			result.push_back(&Items[1382]);
			result.push_back(&Items[1383]);
			result.push_back(&Items[1384]);
			result.push_back(&Items[1385]);
			result.push_back(&Items[1386]);
			result.push_back(&Items[1387]);
			result.push_back(&Items[1388]);
			result.push_back(&Items[1389]);
			result.push_back(&Items[1390]);
			result.push_back(&Items[1391]);
			result.push_back(&Items[1392]);
			result.push_back(&Items[1393]);
			result.push_back(&Items[1394]);
			result.push_back(&Items[1395]);
			result.push_back(&Items[1396]);
			result.push_back(&Items[1397]);
			result.push_back(&Items[1398]);
			result.push_back(&Items[1399]);
			result.push_back(&Items[1400]);
			result.push_back(&Items[1401]);
			result.push_back(&Items[1402]);
			result.push_back(&Items[1403]);
			result.push_back(&Items[1404]);
			result.push_back(&Items[1405]);
			result.push_back(&Items[1406]);
			result.push_back(&Items[1407]);
			result.push_back(&Items[1408]);
			result.push_back(&Items[1409]);
			result.push_back(&Items[1410]);
			result.push_back(&Items[1411]);
			result.push_back(&Items[1412]);
			result.push_back(&Items[1413]);
			result.push_back(&Items[1414]);
			result.push_back(&Items[1415]);
			result.push_back(&Items[1416]);
			result.push_back(&Items[1417]);
			result.push_back(&Items[1418]);
			result.push_back(&Items[1419]);
			result.push_back(&Items[1420]);
			result.push_back(&Items[1421]);
			result.push_back(&Items[1422]);
			result.push_back(&Items[1423]);
			result.push_back(&Items[1424]);
			result.push_back(&Items[1425]);
			result.push_back(&Items[1426]);
			result.push_back(&Items[1427]);
			result.push_back(&Items[1428]);
			result.push_back(&Items[1429]);
			result.push_back(&Items[1430]);
			result.push_back(&Items[1431]);
			result.push_back(&Items[1432]);
			result.push_back(&Items[1433]);
			result.push_back(&Items[1434]);
			result.push_back(&Items[1435]);
			result.push_back(&Items[1436]);
			result.push_back(&Items[1437]);
			result.push_back(&Items[1438]);
			result.push_back(&Items[1439]);
			result.push_back(&Items[1440]);
			result.push_back(&Items[1441]);
			result.push_back(&Items[1442]);
			result.push_back(&Items[1443]);
			result.push_back(&Items[1444]);
			result.push_back(&Items[1445]);
			result.push_back(&Items[1446]);
			result.push_back(&Items[1447]);
			result.push_back(&Items[1448]);
			result.push_back(&Items[1449]);
			result.push_back(&Items[1450]);
			result.push_back(&Items[1451]);
			result.push_back(&Items[1452]);
			result.push_back(&Items[1453]);
			result.push_back(&Items[1454]);
			result.push_back(&Items[1455]);
			result.push_back(&Items[1456]);
			result.push_back(&Items[1457]);
			result.push_back(&Items[1458]);
			result.push_back(&Items[1459]);
			result.push_back(&Items[1460]);
			result.push_back(&Items[1461]);
			result.push_back(&Items[1462]);
			result.push_back(&Items[1463]);
			result.push_back(&Items[1464]);
		}
		return result;
	} break;

	case Section::Objects: {
		static auto result = EmojiPack();
		if (result.isEmpty()) {
			result.reserve(173);
			result.push_back(&Items[1465]);
			result.push_back(&Items[1466]);
			result.push_back(&Items[1467]);
			result.push_back(&Items[1468]);
			result.push_back(&Items[1469]);
			result.push_back(&Items[1470]);
			result.push_back(&Items[1471]);
			result.push_back(&Items[1472]);
			result.push_back(&Items[1473]);
			result.push_back(&Items[1474]);
			result.push_back(&Items[1475]);
			result.push_back(&Items[1476]);
			result.push_back(&Items[1477]);
			result.push_back(&Items[1478]);
			result.push_back(&Items[1479]);
			result.push_back(&Items[1480]);
			result.push_back(&Items[1481]);
			result.push_back(&Items[1482]);
			result.push_back(&Items[1483]);
			result.push_back(&Items[1484]);
			result.push_back(&Items[1485]);
			result.push_back(&Items[1486]);
			result.push_back(&Items[1487]);
			result.push_back(&Items[1488]);
			result.push_back(&Items[1489]);
			result.push_back(&Items[1490]);
			result.push_back(&Items[1491]);
			result.push_back(&Items[1492]);
			result.push_back(&Items[1493]);
			result.push_back(&Items[1494]);
			result.push_back(&Items[1495]);
			result.push_back(&Items[1496]);
			result.push_back(&Items[1497]);
			result.push_back(&Items[1498]);
			result.push_back(&Items[1499]);
			result.push_back(&Items[1500]);
			result.push_back(&Items[1501]);
			result.push_back(&Items[1502]);
			result.push_back(&Items[1503]);
			result.push_back(&Items[1504]);
			result.push_back(&Items[1505]);
			result.push_back(&Items[1506]);
			result.push_back(&Items[1507]);
			result.push_back(&Items[1508]);
			result.push_back(&Items[1509]);
			result.push_back(&Items[1510]);
			result.push_back(&Items[1511]);
			result.push_back(&Items[1512]);
			result.push_back(&Items[1513]);
			result.push_back(&Items[1514]);
			result.push_back(&Items[1515]);
			result.push_back(&Items[1516]);
			result.push_back(&Items[1517]);
			result.push_back(&Items[1518]);
			result.push_back(&Items[1519]);
			result.push_back(&Items[1520]);
			result.push_back(&Items[1521]);
			result.push_back(&Items[1522]);
			result.push_back(&Items[1523]);
			result.push_back(&Items[1524]);
			result.push_back(&Items[1525]);
			result.push_back(&Items[1526]);
			result.push_back(&Items[1527]);
			result.push_back(&Items[1528]);
			result.push_back(&Items[1529]);
			result.push_back(&Items[1530]);
			result.push_back(&Items[1531]);
			result.push_back(&Items[1532]);
			result.push_back(&Items[1533]);
			result.push_back(&Items[1534]);
			result.push_back(&Items[1535]);
			result.push_back(&Items[1536]);
			result.push_back(&Items[1537]);
			result.push_back(&Items[1538]);
			result.push_back(&Items[1539]);
			result.push_back(&Items[1540]);
			result.push_back(&Items[1541]);
			result.push_back(&Items[1542]);
			result.push_back(&Items[1543]);
			result.push_back(&Items[1544]);
			result.push_back(&Items[1545]);
			result.push_back(&Items[1546]);
			result.push_back(&Items[1547]);
			result.push_back(&Items[1548]);
			result.push_back(&Items[1549]);
			result.push_back(&Items[1550]);
			result.push_back(&Items[1551]);
			result.push_back(&Items[1557]);
			result.push_back(&Items[1558]);
			result.push_back(&Items[1559]);
			result.push_back(&Items[1560]);
			result.push_back(&Items[1561]);
			result.push_back(&Items[1562]);
			result.push_back(&Items[1563]);
			result.push_back(&Items[1564]);
			result.push_back(&Items[1565]);
			result.push_back(&Items[1566]);
			result.push_back(&Items[1567]);
			result.push_back(&Items[1568]);
			result.push_back(&Items[1569]);
			result.push_back(&Items[1570]);
			result.push_back(&Items[1571]);
			result.push_back(&Items[1572]);
			result.push_back(&Items[1573]);
			result.push_back(&Items[1574]);
			result.push_back(&Items[1575]);
			result.push_back(&Items[1576]);
			result.push_back(&Items[1577]);
			result.push_back(&Items[1578]);
			result.push_back(&Items[1579]);
			result.push_back(&Items[1580]);
			result.push_back(&Items[1581]);
			result.push_back(&Items[1582]);
			result.push_back(&Items[1583]);
			result.push_back(&Items[1584]);
			result.push_back(&Items[1585]);
			result.push_back(&Items[1586]);
			result.push_back(&Items[1587]);
			result.push_back(&Items[1588]);
			result.push_back(&Items[1589]);
			result.push_back(&Items[1590]);
			result.push_back(&Items[1591]);
			result.push_back(&Items[1592]);
			result.push_back(&Items[1593]);
			result.push_back(&Items[1594]);
			result.push_back(&Items[1595]);
			result.push_back(&Items[1596]);
			result.push_back(&Items[1597]);
			result.push_back(&Items[1598]);
			result.push_back(&Items[1599]);
			result.push_back(&Items[1600]);
			result.push_back(&Items[1601]);
			result.push_back(&Items[1602]);
			result.push_back(&Items[1603]);
			result.push_back(&Items[1604]);
			result.push_back(&Items[1605]);
			result.push_back(&Items[1606]);
			result.push_back(&Items[1607]);
			result.push_back(&Items[1608]);
			result.push_back(&Items[1609]);
			result.push_back(&Items[1610]);
			result.push_back(&Items[1611]);
			result.push_back(&Items[1612]);
			result.push_back(&Items[1613]);
			result.push_back(&Items[1614]);
			result.push_back(&Items[1615]);
			result.push_back(&Items[1616]);
			result.push_back(&Items[1617]);
			result.push_back(&Items[1618]);
			result.push_back(&Items[1619]);
			result.push_back(&Items[1620]);
			result.push_back(&Items[1621]);
			result.push_back(&Items[1622]);
			result.push_back(&Items[1623]);
			result.push_back(&Items[1624]);
			result.push_back(&Items[1625]);
			result.push_back(&Items[1626]);
			result.push_back(&Items[1627]);
			result.push_back(&Items[1628]);
			result.push_back(&Items[1629]);
			result.push_back(&Items[1630]);
			result.push_back(&Items[1631]);
			result.push_back(&Items[1632]);
			result.push_back(&Items[1633]);
			result.push_back(&Items[1634]);
			result.push_back(&Items[1635]);
			result.push_back(&Items[1636]);
			result.push_back(&Items[1637]);
			result.push_back(&Items[1638]);
			result.push_back(&Items[1639]);
			result.push_back(&Items[1640]);
			result.push_back(&Items[1641]);
			result.push_back(&Items[1642]);
		}
		return result;
	} break;

	case Section::Symbols: {
		static auto result = EmojiPack();
		if (result.isEmpty()) {
			result.reserve(524);
			result.push_back(&Items[1643]);
			result.push_back(&Items[1644]);
			result.push_back(&Items[1645]);
			result.push_back(&Items[1646]);
			result.push_back(&Items[1647]);
			result.push_back(&Items[1648]);
			result.push_back(&Items[1649]);
			result.push_back(&Items[1650]);
			result.push_back(&Items[1651]);
			result.push_back(&Items[1652]);
			result.push_back(&Items[1653]);
			result.push_back(&Items[1654]);
			result.push_back(&Items[1655]);
			result.push_back(&Items[1656]);
			result.push_back(&Items[1657]);
			result.push_back(&Items[1658]);
			result.push_back(&Items[1659]);
			result.push_back(&Items[1660]);
			result.push_back(&Items[1661]);
			result.push_back(&Items[1662]);
			result.push_back(&Items[1663]);
			result.push_back(&Items[1664]);
			result.push_back(&Items[1665]);
			result.push_back(&Items[1666]);
			result.push_back(&Items[1667]);
			result.push_back(&Items[1668]);
			result.push_back(&Items[1669]);
			result.push_back(&Items[1670]);
			result.push_back(&Items[1671]);
			result.push_back(&Items[1672]);
			result.push_back(&Items[1673]);
			result.push_back(&Items[1674]);
			result.push_back(&Items[1675]);
			result.push_back(&Items[1676]);
			result.push_back(&Items[1677]);
			result.push_back(&Items[1678]);
			result.push_back(&Items[1679]);
			result.push_back(&Items[1680]);
			result.push_back(&Items[1681]);
			result.push_back(&Items[1682]);
			result.push_back(&Items[1683]);
			result.push_back(&Items[1684]);
			result.push_back(&Items[1685]);
			result.push_back(&Items[1686]);
			result.push_back(&Items[1687]);
			result.push_back(&Items[1688]);
			result.push_back(&Items[1689]);
			result.push_back(&Items[1690]);
			result.push_back(&Items[1691]);
			result.push_back(&Items[1692]);
			result.push_back(&Items[1693]);
			result.push_back(&Items[1694]);
			result.push_back(&Items[1695]);
			result.push_back(&Items[1696]);
			result.push_back(&Items[1697]);
			result.push_back(&Items[1698]);
			result.push_back(&Items[1699]);
			result.push_back(&Items[1700]);
			result.push_back(&Items[1701]);
			result.push_back(&Items[1702]);
			result.push_back(&Items[1703]);
			result.push_back(&Items[1704]);
			result.push_back(&Items[1705]);
			result.push_back(&Items[1706]);
			result.push_back(&Items[1707]);
			result.push_back(&Items[1708]);
			result.push_back(&Items[1709]);
			result.push_back(&Items[1710]);
			result.push_back(&Items[1711]);
			result.push_back(&Items[1712]);
			result.push_back(&Items[1713]);
			result.push_back(&Items[1714]);
			result.push_back(&Items[1715]);
			result.push_back(&Items[1716]);
			result.push_back(&Items[1717]);
			result.push_back(&Items[1718]);
			result.push_back(&Items[1719]);
			result.push_back(&Items[1720]);
			result.push_back(&Items[1721]);
			result.push_back(&Items[1722]);
			result.push_back(&Items[1723]);
			result.push_back(&Items[1724]);
			result.push_back(&Items[1725]);
			result.push_back(&Items[1726]);
			result.push_back(&Items[1727]);
			result.push_back(&Items[1728]);
			result.push_back(&Items[1729]);
			result.push_back(&Items[1730]);
			result.push_back(&Items[1731]);
			result.push_back(&Items[1732]);
			result.push_back(&Items[1733]);
			result.push_back(&Items[1734]);
			result.push_back(&Items[1735]);
			result.push_back(&Items[1736]);
			result.push_back(&Items[1737]);
			result.push_back(&Items[1738]);
			result.push_back(&Items[1739]);
			result.push_back(&Items[1740]);
			result.push_back(&Items[1741]);
			result.push_back(&Items[1742]);
			result.push_back(&Items[1743]);
			result.push_back(&Items[1744]);
			result.push_back(&Items[1745]);
			result.push_back(&Items[1746]);
			result.push_back(&Items[1747]);
			result.push_back(&Items[1748]);
			result.push_back(&Items[1749]);
			result.push_back(&Items[1750]);
			result.push_back(&Items[1751]);
			result.push_back(&Items[1752]);
			result.push_back(&Items[1753]);
			result.push_back(&Items[1754]);
			result.push_back(&Items[1755]);
			result.push_back(&Items[1756]);
			result.push_back(&Items[1757]);
			result.push_back(&Items[1758]);
			result.push_back(&Items[1759]);
			result.push_back(&Items[1760]);
			result.push_back(&Items[1761]);
			result.push_back(&Items[1762]);
			result.push_back(&Items[1763]);
			result.push_back(&Items[1764]);
			result.push_back(&Items[1765]);
			result.push_back(&Items[1766]);
			result.push_back(&Items[1767]);
			result.push_back(&Items[1768]);
			result.push_back(&Items[1769]);
			result.push_back(&Items[1770]);
			result.push_back(&Items[1771]);
			result.push_back(&Items[1772]);
			result.push_back(&Items[1773]);
			result.push_back(&Items[1774]);
			result.push_back(&Items[1775]);
			result.push_back(&Items[1776]);
			result.push_back(&Items[1777]);
			result.push_back(&Items[1778]);
			result.push_back(&Items[1779]);
			result.push_back(&Items[1780]);
			result.push_back(&Items[1781]);
			result.push_back(&Items[1782]);
			result.push_back(&Items[1783]);
			result.push_back(&Items[1784]);
			result.push_back(&Items[1785]);
			result.push_back(&Items[1786]);
			result.push_back(&Items[1787]);
			result.push_back(&Items[1788]);
			result.push_back(&Items[1789]);
			result.push_back(&Items[1790]);
			result.push_back(&Items[1791]);
			result.push_back(&Items[1792]);
			result.push_back(&Items[1793]);
			result.push_back(&Items[1794]);
			result.push_back(&Items[1795]);
			result.push_back(&Items[1796]);
			result.push_back(&Items[1797]);
			result.push_back(&Items[1798]);
			result.push_back(&Items[1799]);
			result.push_back(&Items[1800]);
			result.push_back(&Items[1801]);
			result.push_back(&Items[1802]);
			result.push_back(&Items[1803]);
			result.push_back(&Items[1804]);
			result.push_back(&Items[1805]);
			result.push_back(&Items[1806]);
			result.push_back(&Items[1807]);
			result.push_back(&Items[1808]);
			result.push_back(&Items[1809]);
			result.push_back(&Items[1810]);
			result.push_back(&Items[1811]);
			result.push_back(&Items[1812]);
			result.push_back(&Items[1813]);
			result.push_back(&Items[1814]);
			result.push_back(&Items[1815]);
			result.push_back(&Items[1816]);
			result.push_back(&Items[1817]);
			result.push_back(&Items[1818]);
			result.push_back(&Items[1819]);
			result.push_back(&Items[1820]);
			result.push_back(&Items[1821]);
			result.push_back(&Items[1822]);
			result.push_back(&Items[1823]);
			result.push_back(&Items[1824]);
			result.push_back(&Items[1825]);
			result.push_back(&Items[1826]);
			result.push_back(&Items[1827]);
			result.push_back(&Items[1828]);
			result.push_back(&Items[1829]);
			result.push_back(&Items[1830]);
			result.push_back(&Items[1831]);
			result.push_back(&Items[1832]);
			result.push_back(&Items[1833]);
			result.push_back(&Items[1834]);
			result.push_back(&Items[1835]);
			result.push_back(&Items[1836]);
			result.push_back(&Items[1837]);
			result.push_back(&Items[1838]);
			result.push_back(&Items[1839]);
			result.push_back(&Items[1840]);
			result.push_back(&Items[1841]);
			result.push_back(&Items[1842]);
			result.push_back(&Items[1843]);
			result.push_back(&Items[1844]);
			result.push_back(&Items[1845]);
			result.push_back(&Items[1846]);
			result.push_back(&Items[1847]);
			result.push_back(&Items[1848]);
			result.push_back(&Items[1849]);
			result.push_back(&Items[1850]);
			result.push_back(&Items[1851]);
			result.push_back(&Items[1852]);
			result.push_back(&Items[1853]);
			result.push_back(&Items[1854]);
			result.push_back(&Items[1855]);
			result.push_back(&Items[1856]);
			result.push_back(&Items[1857]);
			result.push_back(&Items[1858]);
			result.push_back(&Items[1859]);
			result.push_back(&Items[1860]);
			result.push_back(&Items[1861]);
			result.push_back(&Items[1862]);
			result.push_back(&Items[1863]);
			result.push_back(&Items[1864]);
			result.push_back(&Items[1865]);
			result.push_back(&Items[1866]);
			result.push_back(&Items[1867]);
			result.push_back(&Items[1868]);
			result.push_back(&Items[1869]);
			result.push_back(&Items[1870]);
			result.push_back(&Items[1871]);
			result.push_back(&Items[1872]);
			result.push_back(&Items[1873]);
			result.push_back(&Items[1874]);
			result.push_back(&Items[1875]);
			result.push_back(&Items[1876]);
			result.push_back(&Items[1877]);
			result.push_back(&Items[1878]);
			result.push_back(&Items[1879]);
			result.push_back(&Items[1880]);
			result.push_back(&Items[1881]);
			result.push_back(&Items[1882]);
			result.push_back(&Items[1883]);
			result.push_back(&Items[1884]);
			result.push_back(&Items[1885]);
			result.push_back(&Items[1886]);
			result.push_back(&Items[1887]);
			result.push_back(&Items[1888]);
			result.push_back(&Items[1889]);
			result.push_back(&Items[1890]);
			result.push_back(&Items[1891]);
			result.push_back(&Items[1892]);
			result.push_back(&Items[1893]);
			result.push_back(&Items[1894]);
			result.push_back(&Items[1895]);
			result.push_back(&Items[1896]);
			result.push_back(&Items[1897]);
			result.push_back(&Items[1898]);
			result.push_back(&Items[1899]);
			result.push_back(&Items[1900]);
			result.push_back(&Items[1901]);
			result.push_back(&Items[1902]);
			result.push_back(&Items[1903]);
			result.push_back(&Items[1904]);
			result.push_back(&Items[1905]);
			result.push_back(&Items[1906]);
			result.push_back(&Items[1907]);
			result.push_back(&Items[1908]);
			result.push_back(&Items[1909]);
			result.push_back(&Items[1910]);
			result.push_back(&Items[1911]);
			result.push_back(&Items[1912]);
			result.push_back(&Items[1913]);
			result.push_back(&Items[1914]);
			result.push_back(&Items[1915]);
			result.push_back(&Items[1916]);
			result.push_back(&Items[1917]);
			result.push_back(&Items[1918]);
			result.push_back(&Items[1919]);
			result.push_back(&Items[1920]);
			result.push_back(&Items[1921]);
			result.push_back(&Items[1922]);
			result.push_back(&Items[1923]);
			result.push_back(&Items[1924]);
			result.push_back(&Items[1925]);
			result.push_back(&Items[1926]);
			result.push_back(&Items[1927]);
			result.push_back(&Items[1928]);
			result.push_back(&Items[1929]);
			result.push_back(&Items[1930]);
			result.push_back(&Items[1931]);
			result.push_back(&Items[1932]);
			result.push_back(&Items[1933]);
			result.push_back(&Items[1934]);
			result.push_back(&Items[1935]);
			result.push_back(&Items[1936]);
			result.push_back(&Items[1937]);
			result.push_back(&Items[1938]);
			result.push_back(&Items[1939]);
			result.push_back(&Items[1940]);
			result.push_back(&Items[1941]);
			result.push_back(&Items[1942]);
			result.push_back(&Items[1943]);
			result.push_back(&Items[1944]);
			result.push_back(&Items[1945]);
			result.push_back(&Items[1946]);
			result.push_back(&Items[1947]);
			result.push_back(&Items[1948]);
			result.push_back(&Items[1949]);
			result.push_back(&Items[1950]);
			result.push_back(&Items[1951]);
			result.push_back(&Items[1952]);
			result.push_back(&Items[1953]);
			result.push_back(&Items[1954]);
			result.push_back(&Items[1955]);
			result.push_back(&Items[1956]);
			result.push_back(&Items[1957]);
			result.push_back(&Items[1958]);
			result.push_back(&Items[1959]);
			result.push_back(&Items[1960]);
			result.push_back(&Items[1961]);
			result.push_back(&Items[1962]);
			result.push_back(&Items[1963]);
			result.push_back(&Items[1964]);
			result.push_back(&Items[1965]);
			result.push_back(&Items[1966]);
			result.push_back(&Items[1967]);
			result.push_back(&Items[1968]);
			result.push_back(&Items[1969]);
			result.push_back(&Items[1970]);
			result.push_back(&Items[1971]);
			result.push_back(&Items[1972]);
			result.push_back(&Items[1973]);
			result.push_back(&Items[1974]);
			result.push_back(&Items[1975]);
			result.push_back(&Items[1976]);
			result.push_back(&Items[1977]);
			result.push_back(&Items[1978]);
			result.push_back(&Items[1979]);
			result.push_back(&Items[1980]);
			result.push_back(&Items[1981]);
			result.push_back(&Items[1982]);
			result.push_back(&Items[1983]);
			result.push_back(&Items[1984]);
			result.push_back(&Items[1985]);
			result.push_back(&Items[1986]);
			result.push_back(&Items[1987]);
			result.push_back(&Items[1988]);
			result.push_back(&Items[1989]);
			result.push_back(&Items[1990]);
			result.push_back(&Items[1991]);
			result.push_back(&Items[1992]);
			result.push_back(&Items[1993]);
			result.push_back(&Items[1994]);
			result.push_back(&Items[1995]);
			result.push_back(&Items[1996]);
			result.push_back(&Items[1997]);
			result.push_back(&Items[1998]);
			result.push_back(&Items[1999]);
			result.push_back(&Items[2000]);
			result.push_back(&Items[2001]);
			result.push_back(&Items[2002]);
			result.push_back(&Items[2003]);
			result.push_back(&Items[2004]);
			result.push_back(&Items[2005]);
			result.push_back(&Items[2006]);
			result.push_back(&Items[2007]);
			result.push_back(&Items[2008]);
			result.push_back(&Items[2009]);
			result.push_back(&Items[2010]);
			result.push_back(&Items[2011]);
			result.push_back(&Items[2012]);
			result.push_back(&Items[2013]);
			result.push_back(&Items[2014]);
			result.push_back(&Items[2015]);
			result.push_back(&Items[2016]);
			result.push_back(&Items[2017]);
			result.push_back(&Items[2018]);
			result.push_back(&Items[2019]);
			result.push_back(&Items[2020]);
			result.push_back(&Items[2021]);
			result.push_back(&Items[2022]);
			result.push_back(&Items[2023]);
			result.push_back(&Items[2024]);
			result.push_back(&Items[2025]);
			result.push_back(&Items[2026]);
			result.push_back(&Items[2027]);
			result.push_back(&Items[2028]);
			result.push_back(&Items[2029]);
			result.push_back(&Items[2030]);
			result.push_back(&Items[2031]);
			result.push_back(&Items[2032]);
			result.push_back(&Items[2033]);
			result.push_back(&Items[2034]);
			result.push_back(&Items[2035]);
			result.push_back(&Items[2036]);
			result.push_back(&Items[2037]);
			result.push_back(&Items[2038]);
			result.push_back(&Items[2039]);
			result.push_back(&Items[2040]);
			result.push_back(&Items[2041]);
			result.push_back(&Items[2042]);
			result.push_back(&Items[2043]);
			result.push_back(&Items[2044]);
			result.push_back(&Items[2045]);
			result.push_back(&Items[2046]);
			result.push_back(&Items[2047]);
			result.push_back(&Items[2048]);
			result.push_back(&Items[2049]);
			result.push_back(&Items[2050]);
			result.push_back(&Items[2051]);
			result.push_back(&Items[2052]);
			result.push_back(&Items[2053]);
			result.push_back(&Items[2054]);
			result.push_back(&Items[2055]);
			result.push_back(&Items[2056]);
			result.push_back(&Items[2057]);
			result.push_back(&Items[2058]);
			result.push_back(&Items[2059]);
			result.push_back(&Items[2060]);
			result.push_back(&Items[2061]);
			result.push_back(&Items[2062]);
			result.push_back(&Items[2063]);
			result.push_back(&Items[2064]);
			result.push_back(&Items[2065]);
			result.push_back(&Items[2066]);
			result.push_back(&Items[2067]);
			result.push_back(&Items[2068]);
			result.push_back(&Items[2069]);
			result.push_back(&Items[2070]);
			result.push_back(&Items[2071]);
			result.push_back(&Items[2072]);
			result.push_back(&Items[2073]);
			result.push_back(&Items[2074]);
			result.push_back(&Items[2075]);
			result.push_back(&Items[2076]);
			result.push_back(&Items[2077]);
			result.push_back(&Items[2078]);
			result.push_back(&Items[2079]);
			result.push_back(&Items[2080]);
			result.push_back(&Items[2081]);
			result.push_back(&Items[2082]);
			result.push_back(&Items[2083]);
			result.push_back(&Items[2084]);
			result.push_back(&Items[2085]);
			result.push_back(&Items[2086]);
			result.push_back(&Items[2087]);
			result.push_back(&Items[2088]);
			result.push_back(&Items[2089]);
			result.push_back(&Items[2090]);
			result.push_back(&Items[2091]);
			result.push_back(&Items[2092]);
			result.push_back(&Items[2093]);
			result.push_back(&Items[2094]);
			result.push_back(&Items[2095]);
			result.push_back(&Items[2096]);
			result.push_back(&Items[2097]);
			result.push_back(&Items[2098]);
			result.push_back(&Items[2099]);
			result.push_back(&Items[2100]);
			result.push_back(&Items[2101]);
			result.push_back(&Items[2102]);
			result.push_back(&Items[2103]);
			result.push_back(&Items[2104]);
			result.push_back(&Items[2105]);
			result.push_back(&Items[2106]);
			result.push_back(&Items[2107]);
			result.push_back(&Items[2108]);
			result.push_back(&Items[2109]);
			result.push_back(&Items[2110]);
			result.push_back(&Items[2111]);
			result.push_back(&Items[2112]);
			result.push_back(&Items[2113]);
			result.push_back(&Items[2114]);
			result.push_back(&Items[2115]);
			result.push_back(&Items[2116]);
			result.push_back(&Items[2117]);
			result.push_back(&Items[2118]);
			result.push_back(&Items[2119]);
			result.push_back(&Items[2120]);
			result.push_back(&Items[2121]);
			result.push_back(&Items[2122]);
			result.push_back(&Items[2123]);
			result.push_back(&Items[2124]);
			result.push_back(&Items[2125]);
			result.push_back(&Items[2126]);
			result.push_back(&Items[2127]);
			result.push_back(&Items[2128]);
			result.push_back(&Items[2129]);
			result.push_back(&Items[2130]);
			result.push_back(&Items[2131]);
			result.push_back(&Items[2132]);
			result.push_back(&Items[2133]);
			result.push_back(&Items[2134]);
			result.push_back(&Items[2135]);
			result.push_back(&Items[2136]);
			result.push_back(&Items[2137]);
			result.push_back(&Items[2138]);
			result.push_back(&Items[2139]);
			result.push_back(&Items[2140]);
			result.push_back(&Items[2141]);
			result.push_back(&Items[2142]);
			result.push_back(&Items[2143]);
			result.push_back(&Items[2144]);
			result.push_back(&Items[2145]);
			result.push_back(&Items[2146]);
			result.push_back(&Items[2147]);
			result.push_back(&Items[2148]);
			result.push_back(&Items[2149]);
			result.push_back(&Items[2150]);
			result.push_back(&Items[2151]);
			result.push_back(&Items[2152]);
			result.push_back(&Items[2153]);
			result.push_back(&Items[2154]);
			result.push_back(&Items[2155]);
			result.push_back(&Items[2156]);
			result.push_back(&Items[2157]);
			result.push_back(&Items[2158]);
			result.push_back(&Items[2159]);
			result.push_back(&Items[2160]);
			result.push_back(&Items[2161]);
			result.push_back(&Items[2162]);
			result.push_back(&Items[2163]);
			result.push_back(&Items[2164]);
			result.push_back(&Items[2165]);
			result.push_back(&Items[2166]);
		}
		return result;
	} break;
	}
	return EmojiPack();
}

int Index() {
	return WorkingIndex;
}

int One::variantsCount() const {
	return hasVariants() ? 5 : 0;
}

int One::variantIndex(EmojiPtr variant) const {
	return (variant - original());
}

EmojiPtr One::variant(int index) const {
	return (index >= 0 && index <= variantsCount()) ? (original() + index) : this;
}

int One::index() const {
	return (this - &Items[0]);
}

} // namespace Emoji
} // namespace Ui
