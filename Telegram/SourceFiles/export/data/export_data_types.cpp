/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/data/export_data_types.h"

#include "export/export_settings.h"
#include "export/output/export_output_file.h"
#include "base/base_file_utilities.h"
#include "ui/text/format_values.h"
#include "core/mime_type.h"
#include "core/utils.h"
#include <QtCore/QDateTime>
#include <QtCore/QRegularExpression>
#include <QtGui/QImageReader>
#include <range/v3/algorithm/max_element.hpp>
#include <range/v3/view/all.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/range/conversion.hpp>

namespace App { // Hackish..
QString formatPhone(QString phone);
} // namespace App

namespace Export {
namespace Data {
namespace {

constexpr auto kUserPeerIdShift = (1ULL << 32);
constexpr auto kChatPeerIdShift = (2ULL << 32);
constexpr auto kMaxImageSize = 10000;
constexpr auto kMigratedMessagesIdShift = -1'000'000'000;

QString PrepareFileNameDatePart(TimeId date) {
	return date
		? ('@' + QString::fromUtf8(FormatDateTime(date, '-', '-', '_')))
		: QString();
}

QString PreparePhotoFileName(int index, TimeId date) {
	return "photo_"
		+ QString::number(index)
		+ PrepareFileNameDatePart(date)
		+ ".jpg";
}

} // namespace

PeerId UserPeerId(int32 userId) {
	return kUserPeerIdShift | uint32(userId);
}

PeerId ChatPeerId(int32 chatId) {
	return kChatPeerIdShift | uint32(chatId);
}

int32 BarePeerId(PeerId peerId) {
	return int32(peerId & 0xFFFFFFFFULL);
}

int PeerColorIndex(int32 bareId) {
	const auto index = std::abs(bareId) % 7;
	const int map[] = { 0, 7, 4, 1, 6, 3, 5 };
	return map[index];
}

int StringBarePeerId(const Utf8String &data) {
	auto result = 0xFF;
	for (const auto ch : data) {
		result *= 239;
		result += ch;
		result &= 0xFF;
	}
	return result;
}

int ApplicationColorIndex(int applicationId) {
	static const auto official = std::map<int, int> {
		{ 1, 0 }, // iOS
		{ 7, 0 }, // iOS X
		{ 6, 1 }, // Android
		{ 21724, 1 }, // Android X
		{ 2834, 2 }, // macOS
		{ 2496, 3 }, // Webogram
		{ 2040, 4 }, // Desktop
		{ 1429, 5 }, // Windows Phone
	};
	if (const auto i = official.find(applicationId); i != end(official)) {
		return i->second;
	}
	return PeerColorIndex(applicationId);
}

int DomainApplicationId(const Utf8String &data) {
	return 0x1000 + StringBarePeerId(data);
}

bool IsChatPeerId(PeerId peerId) {
	return (peerId & kChatPeerIdShift) == kChatPeerIdShift;
}

bool IsUserPeerId(PeerId peerId) {
	return (peerId & kUserPeerIdShift) == kUserPeerIdShift;
}

PeerId ParsePeerId(const MTPPeer &data) {
	return data.match([](const MTPDpeerUser &data) {
		return UserPeerId(data.vuser_id().v);
	}, [](const MTPDpeerChat &data) {
		return ChatPeerId(data.vchat_id().v);
	}, [](const MTPDpeerChannel &data) {
		return ChatPeerId(data.vchannel_id().v);
	});
}

Utf8String ParseString(const MTPstring &data) {
	return data.v;
}

std::vector<TextPart> ParseText(
		const MTPstring &data,
		const QVector<MTPMessageEntity> &entities) {
	using Type = TextPart::Type;
	const auto text = QString::fromUtf8(data.v);
	const auto size = data.v.size();
	const auto mid = [&](int offset, int length) {
		return text.mid(offset, length).toUtf8();
	};
	auto result = std::vector<TextPart>();
	auto offset = 0;
	auto addTextPart = [&](int till) {
		if (till > offset) {
			auto part = TextPart();
			part.text = mid(offset, till - offset);
			result.push_back(std::move(part));
			offset = till;
		}
	};
	for (const auto &entity : entities) {
		const auto start = entity.match([](const auto &data) {
			return data.voffset().v;
		});
		const auto length = entity.match([](const auto &data) {
			return data.vlength().v;
		});

		if (start < offset || length <= 0 || start + length > size) {
			continue;
		}

		addTextPart(start);

		auto part = TextPart();
		part.type = entity.match(
			[](const MTPDmessageEntityUnknown&) { return Type::Unknown; },
			[](const MTPDmessageEntityMention&) { return Type::Mention; },
			[](const MTPDmessageEntityHashtag&) { return Type::Hashtag; },
			[](const MTPDmessageEntityBotCommand&) {
				return Type::BotCommand; },
			[](const MTPDmessageEntityUrl&) { return Type::Url; },
			[](const MTPDmessageEntityEmail&) { return Type::Email; },
			[](const MTPDmessageEntityBold&) { return Type::Bold; },
			[](const MTPDmessageEntityItalic&) { return Type::Italic; },
			[](const MTPDmessageEntityCode&) { return Type::Code; },
			[](const MTPDmessageEntityPre&) { return Type::Pre; },
			[](const MTPDmessageEntityTextUrl&) { return Type::TextUrl; },
			[](const MTPDmessageEntityMentionName&) {
				return Type::MentionName; },
			[](const MTPDinputMessageEntityMentionName&) {
				return Type::MentionName; },
			[](const MTPDmessageEntityPhone&) { return Type::Phone; },
			[](const MTPDmessageEntityCashtag&) { return Type::Cashtag; },
			[](const MTPDmessageEntityUnderline&) { return Type::Underline; },
			[](const MTPDmessageEntityStrike&) { return Type::Strike; },
			[](const MTPDmessageEntityBlockquote&) {
				return Type::Blockquote; },
			[](const MTPDmessageEntityBankCard&) { return Type::BankCard; });
		part.text = mid(start, length);
		part.additional = entity.match(
		[](const MTPDmessageEntityPre &data) {
			return ParseString(data.vlanguage());
		}, [](const MTPDmessageEntityTextUrl &data) {
			return ParseString(data.vurl());
		}, [](const MTPDmessageEntityMentionName &data) {
			return NumberToString(data.vuser_id().v);
		}, [](const auto &) { return Utf8String(); });

		result.push_back(std::move(part));
		offset = start + length;
	}
	addTextPart(size);
	return result;
}

Utf8String FillLeft(const Utf8String &data, int length, char filler) {
	if (length <= data.size()) {
		return data;
	}
	auto result = Utf8String();
	result.reserve(length);
	for (auto i = 0, count = length - data.size(); i != count; ++i) {
		result.append(filler);
	}
	result.append(data);
	return result;
}

bool RefreshFileReference(FileLocation &to, const FileLocation &from) {
	if (to.dcId != from.dcId || to.data.type() != from.data.type()) {
		return false;
	}
	if (to.data.type() == mtpc_inputPhotoFileLocation) {
		const auto &toData = to.data.c_inputPhotoFileLocation();
		const auto &fromData = from.data.c_inputPhotoFileLocation();
		if (toData.vid().v != fromData.vid().v
			|| toData.vthumb_size().v != fromData.vthumb_size().v) {
			return false;
		}
		to = from;
		return true;
	} else if (to.data.type() == mtpc_inputDocumentFileLocation) {
		const auto &toData = to.data.c_inputDocumentFileLocation();
		const auto &fromData = from.data.c_inputDocumentFileLocation();
		if (toData.vid().v != fromData.vid().v
			|| toData.vthumb_size().v != fromData.vthumb_size().v) {
			return false;
		}
		to = from;
		return true;
	}
	return false;
}

Image ParseMaxImage(
		const MTPDphoto &photo,
		const QString &suggestedPath) {
	auto result = Image();
	result.file.suggestedPath = suggestedPath;

	auto maxArea = int64(0);
	for (const auto &size : photo.vsizes().v) {
		size.match([](const MTPDphotoSizeEmpty &) {
		}, [](const MTPDphotoStrippedSize &) {
			// Max image size should not be a stripped image.
		}, [](const MTPDphotoPathSize &) {
			// Max image size should not be a path image.
		}, [&](const auto &data) {
			const auto area = data.vw().v * int64(data.vh().v);
			if (area > maxArea) {
				result.width = data.vw().v;
				result.height = data.vh().v;
				result.file.location = FileLocation{
					photo.vdc_id().v,
					MTP_inputPhotoFileLocation(
						photo.vid(),
						photo.vaccess_hash(),
						photo.vfile_reference(),
						data.vtype()) };
				if constexpr (MTPDphotoCachedSize::Is<decltype(data)>()) {
					result.file.content = data.vbytes().v;
					result.file.size = result.file.content.size();
				} else if constexpr (MTPDphotoSizeProgressive::Is<decltype(data)>()) {
					if (data.vsizes().v.isEmpty()) {
						return;
					}
					result.file.content = QByteArray();
					result.file.size = data.vsizes().v.back().v;
				} else {
					result.file.content = QByteArray();
					result.file.size = data.vsize().v;
				}
				maxArea = area;
			}
		});
	}
	return result;
}

Photo ParsePhoto(const MTPPhoto &data, const QString &suggestedPath) {
	auto result = Photo();
	data.match([&](const MTPDphoto &data) {
		result.id = data.vid().v;
		result.date = data.vdate().v;
		result.image = ParseMaxImage(data, suggestedPath);
	}, [&](const MTPDphotoEmpty &data) {
		result.id = data.vid().v;
	});
	return result;
}

void ParseAttributes(
		Document &result,
		const MTPVector<MTPDocumentAttribute> &attributes) {
	for (const auto &value : attributes.v) {
		value.match([&](const MTPDdocumentAttributeImageSize &data) {
			result.width = data.vw().v;
			result.height = data.vh().v;
		}, [&](const MTPDdocumentAttributeAnimated &data) {
			result.isAnimated = true;
		}, [&](const MTPDdocumentAttributeSticker &data) {
			result.isSticker = true;
			result.stickerEmoji = ParseString(data.valt());
		}, [&](const MTPDdocumentAttributeVideo &data) {
			if (data.is_round_message()) {
				result.isVideoMessage = true;
			} else {
				result.isVideoFile = true;
			}
			result.width = data.vw().v;
			result.height = data.vh().v;
			result.duration = data.vduration().v;
		}, [&](const MTPDdocumentAttributeAudio &data) {
			if (data.is_voice()) {
				result.isVoiceMessage = true;
			} else {
				result.isAudioFile = true;
			}
			if (const auto performer = data.vperformer()) {
				result.songPerformer = ParseString(*performer);
			}
			if (const auto title = data.vtitle()) {
				result.songTitle = ParseString(*title);
			}
			result.duration = data.vduration().v;
		}, [&](const MTPDdocumentAttributeFilename &data) {
			result.name = ParseString(data.vfile_name());
		}, [&](const MTPDdocumentAttributeHasStickers &data) {
		});
	}
}

QString ComputeDocumentName(
		ParseMediaContext &context,
		const Document &data,
		TimeId date) {
	if (!data.name.isEmpty()) {
		return QString::fromUtf8(data.name);
	}
	const auto mimeString = QString::fromUtf8(data.mime);
	const auto mimeType = Core::MimeTypeForName(mimeString);
	const auto hasMimeType = [&](QLatin1String mime) {
		return !mimeString.compare(mime, Qt::CaseInsensitive);
	};
	const auto patterns = mimeType.globPatterns();
	const auto pattern = patterns.isEmpty() ? QString() : patterns.front();
	if (data.isVoiceMessage) {
		const auto isMP3 = hasMimeType(qstr("audio/mp3"));
		return qsl("audio_")
			+ QString::number(++context.audios)
			+ PrepareFileNameDatePart(date)
			+ (isMP3 ? qsl(".mp3") : qsl(".ogg"));
	} else if (data.isVideoFile) {
		const auto extension = pattern.isEmpty()
			? qsl(".mov")
			: QString(pattern).replace('*', QString());
		return qsl("video_")
			+ QString::number(++context.videos)
			+ PrepareFileNameDatePart(date)
			+ extension;
	} else {
		const auto extension = pattern.isEmpty()
			? qsl(".unknown")
			: QString(pattern).replace('*', QString());
		return qsl("file_")
			+ QString::number(++context.files)
			+ PrepareFileNameDatePart(date)
			+ extension;
	}
}

QString DocumentFolder(const Document &data) {
	if (data.isVideoFile) {
		return "video_files";
	} else if (data.isAnimated) {
		return "animations";
	} else if (data.isSticker) {
		return "stickers";
	} else if (data.isVoiceMessage) {
		return "voice_messages";
	} else if (data.isVideoMessage) {
		return "round_video_messages";
	}
	return "files";
}

Image ParseDocumentThumb(
		const MTPDdocument &document,
		const QString &documentPath) {
	const auto thumbs = document.vthumbs();
	if (!thumbs) {
		return Image();
	}

	const auto area = [](const MTPPhotoSize &size) {
		return size.match([](const MTPDphotoSizeEmpty &) {
			return 0;
		}, [](const MTPDphotoStrippedSize &) {
			return 0;
		}, [](const MTPDphotoPathSize &) {
			return 0;
		}, [](const auto &data) {
			return data.vw().v * data.vh().v;
		});
	};
	const auto &list = thumbs->v;
	const auto i = ranges::max_element(list, ranges::less(), area);
	if (i == list.end()) {
		return Image();
	}
	return i->match([](const MTPDphotoSizeEmpty &) {
		return Image();
	}, [](const MTPDphotoStrippedSize &) {
		return Image();
	}, [](const MTPDphotoPathSize &) {
		return Image();
	}, [&](const auto &data) {
		auto result = Image();
		result.width = data.vw().v;
		result.height = data.vh().v;
		result.file.location = FileLocation{
			document.vdc_id().v,
			MTP_inputDocumentFileLocation(
				document.vid(),
				document.vaccess_hash(),
				document.vfile_reference(),
				data.vtype()) };
		if constexpr (MTPDphotoCachedSize::Is<decltype(data)>()) {
			result.file.content = data.vbytes().v;
			result.file.size = result.file.content.size();
		} else if constexpr (MTPDphotoSizeProgressive::Is<decltype(data)>()) {
			if (data.vsizes().v.isEmpty()) {
				return Image();
			}
			result.file.content = QByteArray();
			result.file.size = data.vsizes().v.back().v;
		} else {
			result.file.content = QByteArray();
			result.file.size = data.vsize().v;
		}
		result.file.suggestedPath = documentPath + "_thumb.jpg";
		return result;
	});
}

Document ParseDocument(
		ParseMediaContext &context,
		const MTPDocument &data,
		const QString &suggestedFolder,
		TimeId date) {
	auto result = Document();
	data.match([&](const MTPDdocument &data) {
		result.id = data.vid().v;
		result.date = data.vdate().v;
		result.mime = ParseString(data.vmime_type());
		ParseAttributes(result, data.vattributes());

		result.file.size = data.vsize().v;
		result.file.location.dcId = data.vdc_id().v;
		result.file.location.data = MTP_inputDocumentFileLocation(
			data.vid(),
			data.vaccess_hash(),
			data.vfile_reference(),
			MTP_string());
		result.file.suggestedPath = suggestedFolder
			+ DocumentFolder(result) + '/'
			+ base::FileNameFromUserString(
				ComputeDocumentName(context, result, date));

		result.thumb = ParseDocumentThumb(
			data,
			result.file.suggestedPath);
	}, [&](const MTPDdocumentEmpty &data) {
		result.id = data.vid().v;
	});
	return result;
}

SharedContact ParseSharedContact(
		ParseMediaContext &context,
		const MTPDmessageMediaContact &data,
		const QString &suggestedFolder) {
	auto result = SharedContact();
	result.info.userId = data.vuser_id().v;
	result.info.firstName = ParseString(data.vfirst_name());
	result.info.lastName = ParseString(data.vlast_name());
	result.info.phoneNumber = ParseString(data.vphone_number());
	if (!data.vvcard().v.isEmpty()) {
		result.vcard.content = data.vvcard().v;
		result.vcard.size = data.vvcard().v.size();
		result.vcard.suggestedPath = suggestedFolder
			+ "contacts/contact_"
			+ QString::number(++context.contacts)
			+ ".vcard";
	}
	return result;
}

GeoPoint ParseGeoPoint(const MTPGeoPoint &data) {
	auto result = GeoPoint();
	data.match([&](const MTPDgeoPoint &data) {
		result.latitude = data.vlat().v;
		result.longitude = data.vlong().v;
		result.valid = true;
	}, [](const MTPDgeoPointEmpty &data) {});
	return result;
}

Venue ParseVenue(const MTPDmessageMediaVenue &data) {
	auto result = Venue();
	result.point = ParseGeoPoint(data.vgeo());
	result.title = ParseString(data.vtitle());
	result.address = ParseString(data.vaddress());
	return result;
}

Game ParseGame(const MTPGame &data, int32 botId) {
	return data.match([&](const MTPDgame &data) {
		auto result = Game();
		result.id = data.vid().v;
		result.title = ParseString(data.vtitle());
		result.description = ParseString(data.vdescription());
		result.shortName = ParseString(data.vshort_name());
		result.botId = botId;
		return result;
	});
}

Invoice ParseInvoice(const MTPDmessageMediaInvoice &data) {
	auto result = Invoice();
	result.title = ParseString(data.vtitle());
	result.description = ParseString(data.vdescription());
	result.currency = ParseString(data.vcurrency());
	result.amount = data.vtotal_amount().v;
	if (const auto receiptMsgId = data.vreceipt_msg_id()) {
		result.receiptMsgId = receiptMsgId->v;
	}
	return result;
}

Poll ParsePoll(const MTPDmessageMediaPoll &data) {
	auto result = Poll();
	data.vpoll().match([&](const MTPDpoll &poll) {
		result.id = poll.vid().v;
		result.question = ParseString(poll.vquestion());
		result.closed = poll.is_closed();
		result.answers = ranges::views::all(
			poll.vanswers().v
		) | ranges::views::transform([](const MTPPollAnswer &answer) {
			return answer.match([](const MTPDpollAnswer &answer) {
				auto result = Poll::Answer();
				result.text = ParseString(answer.vtext());
				result.option = answer.voption().v;
				return result;
			});
		}) | ranges::to_vector;
	});
	data.vresults().match([&](const MTPDpollResults &results) {
		if (const auto totalVotes = results.vtotal_voters()) {
			result.totalVotes = totalVotes->v;
		}
		if (const auto resultsList = results.vresults()) {
			for (const auto &single : resultsList->v) {
				single.match([&](const MTPDpollAnswerVoters &voters) {
					const auto &option = voters.voption().v;
					const auto i = ranges::find(
						result.answers,
						voters.voption().v,
						&Poll::Answer::option);
					if (i == end(result.answers)) {
						return;
					}
					i->votes = voters.vvoters().v;
					if (voters.is_chosen()) {
						i->my = true;
					}
				});
			}
		}
	});
	return result;
}

UserpicsSlice ParseUserpicsSlice(
		const MTPVector<MTPPhoto> &data,
		int baseIndex) {
	const auto &list = data.v;
	auto result = UserpicsSlice();
	result.list.reserve(list.size());
	for (const auto &photo : list) {
		const auto suggestedPath = "profile_pictures/"
			+ PreparePhotoFileName(
				++baseIndex,
				(photo.type() == mtpc_photo
					? photo.c_photo().vdate().v
					: TimeId(0)));
		result.list.push_back(ParsePhoto(photo, suggestedPath));
	}
	return result;
}

std::pair<QString, QSize> WriteImageThumb(
		const QString &basePath,
		const QString &largePath,
		Fn<QSize(QSize)> convertSize,
		std::optional<QByteArray> format,
		std::optional<int> quality,
		const QString &postfix) {
	if (largePath.isEmpty()) {
		return {};
	}
	const auto path = basePath + largePath;
	QImageReader reader(path);
	if (!reader.canRead()) {
		return {};
	}
	const auto size = reader.size();
	if (size.isEmpty()
		|| size.width() >= kMaxImageSize
		|| size.height() >= kMaxImageSize) {
		return {};
	}
	auto image = reader.read();
	if (image.isNull()) {
		return {};
	}
	const auto finalSize = convertSize(image.size());
	if (finalSize.isEmpty()) {
		return {};
	}
	image = std::move(image).scaled(
		finalSize,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	const auto finalFormat = format ? *format : reader.format();
	const auto finalQuality = quality ? *quality : reader.quality();
	const auto lastSlash = largePath.lastIndexOf('/');
	const auto firstDot = largePath.indexOf('.', lastSlash + 1);
	const auto thumb = (firstDot >= 0)
		? largePath.mid(0, firstDot) + postfix + largePath.mid(firstDot)
		: largePath + postfix;
	const auto result = Output::File::PrepareRelativePath(basePath, thumb);
	if (!image.save(
			basePath + result,
			finalFormat.constData(),
			finalQuality)) {
		return {};
	}
	return { result, finalSize };
}

QString WriteImageThumb(
		const QString &basePath,
		const QString &largePath,
		int width,
		int height,
		const QString &postfix) {
	return WriteImageThumb(
		basePath,
		largePath,
		[=](QSize size) { return QSize(width, height); },
		std::nullopt,
		std::nullopt,
		postfix).first;
}

ContactInfo ParseContactInfo(const MTPUser &data) {
	auto result = ContactInfo();
	data.match([&](const MTPDuser &data) {
		result.userId = data.vid().v;
		if (const auto firstName = data.vfirst_name()) {
			result.firstName = ParseString(*firstName);
		}
		if (const auto lastName = data.vlast_name()) {
			result.lastName = ParseString(*lastName);
		}
		if (const auto phone = data.vphone()) {
			result.phoneNumber = ParseString(*phone);
		}
	}, [&](const MTPDuserEmpty &data) {
		result.userId = data.vid().v;
	});
	return result;
}

int ContactColorIndex(const ContactInfo &data) {
	if (data.userId != 0) {
		return PeerColorIndex(data.userId);
	}
	return PeerColorIndex(StringBarePeerId(data.phoneNumber));
}

User ParseUser(const MTPUser &data) {
	auto result = User();
	result.info = ParseContactInfo(data);
	data.match([&](const MTPDuser &data) {
		result.id = data.vid().v;
		if (const auto username = data.vusername()) {
			result.username = ParseString(*username);
		}
		if (data.vbot_info_version()) {
			result.isBot = true;
		}
		if (data.is_self()) {
			result.isSelf = true;
		}
		result.input = MTP_inputUser(
			data.vid(),
			MTP_long(data.vaccess_hash().value_or_empty()));
	}, [&](const MTPDuserEmpty &data) {
		result.input = MTP_inputUser(data.vid(), MTP_long(0));
	});
	return result;
}

std::map<int32, User> ParseUsersList(const MTPVector<MTPUser> &data) {
	auto result = std::map<int32, User>();
	for (const auto &user : data.v) {
		auto parsed = ParseUser(user);
		result.emplace(parsed.info.userId, std::move(parsed));
	}
	return result;
}

Chat ParseChat(const MTPChat &data) {
	auto result = Chat();
	data.match([&](const MTPDchat &data) {
		result.id = data.vid().v;
		result.title = ParseString(data.vtitle());
		result.input = MTP_inputPeerChat(MTP_int(result.id));
		if (const auto migratedTo = data.vmigrated_to()) {
			result.migratedToChannelId = migratedTo->match(
			[](const MTPDinputChannel &data) {
				return data.vchannel_id().v;
			}, [](auto&&) { return 0; });
		}
	}, [&](const MTPDchatEmpty &data) {
		result.id = data.vid().v;
		result.input = MTP_inputPeerChat(MTP_int(result.id));
	}, [&](const MTPDchatForbidden &data) {
		result.id = data.vid().v;
		result.title = ParseString(data.vtitle());
		result.input = MTP_inputPeerChat(MTP_int(result.id));
	}, [&](const MTPDchannel &data) {
		result.id = data.vid().v;
		result.isBroadcast = data.is_broadcast();
		result.isSupergroup = data.is_megagroup();
		result.title = ParseString(data.vtitle());
		if (const auto username = data.vusername()) {
			result.username = ParseString(*username);
		}
		result.input = MTP_inputPeerChannel(
			MTP_int(result.id),
			MTP_long(data.vaccess_hash().value_or_empty()));
	}, [&](const MTPDchannelForbidden &data) {
		result.id = data.vid().v;
		result.isBroadcast = data.is_broadcast();
		result.isSupergroup = data.is_megagroup();
		result.title = ParseString(data.vtitle());
		result.input = MTP_inputPeerChannel(
			MTP_int(result.id),
			data.vaccess_hash());
	});
	return result;
}

std::map<int32, Chat> ParseChatsList(const MTPVector<MTPChat> &data) {
	auto result = std::map<int32, Chat>();
	for (const auto &chat : data.v) {
		auto parsed = ParseChat(chat);
		result.emplace(parsed.id, std::move(parsed));
	}
	return result;
}

Utf8String ContactInfo::name() const {
	return firstName.isEmpty()
		? (lastName.isEmpty()
			? Utf8String()
			: lastName)
		: (lastName.isEmpty()
			? firstName
			: firstName + ' ' + lastName);

}

Utf8String User::name() const {
	return info.name();
}

const User *Peer::user() const {
	return std::get_if<User>(&data);
}
const Chat *Peer::chat() const {
	return std::get_if<Chat>(&data);
}

PeerId Peer::id() const {
	if (const auto user = this->user()) {
		return UserPeerId(user->info.userId);
	} else if (const auto chat = this->chat()) {
		return ChatPeerId(chat->id);
	}
	Unexpected("Variant in Peer::id.");
}

Utf8String Peer::name() const {
	if (const auto user = this->user()) {
		return user->name();
	} else if (const auto chat = this->chat()) {
		return chat->title;
	}
	Unexpected("Variant in Peer::id.");
}

MTPInputPeer Peer::input() const {
	if (const auto user = this->user()) {
		if (user->input.type() == mtpc_inputUser) {
			const auto &input = user->input.c_inputUser();
			return MTP_inputPeerUser(input.vuser_id(), input.vaccess_hash());
		}
		return MTP_inputPeerEmpty();
	} else if (const auto chat = this->chat()) {
		return chat->input;
	}
	Unexpected("Variant in Peer::id.");
}

std::map<PeerId, Peer> ParsePeersLists(
		const MTPVector<MTPUser> &users,
		const MTPVector<MTPChat> &chats) {
	auto result = std::map<PeerId, Peer>();
	for (const auto &user : users.v) {
		auto parsed = ParseUser(user);
		result.emplace(
			UserPeerId(parsed.info.userId),
			Peer{ std::move(parsed) });
	}
	for (const auto &chat : chats.v) {
		auto parsed = ParseChat(chat);
		result.emplace(ChatPeerId(parsed.id), Peer{ std::move(parsed) });
	}
	return result;
}

User EmptyUser(int32 userId) {
	return ParseUser(MTP_userEmpty(MTP_int(userId)));
}

Chat EmptyChat(int32 chatId) {
	return ParseChat(MTP_chatEmpty(MTP_int(chatId)));
}

Peer EmptyPeer(PeerId peerId) {
	if (IsUserPeerId(peerId)) {
		return Peer{ EmptyUser(BarePeerId(peerId)) };
	} else if (IsChatPeerId(peerId)) {
		return Peer{ EmptyChat(BarePeerId(peerId)) };
	}
	Unexpected("PeerId in EmptyPeer.");
}

File &Media::file() {
	return v::match(content, [](Photo &data) -> File& {
		return data.image.file;
	}, [](Document &data) -> File& {
		return data.file;
	}, [](SharedContact &data) -> File& {
		return data.vcard;
	}, [](auto&) -> File& {
		static File result;
		return result;
	});
}

const File &Media::file() const {
	return v::match(content, [](const Photo &data) -> const File& {
		return data.image.file;
	}, [](const Document &data) -> const File& {
		return data.file;
	}, [](const SharedContact &data) -> const File& {
		return data.vcard;
	}, [](const auto &) -> const File& {
		static const File result;
		return result;
	});
}

Image &Media::thumb() {
	return v::match(content, [](Document &data) -> Image& {
		return data.thumb;
	}, [](auto&) -> Image& {
		static Image result;
		return result;
	});
}

const Image &Media::thumb() const {
	return v::match(content, [](const Document &data) -> const Image& {
		return data.thumb;
	}, [](const auto &) -> const Image& {
		static const Image result;
		return result;
	});
}

Media ParseMedia(
		ParseMediaContext &context,
		const MTPMessageMedia &data,
		const QString &folder,
		TimeId date) {
	Expects(folder.isEmpty() || folder.endsWith(QChar('/')));

	auto result = Media();
	data.match([&](const MTPDmessageMediaPhoto &data) {
		const auto photo = data.vphoto();
		auto content = photo
			? ParsePhoto(
				*photo,
				folder
				+ "photos/"
				+ PreparePhotoFileName(++context.photos, date))
			: Photo();
		if (const auto ttl = data.vttl_seconds()) {
			result.ttl = ttl->v;
			content.image.file = File();
		}
		result.content = content;
	}, [&](const MTPDmessageMediaGeo &data) {
		result.content = ParseGeoPoint(data.vgeo());
	}, [&](const MTPDmessageMediaContact &data) {
		result.content = ParseSharedContact(context, data, folder);
	}, [&](const MTPDmessageMediaUnsupported &data) {
		result.content = UnsupportedMedia();
	}, [&](const MTPDmessageMediaDocument &data) {
		const auto document = data.vdocument();
		auto content = document
			? ParseDocument(context, *document, folder, date)
			: Document();
		if (const auto ttl = data.vttl_seconds()) {
			result.ttl = ttl->v;
			content.file = File();
		}
		result.content = content;
	}, [&](const MTPDmessageMediaWebPage &data) {
		// Ignore web pages.
	}, [&](const MTPDmessageMediaVenue &data) {
		result.content = ParseVenue(data);
	}, [&](const MTPDmessageMediaGame &data) {
		result.content = ParseGame(data.vgame(), context.botId);
	}, [&](const MTPDmessageMediaInvoice &data) {
		result.content = ParseInvoice(data);
	}, [&](const MTPDmessageMediaGeoLive &data) {
		result.content = ParseGeoPoint(data.vgeo());
		result.ttl = data.vperiod().v;
	}, [&](const MTPDmessageMediaPoll &data) {
		result.content = ParsePoll(data);
	}, [](const MTPDmessageMediaDice &data) {
		// #TODO dice
	}, [](const MTPDmessageMediaEmpty &data) {});
	return result;
}

ServiceAction ParseServiceAction(
		ParseMediaContext &context,
		const MTPMessageAction &data,
		const QString &mediaFolder,
		TimeId date) {
	auto result = ServiceAction();
	data.match([&](const MTPDmessageActionChatCreate &data) {
		auto content = ActionChatCreate();
		content.title = ParseString(data.vtitle());
		content.userIds.reserve(data.vusers().v.size());
		for (const auto &userId : data.vusers().v) {
			content.userIds.push_back(userId.v);
		}
		result.content = content;
	}, [&](const MTPDmessageActionChatEditTitle &data) {
		auto content = ActionChatEditTitle();
		content.title = ParseString(data.vtitle());
		result.content = content;
	}, [&](const MTPDmessageActionChatEditPhoto &data) {
		auto content = ActionChatEditPhoto();
		content.photo = ParsePhoto(
			data.vphoto(),
			mediaFolder
			+ "photos/"
			+ PreparePhotoFileName(++context.photos, date));
		result.content = content;
	}, [&](const MTPDmessageActionChatDeletePhoto &data) {
		result.content = ActionChatDeletePhoto();
	}, [&](const MTPDmessageActionChatAddUser &data) {
		auto content = ActionChatAddUser();
		content.userIds.reserve(data.vusers().v.size());
		for (const auto &user : data.vusers().v) {
			content.userIds.push_back(user.v);
		}
		result.content = content;
	}, [&](const MTPDmessageActionChatDeleteUser &data) {
		auto content = ActionChatDeleteUser();
		content.userId = data.vuser_id().v;
		result.content = content;
	}, [&](const MTPDmessageActionChatJoinedByLink &data) {
		auto content = ActionChatJoinedByLink();
		content.inviterId = data.vinviter_id().v;
		result.content = content;
	}, [&](const MTPDmessageActionChannelCreate &data) {
		auto content = ActionChannelCreate();
		content.title = ParseString(data.vtitle());
		result.content = content;
	}, [&](const MTPDmessageActionChatMigrateTo &data) {
		auto content = ActionChatMigrateTo();
		content.channelId = data.vchannel_id().v;
		result.content = content;
	}, [&](const MTPDmessageActionChannelMigrateFrom &data) {
		auto content = ActionChannelMigrateFrom();
		content.title = ParseString(data.vtitle());
		content.chatId = data.vchat_id().v;
		result.content = content;
	}, [&](const MTPDmessageActionPinMessage &data) {
		result.content = ActionPinMessage();
	}, [&](const MTPDmessageActionHistoryClear &data) {
		result.content = ActionHistoryClear();
	}, [&](const MTPDmessageActionGameScore &data) {
		auto content = ActionGameScore();
		content.gameId = data.vgame_id().v;
		content.score = data.vscore().v;
		result.content = content;
	}, [&](const MTPDmessageActionPaymentSentMe &data) {
		// Should not be in user inbox.
	}, [&](const MTPDmessageActionPaymentSent &data) {
		auto content = ActionPaymentSent();
		content.currency = ParseString(data.vcurrency());
		content.amount = data.vtotal_amount().v;
		result.content = content;
	}, [&](const MTPDmessageActionPhoneCall &data) {
		auto content = ActionPhoneCall();
		if (const auto duration = data.vduration()) {
			content.duration = duration->v;
		}
		if (const auto reason = data.vreason()) {
			using Reason = ActionPhoneCall::DiscardReason;
			content.discardReason = reason->match(
			[](const MTPDphoneCallDiscardReasonMissed &data) {
				return Reason::Missed;
			}, [](const MTPDphoneCallDiscardReasonDisconnect &data) {
				return Reason::Disconnect;
			}, [](const MTPDphoneCallDiscardReasonHangup &data) {
				return Reason::Hangup;
			}, [](const MTPDphoneCallDiscardReasonBusy &data) {
				return Reason::Busy;
			});
		}
		result.content = content;
	}, [&](const MTPDmessageActionScreenshotTaken &data) {
		result.content = ActionScreenshotTaken();
	}, [&](const MTPDmessageActionCustomAction &data) {
		auto content = ActionCustomAction();
		content.message = ParseString(data.vmessage());
		result.content = content;
	}, [&](const MTPDmessageActionBotAllowed &data) {
		auto content = ActionBotAllowed();
		content.domain = ParseString(data.vdomain());
		result.content = content;
	}, [&](const MTPDmessageActionSecureValuesSentMe &data) {
		// Should not be in user inbox.
	}, [&](const MTPDmessageActionSecureValuesSent &data) {
		auto content = ActionSecureValuesSent();
		content.types.reserve(data.vtypes().v.size());
		using Type = ActionSecureValuesSent::Type;
		for (const auto &type : data.vtypes().v) {
			content.types.push_back(type.match(
			[](const MTPDsecureValueTypePersonalDetails &data) {
				return Type::PersonalDetails;
			}, [](const MTPDsecureValueTypePassport &data) {
				return Type::Passport;
			}, [](const MTPDsecureValueTypeDriverLicense &data) {
				return Type::DriverLicense;
			}, [](const MTPDsecureValueTypeIdentityCard &data) {
				return Type::IdentityCard;
			}, [](const MTPDsecureValueTypeInternalPassport &data) {
				return Type::InternalPassport;
			}, [](const MTPDsecureValueTypeAddress &data) {
				return Type::Address;
			}, [](const MTPDsecureValueTypeUtilityBill &data) {
				return Type::UtilityBill;
			}, [](const MTPDsecureValueTypeBankStatement &data) {
				return Type::BankStatement;
			}, [](const MTPDsecureValueTypeRentalAgreement &data) {
				return Type::RentalAgreement;
			}, [](const MTPDsecureValueTypePassportRegistration &data) {
				return Type::PassportRegistration;
			}, [](const MTPDsecureValueTypeTemporaryRegistration &data) {
				return Type::TemporaryRegistration;
			}, [](const MTPDsecureValueTypePhone &data) {
				return Type::Phone;
			}, [](const MTPDsecureValueTypeEmail &data) {
				return Type::Email;
			}));
		}
		result.content = content;
	}, [&](const MTPDmessageActionContactSignUp &data) {
		result.content = ActionContactSignUp();
	}, [&](const MTPDmessageActionGeoProximityReached &data) {
		auto content = ActionGeoProximityReached();
		content.fromId = ParsePeerId(data.vfrom_id());
		if (content.fromId == context.selfPeerId) {
			content.fromSelf = true;
		}
		content.toId = ParsePeerId(data.vto_id());
		if (content.toId == context.selfPeerId) {
			content.toSelf = true;
		}
		content.distance = data.vdistance().v;
		result.content = content;
	}, [&](const MTPDmessageActionGroupCall &data) {
		auto content = ActionGroupCall();
		if (const auto duration = data.vduration()) {
			content.duration = duration->v;
		}
		result.content = content;
	}, [&](const MTPDmessageActionInviteToGroupCall &data) {
		auto content = ActionInviteToGroupCall();
		content.userIds.reserve(data.vusers().v.size());
		for (const auto &user : data.vusers().v) {
			content.userIds.push_back(user.v);
		}
		result.content = content;
	}, [&](const MTPDmessageActionSetMessagesTTL &data) {
		// #TODO ttl
	}, [](const MTPDmessageActionEmpty &data) {});
	return result;
}

File &Message::file() {
	const auto service = &action.content;
	if (const auto photo = std::get_if<ActionChatEditPhoto>(service)) {
		return photo->photo.image.file;
	}
	return media.file();
}

const File &Message::file() const {
	const auto service = &action.content;
	if (const auto photo = std::get_if<ActionChatEditPhoto>(service)) {
		return photo->photo.image.file;
	}
	return media.file();
}

Image &Message::thumb() {
	return media.thumb();
}

const Image &Message::thumb() const {
	return media.thumb();
}

Message ParseMessage(
		ParseMediaContext &context,
		const MTPMessage &data,
		const QString &mediaFolder) {
	auto result = Message();
	data.match([&](const auto &data) {
		result.id = data.vid().v;
		if constexpr (!MTPDmessageEmpty::Is<decltype(data)>()) {
			result.date = data.vdate().v;
			result.out = data.is_out();
			result.selfId = context.selfPeerId;
			result.peerId = ParsePeerId(data.vpeer_id());
			const auto fromId = data.vfrom_id();
			if (IsChatPeerId(result.peerId)) {
				result.chatId = BarePeerId(result.peerId);
			}
			if (fromId) {
				result.fromId = ParsePeerId(*fromId);
			} else {
				result.fromId = result.peerId;
			}
			if (const auto replyTo = data.vreply_to()) {
				replyTo->match([&](const MTPDmessageReplyHeader &data) {
					result.replyToMsgId = data.vreply_to_msg_id().v;
					result.replyToPeerId = data.vreply_to_peer_id()
						? ParsePeerId(*data.vreply_to_peer_id())
						: 0;
					if (result.replyToPeerId == result.peerId) {
						result.replyToPeerId = 0;
					}
				});
			}
		}
	});
	data.match([&](const MTPDmessage &data) {
		if (const auto editDate = data.vedit_date()) {
			result.edited = editDate->v;
		}
		if (const auto fwdFrom = data.vfwd_from()) {
			result.forwardedFromId = fwdFrom->match(
			[](const MTPDmessageFwdHeader &data) {
				return data.vfrom_id()
					? ParsePeerId(*data.vfrom_id())
					: PeerId(0);
			});
			result.forwardedFromName = fwdFrom->match(
			[](const MTPDmessageFwdHeader &data) {
				if (const auto fromName = data.vfrom_name()) {
					return fromName->v;
				}
				return QByteArray();
			});
			result.forwardedDate = fwdFrom->match(
			[](const MTPDmessageFwdHeader &data) {
				return data.vdate().v;
			});
			result.savedFromChatId = fwdFrom->match(
			[](const MTPDmessageFwdHeader &data) {
				if (const auto savedFromPeer = data.vsaved_from_peer()) {
					return ParsePeerId(*savedFromPeer);
				}
				return PeerId(0);
			});
			result.forwarded = result.forwardedFromId
				|| !result.forwardedFromName.isEmpty();
			result.showForwardedAsOriginal = result.forwarded
				&& result.savedFromChatId;
		}
		if (const auto postAuthor = data.vpost_author()) {
			result.signature = ParseString(*postAuthor);
		}
		if (const auto replyTo = data.vreply_to()) {
			replyTo->match([&](const MTPDmessageReplyHeader &data) {
				result.replyToMsgId = data.vreply_to_msg_id().v;
				result.replyToPeerId = data.vreply_to_peer_id()
					? ParsePeerId(*data.vreply_to_peer_id())
					: PeerId(0);
			});
		}
		if (const auto viaBotId = data.vvia_bot_id()) {
			result.viaBotId = viaBotId->v;
		}
		if (const auto media = data.vmedia()) {
			context.botId = (result.viaBotId
				? result.viaBotId
				: IsUserPeerId(result.forwardedFromId)
				? BarePeerId(result.forwardedFromId)
				: result.fromId);
			result.media = ParseMedia(
				context,
				*media,
				mediaFolder,
				result.date);
			if (result.media.ttl && !data.is_out()) {
				result.media.file() = File();
				result.media.thumb().file = File();
			}
			context.botId = 0;
		}
		result.text = ParseText(
			data.vmessage(),
			data.ventities().value_or_empty());
	}, [&](const MTPDmessageService &data) {
		result.action = ParseServiceAction(
			context,
			data.vaction(),
			mediaFolder,
			result.date);
	}, [&](const MTPDmessageEmpty &data) {
		result.id = data.vid().v;
	});
	return result;
}

std::map<uint64, Message> ParseMessagesList(
		PeerId selfId,
		const MTPVector<MTPMessage> &data,
		const QString &mediaFolder) {
	auto context = ParseMediaContext{ .selfPeerId = selfId };
	auto result = std::map<uint64, Message>();
	for (const auto &message : data.v) {
		auto parsed = ParseMessage(context, message, mediaFolder);
		const auto shift = uint64(uint32(parsed.chatId)) << 32;
		result.emplace(shift | uint32(parsed.id), std::move(parsed));
	}
	return result;
}

PersonalInfo ParsePersonalInfo(const MTPUserFull &data) {
	Expects(data.type() == mtpc_userFull);

	const auto &fields = data.c_userFull();
	auto result = PersonalInfo();
	result.user = ParseUser(fields.vuser());
	if (const auto about = fields.vabout()) {
		result.bio = ParseString(*about);
	}
	return result;
}

ContactsList ParseContactsList(const MTPcontacts_Contacts &data) {
	Expects(data.type() == mtpc_contacts_contacts);

	auto result = ContactsList();
	const auto &contacts = data.c_contacts_contacts();
	const auto map = ParseUsersList(contacts.vusers());
	result.list.reserve(contacts.vcontacts().v.size());
	for (const auto &contact : contacts.vcontacts().v) {
		const auto userId = contact.c_contact().vuser_id().v;
		if (const auto i = map.find(userId); i != end(map)) {
			result.list.push_back(i->second.info);
		} else {
			result.list.push_back(EmptyUser(userId).info);
		}
	}
	return result;
}

ContactsList ParseContactsList(const MTPVector<MTPSavedContact> &data) {
	auto result = ContactsList();
	result.list.reserve(data.v.size());
	for (const auto &contact : data.v) {
		auto info = contact.match([](const MTPDsavedPhoneContact &data) {
			auto info = ContactInfo();
			info.firstName = ParseString(data.vfirst_name());
			info.lastName = ParseString(data.vlast_name());
			info.phoneNumber = ParseString(data.vphone());
			info.date = data.vdate().v;
			return info;
		});
		result.list.push_back(std::move(info));
	}
	return result;
}

std::vector<int> SortedContactsIndices(const ContactsList &data) {
	const auto names = ranges::views::all(
		data.list
	) | ranges::views::transform([](const Data::ContactInfo &info) {
		return (QString::fromUtf8(info.firstName)
			+ ' '
			+ QString::fromUtf8(info.lastName)).toLower();
	}) | ranges::to_vector;

	auto indices = ranges::views::ints(0, int(data.list.size()))
		| ranges::to_vector;
	ranges::sort(indices, [&](int i, int j) {
		return names[i] < names[j];
	});
	return indices;
}

bool AppendTopPeers(ContactsList &to, const MTPcontacts_TopPeers &data) {
	return data.match([](const MTPDcontacts_topPeersNotModified &data) {
		return false;
	}, [](const MTPDcontacts_topPeersDisabled &data) {
		return true;
	}, [&](const MTPDcontacts_topPeers &data) {
		const auto peers = ParsePeersLists(data.vusers(), data.vchats());
		const auto append = [&](
				std::vector<TopPeer> &to,
				const MTPVector<MTPTopPeer> &list) {
			for (const auto &topPeer : list.v) {
				to.push_back(topPeer.match([&](const MTPDtopPeer &data) {
					const auto peerId = ParsePeerId(data.vpeer());
					auto peer = [&] {
						const auto i = peers.find(peerId);
						return (i != peers.end())
							? i->second
							: EmptyPeer(peerId);
					}();
					return TopPeer{
						Peer{ std::move(peer) },
						data.vrating().v
					};
				}));
			}
		};
		for (const auto &list : data.vcategories().v) {
			const auto appended = list.match(
			[&](const MTPDtopPeerCategoryPeers &data) {
				const auto category = data.vcategory().type();
				if (category == mtpc_topPeerCategoryCorrespondents) {
					append(to.correspondents, data.vpeers());
					return true;
				} else if (category == mtpc_topPeerCategoryBotsInline) {
					append(to.inlineBots, data.vpeers());
					return true;
				} else if (category == mtpc_topPeerCategoryPhoneCalls) {
					append(to.phoneCalls, data.vpeers());
					return true;
				} else {
					return false;
				}
			});
			if (!appended) {
				return false;
			}
		}
		return true;
	});
}

Session ParseSession(const MTPAuthorization &data) {
	return data.match([&](const MTPDauthorization &data) {
		auto result = Session();
		result.applicationId = data.vapi_id().v;
		result.platform = ParseString(data.vplatform());
		result.deviceModel = ParseString(data.vdevice_model());
		result.systemVersion = ParseString(data.vsystem_version());
		result.applicationName = ParseString(data.vapp_name());
		result.applicationVersion = ParseString(data.vapp_version());
		result.created = data.vdate_created().v;
		result.lastActive = data.vdate_active().v;
		result.ip = ParseString(data.vip());
		result.country = ParseString(data.vcountry());
		result.region = ParseString(data.vregion());
		return result;
	});
}

SessionsList ParseSessionsList(const MTPaccount_Authorizations &data) {
	return data.match([](const MTPDaccount_authorizations &data) {
		auto result = SessionsList();
		const auto &list = data.vauthorizations().v;
		result.list.reserve(list.size());
		for (const auto &session : list) {
			result.list.push_back(ParseSession(session));
		}
		return result;
	});
}

WebSession ParseWebSession(
		const MTPWebAuthorization &data,
		const std::map<int32, User> &users) {
	return data.match([&](const MTPDwebAuthorization &data) {
		auto result = WebSession();
		const auto i = users.find(data.vbot_id().v);
		if (i != users.end() && i->second.isBot) {
			result.botUsername = i->second.username;
		}
		result.domain = ParseString(data.vdomain());
		result.platform = ParseString(data.vplatform());
		result.browser = ParseString(data.vbrowser());
		result.created = data.vdate_created().v;
		result.lastActive = data.vdate_active().v;
		result.ip = ParseString(data.vip());
		result.region = ParseString(data.vregion());
		return result;
	});
}

SessionsList ParseWebSessionsList(
		const MTPaccount_WebAuthorizations &data) {
	return data.match([&](const MTPDaccount_webAuthorizations &data) {
		auto result = SessionsList();
		const auto users = ParseUsersList(data.vusers());
		const auto &list = data.vauthorizations().v;
		result.webList.reserve(list.size());
		for (const auto &session : list) {
			result.webList.push_back(ParseWebSession(session, users));
		}
		return result;
	});
}

DialogInfo *DialogsInfo::item(int index) {
	const auto chatsCount = chats.size();
	return (index < 0)
		? nullptr
		: (index < chatsCount)
		? &chats[index]
		: (index - chatsCount < left.size())
		? &left[index - chatsCount]
		: nullptr;
}

const DialogInfo *DialogsInfo::item(int index) const {
	const auto chatsCount = chats.size();
	return (index < 0)
		? nullptr
		: (index < chatsCount)
		? &chats[index]
		: (index - chatsCount < left.size())
		? &left[index - chatsCount]
		: nullptr;
}

DialogInfo::Type DialogTypeFromChat(const Chat &chat) {
	using Type = DialogInfo::Type;
	return chat.username.isEmpty()
		? (chat.isBroadcast
			? Type::PrivateChannel
			: chat.isSupergroup
			? Type::PrivateSupergroup
			: Type::PrivateGroup)
		: (chat.isBroadcast
			? Type::PublicChannel
			: Type::PublicSupergroup);
}

DialogInfo::Type DialogTypeFromUser(const User &user) {
	return user.isSelf
		? DialogInfo::Type::Self
		: user.isReplies
		? DialogInfo::Type::Replies
		: user.isBot
		? DialogInfo::Type::Bot
		: DialogInfo::Type::Personal;
}

DialogsInfo ParseDialogsInfo(const MTPmessages_Dialogs &data) {
	auto result = DialogsInfo();
	const auto folder = QString();
	data.match([](const MTPDmessages_dialogsNotModified &data) {
		Unexpected("dialogsNotModified in ParseDialogsInfo.");
	}, [&](const auto &data) { // MTPDmessages_dialogs &data) {
		const auto peers = ParsePeersLists(data.vusers(), data.vchats());
		const auto messages = ParseMessagesList(
			PeerId(0), // selfId, not used, we want only messages dates here.
			data.vmessages(),
			folder);
		result.chats.reserve(result.chats.size() + data.vdialogs().v.size());
		for (const auto &dialog : data.vdialogs().v) {
			if (dialog.type() != mtpc_dialog) {
				LOG(("API Error: Unexpected dialog type in chats export."));
				continue;
			}
			const auto &fields = dialog.c_dialog();

			auto info = DialogInfo();
			info.peerId = ParsePeerId(fields.vpeer());
			const auto peerIt = peers.find(info.peerId);
			if (peerIt != end(peers)) {
				const auto &peer = peerIt->second;
				info.type = peer.user()
					? DialogTypeFromUser(*peer.user())
					: DialogTypeFromChat(*peer.chat());
				info.name = peer.user()
					? peer.user()->info.firstName
					: peer.name();
				info.lastName = peer.user()
					? peer.user()->info.lastName
					: Utf8String();
				info.input = peer.input();
				info.migratedToChannelId = peer.chat()
					? peer.chat()->migratedToChannelId
					: 0;
			}
			info.topMessageId = fields.vtop_message().v;
			const auto shift = IsChatPeerId(info.peerId)
				? (uint64(uint32(BarePeerId(info.peerId))) << 32)
				: 0;
			const auto messageIt = messages.find(
				shift | uint32(info.topMessageId));
			if (messageIt != end(messages)) {
				const auto &message = messageIt->second;
				info.topMessageDate = message.date;
			}
			result.chats.push_back(std::move(info));
		}
	});
	return result;
}

DialogInfo DialogInfoFromUser(const User &data) {
	auto result = DialogInfo();
	result.input = (Peer{ data }).input();
	result.name = data.info.firstName;
	result.lastName = data.info.lastName;
	result.peerId = UserPeerId(data.info.userId);
	result.topMessageDate = 0;
	result.topMessageId = 0;
	result.type = DialogTypeFromUser(data);
	result.isLeftChannel = false;
	return result;
}

DialogInfo DialogInfoFromChat(const Chat &data) {
	auto result = DialogInfo();
	result.input = data.input;
	result.name = data.title;
	result.peerId = ChatPeerId(data.id);
	result.topMessageDate = 0;
	result.topMessageId = 0;
	result.type = DialogTypeFromChat(data);
	result.migratedToChannelId = data.migratedToChannelId;
	return result;
}

DialogsInfo ParseLeftChannelsInfo(const MTPmessages_Chats &data) {
	auto result = DialogsInfo();
	data.match([&](const auto &data) { //MTPDmessages_chats &data) {
		result.left.reserve(data.vchats().v.size());
		for (const auto &single : data.vchats().v) {
			auto info = DialogInfoFromChat(ParseChat(single));
			info.isLeftChannel = true;
			result.left.push_back(std::move(info));
		}
	});
	return result;
}

DialogsInfo ParseDialogsInfo(
		const MTPInputPeer &singlePeer,
		const MTPVector<MTPUser> &data) {
	const auto singleId = singlePeer.match(
	[](const MTPDinputPeerUser &data) {
		return data.vuser_id().v;
	}, [](const MTPDinputPeerSelf &data) {
		return 0;
	}, [](const auto &data) -> int {
		Unexpected("Single peer type in ParseDialogsInfo(users).");
	});
	auto result = DialogsInfo();
	result.chats.reserve(data.v.size());
	for (const auto &single : data.v) {
		const auto userId = single.match([&](const auto &data) {
			return data.vid().v;
		});
		if (userId != singleId
			&& (singleId != 0
				|| single.type() != mtpc_user
				|| !single.c_user().is_self())) {
			continue;
		}
		auto info = DialogInfoFromUser(ParseUser(single));
		result.chats.push_back(std::move(info));
	}
	return result;
}

DialogsInfo ParseDialogsInfo(
		const MTPInputPeer &singlePeer,
		const MTPmessages_Chats &data) {
	const auto singleId = singlePeer.match(
	[](const MTPDinputPeerChat &data) {
		return data.vchat_id().v;
	}, [](const MTPDinputPeerChannel &data) {
		return data.vchannel_id().v;
	}, [](const auto &data) -> int {
		Unexpected("Single peer type in ParseDialogsInfo(chats).");
	});
	auto result = DialogsInfo();
	data.match([&](const auto &data) { //MTPDmessages_chats &data) {
		result.chats.reserve(data.vchats().v.size());
		for (const auto &single : data.vchats().v) {
			const auto chatId = single.match([&](const auto &data) {
				return data.vid().v;
			});
			if (chatId != singleId) {
				continue;
			}
			const auto chat = ParseChat(single);
			auto info = DialogInfoFromChat(ParseChat(single));
			info.isLeftChannel = false;
			result.chats.push_back(std::move(info));
		}
	});
	return result;
}

bool AddMigrateFromSlice(
		DialogInfo &to,
		const DialogInfo &from,
		int splitIndex,
		int splitsCount) {
	Expects(splitIndex < splitsCount);

	const auto good = to.migratedFromInput.match([](
			const MTPDinputPeerEmpty &) {
		return true;
	}, [&](const MTPDinputPeerChat & data) {
		return (ChatPeerId(data.vchat_id().v) == from.peerId);
	}, [](auto&&) { return false; });
	if (!good) {
		return false;
	}
	Assert(from.splits.size() == from.messagesCountPerSplit.size());
	for (auto i = 0, count = int(from.splits.size()); i != count; ++i) {
		Assert(from.splits[i] < splitsCount);
		to.splits.push_back(from.splits[i] - splitsCount);
		to.messagesCountPerSplit.push_back(from.messagesCountPerSplit[i]);
	}
	to.migratedFromInput = from.input;
	to.splits.push_back(splitIndex - splitsCount);
	to.messagesCountPerSplit.push_back(0);
	return true;
}

void FinalizeDialogsInfo(DialogsInfo &info, const Settings &settings) {
	auto &chats = info.chats;
	auto &left = info.left;
	const auto fullCount = chats.size() + left.size();
	const auto digits = Data::NumberToString(fullCount - 1).size();
	auto index = 0;
	for (auto &dialog : chats) {
		const auto number = Data::NumberToString(++index, digits, '0');
		dialog.relativePath = settings.onlySinglePeer()
			? QString()
			: "chats/chat_" + QString::fromUtf8(number) + '/';

		using DialogType = DialogInfo::Type;
		using Type = Settings::Type;
		const auto setting = [&] {
			switch (dialog.type) {
			case DialogType::Self:
			case DialogType::Personal: return Type::PersonalChats;
			case DialogType::Bot: return Type::BotChats;
			case DialogType::PrivateGroup:
			case DialogType::PrivateSupergroup: return Type::PrivateGroups;
			case DialogType::PrivateChannel: return Type::PrivateChannels;
			case DialogType::PublicSupergroup: return Type::PublicGroups;
			case DialogType::PublicChannel: return Type::PublicChannels;
			}
			Unexpected("Type in ApiWrap::onlyMyMessages.");
		}();
		dialog.onlyMyMessages = ((settings.fullChats & setting) != setting);

		ranges::sort(dialog.splits);
	}
	for (auto &dialog : left) {
		Assert(!settings.onlySinglePeer());

		const auto number = Data::NumberToString(++index, digits, '0');
		dialog.relativePath = "chats/chat_" + number + '/';
		dialog.onlyMyMessages = true;
	}
}

MessagesSlice ParseMessagesSlice(
		ParseMediaContext &context,
		const MTPVector<MTPMessage> &data,
		const MTPVector<MTPUser> &users,
		const MTPVector<MTPChat> &chats,
		const QString &mediaFolder) {
	const auto &list = data.v;
	auto result = MessagesSlice();
	result.list.reserve(list.size());
	for (auto i = list.size(); i != 0;) {
		const auto &message = list[--i];
		result.list.push_back(ParseMessage(context, message, mediaFolder));
	}
	result.peers = ParsePeersLists(users, chats);
	return result;
}

MessagesSlice AdjustMigrateMessageIds(MessagesSlice slice) {
	for (auto &message : slice.list) {
		message.id += kMigratedMessagesIdShift;
		if (message.replyToMsgId && !message.replyToPeerId) {
			message.replyToMsgId += kMigratedMessagesIdShift;
		}
	}
	return slice;
}

TimeId SingleMessageDate(const MTPmessages_Messages &data) {
	return data.match([&](const MTPDmessages_messagesNotModified &data) {
		return 0;
	}, [&](const auto &data) {
		const auto &list = data.vmessages().v;
		if (list.isEmpty()) {
			return 0;
		}
		return list[0].match([](const MTPDmessageEmpty &data) {
			return 0;
		}, [](const auto &data) {
			return data.vdate().v;
		});
	});
}

bool SingleMessageBefore(
		const MTPmessages_Messages &data,
		TimeId date) {
	const auto single = SingleMessageDate(data);
	return (single > 0 && single < date);
}

bool SingleMessageAfter(
		const MTPmessages_Messages &data,
		TimeId date) {
	const auto single = SingleMessageDate(data);
	return (single > 0 && single > date);
}

bool SkipMessageByDate(const Message &message, const Settings &settings) {
	const auto goodFrom = (settings.singlePeerFrom <= 0)
		|| (settings.singlePeerFrom <= message.date);
	const auto goodTill = (settings.singlePeerTill <= 0)
		|| (message.date < settings.singlePeerTill);
	return !goodFrom || !goodTill;
}

Utf8String FormatPhoneNumber(const Utf8String &phoneNumber) {
	return phoneNumber.isEmpty()
		? Utf8String()
		: App::formatPhone(QString::fromUtf8(phoneNumber)).toUtf8();
}

Utf8String FormatDateTime(
		TimeId date,
		QChar dateSeparator,
		QChar timeSeparator,
		QChar separator) {
	if (!date) {
		return Utf8String();
	}
	const auto value = QDateTime::fromTime_t(date);
	return (QString("%1") + dateSeparator + "%2" + dateSeparator + "%3"
		+ separator + "%4" + timeSeparator + "%5" + timeSeparator + "%6"
	).arg(value.date().day(), 2, 10, QChar('0')
	).arg(value.date().month(), 2, 10, QChar('0')
	).arg(value.date().year()
	).arg(value.time().hour(), 2, 10, QChar('0')
	).arg(value.time().minute(), 2, 10, QChar('0')
	).arg(value.time().second(), 2, 10, QChar('0')
	).toUtf8();
}

Utf8String FormatMoneyAmount(uint64 amount, const Utf8String &currency) {
	return Ui::FillAmountAndCurrency(
		amount,
		QString::fromUtf8(currency)).toUtf8();
}

Utf8String FormatFileSize(int64 size) {
	return Ui::FormatSizeText(size).toUtf8();
}

Utf8String FormatDuration(int64 seconds) {
	return Ui::FormatDurationText(seconds).toUtf8();
}

} // namespace Data
} // namespace Export
