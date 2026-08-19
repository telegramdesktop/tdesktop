/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_api_wrap.h"

#include "export/export_settings.h"
#include "export/data/export_data_types.h"
#include "export/output/export_output_result.h"
#include "export/output/export_output_file.h"
#include "mtproto/mtproto_response.h"
#include "base/bytes.h"
#include "base/options.h"
#include "base/random.h"
#include <set>
#include <deque>

namespace Export {
namespace {

constexpr auto kUserpicsSliceLimit = 100;
constexpr auto kFileChunkSize = 128 * 1024;
constexpr auto kFileRequestsCount = 2;
//constexpr auto kFileNextRequestDelay = crl::time(20);
constexpr auto kChatsSliceLimit = 100;
constexpr auto kMessagesSliceLimit = 100;
constexpr auto kTopPeerSliceLimit = 100;
constexpr auto kFileMaxSize = 4000 * int64(1024 * 1024);
constexpr auto kLocationCacheSize = 100'000;
constexpr auto kMaxEmojiPerRequest = 100;
constexpr auto kStoriesSliceLimit = 100;
constexpr auto kProfileMusicSliceLimit = 100;

struct LocationKey {
	uint64 type;
	uint64 id;

	inline bool operator<(const LocationKey &other) const {
		return std::tie(type, id) < std::tie(other.type, other.id);
	}
};

LocationKey ComputeLocationKey(const Data::FileLocation &value) {
	auto result = LocationKey();
	result.type = value.dcId;
	value.data.match([&](const MTPDinputDocumentFileLocation &data) {
		const auto letter = data.vthumb_size().v.isEmpty()
			? char(0)
			: data.vthumb_size().v[0];
		result.type |= (2ULL << 24);
		result.type |= (uint64(uint32(letter)) << 16);
		result.id = data.vid().v;
	}, [&](const MTPDinputPhotoFileLocation &data) {
		const auto letter = data.vthumb_size().v.isEmpty()
			? char(0)
			: data.vthumb_size().v[0];
		result.type |= (6ULL << 24);
		result.type |= (uint64(uint32(letter)) << 16);
		result.id = data.vid().v;
	}, [&](const MTPDinputTakeoutFileLocation &data) {
		result.type |= (5ULL << 24);
	}, [](const auto &data) {
		Unexpected("File location type in Export::ComputeLocationKey.");
	});
	return result;
}

Settings::Type SettingsFromDialogsType(Data::DialogInfo::Type type) {
	using DialogType = Data::DialogInfo::Type;
	switch (type) {
	case DialogType::Self:
	case DialogType::Personal:
		return Settings::Type::PersonalChats;
	case DialogType::Bot:
		return Settings::Type::BotChats;
	case DialogType::PrivateGroup:
	case DialogType::PrivateSupergroup:
		return Settings::Type::PrivateGroups;
	case DialogType::PublicSupergroup:
		return Settings::Type::PublicGroups;
	case DialogType::PrivateChannel:
		return Settings::Type::PrivateChannels;
	case DialogType::PublicChannel:
		return Settings::Type::PublicChannels;
	}
	return Settings::Type(0);
}

MediaSettings::Type DocumentMediaType(const Data::Document &document) {
	using Type = MediaSettings::Type;
	if (document.isSticker) {
		return Type::Sticker;
	} else if (document.isVideoMessage) {
		return Type::VideoMessage;
	} else if (document.isVoiceMessage) {
		return Type::VoiceMessage;
	} else if (document.isAnimated) {
		return Type::GIF;
	} else if (document.isVideoFile) {
		return Type::Video;
	}
	return Type::File;
}

MediaSettings::Type OrdinaryMediaType(const Data::Media &media) {
	return v::match(media.content, [](const Data::Document &document) {
		return DocumentMediaType(document);
	}, [](const auto &) {
		return MediaSettings::Type::Photo;
	});
}

std::optional<MTPRichMessage> ExtractFullRichMessage(
		const MTPmessages_Messages &result,
		int32 messageId) {
	auto matches = 0;
	auto richMessage = std::optional<MTPRichMessage>();
	result.match([&](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		for (const auto &entry : data.vmessages().v) {
			entry.match([&](const auto &message) {
				if (message.vid().v != messageId) {
					return;
				}
				++matches;
				if constexpr (MTPDmessage::Is<decltype(message)>()) {
					const auto candidate = message.vrich_message();
					if (candidate && !candidate->data().is_part()) {
						richMessage = *candidate;
					}
				}
			});
		}
	});
	if (matches != 1) {
		return std::nullopt;
	}
	return richMessage;
}

std::optional<Data::FileLocation> RefreshRichMessageFileReference(
		const Data::FileLocation &location,
		const Data::RichMessage &message) {
	auto refreshed = location;
	for (const auto &entry : message.photos) {
		if (Data::RefreshFileReference(
				refreshed,
				entry.second.image.file.location)) {
			return refreshed;
		}
	}
	for (const auto &entry : message.documents) {
		const auto &document = entry.second;
		if (Data::RefreshFileReference(
				refreshed,
				document.file.location)) {
			return refreshed;
		} else if (document.thumb.width > 0
			&& Data::RefreshFileReference(
				refreshed,
				document.thumb.file.location)) {
			return refreshed;
		}
	}
	return std::nullopt;
}

template <
	typename TextCallback,
	typename PhotoCallback,
	typename DocumentCallback>
struct RichMessageCallbacks {
	TextCallback &text;
	PhotoCallback &photo;
	DocumentCallback &document;
};

template <typename RichMessage, typename Callbacks>
void VisitRichPhoto(
		RichMessage &message,
		uint64 id,
		Callbacks &callbacks) {
	const auto i = message.photos.find(id);
	if (i != end(message.photos)) {
		callbacks.photo(i->second);
	}
}

template <typename RichMessage, typename Callbacks>
void VisitRichDocument(
		RichMessage &message,
		uint64 id,
		Callbacks &callbacks) {
	const auto i = message.documents.find(id);
	if (i != end(message.documents)) {
		callbacks.document(i->second);
	}
}

template <typename RichMessage, typename Text, typename Callbacks>
void VisitRichText(
		RichMessage &message,
		Text &text,
		Callbacks &callbacks) {
	callbacks.text(text);
	if (text.type == Data::RichText::Type::InlineImage) {
		VisitRichDocument(message, text.id, callbacks);
	}
	for (auto &child : text.children) {
		VisitRichText(message, child, callbacks);
	}
	for (auto &child : text.oldChildren) {
		VisitRichText(message, child, callbacks);
	}
}

template <typename RichMessage, typename Caption, typename Callbacks>
void VisitRichCaption(
		RichMessage &message,
		Caption &caption,
		Callbacks &callbacks) {
	VisitRichText(message, caption.text, callbacks);
	VisitRichText(message, caption.credit, callbacks);
}

template <typename RichMessage, typename Block, typename Callbacks>
void VisitRichBlock(
		RichMessage &message,
		Block &block,
		Callbacks &callbacks);

template <typename RichMessage, typename Blocks, typename Callbacks>
void VisitRichBlocks(
		RichMessage &message,
		Blocks &blocks,
		Callbacks &callbacks) {
	for (auto &block : blocks) {
		VisitRichBlock(message, block, callbacks);
	}
}

template <typename RichMessage, typename Block, typename Callbacks>
void VisitRichBlock(
		RichMessage &message,
		Block &block,
		Callbacks &callbacks) {
	using Kind = Data::RichBlock::Kind;
	using ListContent = Data::RichListItemContent;
	using QuoteContent = Data::RichQuoteContent;
	switch (block.kind) {
	case Kind::Heading:
	case Kind::Paragraph:
	case Kind::Footer:
	case Kind::Thinking:
	case Kind::AuthorDate:
	case Kind::Code:
		VisitRichText(message, block.text, callbacks);
		break;
	case Kind::List:
		for (auto &item : block.listItems) {
			switch (item.content) {
			case ListContent::Text:
				if (item.text) {
					VisitRichText(message, *item.text, callbacks);
				}
				break;
			case ListContent::Blocks:
				VisitRichBlocks(message, item.blocks, callbacks);
				break;
			}
		}
		break;
	case Kind::Quote:
		switch (block.quoteContent) {
		case QuoteContent::Text:
			VisitRichText(message, block.text, callbacks);
			break;
		case QuoteContent::Blocks:
			VisitRichBlocks(message, block.blocks, callbacks);
			break;
		}
		VisitRichText(message, block.quoteCaption, callbacks);
		break;
	case Kind::Photo:
		VisitRichPhoto(message, block.photoId, callbacks);
		VisitRichCaption(message, block.caption, callbacks);
		break;
	case Kind::Video:
	case Kind::Audio:
		VisitRichDocument(message, block.documentId, callbacks);
		VisitRichCaption(message, block.caption, callbacks);
		break;
	case Kind::Cover:
		Expects(block.blocks.size() == 1);
		VisitRichBlock(message, block.blocks.front(), callbacks);
		break;
	case Kind::Embed:
		if (block.posterPhotoId) {
			VisitRichPhoto(message, *block.posterPhotoId, callbacks);
		}
		VisitRichCaption(message, block.caption, callbacks);
		break;
	case Kind::EmbedPost:
		VisitRichPhoto(message, block.authorPhotoId, callbacks);
		VisitRichBlocks(message, block.blocks, callbacks);
		VisitRichCaption(message, block.caption, callbacks);
		break;
	case Kind::Collage:
	case Kind::Slideshow:
		VisitRichBlocks(message, block.blocks, callbacks);
		VisitRichCaption(message, block.caption, callbacks);
		break;
	case Kind::Table:
		VisitRichText(message, block.text, callbacks);
		for (auto &row : block.tableRows) {
			for (auto &cell : row.cells) {
				if (cell.text) {
					VisitRichText(message, *cell.text, callbacks);
				}
			}
		}
		break;
	case Kind::Details:
		VisitRichText(message, block.text, callbacks);
		VisitRichBlocks(message, block.blocks, callbacks);
		break;
	case Kind::RelatedArticles:
		VisitRichText(message, block.text, callbacks);
		for (auto &article : block.relatedArticles) {
			if (article.photoId) {
				VisitRichPhoto(message, *article.photoId, callbacks);
			}
		}
		break;
	case Kind::Map:
	case Kind::InputMap:
		VisitRichCaption(message, block.caption, callbacks);
		break;
	case Kind::Unsupported:
	case Kind::Divider:
	case Kind::Anchor:
	case Kind::Channel:
	case Kind::Math:
	case Kind::Unknown:
		break;
	}
}

template <
	typename RichMessage,
	typename TextCallback,
	typename PhotoCallback,
	typename DocumentCallback>
void VisitRichMessageImpl(
		RichMessage &message,
		TextCallback &textCallback,
		PhotoCallback &photoCallback,
		DocumentCallback &documentCallback) {
	auto callbacks = RichMessageCallbacks<
		TextCallback,
		PhotoCallback,
		DocumentCallback>{
		textCallback,
		photoCallback,
		documentCallback,
	};
	VisitRichBlocks(message, message.blocks, callbacks);
}

template <
	typename TextCallback,
	typename PhotoCallback,
	typename DocumentCallback>
void VisitRichMessage(
		const Data::RichMessage &message,
		TextCallback &&textCallback,
		PhotoCallback &&photoCallback,
		DocumentCallback &&documentCallback) {
	VisitRichMessageImpl(
		message,
		textCallback,
		photoCallback,
		documentCallback);
}

template <
	typename TextCallback,
	typename PhotoCallback,
	typename DocumentCallback>
void VisitRichMessage(
		Data::RichMessage &message,
		TextCallback &&textCallback,
		PhotoCallback &&photoCallback,
		DocumentCallback &&documentCallback) {
	VisitRichMessageImpl(
		message,
		textCallback,
		photoCallback,
		documentCallback);
}

} // namespace

class ApiWrap::LoadedFileCache {
public:
	using Location = Data::FileLocation;

	LoadedFileCache(int limit);

	void save(const Location &location, const QString &relativePath);
	std::optional<QString> find(const Location &location) const;

private:
	int _limit = 0;
	std::map<LocationKey, QString> _map;
	std::deque<LocationKey> _list;

};

struct ApiWrap::StartProcess {
	FnMut<void(StartInfo)> done;

	enum class Step {
		UserpicsCount,
		StoriesCount,
		ProfileMusicCount,
		SplitRanges,
		DialogsCount,
		LeftChannelsCount,
	};
	std::deque<Step> steps;
	int splitIndex = 0;
	StartInfo info;
};

struct ApiWrap::ContactsProcess {
	FnMut<void(Data::ContactsList&&)> done;

	Data::ContactsList result;

	int topPeersOffset = 0;
};

struct ApiWrap::UserpicsProcess {
	FnMut<bool(Data::UserpicsInfo&&)> start;
	Fn<bool(DownloadProgress)> fileProgress;
	Fn<bool(Data::UserpicsSlice&&)> handleSlice;
	FnMut<void()> finish;

	int processed = 0;
	std::optional<Data::UserpicsSlice> slice;
	uint64 maxId = 0;
	bool lastSlice = false;
	int fileIndex = 0;
};

struct ApiWrap::StoriesProcess {
	FnMut<bool(Data::StoriesInfo&&)> start;
	Fn<bool(DownloadProgress)> fileProgress;
	Fn<bool(Data::StoriesSlice&&)> handleSlice;
	FnMut<void()> finish;

	int processed = 0;
	std::optional<Data::StoriesSlice> slice;
	int offsetId = 0;
	bool lastSlice = false;
	int fileIndex = 0;
};

struct ApiWrap::ProfileMusicProcess {
	FnMut<bool(Data::ProfileMusicInfo&&)> start;
	Fn<bool(DownloadProgress)> fileProgress;
	Fn<bool(Data::ProfileMusicSlice&&)> handleSlice;
	FnMut<void()> finish;

	int processed = 0;
	std::optional<Data::ProfileMusicSlice> slice;
	int offsetId = 0;
	bool lastSlice = false;
	int fileIndex = 0;
};

struct ApiWrap::OtherDataProcess {
	Data::File file;
	FnMut<void(Data::File&&)> done;
};

struct ApiWrap::FileProcess {
	FileProcess(const QString &path, Output::Stats *stats);

	Output::File file;
	QString relativePath;

	Fn<bool(FileProgress)> progress;
	FnMut<void(const QString &relativePath)> done;

	uint64 randomId = 0;
	Data::FileLocation location;
	Data::FileOrigin origin;
	int64 offset = 0;
	int64 size = 0;

	struct Request {
		int64 offset = 0;
		QByteArray bytes;
	};
	std::deque<Request> requests;
	mtpRequestId requestId = 0;
};

struct ApiWrap::FileProgress {
	int64 ready = 0;
	int64 total = 0;
};

struct ApiWrap::FilePolicy {
	const Data::Message *message = nullptr;
	MediaSettings::Type type = MediaSettings::Type();
	int64 controllingSize = 0;
};

struct ApiWrap::MessageFileWork {
	Data::File *file = nullptr;
	int64 controllingSize = 0;
	MediaSettings::Type type = MediaSettings::Type();
	bool rich : 1 = false;
};

struct ApiWrap::ChatsProcess {
	Fn<bool(int count)> progress;
	FnMut<void(Data::DialogsInfo&&)> done;

	Data::DialogsInfo info;
	int processedCount = 0;
	std::map<PeerId, int> indexByPeer;
};

struct ApiWrap::LeftChannelsProcess : ChatsProcess {
	int fullCount = 0;
	int offset = 0;
	bool finished = false;
};

struct ApiWrap::DialogsProcess : ChatsProcess {
	int splitIndexPlusOne = 0;
	TimeId offsetDate = 0;
	int32 offsetId = 0;
	MTPInputPeer offsetPeer = MTP_inputPeerEmpty();
};

struct ApiWrap::AbstractMessagesProcess {
	Fn<bool(DownloadProgress)> fileProgress;
	Fn<bool(Data::MessagesSlice&&)> handleSlice;
	FnMut<void()> done;

	FnMut<void(MTPmessages_Messages&&)> requestDone;

	Data::ParseMediaContext context;
	std::optional<Data::MessagesSlice> slice;
	std::vector<MessageFileWork> messageFileWork;
	bool lastSlice = false;
	int hydrationIndex = 0;
	int fileIndex = 0;
	int messageFileWorkIndex = 0;
	int messageFileWorkMessageIndex = -1;
};

struct ApiWrap::ChatProcess : AbstractMessagesProcess {
	Data::DialogInfo info;

	FnMut<bool(const Data::DialogInfo &)> start;

	int localSplitIndex = 0;
	int32 largestIdPlusOne = 1;
};

struct ApiWrap::TopicProcess : AbstractMessagesProcess {
	PeerId peerId = 0;
	MTPInputPeer inputPeer;
	int32 topicRootId = 0;
	QString relativePath;

	FnMut<bool(int count)> start;

	int32 offsetId = 0;
	int totalCount = 0;
	int processedCount = 0;
};


template <typename Request>
class ApiWrap::RequestBuilder {
public:
	using Original = MTP::ConcurrentSender::SpecificRequestBuilder<Request>;
	using Response = typename Request::ResponseType;

	RequestBuilder(
		Original &&builder,
		Fn<void(const MTP::Error&)> commonFailHandler);

	[[nodiscard]] RequestBuilder &done(FnMut<void()> &&handler);
	[[nodiscard]] RequestBuilder &done(
		FnMut<void(Response &&)> &&handler);
	[[nodiscard]] RequestBuilder &fail(
		Fn<bool(const MTP::Error&)> &&handler);

	mtpRequestId send();

private:
	Original _builder;
	Fn<void(const MTP::Error&)> _commonFailHandler;

};

template <typename Request>
ApiWrap::RequestBuilder<Request>::RequestBuilder(
	Original &&builder,
	Fn<void(const MTP::Error&)> commonFailHandler)
: _builder(std::move(builder))
, _commonFailHandler(std::move(commonFailHandler)) {
}

template <typename Request>
auto ApiWrap::RequestBuilder<Request>::done(
	FnMut<void()> &&handler
) -> RequestBuilder& {
	if (handler) {
		[[maybe_unused]] auto &silence_warning = _builder.done(std::move(handler));
	}
	return *this;
}

template <typename Request>
auto ApiWrap::RequestBuilder<Request>::done(
	FnMut<void(Response &&)> &&handler
) -> RequestBuilder& {
	if (handler) {
		[[maybe_unused]] auto &silence_warning = _builder.done(std::move(handler));
	}
	return *this;
}

template <typename Request>
auto ApiWrap::RequestBuilder<Request>::fail(
	Fn<bool(const MTP::Error &)> &&handler
) -> RequestBuilder& {
	if (handler) {
		[[maybe_unused]] auto &silence_warning = _builder.fail([
			common = base::take(_commonFailHandler),
			specific = std::move(handler)
		](const MTP::Error &error) {
			if (!specific(error)) {
				common(error);
			}
		});
	}
	return *this;
}

template <typename Request>
mtpRequestId ApiWrap::RequestBuilder<Request>::send() {
	return _commonFailHandler
		? _builder.fail(base::take(_commonFailHandler)).send()
		: _builder.send();
}

ApiWrap::LoadedFileCache::LoadedFileCache(int limit) : _limit(limit) {
	Expects(limit >= 0);
}

void ApiWrap::LoadedFileCache::save(
		const Location &location,
		const QString &relativePath) {
	if (!location) {
		return;
	}
	const auto key = ComputeLocationKey(location);
	_map[key] = relativePath;
	_list.push_back(key);
	if (_list.size() > _limit) {
		const auto key = _list.front();
		_list.pop_front();
		_map.erase(key);
	}
}

std::optional<QString> ApiWrap::LoadedFileCache::find(
		const Location &location) const {
	if (!location) {
		return std::nullopt;
	}
	const auto key = ComputeLocationKey(location);
	if (const auto i = _map.find(key); i != end(_map)) {
		return i->second;
	}
	return std::nullopt;
}

ApiWrap::FileProcess::FileProcess(const QString &path, Output::Stats *stats)
: file(path, stats) {
}

template <typename Request>
auto ApiWrap::mainRequest(Request &&request) {
	Expects(_takeoutId.has_value());

	auto original = std::move(_mtp.request(MTPInvokeWithTakeout<Request>(
		MTP_long(*_takeoutId),
		std::forward<Request>(request)
	)).toDC(MTP::ShiftDcId(0, MTP::kExportDcShift)));

	return RequestBuilder<MTPInvokeWithTakeout<Request>>(
		std::move(original),
		[=](const MTP::Error &result) { error(result); });
}

template <typename Request>
auto ApiWrap::splitRequest(int index, Request &&request) {
	Expects(index < _splits.size());

	//if (index == _splits.size() - 1) {
	//	return mainRequest(std::forward<Request>(request));
	//}
	return mainRequest(MTPInvokeWithMessagesRange<Request>(
		_splits[index],
		std::forward<Request>(request)));
}

auto ApiWrap::fileRequest(const Data::FileLocation &location, int64 offset) {
	Expects(location.dcId != 0
		|| location.data.type() == mtpc_inputTakeoutFileLocation);
	Expects(_takeoutId.has_value());
	Expects(_fileProcess->requestId == 0);

	return std::move(_mtp.request(MTPInvokeWithTakeout<MTPupload_GetFile>(
		MTP_long(*_takeoutId),
		MTPupload_GetFile(
			MTP_flags(0),
			location.data,
			MTP_long(offset),
			MTP_int(kFileChunkSize))
	)).fail([=](const MTP::Error &result) {
		_fileProcess->requestId = 0;
		if (result.type() == u"TAKEOUT_FILE_EMPTY"_q
			&& _otherDataProcess != nullptr) {
			filePartDone(
				0,
				MTP_upload_file(
					MTP_storage_filePartial(),
					MTP_int(0),
					MTP_bytes()));
		} else if (result.type() == u"LOCATION_INVALID"_q
			|| result.type() == u"VERSION_INVALID"_q
			|| result.type() == u"LOCATION_NOT_AVAILABLE"_q) {
			filePartUnavailable();
		} else if (result.code() == 400
			&& result.type().startsWith(u"FILE_REFERENCE_"_q)) {
			filePartRefreshReference(offset);
		} else {
			error(std::move(result));
		}
	}).toDC(MTP::ShiftDcId(location.dcId, MTP::kExportMediaDcShift)));
}

ApiWrap::ApiWrap(
	base::weak_qptr<MTP::Instance> weak,
	Fn<void(FnMut<void()>)> runner)
: _mtp(weak, std::move(runner))
, _fileCache(std::make_unique<LoadedFileCache>(kLocationCacheSize)) {
}

rpl::producer<MTP::Error> ApiWrap::errors() const {
	return _errors.events();
}

rpl::producer<Output::Result> ApiWrap::ioErrors() const {
	return _ioErrors.events();
}

void ApiWrap::startExport(
		const Settings &settings,
		Output::Stats *stats,
		FnMut<void(StartInfo)> done) {
	Expects(_settings == nullptr);
	Expects(_startProcess == nullptr);

	_settings = std::make_unique<Settings>(settings);
	_stats = stats;
	_startProcess = std::make_unique<StartProcess>();
	_startProcess->done = std::move(done);

	using Step = StartProcess::Step;
	if (_settings->types & Settings::Type::Userpics) {
		_startProcess->steps.push_back(Step::UserpicsCount);
	}
	if (_settings->types & Settings::Type::Stories) {
		_startProcess->steps.push_back(Step::StoriesCount);
	}
	if (_settings->types & Settings::Type::ProfileMusic) {
		_startProcess->steps.push_back(Step::ProfileMusicCount);
	}
	if (_settings->types & Settings::Type::AnyChatsMask) {
		_startProcess->steps.push_back(Step::SplitRanges);
		_startProcess->steps.push_back(Step::DialogsCount);
	}
	if (_settings->types & Settings::Type::GroupsChannelsMask) {
		if (!_settings->onlySinglePeer()) {
			_startProcess->steps.push_back(Step::LeftChannelsCount);
		}
	}
	startMainSession([=] {
		sendNextStartRequest();
	});
}

void ApiWrap::sendNextStartRequest() {
	Expects(_startProcess != nullptr);

	auto &steps = _startProcess->steps;
	if (steps.empty()) {
		finishStartProcess();
		return;
	}
	using Step = StartProcess::Step;
	const auto step = steps.front();
	steps.pop_front();
	switch (step) {
	case Step::UserpicsCount:
		return requestUserpicsCount();
	case Step::StoriesCount:
		return requestStoriesCount();
	case Step::ProfileMusicCount:
		return requestProfileMusicCount();
	case Step::SplitRanges:
		return requestSplitRanges();
	case Step::DialogsCount:
		return requestDialogsCount();
	case Step::LeftChannelsCount:
		return requestLeftChannelsCount();
	}
	Unexpected("Step in ApiWrap::sendNextStartRequest.");
}

void ApiWrap::requestUserpicsCount() {
	Expects(_startProcess != nullptr);

	mainRequest(MTPphotos_GetUserPhotos(
		_user,
		MTP_int(0),  // offset
		MTP_long(0), // max_id
		MTP_int(0)   // limit
	)).done([=](const MTPphotos_Photos &result) {
		Expects(_settings != nullptr);
		Expects(_startProcess != nullptr);

		_startProcess->info.userpicsCount = result.match(
		[](const MTPDphotos_photos &data) {
			return int(data.vphotos().v.size());
		}, [](const MTPDphotos_photosSlice &data) {
			return data.vcount().v;
		});

		sendNextStartRequest();
	}).send();
}

void ApiWrap::requestStoriesCount() {
	Expects(_startProcess != nullptr);

	mainRequest(MTPstories_GetStoriesArchive(
		MTP_inputPeerSelf(),
		MTP_int(0), // offset_id
		MTP_int(0) // limit
	)).done([=](const MTPstories_Stories &result) {
		Expects(_settings != nullptr);
		Expects(_startProcess != nullptr);

		_startProcess->info.storiesCount = result.data().vcount().v;

		sendNextStartRequest();
	}).send();
}

void ApiWrap::requestProfileMusicCount() {
	Expects(_startProcess != nullptr);

	mainRequest(MTPusers_GetSavedMusic(
		_user,
		MTP_int(0), // offset
		MTP_int(0), // limit
		MTP_long(0) // hash
	)).done([=](const MTPusers_SavedMusic &result) {
		Expects(_settings != nullptr);
		Expects(_startProcess != nullptr);

		const auto count = result.match(
		[](const MTPDusers_savedMusic &data) {
			return data.vcount().v;
		}, [](const MTPDusers_savedMusicNotModified &data) {
			return -1;
		});
		if (count < 0) {
			error("Unexpected messagesNotModified received.");
			return;
		}
		_startProcess->info.profileMusicCount = count;

		sendNextStartRequest();
	}).send();
}

void ApiWrap::requestSplitRanges() {
	Expects(_startProcess != nullptr);

	mainRequest(MTPmessages_GetSplitRanges(
	)).done([=](const MTPVector<MTPMessageRange> &result) {
		_splits = result.v;
		if (_splits.empty()) {
			_splits.push_back(MTP_messageRange(
				MTP_int(1),
				MTP_int(std::numeric_limits<int>::max())));
		}
		_startProcess->splitIndex = useOnlyLastSplit()
			? (_splits.size() - 1)
			: 0;

		sendNextStartRequest();
	}).send();
}

void ApiWrap::requestDialogsCount() {
	Expects(_startProcess != nullptr);

	if (_settings->onlySinglePeer()) {
		_startProcess->info.dialogsCount
			= (_settings->singlePeer.type() == mtpc_inputPeerChannel
				? 1
				: _splits.size());
		sendNextStartRequest();
		return;
	}

	const auto offsetDate = 0;
	const auto offsetId = 0;
	const auto offsetPeer = MTP_inputPeerEmpty();
	const auto limit = 1;
	const auto hash = uint64(0);
	splitRequest(_startProcess->splitIndex, MTPmessages_GetDialogs(
		MTP_flags(0),
		MTPint(), // folder_id
		MTP_int(offsetDate),
		MTP_int(offsetId),
		offsetPeer,
		MTP_int(limit),
		MTP_long(hash)
	)).done([=](const MTPmessages_Dialogs &result) {
		Expects(_settings != nullptr);
		Expects(_startProcess != nullptr);

		const auto count = result.match(
		[](const MTPDmessages_dialogs &data) {
			return int(data.vdialogs().v.size());
		}, [](const MTPDmessages_dialogsSlice &data) {
			return data.vcount().v;
		}, [](const MTPDmessages_dialogsNotModified &data) {
			return -1;
		});
		if (count < 0) {
			error("Unexpected dialogsNotModified received.");
			return;
		}
		_startProcess->info.dialogsCount += count;

		if (++_startProcess->splitIndex >= _splits.size()) {
			sendNextStartRequest();
		} else {
			requestDialogsCount();
		}
	}).send();
}

void ApiWrap::requestLeftChannelsCount() {
	Expects(_startProcess != nullptr);
	Expects(_leftChannelsProcess == nullptr);

	_leftChannelsProcess = std::make_unique<LeftChannelsProcess>();
	requestLeftChannelsSliceGeneric([=] {
		Expects(_startProcess != nullptr);
		Expects(_leftChannelsProcess != nullptr);

		_startProcess->info.dialogsCount
			+= _leftChannelsProcess->fullCount;
		sendNextStartRequest();
	});
}

void ApiWrap::finishStartProcess() {
	Expects(_startProcess != nullptr);

	const auto process = base::take(_startProcess);
	process->done(process->info);
}

bool ApiWrap::useOnlyLastSplit() const {
	return !(_settings->types & Settings::Type::NonChannelChatsMask);
}

void ApiWrap::requestLeftChannelsList(
		Fn<bool(int count)> progress,
		FnMut<void(Data::DialogsInfo&&)> done) {
	Expects(_leftChannelsProcess != nullptr);

	_leftChannelsProcess->progress = std::move(progress);
	_leftChannelsProcess->done = std::move(done);
	requestLeftChannelsSlice();
}

void ApiWrap::requestLeftChannelsSlice() {
	requestLeftChannelsSliceGeneric([=] {
		Expects(_leftChannelsProcess != nullptr);

		if (_leftChannelsProcess->finished) {
			const auto process = base::take(_leftChannelsProcess);
			process->done(std::move(process->info));
		} else {
			requestLeftChannelsSlice();
		}
	});
}

void ApiWrap::requestDialogsList(
		Fn<bool(int count)> progress,
		FnMut<void(Data::DialogsInfo&&)> done) {
	Expects(_dialogsProcess == nullptr);

	_dialogsProcess = std::make_unique<DialogsProcess>();
	_dialogsProcess->splitIndexPlusOne = _splits.size();
	_dialogsProcess->progress = std::move(progress);
	_dialogsProcess->done = std::move(done);

	requestDialogsSlice();
}

void ApiWrap::startMainSession(FnMut<void()> done) {
	using Type = Settings::Type;
	const auto sizeLimit = _settings->media.sizeLimit;
	const auto hasFiles = ((_settings->media.types != 0) && (sizeLimit > 0))
		|| (_settings->types & Type::Userpics)
		|| (_settings->types & Type::Stories);

	using Flag = MTPaccount_InitTakeoutSession::Flag;
	const auto flags = Flag(0)
		| (_settings->types & Type::Contacts ? Flag::f_contacts : Flag(0))
		| (hasFiles ? Flag::f_files : Flag(0))
		| ((hasFiles && sizeLimit < kFileMaxSize)
			? Flag::f_file_max_size
			: Flag(0))
		| (_settings->types & (Type::PersonalChats | Type::BotChats)
			? Flag::f_message_users
			: Flag(0))
		| (_settings->types & Type::PrivateGroups
			? (Flag::f_message_chats | Flag::f_message_megagroups)
			: Flag(0))
		| (_settings->types & Type::PublicGroups
			? Flag::f_message_megagroups
			: Flag(0))
		| (_settings->types & (Type::PrivateChannels | Type::PublicChannels)
			? Flag::f_message_channels
			: Flag(0));

	_mtp.request(MTPusers_GetUsers(
		MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())
	)).done([=, done = std::move(done)](
			const MTPVector<MTPUser> &result) mutable {
		for (const auto &user : result.v) {
			user.match([&](const MTPDuser &data) {
				if (data.is_self()) {
					_selfId.emplace(data.vid());
				}
			}, [&](const MTPDuserEmpty&) {
			});
		}
		if (!_selfId) {
			error("Could not retrieve selfId.");
			return;
		}
		_mtp.request(MTPaccount_InitTakeoutSession(
			MTP_flags(flags),
			MTP_long(sizeLimit)
		)).done([=, done = std::move(done)](
				const MTPaccount_Takeout &result) mutable {
			_takeoutId = result.match([](const MTPDaccount_takeout &data) {
				return data.vid().v;
			});
			done();
		}).fail([=](const MTP::Error &result) {
			error(result);
		}).toDC(MTP::ShiftDcId(0, MTP::kExportDcShift)).send();
	}).fail([=](const MTP::Error &result) {
		error(result);
	}).send();
}

void ApiWrap::requestPersonalInfo(FnMut<void(Data::PersonalInfo&&)> done) {
	mainRequest(MTPusers_GetFullUser(
		_user
	)).done([=, done = std::move(done)](const MTPusers_UserFull &result) mutable {
		result.match([&](const MTPDusers_userFull &data) {
			if (!data.vusers().v.empty()) {
				done(Data::ParsePersonalInfo(data));
			} else {
				error("Bad user type.");
			}
		});
	}).send();
}

void ApiWrap::requestOtherData(
		const QString &suggestedPath,
		FnMut<void(Data::File&&)> done) {
	Expects(_otherDataProcess == nullptr);

	_otherDataProcess = std::make_unique<OtherDataProcess>();
	_otherDataProcess->done = std::move(done);
	_otherDataProcess->file.location.data = MTP_inputTakeoutFileLocation();
	_otherDataProcess->file.suggestedPath = suggestedPath;
	loadFile(
		_otherDataProcess->file,
		Data::FileOrigin(),
		[](FileProgress progress) { return true; },
		[=](const QString &result) { otherDataDone(result); });
}

void ApiWrap::otherDataDone(const QString &relativePath) {
	Expects(_otherDataProcess != nullptr);

	const auto process = base::take(_otherDataProcess);
	process->file.relativePath = relativePath;
	if (relativePath.isEmpty()) {
		process->file.skipReason = Data::File::SkipReason::Unavailable;
	}
	process->done(std::move(process->file));
}

void ApiWrap::requestUserpics(
		FnMut<bool(Data::UserpicsInfo&&)> start,
		Fn<bool(DownloadProgress)> progress,
		Fn<bool(Data::UserpicsSlice&&)> slice,
		FnMut<void()> finish) {
	Expects(_userpicsProcess == nullptr);

	_userpicsProcess = std::make_unique<UserpicsProcess>();
	_userpicsProcess->start = std::move(start);
	_userpicsProcess->fileProgress = std::move(progress);
	_userpicsProcess->handleSlice = std::move(slice);
	_userpicsProcess->finish = std::move(finish);

	mainRequest(MTPphotos_GetUserPhotos(
		_user,
		MTP_int(0), // offset
		MTP_long(_userpicsProcess->maxId),
		MTP_int(kUserpicsSliceLimit)
	)).done([=](const MTPphotos_Photos &result) mutable {
		Expects(_userpicsProcess != nullptr);

		auto startInfo = result.match(
		[](const MTPDphotos_photos &data) {
			return Data::UserpicsInfo{ int(data.vphotos().v.size()) };
		}, [](const MTPDphotos_photosSlice &data) {
			return Data::UserpicsInfo{ data.vcount().v };
		});
		if (!_userpicsProcess->start(std::move(startInfo))) {
			return;
		}

		handleUserpicsSlice(result);
	}).send();
}

void ApiWrap::handleUserpicsSlice(const MTPphotos_Photos &result) {
	Expects(_userpicsProcess != nullptr);

	result.match([&](const auto &data) {
		if constexpr (MTPDphotos_photos::Is<decltype(data)>()) {
			_userpicsProcess->lastSlice = true;
		}
		loadUserpicsFiles(Data::ParseUserpicsSlice(
			data.vphotos(),
			_userpicsProcess->processed));
	});
}

void ApiWrap::loadUserpicsFiles(Data::UserpicsSlice &&slice) {
	Expects(_userpicsProcess != nullptr);
	Expects(!_userpicsProcess->slice.has_value());

	if (slice.list.empty()) {
		_userpicsProcess->lastSlice = true;
	}
	_userpicsProcess->slice = std::move(slice);
	_userpicsProcess->fileIndex = 0;
	loadNextUserpic();
}

void ApiWrap::loadNextUserpic() {
	Expects(_userpicsProcess != nullptr);
	Expects(_userpicsProcess->slice.has_value());

	for (auto &list = _userpicsProcess->slice->list
		; _userpicsProcess->fileIndex < list.size()
		; ++_userpicsProcess->fileIndex) {
		const auto ready = processFileLoad(
			list[_userpicsProcess->fileIndex].image.file,
			Data::FileOrigin(),
			[=](FileProgress value) { return loadUserpicProgress(value); },
			[=](const QString &path) { loadUserpicDone(path); });
		if (!ready) {
			return;
		}
	}
	finishUserpicsSlice();
}

void ApiWrap::finishUserpicsSlice() {
	Expects(_userpicsProcess != nullptr);
	Expects(_userpicsProcess->slice.has_value());

	auto slice = *base::take(_userpicsProcess->slice);
	if (!slice.list.empty()) {
		_userpicsProcess->processed += slice.list.size();
		_userpicsProcess->maxId = slice.list.back().id;
		if (!_userpicsProcess->handleSlice(std::move(slice))) {
			return;
		}
	}
	if (_userpicsProcess->lastSlice) {
		finishUserpics();
		return;
	}

	mainRequest(MTPphotos_GetUserPhotos(
		_user,
		MTP_int(0), // offset
		MTP_long(_userpicsProcess->maxId),
		MTP_int(kUserpicsSliceLimit)
	)).done([=](const MTPphotos_Photos &result) {
		handleUserpicsSlice(result);
	}).send();
}

bool ApiWrap::loadUserpicProgress(FileProgress progress) {
	Expects(_fileProcess != nullptr);
	Expects(_userpicsProcess != nullptr);
	Expects(_userpicsProcess->slice.has_value());
	Expects((_userpicsProcess->fileIndex >= 0)
		&& (_userpicsProcess->fileIndex
			< _userpicsProcess->slice->list.size()));

	return _userpicsProcess->fileProgress(DownloadProgress{
		_fileProcess->randomId,
		_fileProcess->relativePath,
		_userpicsProcess->fileIndex,
		progress.ready,
		progress.total });
}

void ApiWrap::loadUserpicDone(const QString &relativePath) {
	Expects(_userpicsProcess != nullptr);
	Expects(_userpicsProcess->slice.has_value());
	Expects((_userpicsProcess->fileIndex >= 0)
		&& (_userpicsProcess->fileIndex
			< _userpicsProcess->slice->list.size()));

	const auto index = _userpicsProcess->fileIndex;
	auto &file = _userpicsProcess->slice->list[index].image.file;
	file.relativePath = relativePath;
	if (relativePath.isEmpty()) {
		file.skipReason = Data::File::SkipReason::Unavailable;
	}
	loadNextUserpic();
}

void ApiWrap::finishUserpics() {
	Expects(_userpicsProcess != nullptr);

	base::take(_userpicsProcess)->finish();
}

void ApiWrap::requestStories(
		FnMut<bool(Data::StoriesInfo&&)> start,
		Fn<bool(DownloadProgress)> progress,
		Fn<bool(Data::StoriesSlice&&)> slice,
		FnMut<void()> finish) {
	Expects(_storiesProcess == nullptr);

	_storiesProcess = std::make_unique<StoriesProcess>();
	_storiesProcess->start = std::move(start);
	_storiesProcess->fileProgress = std::move(progress);
	_storiesProcess->handleSlice = std::move(slice);
	_storiesProcess->finish = std::move(finish);

	mainRequest(MTPstories_GetStoriesArchive(
		MTP_inputPeerSelf(),
		MTP_int(_storiesProcess->offsetId),
		MTP_int(kStoriesSliceLimit)
	)).done([=](const MTPstories_Stories &result) mutable {
		Expects(_storiesProcess != nullptr);

		auto startInfo = Data::StoriesInfo{ result.data().vcount().v };
		if (!_storiesProcess->start(std::move(startInfo))) {
			return;
		}

		handleStoriesSlice(result);
	}).send();
}

void ApiWrap::handleStoriesSlice(const MTPstories_Stories &result) {
	Expects(_storiesProcess != nullptr);

	loadStoriesFiles(Data::ParseStoriesSlice(
		result.data().vstories(),
		_storiesProcess->processed));
}

void ApiWrap::loadStoriesFiles(Data::StoriesSlice &&slice) {
	Expects(_storiesProcess != nullptr);
	Expects(!_storiesProcess->slice.has_value());

	if (!slice.lastId) {
		_storiesProcess->lastSlice = true;
	}
	_storiesProcess->slice = std::move(slice);
	_storiesProcess->fileIndex = 0;
	loadNextStory();
}

void ApiWrap::loadNextStory() {
	Expects(_storiesProcess != nullptr);
	Expects(_storiesProcess->slice.has_value());

	for (auto &list = _storiesProcess->slice->list
		; _storiesProcess->fileIndex < list.size()
		; ++_storiesProcess->fileIndex) {
		auto &story = list[_storiesProcess->fileIndex];
		const auto origin = Data::FileOrigin{ .storyId = story.id };
		const auto ready = processFileLoad(
			story.file(),
			origin,
			[=](FileProgress value) { return loadStoryProgress(value); },
			[=](const QString &path) { loadStoryDone(path); });
		if (!ready) {
			return;
		}
		const auto thumbProgress = [=](FileProgress value) {
			return loadStoryThumbProgress(value);
		};
		const auto thumbReady = processFileLoad(
			story.thumb().file,
			origin,
			thumbProgress,
			[=](const QString &path) { loadStoryThumbDone(path); },
			nullptr,
			&story);
		if (!thumbReady) {
			return;
		}
	}
	finishStoriesSlice();
}

void ApiWrap::finishStoriesSlice() {
	Expects(_storiesProcess != nullptr);
	Expects(_storiesProcess->slice.has_value());

	auto slice = *base::take(_storiesProcess->slice);
	if (slice.lastId) {
		_storiesProcess->processed += slice.list.size();
		_storiesProcess->offsetId = slice.lastId;
		if (!_storiesProcess->handleSlice(std::move(slice))) {
			return;
		}
	}
	if (_storiesProcess->lastSlice) {
		finishStories();
		return;
	}

	mainRequest(MTPstories_GetStoriesArchive(
		MTP_inputPeerSelf(),
		MTP_int(_storiesProcess->offsetId),
		MTP_int(kStoriesSliceLimit)
	)).done([=](const MTPstories_Stories &result) {
		handleStoriesSlice(result);
	}).send();
}

bool ApiWrap::loadStoryProgress(FileProgress progress) {
	Expects(_fileProcess != nullptr);
	Expects(_storiesProcess != nullptr);
	Expects(_storiesProcess->slice.has_value());
	Expects((_storiesProcess->fileIndex >= 0)
		&& (_storiesProcess->fileIndex
			< _storiesProcess->slice->list.size()));

	return _storiesProcess->fileProgress(DownloadProgress{
		_fileProcess->randomId,
		_fileProcess->relativePath,
		_storiesProcess->fileIndex,
		progress.ready,
		progress.total });
}

void ApiWrap::loadStoryDone(const QString &relativePath) {
	Expects(_storiesProcess != nullptr);
	Expects(_storiesProcess->slice.has_value());
	Expects((_storiesProcess->fileIndex >= 0)
		&& (_storiesProcess->fileIndex
			< _storiesProcess->slice->list.size()));

	const auto index = _storiesProcess->fileIndex;
	auto &file = _storiesProcess->slice->list[index].file();
	file.relativePath = relativePath;
	if (relativePath.isEmpty()) {
		file.skipReason = Data::File::SkipReason::Unavailable;
	}
	loadNextStory();
}

bool ApiWrap::loadStoryThumbProgress(FileProgress progress) {
	return loadStoryProgress(progress);
}

void ApiWrap::loadStoryThumbDone(const QString &relativePath) {
	Expects(_storiesProcess != nullptr);
	Expects(_storiesProcess->slice.has_value());
	Expects((_storiesProcess->fileIndex >= 0)
		&& (_storiesProcess->fileIndex
			< _storiesProcess->slice->list.size()));

	const auto index = _storiesProcess->fileIndex;
	auto &file = _storiesProcess->slice->list[index].thumb().file;
	file.relativePath = relativePath;
	if (relativePath.isEmpty()) {
		file.skipReason = Data::File::SkipReason::Unavailable;
	}
	loadNextStory();
}

void ApiWrap::finishStories() {
	Expects(_storiesProcess != nullptr);

	base::take(_storiesProcess)->finish();
}

void ApiWrap::requestProfileMusic(
		FnMut<bool(Data::ProfileMusicInfo&&)> start,
		Fn<bool(DownloadProgress)> progress,
		Fn<bool(Data::ProfileMusicSlice&&)> slice,
		FnMut<void()> finish) {
	Expects(_profileMusicProcess == nullptr);

	_profileMusicProcess = std::make_unique<ProfileMusicProcess>();
	_profileMusicProcess->start = std::move(start);
	_profileMusicProcess->fileProgress = std::move(progress);
	_profileMusicProcess->handleSlice = std::move(slice);
	_profileMusicProcess->finish = std::move(finish);

	mainRequest(MTPusers_GetSavedMusic(
		_user,
		MTP_int(0), // offset
		MTP_int(kProfileMusicSliceLimit), // limit
		MTP_long(0) // hash
	)).done([=](const MTPusers_SavedMusic &result) mutable {
		Expects(_profileMusicProcess != nullptr);

		auto startInfo = result.match(
		[](const MTPDusers_savedMusic &data) {
			return Data::ProfileMusicInfo{ data.vcount().v };
		}, [](const MTPDusers_savedMusicNotModified &data) {
			return Data::ProfileMusicInfo{ 0 };
		});
		if (!_profileMusicProcess->start(std::move(startInfo))) {
			return;
		}

		handleProfileMusicSlice(result);
	}).send();
}

void ApiWrap::handleProfileMusicSlice(const MTPusers_SavedMusic &result) {
	Expects(_profileMusicProcess != nullptr);
	Expects(_selfId.has_value());

	auto context = Data::ParseMediaContext();
	context.selfPeerId = peerFromUser(*_selfId);

	auto slice = result.match([&](const MTPDusers_savedMusic &data) {
		if (data.vdocuments().v.size() < kProfileMusicSliceLimit) {
			_profileMusicProcess->lastSlice = true;
		}
		auto result = Data::MessagesSlice();
		for (const auto &doc : data.vdocuments().v) {
			auto message = Data::Message();
			message.id = ++_profileMusicProcess->processed;
			message.date = 0;
			message.media.content = Data::ParseDocument(
				context,
				doc,
				"profile_music/",
				0);
			result.list.push_back(std::move(message));
		}
		return result;
	}, [&](const MTPDusers_savedMusicNotModified &) {
		_profileMusicProcess->lastSlice = true;
		return Data::MessagesSlice();
	});

	auto profileSlice = Data::ProfileMusicSlice();
	profileSlice.list.reserve(slice.list.size());
	for (auto &message : slice.list) {
		if (v::is<Data::Document>(message.media.content)) {
			const auto &doc = v::get<Data::Document>(message.media.content);
			if (doc.isAudioFile) {
				profileSlice.list.push_back(std::move(message));
			}
		}
	}

	loadProfileMusicFiles(std::move(profileSlice));
}

void ApiWrap::loadProfileMusicFiles(Data::ProfileMusicSlice &&slice) {
	Expects(_profileMusicProcess != nullptr);
	Expects(!_profileMusicProcess->slice.has_value());

	if (slice.list.empty()) {
		_profileMusicProcess->lastSlice = true;
	}
	_profileMusicProcess->slice = std::move(slice);
	_profileMusicProcess->fileIndex = 0;
	loadNextProfileMusic();
}

void ApiWrap::loadNextProfileMusic() {
	Expects(_profileMusicProcess != nullptr);
	Expects(_profileMusicProcess->slice.has_value());

	for (auto &list = _profileMusicProcess->slice->list
		; _profileMusicProcess->fileIndex < list.size()
		; ++_profileMusicProcess->fileIndex) {
		auto &message = list[_profileMusicProcess->fileIndex];
		const auto origin = Data::FileOrigin{ .messageId = message.id };
		const auto ready = processFileLoad(
			message.file(),
			origin,
			[=](FileProgress value) { return loadProfileMusicProgress(value); },
			[=](const QString &path) { loadProfileMusicDone(path); },
			&message);
		if (!ready) {
			return;
		}
		const auto thumbProgress = [=](FileProgress value) {
			return loadProfileMusicThumbProgress(value);
		};
		const auto thumbReady = processFileLoad(
			message.thumb().file,
			origin,
			thumbProgress,
			[=](const QString &path) { loadProfileMusicThumbDone(path); },
			&message);
		if (!thumbReady) {
			return;
		}
	}
	finishProfileMusicSlice();
}

void ApiWrap::finishProfileMusicSlice() {
	Expects(_profileMusicProcess != nullptr);
	Expects(_profileMusicProcess->slice.has_value());

	auto slice = *base::take(_profileMusicProcess->slice);
	if (!slice.list.empty()) {
		_profileMusicProcess->processed += slice.list.size();
		_profileMusicProcess->offsetId = slice.list.back().id;
		if (!_profileMusicProcess->handleSlice(std::move(slice))) {
			return;
		}
	}
	if (_profileMusicProcess->lastSlice) {
		finishProfileMusic();
		return;
	}

	mainRequest(MTPusers_GetSavedMusic(
		_user,
		MTP_int(_profileMusicProcess->offsetId),
		MTP_int(kProfileMusicSliceLimit),
		MTP_long(0)
	)).done([=](const MTPusers_SavedMusic &result) {
		handleProfileMusicSlice(result);
	}).send();
}

bool ApiWrap::loadProfileMusicProgress(FileProgress progress) {
	Expects(_fileProcess != nullptr);
	Expects(_profileMusicProcess != nullptr);
	Expects(_profileMusicProcess->slice.has_value());
	Expects((_profileMusicProcess->fileIndex >= 0)
		&& (_profileMusicProcess->fileIndex
			< _profileMusicProcess->slice->list.size()));

	return _profileMusicProcess->fileProgress(DownloadProgress{
		_fileProcess->randomId,
		_fileProcess->relativePath,
		_profileMusicProcess->fileIndex,
		progress.ready,
		progress.total });
}

void ApiWrap::loadProfileMusicDone(const QString &relativePath) {
	Expects(_profileMusicProcess != nullptr);
	Expects(_profileMusicProcess->slice.has_value());
	Expects((_profileMusicProcess->fileIndex >= 0)
		&& (_profileMusicProcess->fileIndex
			< _profileMusicProcess->slice->list.size()));

	const auto index = _profileMusicProcess->fileIndex;
	auto &file = _profileMusicProcess->slice->list[index].file();
	file.relativePath = relativePath;
	if (relativePath.isEmpty()) {
		file.skipReason = Data::File::SkipReason::Unavailable;
	}
	loadNextProfileMusic();
}

bool ApiWrap::loadProfileMusicThumbProgress(FileProgress progress) {
	return loadProfileMusicProgress(progress);
}

void ApiWrap::loadProfileMusicThumbDone(const QString &relativePath) {
	Expects(_profileMusicProcess != nullptr);
	Expects(_profileMusicProcess->slice.has_value());
	Expects((_profileMusicProcess->fileIndex >= 0)
		&& (_profileMusicProcess->fileIndex
			< _profileMusicProcess->slice->list.size()));

	const auto index = _profileMusicProcess->fileIndex;
	auto &file = _profileMusicProcess->slice->list[index].thumb().file;
	file.relativePath = relativePath;
	if (relativePath.isEmpty()) {
		file.skipReason = Data::File::SkipReason::Unavailable;
	}
	loadNextProfileMusic();
}

void ApiWrap::finishProfileMusic() {
	Expects(_profileMusicProcess != nullptr);

	base::take(_profileMusicProcess)->finish();
}

void ApiWrap::requestContacts(FnMut<void(Data::ContactsList&&)> done) {
	Expects(_contactsProcess == nullptr);

	_contactsProcess = std::make_unique<ContactsProcess>();
	_contactsProcess->done = std::move(done);
	mainRequest(MTPcontacts_GetSaved(
	)).done([=](const MTPVector<MTPSavedContact> &result) {
		_contactsProcess->result = Data::ParseContactsList(result);

		const auto resolve = [=](int index, const auto &resolveNext) -> void {
			if (index == _contactsProcess->result.list.size()) {
				return requestTopPeersSlice();
			}
			const auto &contact = _contactsProcess->result.list[index];
			mainRequest(MTPcontacts_ResolvePhone(
				MTP_string(qs(contact.phoneNumber))
			)).done([=](const MTPcontacts_ResolvedPeer &result) {
				auto &contact = _contactsProcess->result.list[index];
				contact.userId = result.data().vpeer().match([&](
						const MTPDpeerUser &user) {
					return UserId(user.vuser_id());
				}, [](const auto &) {
					return UserId();
				});
				resolveNext(index + 1, resolveNext);
			}).fail([=](const MTP::Error &) {
				resolveNext(index + 1, resolveNext);
				return true;
			}).send();
		};

		if (base::options::lookup<bool>("show-peer-id-below-about").value()) {
			resolve(0, resolve);
		} else {
			requestTopPeersSlice();
		}

	}).send();
}

void ApiWrap::requestTopPeersSlice() {
	Expects(_contactsProcess != nullptr);

	using Flag = MTPcontacts_GetTopPeers::Flag;
	mainRequest(MTPcontacts_GetTopPeers(
		MTP_flags(Flag::f_correspondents
			| Flag::f_bots_inline
			| Flag::f_phone_calls),
		MTP_int(_contactsProcess->topPeersOffset),
		MTP_int(kTopPeerSliceLimit),
		MTP_long(0) // hash
	)).done([=](const MTPcontacts_TopPeers &result) {
		Expects(_contactsProcess != nullptr);

		if (!Data::AppendTopPeers(_contactsProcess->result, result)) {
			error("Unexpected data in ApiWrap::requestTopPeersSlice.");
			return;
		}

		const auto offset = _contactsProcess->topPeersOffset;
		const auto loaded = result.match(
		[](const MTPDcontacts_topPeersNotModified &data) {
			return true;
		}, [](const MTPDcontacts_topPeersDisabled &data) {
			return true;
		}, [&](const MTPDcontacts_topPeers &data) {
			for (const auto &category : data.vcategories().v) {
				const auto loaded = category.match(
				[&](const MTPDtopPeerCategoryPeers &data) {
					return offset + data.vpeers().v.size() >= data.vcount().v;
				});
				if (!loaded) {
					return false;
				}
			}
			return true;
		});

		if (loaded) {
			auto process = base::take(_contactsProcess);
			process->done(std::move(process->result));
		} else {
			_contactsProcess->topPeersOffset = std::max(std::max(
				_contactsProcess->result.correspondents.size(),
				_contactsProcess->result.inlineBots.size()),
				_contactsProcess->result.phoneCalls.size());
			requestTopPeersSlice();
		}
	}).send();
}

void ApiWrap::requestSessions(FnMut<void(Data::SessionsList&&)> done) {
	mainRequest(MTPaccount_GetAuthorizations(
	)).done([=, done = std::move(done)](
			const MTPaccount_Authorizations &result) mutable {
		auto list = Data::ParseSessionsList(result);
		mainRequest(MTPaccount_GetWebAuthorizations(
		)).done([=, done = std::move(done), list = std::move(list)](
				const MTPaccount_WebAuthorizations &result) mutable {
			list.webList = Data::ParseWebSessionsList(result).webList;
			done(std::move(list));
		}).send();
	}).send();
}

void ApiWrap::requestMessages(
		const Data::DialogInfo &info,
		FnMut<bool(const Data::DialogInfo &)> start,
		Fn<bool(DownloadProgress)> progress,
		Fn<bool(Data::MessagesSlice&&)> slice,
		FnMut<void()> done) {
	Expects(_chatProcess == nullptr);
	Expects(_selfId.has_value());

	_chatProcess = std::make_unique<ChatProcess>();
	_chatProcess->context.selfPeerId = peerFromUser(*_selfId);
	_chatProcess->info = info;
	_chatProcess->start = std::move(start);
	_chatProcess->fileProgress = std::move(progress);
	_chatProcess->handleSlice = std::move(slice);
	_chatProcess->done = std::move(done);

	requestMessagesCount(0);
}

void ApiWrap::requestMessagesCount(int localSplitIndex) {
	Expects(_chatProcess != nullptr);
	Expects(localSplitIndex < _chatProcess->info.splits.size());

	requestChatMessages(
		_chatProcess->info.splits[localSplitIndex],
		0, // offset_id
		0, // add_offset
		1, // limit
		[=](const MTPmessages_Messages &result) {
		Expects(_chatProcess != nullptr);

		const auto count = result.match(
			[](const MTPDmessages_messages &data) {
			return int(data.vmessages().v.size());
		}, [](const MTPDmessages_messagesSlice &data) {
			return data.vcount().v;
		}, [](const MTPDmessages_channelMessages &data) {
			return data.vcount().v;
		}, [](const MTPDmessages_messagesNotModified &data) {
			return -1;
		});
		if (count < 0) {
			error("Unexpected messagesNotModified received.");
			return;
		}
		const auto skipSplit = !Data::SingleMessageAfter(
			result,
			_settings->singlePeerFrom);
		if (skipSplit) {
			// No messages from the requested range, skip this split.
			messagesCountLoaded(localSplitIndex, 0);
			return;
		}
		checkFirstMessageDate(localSplitIndex, count);
	});
}

void ApiWrap::checkFirstMessageDate(int localSplitIndex, int count) {
	Expects(_chatProcess != nullptr);
	Expects(localSplitIndex < _chatProcess->info.splits.size());

	if (_settings->singlePeerTill <= 0) {
		messagesCountLoaded(localSplitIndex, count);
		return;
	}

	// Request first message in this split to check if its' date < till.
	requestChatMessages(
		_chatProcess->info.splits[localSplitIndex],
		1, // offset_id
		-1, // add_offset
		1, // limit
		[=](const MTPmessages_Messages &result) {
		Expects(_chatProcess != nullptr);

		const auto skipSplit = !Data::SingleMessageBefore(
			result,
			_settings->singlePeerTill);
		messagesCountLoaded(localSplitIndex, skipSplit ? 0 : count);
	});
}

void ApiWrap::messagesCountLoaded(int localSplitIndex, int count) {
	Expects(_chatProcess != nullptr);
	Expects(localSplitIndex < _chatProcess->info.splits.size());

	_chatProcess->info.messagesCountPerSplit[localSplitIndex] = count;
	if (localSplitIndex + 1 < _chatProcess->info.splits.size()) {
		requestMessagesCount(localSplitIndex + 1);
	} else if (_chatProcess->start(_chatProcess->info)) {
		requestMessagesSlice();
	}
}

void ApiWrap::finishExport(FnMut<void()> done) {
	const auto guard = gsl::finally([&] { _takeoutId = std::nullopt; });

	mainRequest(MTPaccount_FinishTakeoutSession(
		MTP_flags(MTPaccount_FinishTakeoutSession::Flag::f_success)
	)).done(std::move(done)).send();
}

void ApiWrap::skipFile(uint64 randomId) {
	if (!_fileProcess || _fileProcess->randomId != randomId) {
		return;
	}
	LOG(("Export Info: File skipped."));
	Assert(!_fileProcess->requests.empty());
	Assert(_fileProcess->requestId != 0);
	_mtp.request(base::take(_fileProcess->requestId)).cancel();
	base::take(_fileProcess)->done(QString());
}

void ApiWrap::cancelExportFast() {
	if (_takeoutId.has_value()) {
		const auto requestId = mainRequest(MTPaccount_FinishTakeoutSession(
			MTP_flags(0)
		)).send();
		_mtp.request(requestId).detach();
	}
}

void ApiWrap::requestSinglePeerDialog() {
	auto doneSinglePeer = [=](const auto &result) {
		appendSinglePeerDialogs(
			Data::ParseDialogsInfo(_settings->singlePeer, result));
	};
	const auto requestUser = [&](const MTPInputUser &data) {
		mainRequest(MTPusers_GetUsers(
			MTP_vector<MTPInputUser>(1, data)
		)).done(std::move(doneSinglePeer)).send();
	};
	_settings->singlePeer.match([&](const MTPDinputPeerUser &data) {
		requestUser(MTP_inputUser(data.vuser_id(), data.vaccess_hash()));
	}, [&](const MTPDinputPeerChat &data) {
		mainRequest(MTPmessages_GetChats(
			MTP_vector<MTPlong>(1, data.vchat_id())
		)).done(std::move(doneSinglePeer)).send();
	}, [&](const MTPDinputPeerChannel &data) {
		mainRequest(MTPchannels_GetChannels(
			MTP_vector<MTPInputChannel>(
				1,
				MTP_inputChannel(data.vchannel_id(), data.vaccess_hash()))
		)).done(std::move(doneSinglePeer)).send();
	}, [&](const MTPDinputPeerSelf &data) {
		requestUser(MTP_inputUserSelf());
	}, [&](const MTPDinputPeerUserFromMessage &data) {
		Unexpected("From message peer in ApiWrap::requestSinglePeerDialog.");
	}, [&](const MTPDinputPeerChannelFromMessage &data) {
		Unexpected("From message peer in ApiWrap::requestSinglePeerDialog.");
	}, [](const MTPDinputPeerEmpty &data) {
		Unexpected("Empty peer in ApiWrap::requestSinglePeerDialog.");
	});
}

mtpRequestId ApiWrap::requestSinglePeerMigrated(
		const Data::DialogInfo &info) {
	const auto input = info.input.match([&](
		const MTPDinputPeerChannel & data) {
		return MTP_inputChannel(
			data.vchannel_id(),
			data.vaccess_hash());
	}, [](auto&&) -> MTPinputChannel {
		Unexpected("Peer type in a supergroup.");
	});
	return mainRequest(MTPchannels_GetFullChannel(
		input
	)).done([=](const MTPmessages_ChatFull &result) {
		auto info = result.match([&](
				const MTPDmessages_chatFull &data) {
			const auto migratedChatId = data.vfull_chat().match([&](
					const MTPDchannelFull &data) {
				return data.vmigrated_from_chat_id().value_or_empty();
			}, [](auto &&other) -> BareId {
				return 0;
			});
			return migratedChatId
				? Data::ParseDialogsInfo(
					MTP_inputPeerChat(MTP_long(migratedChatId)),
					MTP_messages_chats(data.vchats()))
				: Data::DialogsInfo();
		});
		appendSinglePeerDialogs(std::move(info));
	}).send();
}

void ApiWrap::appendSinglePeerDialogs(Data::DialogsInfo &&info) {
	const auto isSupergroupType = [](Data::DialogInfo::Type type) {
		using Type = Data::DialogInfo::Type;
		return (type == Type::PrivateSupergroup)
			|| (type == Type::PublicSupergroup);
	};
	const auto isChannelType = [](Data::DialogInfo::Type type) {
		using Type = Data::DialogInfo::Type;
		return (type == Type::PrivateChannel)
			|| (type == Type::PublicChannel);
	};

	auto migratedRequestId = mtpRequestId(0);
	const auto last = _dialogsProcess->splitIndexPlusOne - 1;
	for (auto &info : info.chats) {
		if (isSupergroupType(info.type) && !migratedRequestId) {
			migratedRequestId = requestSinglePeerMigrated(info);
			continue;
		} else if (isChannelType(info.type) || info.isMonoforum) {
			continue;
		}
		for (auto i = last; i != 0; --i) {
			info.splits.push_back(i - 1);
			info.messagesCountPerSplit.push_back(0);
		}
	}

	if (!migratedRequestId) {
		_dialogsProcess->processedCount += info.chats.size();
	}
	appendDialogsSlice(std::move(info));

	if (migratedRequestId
		|| !_dialogsProcess->progress(_dialogsProcess->processedCount)) {
		return;
	}
	finishDialogsList();
}

void ApiWrap::requestDialogsSlice() {
	Expects(_dialogsProcess != nullptr);

	if (_settings->onlySinglePeer()) {
		requestSinglePeerDialog();
		return;
	}

	const auto splitIndex = _dialogsProcess->splitIndexPlusOne - 1;
	const auto hash = uint64(0);
	splitRequest(splitIndex, MTPmessages_GetDialogs(
		MTP_flags(0),
		MTPint(), // folder_id
		MTP_int(_dialogsProcess->offsetDate),
		MTP_int(_dialogsProcess->offsetId),
		_dialogsProcess->offsetPeer,
		MTP_int(kChatsSliceLimit),
		MTP_long(hash)
	)).done([=](const MTPmessages_Dialogs &result) {
		if (result.type() == mtpc_messages_dialogsNotModified) {
			error("Unexpected dialogsNotModified received.");
			return;
		}
		auto finished = result.match(
		[](const MTPDmessages_dialogs &data) {
			return true;
		}, [](const MTPDmessages_dialogsSlice &data) {
			return data.vdialogs().v.isEmpty();
		}, [](const MTPDmessages_dialogsNotModified &data) {
			return true;
		});

		auto info = Data::ParseDialogsInfo(result);
		_dialogsProcess->processedCount += info.chats.size();
		const auto last = info.chats.empty()
			? Data::DialogInfo()
			: info.chats.back();
		appendDialogsSlice(std::move(info));

		if (!_dialogsProcess->progress(_dialogsProcess->processedCount)) {
			return;
		}

		if (!finished && last.topMessageDate > 0) {
			_dialogsProcess->offsetId = last.topMessageId;
			_dialogsProcess->offsetDate = last.topMessageDate;
			_dialogsProcess->offsetPeer = last.input;
		} else if (!useOnlyLastSplit()
			&& --_dialogsProcess->splitIndexPlusOne > 0) {
			_dialogsProcess->offsetId = 0;
			_dialogsProcess->offsetDate = 0;
			_dialogsProcess->offsetPeer = MTP_inputPeerEmpty();
		} else {
			requestLeftChannelsIfNeeded();
			return;
		}
		requestDialogsSlice();
	}).send();
}

void ApiWrap::appendDialogsSlice(Data::DialogsInfo &&info) {
	Expects(_dialogsProcess != nullptr);
	Expects(_dialogsProcess->splitIndexPlusOne <= _splits.size());

	appendChatsSlice(
		*_dialogsProcess,
		_dialogsProcess->info.chats,
		std::move(info.chats),
		_dialogsProcess->splitIndexPlusOne - 1);
}

void ApiWrap::requestLeftChannelsIfNeeded() {
	if (_settings->types & Settings::Type::GroupsChannelsMask) {
		requestLeftChannelsList([=](int count) {
			Expects(_dialogsProcess != nullptr);

			return _dialogsProcess->progress(
				_dialogsProcess->processedCount + count);
		}, [=](Data::DialogsInfo &&result) {
			Expects(_dialogsProcess != nullptr);

			_dialogsProcess->info.left = std::move(result.left);
			finishDialogsList();
		});
	} else {
		finishDialogsList();
	}
}

void ApiWrap::finishDialogsList() {
	Expects(_dialogsProcess != nullptr);

	const auto process = base::take(_dialogsProcess);
	Data::FinalizeDialogsInfo(process->info, *_settings);
	process->done(std::move(process->info));
}

void ApiWrap::requestLeftChannelsSliceGeneric(FnMut<void()> done) {
	Expects(_leftChannelsProcess != nullptr);

	mainRequest(MTPchannels_GetLeftChannels(
		MTP_int(_leftChannelsProcess->offset)
	)).done([=, done = std::move(done)](
			const MTPmessages_Chats &result) mutable {
		Expects(_leftChannelsProcess != nullptr);

		appendLeftChannelsSlice(Data::ParseLeftChannelsInfo(result));

		const auto process = _leftChannelsProcess.get();
		process->offset += result.match(
		[](const auto &data) {
			return int(data.vchats().v.size());
		});

		process->fullCount = result.match(
		[](const MTPDmessages_chats &data) {
			return int(data.vchats().v.size());
		}, [](const MTPDmessages_chatsSlice &data) {
			return data.vcount().v;
		});

		process->finished = result.match(
		[](const MTPDmessages_chats &data) {
			return true;
		}, [](const MTPDmessages_chatsSlice &data) {
			return data.vchats().v.isEmpty();
		});

		if (process->progress) {
			if (!process->progress(process->info.left.size())) {
				return;
			}
		}

		done();
	}).send();
}

void ApiWrap::appendLeftChannelsSlice(Data::DialogsInfo &&info) {
	Expects(_leftChannelsProcess != nullptr);
	Expects(!_splits.empty());

	appendChatsSlice(
		*_leftChannelsProcess,
		_leftChannelsProcess->info.left,
		std::move(info.left),
		_splits.size() - 1);
}

void ApiWrap::appendChatsSlice(
		ChatsProcess &process,
		std::vector<Data::DialogInfo> &to,
		std::vector<Data::DialogInfo> &&from,
		int splitIndex) {
	Expects(_settings != nullptr);

	const auto types = _settings->types;
	const auto goodByTypes = [&](const Data::DialogInfo &info) {
		return ((types & SettingsFromDialogsType(info.type)) != 0);
	};
	auto filtered = ranges::views::all(
		from
	) | ranges::views::filter([&](const Data::DialogInfo &info) {
		if (goodByTypes(info)) {
			return true;
		} else if (info.migratedToChannelId
			&& (((types & Settings::Type::PublicGroups) != 0)
				|| ((types & Settings::Type::PrivateGroups) != 0))) {
			return true;
		}
		return false;
	});
	to.reserve(to.size() + from.size());
	for (auto &info : filtered) {
		const auto nextIndex = to.size();
		if (info.migratedToChannelId) {
			const auto toPeerId = PeerId(info.migratedToChannelId);
			const auto i = process.indexByPeer.find(toPeerId);
			if (i != process.indexByPeer.end()
				&& Data::AddMigrateFromSlice(
					to[i->second],
					info,
					splitIndex,
					int(_splits.size()))) {
				continue;
			} else if (!goodByTypes(info)) {
				continue;
			}
		}
		const auto &[i, ok] = process.indexByPeer.emplace(
			info.peerId,
			nextIndex);
		if (ok) {
			to.push_back(std::move(info));
		}
		to[i->second].splits.push_back(splitIndex);
		to[i->second].messagesCountPerSplit.push_back(0);
	}
}

void ApiWrap::requestMessagesSlice() {
	Expects(_chatProcess != nullptr);

	const auto count = _chatProcess->info.messagesCountPerSplit[
		_chatProcess->localSplitIndex];
	if (!count) {
		startMessagesSlice({});
		return;
	}
	requestChatMessages(
		_chatProcess->info.splits[_chatProcess->localSplitIndex],
		_chatProcess->largestIdPlusOne,
		-kMessagesSliceLimit,
		kMessagesSliceLimit,
		[=](const MTPmessages_Messages &result) {
		Expects(_chatProcess != nullptr);

		result.match([&](const MTPDmessages_messagesNotModified &data) {
			error("Unexpected messagesNotModified received.");
		}, [&](const auto &data) {
			if constexpr (MTPDmessages_messages::Is<decltype(data)>()) {
				_chatProcess->lastSlice = true;
			}
			startMessagesSlice(Data::ParseMessagesSlice(
				_chatProcess->context,
				data.vmessages(),
				data.vusers(),
				data.vchats(),
				_chatProcess->info.relativePath));
		});
	});
}

void ApiWrap::requestChatMessages(
		int splitIndex,
		int offsetId,
		int addOffset,
		int limit,
		FnMut<void(MTPmessages_Messages&&)> done) {
	Expects(_chatProcess != nullptr);

	_chatProcess->requestDone = std::move(done);
	const auto doneHandler = [=](MTPmessages_Messages &&result) {
		Expects(_chatProcess != nullptr);

		base::take(_chatProcess->requestDone)(std::move(result));
	};
	const auto splitsCount = int(_splits.size());
	const auto realPeerInput = (splitIndex >= 0)
		? _chatProcess->info.input
		: _chatProcess->info.migratedFromInput;
	const auto outgoingInput = _chatProcess->info.isMonoforum
		? _chatProcess->info.monoforumBroadcastInput
		: MTP_inputPeerSelf();
	const auto realSplitIndex = (splitIndex >= 0)
		? splitIndex
		: (splitsCount + splitIndex);
	if (_chatProcess->info.onlyMyMessages) {
		splitRequest(realSplitIndex, MTPmessages_Search(
			MTP_flags(MTPmessages_Search::Flag::f_from_id),
			realPeerInput,
			MTP_string(), // query
			outgoingInput,
			MTPInputPeer(), // saved_peer_id
			MTPVector<MTPReaction>(), // saved_reaction
			MTPint(), // top_msg_id
			MTP_inputMessagesFilterEmpty(),
			MTP_int(0), // min_date
			MTP_int(0), // max_date
			MTP_int(offsetId),
			MTP_int(addOffset),
			MTP_int(limit),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0) // hash
		)).done(doneHandler).send();
	} else {
		splitRequest(realSplitIndex, MTPmessages_GetHistory(
			realPeerInput,
			MTP_int(offsetId),
			MTP_int(0), // offset_date
			MTP_int(addOffset),
			MTP_int(limit),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0)  // hash
		)).fail([=](const MTP::Error &error) {
			Expects(_chatProcess != nullptr);

			if (error.type() == u"CHANNEL_PRIVATE"_q) {
				if (realPeerInput.type() == mtpc_inputPeerChannel
					&& !_chatProcess->info.onlyMyMessages) {

					// Perhaps we just left / were kicked from channel.
					// Just switch to only my messages.
					_chatProcess->info.onlyMyMessages = true;
					requestChatMessages(
						splitIndex,
						offsetId,
						addOffset,
						limit,
						base::take(_chatProcess->requestDone));
					return true;
				}
			}
			return false;
		}).done(doneHandler).send();
	}
}

void ApiWrap::startMessagesSlice(Data::MessagesSlice &&slice) {
	Expects(bool(_chatProcess) != bool(_topicProcess));

	const auto process = _topicProcess
		? static_cast<AbstractMessagesProcess*>(_topicProcess.get())
		: static_cast<AbstractMessagesProcess*>(_chatProcess.get());
	Expects(!process->slice.has_value());

	if (slice.list.empty()) {
		process->lastSlice = true;
	}
	process->slice = std::move(slice);
	process->hydrationIndex = 0;
	process->fileIndex = 0;
	process->messageFileWork.clear();
	process->messageFileWorkIndex = 0;
	process->messageFileWorkMessageIndex = -1;

	resumeMessagesSlice();
}

void ApiWrap::resumeMessagesSlice() {
	Expects(bool(_chatProcess) != bool(_topicProcess));

	const auto topic = (_topicProcess != nullptr);
	const auto process = topic
		? static_cast<AbstractMessagesProcess*>(_topicProcess.get())
		: static_cast<AbstractMessagesProcess*>(_chatProcess.get());
	Expects(process->slice.has_value());

	auto &list = process->slice->list;
	while (process->hydrationIndex < list.size()) {
		const auto index = process->hydrationIndex;
		const auto &message = list[index];
		if (Data::SkipMessageByDate(message, *_settings)) {
			++process->hydrationIndex;
			continue;
		}
		if (!message.richMessage || !message.richMessage->part) {
			++process->hydrationIndex;
			continue;
		}
		const auto rawId = message.id;
		auto peer = topic
			? _topicProcess->inputPeer
			: _chatProcess->info.input;
		if (!topic) {
			const auto splitIndex = _chatProcess->info.splits[
				_chatProcess->localSplitIndex];
			if (splitIndex < 0) {
				peer = _chatProcess->info.migratedFromInput;
			}
		}
		mainRequest(MTPmessages_GetRichMessage(
			peer,
			MTP_int(rawId)
		)).done([=](const MTPmessages_Messages &result) {
			hydrateMessageDone(topic, index, rawId, result);
		}).send();
		return;
	}

	collectMessagesCustomEmoji(*process->slice);
	if (topic) {
		resolveTopicCustomEmoji();
	} else {
		resolveCustomEmoji();
	}
}

void ApiWrap::hydrateMessageDone(
		bool topic,
		int index,
		int32 rawId,
		const MTPmessages_Messages &result) {
	const auto richMessage = ExtractFullRichMessage(result, rawId);
	const auto process = topic
		? static_cast<AbstractMessagesProcess*>(_topicProcess.get())
		: static_cast<AbstractMessagesProcess*>(_chatProcess.get());
	const auto otherProcess = topic
		? static_cast<AbstractMessagesProcess*>(_chatProcess.get())
		: static_cast<AbstractMessagesProcess*>(_topicProcess.get());
	if (!richMessage
		|| !process
		|| otherProcess
		|| !process->slice
		|| process->hydrationIndex != index) {
		error("Unexpected rich message hydration result.");
		return;
	}
	auto &list = process->slice->list;
	if (index < 0 || index >= list.size()) {
		error("Unexpected rich message hydration result.");
		return;
	}
	auto &message = list[index];
	if (message.id != rawId
		|| !message.richMessage
		|| !message.richMessage->part) {
		error("Unexpected rich message hydration result.");
		return;
	}
	const auto &relativePath = topic
		? _topicProcess->relativePath
		: _chatProcess->info.relativePath;
	auto parsed = Data::ParseRichMessage(
		process->context,
		*richMessage,
		relativePath,
		message.date);
	if (parsed.part
		|| !process->slice
		|| process->hydrationIndex != index
		|| index < 0
		|| index >= process->slice->list.size()) {
		error("Unexpected rich message hydration result.");
		return;
	}
	auto &stored = process->slice->list[index];
	if (stored.id != rawId
		|| !stored.richMessage
		|| !stored.richMessage->part) {
		error("Unexpected rich message hydration result.");
		return;
	}
	stored.richMessage = std::move(parsed);
	++process->hydrationIndex;
	resumeMessagesSlice();
}

void ApiWrap::collectMessagesCustomEmoji(const Data::MessagesSlice &slice) {
	const auto collect = [&](uint64 id) {
		if (id && !_resolvedCustomEmoji.contains(id)) {
			_unresolvedCustomEmoji.emplace(id);
		}
	};
	for (const auto &message : slice.list) {
		if (Data::SkipMessageByDate(message, *_settings)) {
			continue;
		}
		for (const auto &part : message.text) {
			if (part.type == Data::TextPart::Type::CustomEmoji) {
				collect(part.additional.toULongLong());
			}
		}
		for (const auto &reaction : message.reactions) {
			if (reaction.type == Data::Reaction::Type::CustomEmoji) {
				collect(reaction.documentId.toULongLong());
			}
		}
		if (message.richMessage) {
			VisitRichMessage(
				*message.richMessage,
				[&](const Data::RichText &text) {
					if (text.type == Data::RichText::Type::CustomEmoji) {
						collect(text.id);
					}
				},
				[](const Data::Photo &) {},
				[](const Data::Document &) {});
		}
	}
}

void ApiWrap::resolveCustomEmoji() {
	if (_unresolvedCustomEmoji.empty()) {
		loadNextMessageFile();
		return;
	}
	const auto count = std::min(
		int(_unresolvedCustomEmoji.size()),
		kMaxEmojiPerRequest);
	auto v = QVector<MTPlong>();
	v.reserve(count);
	const auto till = end(_unresolvedCustomEmoji);
	const auto from = end(_unresolvedCustomEmoji) - count;
	for (auto i = from; i != till; ++i) {
		v.push_back(MTP_long(*i));
	}
	_unresolvedCustomEmoji.erase(from, till);
	const auto finalize = [=] {
		for (const auto &id : v) {
			if (_resolvedCustomEmoji.contains(id.v)) {
				continue;
			}
			_resolvedCustomEmoji.emplace(
				id.v,
				Data::Document{
					.file = {
						.skipReason = Data::File::SkipReason::Unavailable,
					},
				});
		}
		resolveCustomEmoji();
	};
	mainRequest(MTPmessages_GetCustomEmojiDocuments(
		MTP_vector<MTPlong>(v)
	)).fail([=](const MTP::Error &error) {
		LOG(("Export Error: Failed to get documents for emoji."));
		finalize();
		return true;
	}).done([=](const MTPVector<MTPDocument> &result) {
		for (const auto &entry : result.v) {
			auto document = Data::ParseDocument(
				_chatProcess->context,
				entry,
				_chatProcess->info.relativePath,
				TimeId());
			_resolvedCustomEmoji.emplace(document.id, std::move(document));
		}
		finalize();
	}).send();
}

Data::Message *ApiWrap::currentFileMessage() const {
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());

	return &_chatProcess->slice->list[_chatProcess->fileIndex];
}

Data::FileOrigin ApiWrap::currentFileMessageOrigin() const {
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());

	const auto splitIndex = _chatProcess->info.splits[
		_chatProcess->localSplitIndex];
	auto result = Data::FileOrigin();
	result.messageId = currentFileMessage()->id;
	result.split = (splitIndex >= 0)
		? splitIndex
		: (int(_splits.size()) + splitIndex);
	result.peer = (splitIndex >= 0)
		? _chatProcess->info.input
		: _chatProcess->info.migratedFromInput;
	return result;
}

std::optional<QByteArray> ApiWrap::getCustomEmoji(
		Data::Message &message,
		QByteArray &data) {
	if (const auto id = data.toULongLong()) {
		const auto i = _resolvedCustomEmoji.find(id);
		if (i == end(_resolvedCustomEmoji)) {
			return Data::TextPart::UnavailableEmoji();
		}
		auto &document = i->second;
		auto &file = document.file;
		const auto fileProgress = [=](FileProgress value) {
			if (_chatProcess) {
				return loadMessageEmojiProgress(value);
			} else if (_topicProcess) {
				return loadTopicEmojiProgress(value);
			}
			return true;
		};
		const auto policy = FilePolicy{
			.message = &message,
			.type = DocumentMediaType(document),
			.controllingSize = document.file.size,
		};
		const auto ready = processFileLoad(
			file,
			{ .customEmojiId = id },
			fileProgress,
			[=](const QString &path) { loadCustomEmojiDone(id, path); },
			policy);
		if (!ready) {
			return std::nullopt;
		}
		using SkipReason = Data::File::SkipReason;
		if (file.skipReason == SkipReason::Unavailable) {
			return Data::TextPart::UnavailableEmoji();
		} else if (file.skipReason == SkipReason::FileType
			|| file.skipReason == SkipReason::FileSize) {
			return QByteArray();
		} else {
			return file.relativePath.toUtf8();
		}
	}
	return data;
}

bool ApiWrap::messageCustomEmojiReady(Data::Message &message) {
	const auto resolve = [&](QByteArray &data) {
		auto result = getCustomEmoji(message, data);
		if (!result.has_value()) {
			return false;
		}
		data = base::take(*result);
		return true;
	};
	for (auto &part : message.text) {
		if (part.type == Data::TextPart::Type::CustomEmoji) {
			if (!resolve(part.additional)) {
				return false;
			}
		}
	}
	for (auto &reaction : message.reactions) {
		if (reaction.type == Data::Reaction::Type::CustomEmoji) {
			if (!resolve(reaction.documentId)) {
				return false;
			}
		}
	}
	if (!message.richMessage) {
		return true;
	}
	auto ready = true;
	VisitRichMessage(
		*message.richMessage,
		[&](Data::RichText &text) {
			if (ready
				&& text.type == Data::RichText::Type::CustomEmoji) {
				ready = resolve(text.customEmojiData);
			}
		},
		[](Data::Photo &) {},
		[](Data::Document &) {});
	return ready;
}

void ApiWrap::buildMessageFileWork(
		AbstractMessagesProcess &process,
		Data::Message &message) {
	Expects(process.messageFileWork.empty());
	Expects(process.messageFileWorkIndex == 0);
	Expects(process.messageFileWorkMessageIndex < 0);

	const auto append = [&process](
			Data::File *file,
			MediaSettings::Type type,
			int64 controllingSize,
			bool rich) {
		Expects(file != nullptr);
		process.messageFileWork.push_back(MessageFileWork{
			.file = file,
			.controllingSize = controllingSize,
			.type = type,
			.rich = rich,
		});
	};

	auto ordinaryMain = static_cast<Data::File*>(nullptr);
	auto ordinaryDocument = static_cast<Data::Document*>(nullptr);
	auto ordinaryType = MediaSettings::Type::Photo;
	v::match(
		message.action.content,
		[&](Data::ActionChatEditPhoto &action) {
			ordinaryMain = &action.photo.image.file;
		},
		[&](Data::ActionSuggestProfilePhoto &action) {
			ordinaryMain = &action.photo.image.file;
		},
		[](auto &) {});
	v::match(
		message.media.content,
		[&](Data::Photo &photo) {
			if (!ordinaryMain) {
				ordinaryMain = &photo.image.file;
			}
		},
		[&](Data::Document &document) {
			ordinaryDocument = &document;
			if (!ordinaryMain) {
				ordinaryMain = &document.file;
				ordinaryType = DocumentMediaType(document);
			}
		},
		[&](Data::SharedContact &contact) {
			if (!ordinaryMain) {
				ordinaryMain = &contact.vcard;
			}
		},
		[](auto &) {});
	const auto ordinarySize = ordinaryMain ? ordinaryMain->size : 0;
	if (ordinaryMain) {
		append(ordinaryMain, ordinaryType, ordinarySize, false);
	}
	if (ordinaryDocument && ordinaryDocument->thumb.width > 0) {
		append(
			&ordinaryDocument->thumb.file,
			DocumentMediaType(*ordinaryDocument),
			ordinarySize,
			false);
	}

	if (message.richMessage) {
		auto richFiles = std::set<Data::File*>();
		const auto appendRich = [&richFiles, &append](
				Data::File &file,
				MediaSettings::Type type,
				int64 controllingSize) {
			if (richFiles.emplace(&file).second) {
				append(&file, type, controllingSize, true);
			}
		};
		VisitRichMessage(
			*message.richMessage,
			[](Data::RichText &) {},
			[&](Data::Photo &photo) {
				appendRich(
					photo.image.file,
					MediaSettings::Type::Photo,
					photo.image.file.size);
			},
			[&](Data::Document &document) {
				const auto type = DocumentMediaType(document);
				const auto controllingSize = document.file.size;
				appendRich(document.file, type, controllingSize);
				if (document.thumb.width > 0) {
					appendRich(document.thumb.file, type, controllingSize);
				}
			});
	}

	process.messageFileWorkMessageIndex = process.fileIndex;
}

void ApiWrap::loadNextMessageFile() {
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());
	Expects(_chatProcess->hydrationIndex
		== _chatProcess->slice->list.size());

	auto &process = *_chatProcess;
	auto &list = process.slice->list;
	while (process.fileIndex < list.size()) {
		const auto index = process.fileIndex;
		auto &message = list[index];
		if (Data::SkipMessageByDate(message, *_settings)) {
			Expects(process.messageFileWork.empty());
			Expects(process.messageFileWorkMessageIndex < 0);
			++process.fileIndex;
			continue;
		}
		if (!messageCustomEmojiReady(message)) {
			return;
		}
		if (process.messageFileWorkMessageIndex < 0) {
			buildMessageFileWork(process, message);
		}
		Expects(process.messageFileWorkMessageIndex == index);
		while (process.messageFileWorkIndex
				< process.messageFileWork.size()) {
			const auto work = process.messageFileWork[
				process.messageFileWorkIndex];
			const auto target = work.file;
			Expects(target != nullptr);
			auto origin = currentFileMessageOrigin();
			origin.richMessage = work.rich;
			const auto policy = FilePolicy{
				.message = &message,
				.type = work.type,
				.controllingSize = work.controllingSize,
			};
			const auto ready = processFileLoad(
				*target,
				origin,
				[=](FileProgress value) {
					return loadMessageFileProgress(index, value);
				},
				[=](const QString &path) {
					loadMessageFileDone(index, target, path);
				},
				policy);
			if (!ready) {
				return;
			}
			Expects(!target->relativePath.isEmpty()
				|| target->skipReason != Data::File::SkipReason::None);
			++process.messageFileWorkIndex;
		}
		process.messageFileWork.clear();
		process.messageFileWorkIndex = 0;
		process.messageFileWorkMessageIndex = -1;
		++process.fileIndex;
	}
	finishMessagesSlice();
}

void ApiWrap::finishMessagesSlice() {
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());
	Expects(_chatProcess->messageFileWork.empty());
	Expects(_chatProcess->messageFileWorkIndex == 0);
	Expects(_chatProcess->messageFileWorkMessageIndex < 0);

	auto slice = *base::take(_chatProcess->slice);
	if (!slice.list.empty()) {
		_chatProcess->largestIdPlusOne = slice.list.back().id + 1;
		const auto splitIndex = _chatProcess->info.splits[
			_chatProcess->localSplitIndex];
		if (splitIndex < 0) {
			slice = AdjustMigrateMessageIds(std::move(slice));
		}
		if (!_chatProcess->handleSlice(std::move(slice))) {
			return;
		}
	}
	if (_chatProcess->lastSlice
		&& (++_chatProcess->localSplitIndex
			< _chatProcess->info.splits.size())) {
		_chatProcess->lastSlice = false;
		_chatProcess->largestIdPlusOne = 1;
	}
	if (!_chatProcess->lastSlice) {
		requestMessagesSlice();
	} else {
		finishMessages();
	}
}

bool ApiWrap::loadMessageFileProgress(int index, FileProgress progress) {
	Expects(_fileProcess != nullptr);
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());
	Expects(index >= 0 && index < _chatProcess->slice->list.size());
	Expects(_chatProcess->fileIndex == index);

	return _chatProcess->fileProgress(DownloadProgress{
		.randomId = _fileProcess->randomId,
		.path = _fileProcess->relativePath,
		.itemIndex = index,
		.ready = progress.ready,
		.total = progress.total });
}

void ApiWrap::loadMessageFileDone(
		int index,
		Data::File *file,
		const QString &relativePath) {
	Expects(_chatProcess != nullptr);
	Expects(_chatProcess->slice.has_value());
	Expects(file != nullptr);
	Expects(index >= 0 && index < _chatProcess->slice->list.size());
	Expects(_chatProcess->fileIndex == index);
	Expects(_chatProcess->messageFileWorkMessageIndex == index);
	Expects(_chatProcess->messageFileWorkIndex >= 0);
	Expects(_chatProcess->messageFileWorkIndex
		< _chatProcess->messageFileWork.size());
	Expects(_chatProcess->messageFileWork[
		_chatProcess->messageFileWorkIndex].file == file);

	file->relativePath = relativePath;
	if (relativePath.isEmpty()) {
		file->skipReason = Data::File::SkipReason::Unavailable;
	}
	loadNextMessageFile();
}

bool ApiWrap::loadMessageEmojiProgress(FileProgress progress) {
	Expects(_chatProcess != nullptr);

	return loadMessageFileProgress(_chatProcess->fileIndex, progress);
}

void ApiWrap::loadMessageEmojiDone(uint64 id, const QString &relativePath) {
	const auto i = _resolvedCustomEmoji.find(id);
	if (i != end(_resolvedCustomEmoji)) {
		i->second.file.relativePath = relativePath;
		if (relativePath.isEmpty()) {
			i->second.file.skipReason = Data::File::SkipReason::Unavailable;
		}
	}
	loadNextMessageFile();
}

bool ApiWrap::loadTopicEmojiProgress(FileProgress progress) {
	Expects(_topicProcess != nullptr);

	return loadTopicMessageFileProgress(
		_topicProcess->fileIndex,
		progress);
}

void ApiWrap::loadCustomEmojiDone(uint64 id, const QString &relativePath) {
	const auto i = _resolvedCustomEmoji.find(id);
	if (i != end(_resolvedCustomEmoji)) {
		i->second.file.relativePath = relativePath;
		if (relativePath.isEmpty()) {
			i->second.file.skipReason = Data::File::SkipReason::Unavailable;
		}
	}
	if (_chatProcess) {
		loadNextMessageFile();
	} else if (_topicProcess) {
		loadNextTopicMessageFile();
	}
}

void ApiWrap::finishMessages() {
	Expects(_chatProcess != nullptr);
	Expects(!_chatProcess->slice.has_value());

	const auto process = base::take(_chatProcess);
	process->done();
}

void ApiWrap::requestTopicMessages(
		PeerId peerId,
		MTPInputPeer inputPeer,
		int32 topicRootId,
		FnMut<bool(int count)> start,
		Fn<bool(DownloadProgress)> progress,
		Fn<bool(Data::MessagesSlice&&)> slice,
		FnMut<void()> done) {
	Expects(_topicProcess == nullptr);
	Expects(_selfId.has_value());

	_topicProcess = std::make_unique<TopicProcess>();
	_topicProcess->context.selfPeerId = peerFromUser(*_selfId);
	_topicProcess->peerId = peerId;
	_topicProcess->inputPeer = inputPeer;
	_topicProcess->topicRootId = topicRootId;
	_topicProcess->relativePath = "chats/chat_"
		+ QString::number(peerId.value)
		+ "/topic_"
		+ QString::number(topicRootId)
		+ "/";
	_topicProcess->start = std::move(start);
	_topicProcess->fileProgress = std::move(progress);
	_topicProcess->handleSlice = std::move(slice);
	_topicProcess->done = std::move(done);

	mainRequest(MTPchannels_GetMessages(
		MTP_inputChannel(
			inputPeer.c_inputPeerChannel().vchannel_id(),
			inputPeer.c_inputPeerChannel().vaccess_hash()),
		MTP_vector<MTPInputMessage>(
			1,
			MTP_inputMessageID(MTP_int(topicRootId)))
	)).done([=](const MTPmessages_Messages &rootResult) {
		Expects(_topicProcess != nullptr);

		auto rootSlice = rootResult.match([&](
				const MTPDmessages_messagesNotModified &) {
			return Data::MessagesSlice();
		}, [&](const auto &data) {
			return Data::ParseMessagesSlice(
				_topicProcess->context,
				data.vmessages(),
				data.vusers(),
				data.vchats(),
				_topicProcess->relativePath);
		});

		auto rootSlicePtr = std::make_shared<Data::MessagesSlice>(
			std::move(rootSlice));

		requestTopicReplies(
			0,
			0,
			kMessagesSliceLimit,
			[=](const MTPmessages_Messages &result) {
				Expects(_topicProcess != nullptr);

				const auto count = result.match(
					[](const MTPDmessages_messages &data) {
					return int(data.vmessages().v.size());
				}, [](const MTPDmessages_messagesSlice &data) {
					return data.vcount().v;
				}, [](const MTPDmessages_channelMessages &data) {
					return data.vcount().v;
				}, [](const MTPDmessages_messagesNotModified &data) {
					return -1;
				});
				if (count < 0) {
					error("Unexpected messagesNotModified received.");
					return;
				}
				_topicProcess->totalCount = count;
				if (!_topicProcess->start(count)) {
					return;
				}

				if (!rootSlicePtr->list.empty()) {
					startMessagesSlice(std::move(*rootSlicePtr));
					return;
				}

				requestTopicMessagesSlice();
			});
	}).send();
}

void ApiWrap::requestTopicMessagesSlice() {
	Expects(_topicProcess != nullptr);

	const auto offsetId = (_topicProcess->offsetId == 0)
		? 1
		: (_topicProcess->offsetId + 1);
	requestTopicReplies(
		offsetId,
		-kMessagesSliceLimit,
		kMessagesSliceLimit,
		[=](const MTPmessages_Messages &result) {
			Expects(_topicProcess != nullptr);

			result.match([&](const MTPDmessages_messagesNotModified &data) {
				error("Unexpected messagesNotModified received.");
			}, [&](const auto &data) {
				if constexpr (MTPDmessages_messages::Is<decltype(data)>()) {
					_topicProcess->lastSlice = true;
				}
				auto slice = Data::ParseMessagesSlice(
					_topicProcess->context,
					data.vmessages(),
					data.vusers(),
					data.vchats(),
					_topicProcess->relativePath);
				if (slice.list.empty()) {
					_topicProcess->lastSlice = true;
				}
				loadTopicMessagesFiles(std::move(slice));
			});
		});
}

void ApiWrap::requestTopicReplies(
		int offsetId,
		int addOffset,
		int limit,
		FnMut<void(MTPmessages_Messages&&)> done) {
	Expects(_topicProcess != nullptr);

	_topicProcess->requestDone = std::move(done);
	const auto doneHandler = [=](MTPmessages_Messages &&result) {
		Expects(_topicProcess != nullptr);
		base::take(_topicProcess->requestDone)(std::move(result));
	};

	mainRequest(MTPmessages_GetReplies(
		_topicProcess->inputPeer,
		MTP_int(_topicProcess->topicRootId),
		MTP_int(offsetId),
		MTP_int(0),
		MTP_int(addOffset),
		MTP_int(limit),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done(doneHandler).send();
}

void ApiWrap::loadTopicMessagesFiles(Data::MessagesSlice &&slice) {
	Expects(_topicProcess != nullptr);
	Expects(!_topicProcess->slice.has_value());

	startMessagesSlice(std::move(slice));
}

void ApiWrap::resolveTopicCustomEmoji() {
	Expects(_topicProcess != nullptr);
	Expects(_topicProcess->slice.has_value());
	Expects(_topicProcess->hydrationIndex
		== _topicProcess->slice->list.size());

	if (_unresolvedCustomEmoji.empty()) {
		loadNextTopicMessageFile();
		return;
	}
	const auto count = std::min(
		int(_unresolvedCustomEmoji.size()),
		kMaxEmojiPerRequest);
	auto v = QVector<MTPlong>();
	v.reserve(count);
	const auto till = end(_unresolvedCustomEmoji);
	const auto from = end(_unresolvedCustomEmoji) - count;
	for (auto i = from; i != till; ++i) {
		v.push_back(MTP_long(*i));
	}
	_unresolvedCustomEmoji.erase(from, till);
	const auto finalize = [=] {
		for (const auto &id : v) {
			if (_resolvedCustomEmoji.contains(id.v)) {
				continue;
			}
			_resolvedCustomEmoji.emplace(
				id.v,
				Data::Document{
					.file = {
						.skipReason = Data::File::SkipReason::Unavailable,
					},
				});
		}
		resolveTopicCustomEmoji();
	};
	mainRequest(MTPmessages_GetCustomEmojiDocuments(
		MTP_vector<MTPlong>(v)
	)).fail([=](const MTP::Error &error) {
		LOG(("Export Error: Failed to get documents for emoji."));
		finalize();
		return true;
	}).done([=](const MTPVector<MTPDocument> &result) {
		for (const auto &entry : result.v) {
			auto document = Data::ParseDocument(
				_topicProcess->context,
				entry,
				_topicProcess->relativePath,
				TimeId());
			_resolvedCustomEmoji.emplace(document.id, std::move(document));
		}
		finalize();
	}).send();
}

void ApiWrap::loadNextTopicMessageFile() {
	Expects(_topicProcess != nullptr);
	Expects(_topicProcess->slice.has_value());
	Expects(_topicProcess->hydrationIndex
		== _topicProcess->slice->list.size());

	auto &process = *_topicProcess;
	auto &list = process.slice->list;
	while (process.fileIndex < list.size()) {
		const auto index = process.fileIndex;
		auto &message = list[index];
		if (Data::SkipMessageByDate(message, *_settings)) {
			Expects(process.messageFileWork.empty());
			Expects(process.messageFileWorkIndex == 0);
			Expects(process.messageFileWorkMessageIndex < 0);
			++process.fileIndex;
			continue;
		}
		if (!messageCustomEmojiReady(message)) {
			return;
		}
		if (process.messageFileWorkMessageIndex < 0) {
			buildMessageFileWork(process, message);
		}
		Expects(process.messageFileWorkMessageIndex == index);
		while (process.messageFileWorkIndex
				< process.messageFileWork.size()) {
			const auto work = process.messageFileWork[
				process.messageFileWorkIndex];
			const auto target = work.file;
			Expects(target != nullptr);
			auto origin = Data::FileOrigin{
				.peer = process.inputPeer,
				.messageId = message.id,
			};
			origin.richMessage = work.rich;
			const auto policy = FilePolicy{
				.message = &message,
				.type = work.type,
				.controllingSize = work.controllingSize,
			};
			const auto ready = processFileLoad(
				*target,
				origin,
				[=](FileProgress value) {
					return loadTopicMessageFileProgress(index, value);
				},
				[=](const QString &path) {
					loadTopicMessageFileDone(index, target, path);
				},
				policy);
			if (!ready) {
				return;
			}
			Expects(!target->relativePath.isEmpty()
				|| target->skipReason != Data::File::SkipReason::None);
			++process.messageFileWorkIndex;
		}
		process.messageFileWork.clear();
		process.messageFileWorkIndex = 0;
		process.messageFileWorkMessageIndex = -1;
		++process.fileIndex;
	}
	finishTopicMessagesSlice();
}

void ApiWrap::finishTopicMessagesSlice() {
	Expects(_topicProcess != nullptr);
	Expects(_topicProcess->slice.has_value());
	Expects(_topicProcess->hydrationIndex
		== _topicProcess->slice->list.size());
	Expects(_topicProcess->messageFileWork.empty());
	Expects(_topicProcess->messageFileWorkIndex == 0);
	Expects(_topicProcess->messageFileWorkMessageIndex < 0);

	auto slice = *base::take(_topicProcess->slice);
	if (!slice.list.empty()) {
		_topicProcess->offsetId = slice.list.back().id;
		_topicProcess->processedCount += slice.list.size();
		if (!_topicProcess->handleSlice(std::move(slice))) {
			return;
		}
	}

	const auto reachedTotal = _topicProcess->totalCount > 0
		&& _topicProcess->processedCount >= _topicProcess->totalCount;

	if (!_topicProcess->lastSlice && !reachedTotal) {
		requestTopicMessagesSlice();
	} else {
		finishTopicMessages();
	}
}

bool ApiWrap::loadTopicMessageFileProgress(
		int index,
		FileProgress progress) {
	Expects(_fileProcess != nullptr);
	Expects(_topicProcess != nullptr);
	Expects(_topicProcess->slice.has_value());
	Expects(index >= 0 && index < _topicProcess->slice->list.size());
	Expects(_topicProcess->fileIndex == index);

	return _topicProcess->fileProgress(DownloadProgress{
		.randomId = _fileProcess->randomId,
		.path = _fileProcess->relativePath,
		.itemIndex = index,
		.ready = progress.ready,
		.total = progress.total });
}

void ApiWrap::loadTopicMessageFileDone(
		int index,
		Data::File *file,
		const QString &relativePath) {
	Expects(_topicProcess != nullptr);
	Expects(_topicProcess->slice.has_value());
	Expects(file != nullptr);
	Expects(index >= 0 && index < _topicProcess->slice->list.size());
	Expects(_topicProcess->fileIndex == index);
	Expects(_topicProcess->messageFileWorkMessageIndex == index);
	Expects(_topicProcess->messageFileWorkIndex >= 0);
	Expects(_topicProcess->messageFileWorkIndex
		< _topicProcess->messageFileWork.size());
	Expects(_topicProcess->messageFileWork[
		_topicProcess->messageFileWorkIndex].file == file);

	file->relativePath = relativePath;
	if (relativePath.isEmpty()) {
		file->skipReason = Data::File::SkipReason::Unavailable;
	}
	loadNextTopicMessageFile();
}

void ApiWrap::finishTopicMessages() {
	Expects(_topicProcess != nullptr);
	Expects(!_topicProcess->slice.has_value());

	const auto process = base::take(_topicProcess);
	process->done();
}

bool ApiWrap::processFileLoad(
		Data::File &file,
		const Data::FileOrigin &origin,
		Fn<bool(FileProgress)> progress,
		FnMut<void(QString)> done,
		const FilePolicy &policy) {
	using SkipReason = Data::File::SkipReason;

	Expects(_settings != nullptr);
	Expects(policy.message != nullptr);
	Expects(policy.type != MediaSettings::Type());

	if (!file.relativePath.isEmpty()
		|| file.skipReason != SkipReason::None) {
		return true;
	} else if (Data::SkipMessageByDate(*policy.message, *_settings)) {
		file.skipReason = SkipReason::DateLimits;
		return true;
	} else if (!file.location && file.content.isEmpty()) {
		file.skipReason = SkipReason::Unavailable;
		return true;
	} else if ((_settings->media.types & policy.type) != policy.type) {
		file.skipReason = SkipReason::FileType;
		return true;
	} else if (policy.controllingSize > _settings->media.sizeLimit) {
		file.skipReason = SkipReason::FileSize;
		return true;
	} else if (writePreloadedFile(file, origin)) {
		return !file.relativePath.isEmpty();
	}
	loadFile(file, origin, std::move(progress), std::move(done));
	return false;
}

bool ApiWrap::processFileLoad(
		Data::File &file,
		const Data::FileOrigin &origin,
		Fn<bool(FileProgress)> progress,
		FnMut<void(QString)> done,
		Data::Message *message,
		Data::Story *story) {
	using SkipReason = Data::File::SkipReason;

	if (!file.relativePath.isEmpty()
		|| file.skipReason != SkipReason::None) {
		return true;
	} else if (!file.location && file.content.isEmpty()) {
		file.skipReason = SkipReason::Unavailable;
		return true;
	} else if (writePreloadedFile(file, origin)) {
		return !file.relativePath.isEmpty();
	}

	const auto media = message
		? &message->media
		: story
		? &story->media
		: nullptr;
	const auto type = media
		? OrdinaryMediaType(*media)
		: MediaSettings::Type(0);

	const auto fullSize = message
		? message->file().size
		: story
		? story->file().size
		: file.size;
	if (message && Data::SkipMessageByDate(*message, *_settings)) {
		file.skipReason = SkipReason::DateLimits;
		return true;
	} else if (!story && (_settings->media.types & type) != type) {
		file.skipReason = SkipReason::FileType;
		return true;
	} else if (!story && fullSize > _settings->media.sizeLimit) {
		// Don't load thumbs for large files that we skip.
		file.skipReason = SkipReason::FileSize;
		return true;
	}
	loadFile(file, origin, std::move(progress), std::move(done));
	return false;
}

bool ApiWrap::writePreloadedFile(
		Data::File &file,
		const Data::FileOrigin &origin) {
	Expects(_settings != nullptr);

	using namespace Output;

	if (const auto path = _fileCache->find(file.location)) {
		file.relativePath = *path;
		return true;
	} else if (!file.content.isEmpty()) {
		const auto process = prepareFileProcess(file, origin);
		if (const auto result = process->file.writeBlock(file.content)) {
			file.relativePath = process->relativePath;
			_fileCache->save(file.location, file.relativePath);
		} else {
			ioError(result);
		}
		return true;
	}
	return false;
}

void ApiWrap::loadFile(
		const Data::File &file,
		const Data::FileOrigin &origin,
		Fn<bool(FileProgress)> progress,
		FnMut<void(QString)> done) {
	Expects(_fileProcess == nullptr);
	Expects(file.location.dcId != 0
		|| file.location.data.type() == mtpc_inputTakeoutFileLocation);

	_fileProcess = prepareFileProcess(file, origin);
	_fileProcess->progress = std::move(progress);
	_fileProcess->done = std::move(done);

	if (_fileProcess->progress) {
		const auto progress = FileProgress{
			_fileProcess->file.size(),
			_fileProcess->size
		};
		if (!_fileProcess->progress(progress)) {
			return;
		}
	}

	loadFilePart();

	Ensures(_fileProcess->requestId != 0);
}

auto ApiWrap::prepareFileProcess(
	const Data::File &file,
	const Data::FileOrigin &origin) const
-> std::unique_ptr<FileProcess> {
	Expects(_settings != nullptr);

	const auto relativePath = Output::File::PrepareRelativePath(
		_settings->path,
		file.suggestedPath);
	auto result = std::make_unique<FileProcess>(
		_settings->path + relativePath,
		_stats);
	result->relativePath = relativePath;
	result->location = file.location;
	result->size = file.size;
	result->origin = origin;
	result->randomId = base::RandomValue<uint64>();
	return result;
}

void ApiWrap::loadFilePart() {
	if (!_fileProcess
		|| _fileProcess->requestId
		|| _fileProcess->requests.size() >= kFileRequestsCount
		|| (_fileProcess->size > 0
			&& _fileProcess->offset >= _fileProcess->size)) {
		return;
	}

	const auto offset = _fileProcess->offset;
	_fileProcess->requests.push_back({ offset });
	_fileProcess->requestId = fileRequest(
		_fileProcess->location,
		_fileProcess->offset
	).done([=](const MTPupload_File &result) {
		_fileProcess->requestId = 0;
		filePartDone(offset, result);
	}).send();
	_fileProcess->offset += kFileChunkSize;

	if (_fileProcess->size > 0
		&& _fileProcess->requests.size() < kFileRequestsCount) {
		// Only one request at a time supported right now.
		//const auto runner = _runner;
		//crl::on_main([=] {
		//	QTimer::singleShot(kFileNextRequestDelay, [=] {
		//		runner([=] {
		//			loadFilePart();
		//		});
		//	});
		//});
	}
}

void ApiWrap::filePartDone(int64 offset, const MTPupload_File &result) {
	Expects(_fileProcess != nullptr);
	Expects(!_fileProcess->requests.empty());

	if (result.type() == mtpc_upload_fileCdnRedirect) {
		error("Cdn redirect is not supported.");
		return;
	}
	const auto &data = result.c_upload_file();
	if (data.vbytes().v.isEmpty()) {
		if (_fileProcess->size > 0) {
			error("Empty bytes received in file part.");
			return;
		}
		const auto result = _fileProcess->file.writeBlock({});
		if (!result) {
			ioError(result);
			return;
		}
	} else {
		using Request = FileProcess::Request;
		auto &requests = _fileProcess->requests;
		const auto i = ranges::find(
			requests,
			offset,
			[](const Request &request) { return request.offset; });
		Assert(i != end(requests));

		i->bytes = data.vbytes().v;

		auto &file = _fileProcess->file;
		while (!requests.empty() && !requests.front().bytes.isEmpty()) {
			const auto &bytes = requests.front().bytes;
			if (const auto result = file.writeBlock(bytes); !result) {
				ioError(result);
				return;
			}
			requests.pop_front();
		}

		if (_fileProcess->progress) {
			_fileProcess->progress(FileProgress{
				file.size(),
				_fileProcess->size });
		}

		if (!requests.empty()
			|| !_fileProcess->size
			|| _fileProcess->size > _fileProcess->offset) {
			loadFilePart();
			return;
		}
	}

	auto process = base::take(_fileProcess);
	const auto relativePath = process->relativePath;
	_fileCache->save(process->location, relativePath);
	process->done(process->relativePath);
}

QString ApiWrap::filePartMediaFolder() const {
	if (_chatProcess) {
		return _chatProcess->info.relativePath;
	} else if (_topicProcess) {
		return _topicProcess->relativePath;
	}
	return QString();
}

void ApiWrap::filePartRetryReference(
		int64 offset,
		Data::FileLocation location) {
	Expects(_fileProcess != nullptr);
	Expects(_fileProcess->requestId == 0);

	_fileProcess->location = std::move(location);
	_fileProcess->requestId = fileRequest(
		_fileProcess->location,
		offset
	).done([=](const MTPupload_File &result) {
		_fileProcess->requestId = 0;
		filePartDone(offset, result);
	}).send();
}

void ApiWrap::filePartRefreshReference(int64 offset) {
	Expects(_fileProcess != nullptr);
	Expects(_fileProcess->requestId == 0);

	const auto origin = _fileProcess->origin;
	if (origin.storyId) {
		_fileProcess->requestId = mainRequest(MTPstories_GetStoriesByID(
			MTP_inputPeerSelf(),
			MTP_vector<MTPint>(1, MTP_int(origin.storyId))
		)).fail([=](const MTP::Error &error) {
			_fileProcess->requestId = 0;
			filePartUnavailable();
			return true;
		}).done([=](const MTPstories_Stories &result) {
			_fileProcess->requestId = 0;
			filePartExtractReference(offset, result);
		}).send();
		return;
	}
	if (origin.customEmojiId) {
		const auto folder = filePartMediaFolder();
		_fileProcess->requestId = mainRequest(
			MTPmessages_GetCustomEmojiDocuments(
				MTP_vector<MTPlong>(
					1,
					MTP_long(origin.customEmojiId)))
		).fail([=](const MTP::Error &) {
			_fileProcess->requestId = 0;
			filePartUnavailable();
			return true;
		}).done([=](const MTPVector<MTPDocument> &result) {
			_fileProcess->requestId = 0;
			filePartExtractCustomEmojiReference(
				offset,
				origin.customEmojiId,
				folder,
				result);
		}).send();
		return;
	}
	if (origin.richMessage) {
		if (!origin.messageId) {
			filePartUnavailable();
			return;
		}
		const auto folder = filePartMediaFolder();
		_fileProcess->requestId = mainRequest(MTPmessages_GetRichMessage(
			origin.peer,
			MTP_int(origin.messageId)
		)).fail([=](const MTP::Error &) {
			_fileProcess->requestId = 0;
			filePartUnavailable();
			return true;
		}).done([=](const MTPmessages_Messages &result) {
			_fileProcess->requestId = 0;
			filePartExtractRichReference(
				offset,
				origin,
				folder,
				result);
		}).send();
		return;
	}
	if (!origin.messageId) {
		error("FILE_REFERENCE error for non-message file.");
		return;
	}
	if (origin.peer.type() == mtpc_inputPeerChannel
		|| origin.peer.type() == mtpc_inputPeerChannelFromMessage) {
		const auto channel = (origin.peer.type() == mtpc_inputPeerChannel)
			? MTP_inputChannel(
				origin.peer.c_inputPeerChannel().vchannel_id(),
				origin.peer.c_inputPeerChannel().vaccess_hash())
			: MTP_inputChannelFromMessage(
				origin.peer.c_inputPeerChannelFromMessage().vpeer(),
				origin.peer.c_inputPeerChannelFromMessage().vmsg_id(),
				origin.peer.c_inputPeerChannelFromMessage().vchannel_id());
		_fileProcess->requestId = mainRequest(MTPchannels_GetMessages(
			channel,
			MTP_vector<MTPInputMessage>(
				1,
				MTP_inputMessageID(MTP_int(origin.messageId)))
		)).fail([=](const MTP::Error &error) {
			_fileProcess->requestId = 0;
			filePartUnavailable();
			return true;
		}).done([=](const MTPmessages_Messages &result) {
			_fileProcess->requestId = 0;
			filePartExtractReference(offset, result);
		}).send();
	} else {
		_fileProcess->requestId = splitRequest(
			origin.split,
			MTPmessages_GetMessages(
				MTP_vector<MTPInputMessage>(
					1,
					MTP_inputMessageID(MTP_int(origin.messageId)))
			)
		).fail([=](const MTP::Error &error) {
			_fileProcess->requestId = 0;
			filePartUnavailable();
			return true;
		}).done([=](const MTPmessages_Messages &result) {
			_fileProcess->requestId = 0;
			filePartExtractReference(offset, result);
		}).send();
	}
}

void ApiWrap::filePartExtractCustomEmojiReference(
		int64 offset,
		uint64 customEmojiId,
		const QString &folder,
		const MTPVector<MTPDocument> &result) {
	Expects(_fileProcess != nullptr);
	Expects(_fileProcess->requestId == 0);
	Expects(_selfId.has_value());

	auto context = Data::ParseMediaContext();
	context.selfPeerId = peerFromUser(*_selfId);
	auto document = std::optional<Data::Document>();
	auto matches = 0;
	for (const auto &entry : result.v) {
		auto parsed = Data::ParseDocument(
			context,
			entry,
			folder,
			TimeId());
		if (parsed.id != customEmojiId) {
			continue;
		}
		++matches;
		if (matches == 1) {
			document = std::move(parsed);
		}
	}
	if (matches != 1 || !document) {
		filePartUnavailable();
		return;
	}
	auto refreshed = _fileProcess->location;
	const auto mainRefreshed = Data::RefreshFileReference(
		refreshed,
		document->file.location);
	const auto thumbRefreshed = !mainRefreshed
		&& document->thumb.width > 0
		&& Data::RefreshFileReference(
			refreshed,
			document->thumb.file.location);
	if (!mainRefreshed && !thumbRefreshed) {
		filePartUnavailable();
		return;
	}
	filePartRetryReference(offset, std::move(refreshed));
}

void ApiWrap::filePartExtractRichReference(
		int64 offset,
		const Data::FileOrigin &origin,
		const QString &folder,
		const MTPmessages_Messages &result) {
	Expects(_fileProcess != nullptr);
	Expects(_fileProcess->requestId == 0);
	Expects(_selfId.has_value());

	const auto richMessage = ExtractFullRichMessage(
		result,
		origin.messageId);
	if (!richMessage) {
		filePartUnavailable();
		return;
	}
	auto context = Data::ParseMediaContext();
	context.selfPeerId = peerFromUser(*_selfId);
	const auto parsed = Data::ParseRichMessage(
		context,
		*richMessage,
		folder,
		TimeId());
	if (parsed.part) {
		filePartUnavailable();
		return;
	}
	const auto refreshed = RefreshRichMessageFileReference(
		_fileProcess->location,
		parsed);
	if (!refreshed) {
		filePartUnavailable();
		return;
	}
	filePartRetryReference(offset, *refreshed);
}

void ApiWrap::filePartExtractReference(
		int64 offset,
		const MTPmessages_Messages &result) {
	Expects(_fileProcess != nullptr);
	Expects(_fileProcess->requestId == 0);

	const auto folder = filePartMediaFolder();
	result.match([&](const MTPDmessages_messagesNotModified &data) {
		error("Unexpected messagesNotModified received.");
	}, [&](const auto &data) {
		Expects(_selfId.has_value());

		auto context = Data::ParseMediaContext();
		context.selfPeerId = peerFromUser(*_selfId);
		const auto messages = Data::ParseMessagesSlice(
			context,
			data.vmessages(),
			data.vusers(),
			data.vchats(),
			folder);
		for (const auto &message : messages.list) {
			if (message.id == _fileProcess->origin.messageId) {
				auto refreshed = _fileProcess->location;
				if (Data::RefreshFileReference(
						refreshed,
						message.file().location)
					|| Data::RefreshFileReference(
						refreshed,
						message.thumb().file.location)) {
					filePartRetryReference(
						offset,
						std::move(refreshed));
					return;
				}
			}
		}
		filePartUnavailable();
	});
}

void ApiWrap::filePartExtractReference(
		int64 offset,
		const MTPstories_Stories &result) {
	Expects(_fileProcess != nullptr);
	Expects(_fileProcess->requestId == 0);

	const auto stories = Data::ParseStoriesSlice(
		result.data().vstories(),
		0);
	for (const auto &story : stories.list) {
		if (story.id == _fileProcess->origin.storyId) {
			auto refreshed = _fileProcess->location;
			if (Data::RefreshFileReference(
					refreshed,
					story.file().location)
				|| Data::RefreshFileReference(
					refreshed,
					story.thumb().file.location)) {
				filePartRetryReference(
					offset,
					std::move(refreshed));
				return;
			}
		}
	}
	filePartUnavailable();
}

void ApiWrap::filePartUnavailable() {
	Expects(_fileProcess != nullptr);
	Expects(!_fileProcess->requests.empty());

	LOG(("Export Error: File unavailable."));

	base::take(_fileProcess)->done(QString());
}

void ApiWrap::error(const MTP::Error &error) {
	_errors.fire_copy(error);
}

void ApiWrap::error(const QString &text) {
	error(MTP::Error(
		MTP_rpc_error(MTP_int(0), MTP_string("API_ERROR: " + text))));
}

void ApiWrap::ioError(const Output::Result &result) {
	_ioErrors.fire_copy(result);
}

ApiWrap::~ApiWrap() = default;

} // namespace Export
