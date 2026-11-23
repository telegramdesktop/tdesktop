/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_bot_media_previews.h"

#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"

namespace Api {
namespace {

[[nodiscard]] MTPInputMedia ToInputMedia(const BotPreviewMedia &media) {
	const auto photo = [](not_null<PhotoData*> data) {
		return MTP_inputMediaPhoto(
			MTP_flags(0),
			data->mtpInput(),
			MTP_int(0),
			MTPInputDocument());
	};
	const auto file = [](not_null<DocumentData*> data) {
		return MTP_inputMediaDocument(
			MTP_flags(0),
			data->mtpInput(),
			MTP_inputPhotoEmpty(),
			MTP_int(0),
			MTP_int(0),
			MTPstring());
	};
	return v::match(media, photo, file);
}

[[nodiscard]] MTPVector<MTPInputMedia> ToInputMediaVector(
		const std::vector<BotPreviewMedia> &list) {
	auto media = QVector<MTPInputMedia>();
	media.reserve(list.size());
	for (const auto &item : list) {
		media.push_back(ToInputMedia(item));
	}
	return MTP_vector<MTPInputMedia>(std::move(media));
}

} // namespace

BotMediaPreviews::BotMediaPreviews(not_null<UserData*> bot)
: _bot(bot)
, _api(&bot->session().mtp()) {
}

void BotMediaPreviews::load(
		const std::optional<QString> &language,
		Fn<void(BotMediaPreviewList)> done,
		Fail fail) {
	const auto failed = [fail = std::move(fail)](
			const MTP::Error &error) {
		fail(error.type());
	};
	if (language) {
		_api.request(
			MTPbots_GetPreviewInfo(
				_bot->inputUser(),
				MTP_string(*language)
			)).done([=](const MTPbots_PreviewInfo &result) {
			const auto &data = result.data();
			auto list = BotMediaPreviewList{
				.media = parse(data.vmedia()),
			};
			list.languages.reserve(data.vlang_codes().v.size());
			for (const auto &code : data.vlang_codes().v) {
				list.languages.push_back(qs(code));
			}
			done(std::move(list));
		}).fail(failed).handleFloodErrors().send();
	} else {
		_api.request(
			MTPbots_GetPreviewMedias(
				_bot->inputUser()
			)).done([=](
				const MTPVector<MTPBotPreviewMedia> &result) {
			done({ .media = parse(result) });
		}).fail(failed).handleFloodErrors().send();
	}
}

void BotMediaPreviews::save(
		const QString &language,
		std::optional<BotPreviewMedia> replaced,
		MTPInputMedia media,
		Published done,
		Fail fail) {
	const auto lang = language.isEmpty() ? u"default"_q : language;
	const auto method = replaced
		? u"bots.editPreviewMedia"_q
		: u"bots.addPreviewMedia"_q;
	const auto published = [=](const MTPBotPreviewMedia &result) {
		LOG(("Bot preview %1 accepted lang=%2").arg(method, lang));
		done(parse(result.data().vmedia()));
	};
	const auto publishFailed = [=](QString error) {
		LOG(("Bot preview %1 failed lang=%2 error=%3"
			).arg(method, lang, error));
		fail(std::move(error));
	};
	const auto uploadFailed = [=](QString error) {
		LOG(("Bot preview messages.uploadMedia failed "
			"lang=%1 error=%2"
			).arg(lang, error));
		fail(std::move(error));
	};
	const auto uploaded = [=](const MTPMessageMedia &result) {
		if (const auto parsed = parse(result)) {
			LOG(("Bot preview messages.uploadMedia accepted "
				"lang=%1"
				).arg(lang));
			saveUploaded(
				language,
				replaced,
				ToInputMedia(*parsed),
				published,
				publishFailed);
		} else {
			uploadFailed(u"BOT_PREVIEW_UPLOAD_FAILED"_q);
		}
	};
	const auto failed = [=](const MTP::Error &error) {
		uploadFailed(error.type());
	};
	_api.request(
		MTPmessages_UploadMedia(
			MTP_flags(0),
			MTPstring(),
			_bot->input(),
			media
		)).done(uploaded).fail(failed).handleFloodErrors().send();
}

void BotMediaPreviews::saveUploaded(
		const QString &language,
		std::optional<BotPreviewMedia> replaced,
		MTPInputMedia media,
		Fn<void(const MTPBotPreviewMedia&)> done,
		Fail fail) {
	if (replaced) {
		const auto request = [=] {
			return MTPbots_EditPreviewMedia(
				_bot->inputUser(),
				MTP_string(language),
				ToInputMedia(*replaced),
				media);
		};
		sendMutation(
			request,
			language,
			std::move(done),
			std::move(fail),
			true);
	} else {
		const auto request = [=] {
			return MTPbots_AddPreviewMedia(
				_bot->inputUser(),
				MTP_string(language),
				media);
		};
		sendMutation(
			request,
			language,
			std::move(done),
			std::move(fail),
			false);
	}
}

void BotMediaPreviews::remove(
		const QString &language,
		std::vector<BotPreviewMedia> media,
		Done done,
		Fail fail) {
	const auto request = [=, media = std::move(media)] {
		return MTPbots_DeletePreviewMedia(
			_bot->inputUser(),
			MTP_string(language),
			ToInputMediaVector(media));
	};
	sendMutation(
		request,
		language,
		std::move(done),
		std::move(fail),
		true);
}

void BotMediaPreviews::reorder(
		const QString &language,
		std::vector<BotPreviewMedia> order,
		Done done,
		Fail fail) {
	const auto request = [=, order = std::move(order)] {
		return MTPbots_ReorderPreviewMedias(
			_bot->inputUser(),
			MTP_string(language),
			ToInputMediaVector(order));
	};
	sendMutation(
		request,
		language,
		std::move(done),
		std::move(fail),
		true);
}

template <typename Request, typename Callback>
void BotMediaPreviews::sendMutation(
		Request request,
		const QString &language,
		Callback done,
		Fail fail,
		bool refreshReferences) {
	using Method = std::invoke_result_t<Request>;
	using Result = typename Method::ResponseType;
	const auto completed = [=](const Result &result) {
		if constexpr (std::is_same_v<Result, MTPBool>) {
			if (!mtpIsTrue(result)) {
				fail(u"BOT_PREVIEW_UPDATE_FAILED"_q);
				return;
			}
		}
		_bot->session().api().reloadFullPeer(_bot);
		if constexpr (std::is_invocable_v<Callback, const Result&>) {
			done(result);
		} else {
			done();
		}
	};
	const auto failed = [=](const MTP::Error &error) {
		const auto type = error.type();
		const auto expired = type.startsWith(u"FILE_REFERENCE_"_q);
		if (!refreshReferences || !expired) {
			fail(type);
			return;
		}
		const auto origin = Data::FileOriginBotMediaPreview{
			.botId = peerToUser(_bot->id),
			.language = language,
		};
		const auto weak = base::make_weak(this);
		const auto retry = [=](
				const Data::UpdatedFileReferences &refs) {
			if (!weak) {
				return;
			}
			if (refs.data.empty()) {
				fail(type);
			} else {
				sendMutation(
					request,
					language,
					done,
					fail,
					false);
			}
		};
		_bot->session().api().refreshFileReference(origin, retry);
	};
	_api.request(
		request()
	).done(
		completed
	).fail(
		failed
	).handleFloodErrors().send();
}

std::vector<BotPreviewMedia> BotMediaPreviews::parse(
		const MTPVector<MTPBotPreviewMedia> &list) const {
	auto result = std::vector<BotPreviewMedia>();
	result.reserve(list.v.size());
	for (const auto &entry : list.v) {
		if (auto parsed = parse(entry.data().vmedia())) {
			result.push_back(*parsed);
		}
	}
	return result;
}

std::optional<BotPreviewMedia> BotMediaPreviews::parse(
		const MTPMessageMedia &media) const {
	auto result = std::optional<BotPreviewMedia>();
	auto &owner = _bot->owner();
	const auto collect = [&](auto parsed) {
		if (!parsed->isNull()) {
			result = parsed;
		}
	};
	const auto photo = [&](const MTPDmessageMediaPhoto &data) {
		if (const auto value = data.vphoto()) {
			collect(owner.processPhoto(*value));
		}
	};
	const auto file = [&](const MTPDmessageMediaDocument &data) {
		if (const auto value = data.vdocument()) {
			const auto document = owner.processDocument(
				*value,
				data.valt_documents());
			if (document->isVideoFile() || document->isGifv()) {
				collect(document);
			}
		}
	};
	media.match(photo, file, [](const auto &) {});
	return result;
}

} // namespace Api
