/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_session.h"

#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <crl/crl_async.h>
#include <crl/crl_on_main.h>
#include <rpl/variable.h>

#include "api/api_sending.h"
#include "api/api_editing.h"
#include "apiwrap.h"
#include "base/algorithm.h"
#include "base/flat_map.h"
#include "base/timer.h"
#include "base/weak_qptr.h"
#include "base/weak_ptr.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/application.h"
#include "data/data_file_origin.h"
#include "core/shortcuts.h"
#include "data/components/ephemeral_messages.h"
#include "data/data_drafts.h"
#include "data/data_document.h"
#include "data/data_location.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "iv/iv_cached_media.h"
#include "iv/editor/iv_editor_box.h"
#include "iv/editor/iv_editor_state.h"
#include "iv/editor/iv_editor_widget.h"
#include "iv/iv_instance.h"
#include "iv/iv_rich_message_serializer.h"
#include "iv/iv_rich_page.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "menu/menu_send.h"
#include "settings/sections/settings_premium.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "storage/storage_account.h"
#include "storage/storage_media_prepare.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/controls/location_picker.h"
#include "ui/controls/send_button.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/separate_panel.h"
#include "window/window_session_controller.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "styles/style_boxes.h"
#include "styles/style_iv.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Iv::Editor {
namespace {

using PreparedFile = Ui::PreparedFile;
using PreparedFileType = Ui::PreparedFile::Type;
using PreparedList = Ui::PreparedList;

constexpr auto kRichDraftAutosaveTimeout = crl::time(10 * 1000);

class ArticleSession;

struct ComposeThreadKey {
	Main::Session *session = nullptr;
	PeerId peerId = 0;
	::Data::DraftKey draftKey = ::Data::DraftKey::None();

	friend inline auto operator<=>(ComposeThreadKey, ComposeThreadKey) = default;
};

struct ComposeThreadEntry {
	std::weak_ptr<ArticleSession> articleSession;
	rpl::variable<bool> fieldVisible = false;
	ThreadFieldDraftReader readDraft;
	ThreadFieldDraftSaver saveDraft;
	ThreadFieldMigratedAway migratedAway;
};

[[nodiscard]] ComposeThreadKey ComposeKey(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		PeerId monoforumPeerId) {
	return {
		.session = session.get(),
		.peerId = peerId,
		.draftKey = ::Data::DraftKey::Cloud(topicRootId, monoforumPeerId),
	};
}

[[nodiscard]] ::Data::FileOrigin ComposeDraftOrigin(
		const ComposeThreadKey &key) {
	return ::Data::FileOriginCloudDraft{
		.peerId = key.peerId,
		.topicRootId = key.draftKey.topicRootId(),
		.monoforumPeerId = key.draftKey.monoforumPeerId(),
	};
}

[[nodiscard]] auto &ComposeThreads() {
	static auto result = base::flat_map<ComposeThreadKey, ComposeThreadEntry>();
	return result;
}

[[nodiscard]] ComposeThreadEntry &ComposeThreadEntryFor(
		const ComposeThreadKey &key) {
	return ComposeThreads()[key];
}

[[nodiscard]] ComposeThreadEntry *LookupComposeThreadEntry(
		const ComposeThreadKey &key) {
	auto &threads = ComposeThreads();
	const auto i = threads.find(key);
	return (i != end(threads)) ? &i->second : nullptr;
}

void SetComposeFieldVisible(
		const ComposeThreadKey &key,
		bool visible) {
	ComposeThreadEntryFor(key).fieldVisible.force_assign(visible);
}

enum class AttachmentState : uchar {
	Uploading,
	Finalizing,
	Ready,
	Failed,
};

enum class AttachmentInsertMode : uchar {
	Normal,
	ClipboardPaste,
	ReplaceBlock,
};

struct PreparedDocumentInfo {
	QSize dimensions;
	QString title;
	QString performer;
	QString fileName;
	int duration = 0;
	bool audio = false;
	bool animation = false;
	bool video = false;
};

struct AttachmentMeta {
	PreparedFileType type = PreparedFileType::None;
	RichPage::BlockKind blockKind = RichPage::BlockKind::Unsupported;
	QString caption;
	QString displayName;
	QSize dimensions;
	QString audioTitle;
	QString audioPerformer;
	QString audioFileName;
	int audioDuration = 0;
	bool spoiler = false;
	bool autoplay = false;
	bool loop = false;
};

class PrepareAttachmentTask final : public Task {
public:
	PrepareAttachmentTask(
		FileLoadTask::Args &&args,
		Fn<void(std::shared_ptr<FilePrepareResult>)> done)
	: _task(std::move(args))
	, _done(std::move(done)) {
	}

	void process() override {
		_task.process({});
	}

	void finish() override {
		_done(_task.peekResult());
	}

private:
	FileLoadTask _task;
	Fn<void(std::shared_ptr<FilePrepareResult>)> _done;

};

[[nodiscard]] Ui::LocationPickerConfig ResolveMapsConfig(
		not_null<Main::Session*> session) {
	const auto &appConfig = session->appConfig();
	auto map = appConfig.get<base::flat_map<QString, QString>>(
		u"tdesktop_config_map"_q,
		base::flat_map<QString, QString>());
	return {
		.mapsToken = map[u"maps"_q],
		.geoToken = map[u"geo"_q],
	};
}

[[nodiscard]] QString PreparedFileName(const PreparedFile &file) {
	return file.displayName.isEmpty()
		? QFileInfo(file.path).fileName()
		: file.displayName;
}

[[nodiscard]] bool AcceptedPreparedFileType(PreparedFileType type) {
	return (type == PreparedFileType::Photo)
		|| (type == PreparedFileType::Video)
		|| (type == PreparedFileType::Music);
}

[[nodiscard]] bool CanUseRichMessages(not_null<Main::Session*> session) {
	return session->premium();
}

enum class RichMessagePosting {
	Disabled,
	Premium,
	Enabled,
};

[[nodiscard]] RichMessagePosting RichMessagePostingMode(
		not_null<Main::Session*> session) {
	const auto value = session->appConfig().get<QString>(
		u"rich_message_posting"_q,
		u"disabled"_q);
	if (value == u"enabled"_q) {
		return RichMessagePosting::Enabled;
	} else if (value == u"premium"_q) {
		return RichMessagePosting::Premium;
	}
	return RichMessagePosting::Disabled;
}

[[nodiscard]] bool IsRichMessageMediaKind(RichPage::BlockKind kind) {
	switch (kind) {
	case RichPage::BlockKind::Photo:
	case RichPage::BlockKind::Video:
	case RichPage::BlockKind::Audio:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] bool IsPhotoVideoRichMessageKind(RichPage::BlockKind kind) {
	return (kind == RichPage::BlockKind::Photo)
		|| (kind == RichPage::BlockKind::Video);
}

void CountRichPageMedia(
		const std::vector<RichPage::Block> &blocks,
		int *result) {
	for (const auto &block : blocks) {
		if (IsRichMessageMediaKind(block.kind)) {
			++(*result);
		}
		CountRichPageMedia(block.blocks, result);
		for (const auto &item : block.listItems) {
			CountRichPageMedia(item.blocks, result);
		}
		for (const auto &item : block.mediaItems) {
			if (IsRichMessageMediaKind(item.kind)) {
				++(*result);
			}
		}
	}
}

[[nodiscard]] int CountRichPageMedia(const RichPage &page) {
	auto result = 0;
	CountRichPageMedia(page.blocks, &result);
	return result;
}

template <typename Container>
[[nodiscard]] int CountAcceptedPreparedFiles(const Container &files) {
	auto result = 0;
	for (const auto &file : files) {
		if (AcceptedPreparedFileType(file.type)) {
			++result;
		}
	}
	return result;
}

[[nodiscard]] int CountAcceptedPreparedFiles(const PreparedList &list) {
	return CountAcceptedPreparedFiles(list.files)
		+ int(list.filesToProcess.size());
}

[[nodiscard]] bool IsReplacing(
		AttachmentInsertMode insertMode,
		const std::optional<State::ReplaceTarget> &replaceTarget) {
	return (insertMode == AttachmentInsertMode::ReplaceBlock)
		&& replaceTarget.has_value();
}

[[nodiscard]] RichPage::RichText ToRichText(QString text) {
	auto result = RichPage::RichText();
	result.text.text = std::move(text);
	return result;
}

[[nodiscard]] RichPage::BlockKind BlockKindForPreparedType(
		PreparedFileType type) {
	switch (type) {
	case PreparedFileType::Photo:
		return RichPage::BlockKind::Photo;
	case PreparedFileType::Video:
		return RichPage::BlockKind::Video;
	case PreparedFileType::Music:
		return RichPage::BlockKind::Audio;
	default:
		return RichPage::BlockKind::Unsupported;
	}
}

[[nodiscard]] PreparedDocumentInfo DocumentInfoFromPrepared(
		const MTPDocument &document);

[[nodiscard]] RichPage::BlockKind BlockKindForPreparedResult(
		const FilePrepareResult &prepared) {
	if (prepared.type == SendMediaType::Photo) {
		return RichPage::BlockKind::Photo;
	}
	if (prepared.type != SendMediaType::File) {
		return RichPage::BlockKind::Unsupported;
	}
	const auto info = DocumentInfoFromPrepared(prepared.document);
	if (info.video) {
		return RichPage::BlockKind::Video;
	}
	if (info.audio) {
		return RichPage::BlockKind::Audio;
	}
	return RichPage::BlockKind::Unsupported;
}

[[nodiscard]] QSize PhotoSizeFromPrepared(const MTPPhoto &photo) {
	auto result = QSize();
	photo.match([](const MTPDphotoEmpty &) {
	}, [&](const MTPDphoto &data) {
		const auto assign = [&](const QString &type, int width, int height) {
			if (result.isEmpty() && (type == u"x"_q || type == u"w"_q)) {
				result = QSize(width, height);
			}
			if (type == u"y"_q) {
				result = QSize(width, height);
			}
		};
		for (const auto &size : data.vsizes().v) {
			size.match([](const MTPDphotoSizeEmpty &) {
			}, [&](const MTPDphotoSize &row) {
				assign(qs(row.vtype()), row.vw().v, row.vh().v);
			}, [&](const MTPDphotoCachedSize &row) {
				assign(qs(row.vtype()), row.vw().v, row.vh().v);
			}, [&](const MTPDphotoStrippedSize &) {
			}, [&](const MTPDphotoSizeProgressive &row) {
				assign(qs(row.vtype()), row.vw().v, row.vh().v);
			}, [&](const MTPDphotoPathSize &) {
			});
		}
	});
	return result;
}

[[nodiscard]] PreparedDocumentInfo DocumentInfoFromPrepared(
		const MTPDocument &document) {
	auto result = PreparedDocumentInfo();
	document.match([](const MTPDdocumentEmpty &) {
	}, [&](const MTPDdocument &data) {
		const auto assign = [&](int width, int height, bool force) {
			if (width <= 0 || height <= 0) {
				return;
			}
			if (force || result.dimensions.isEmpty()) {
				result.dimensions = QSize(width, height);
			}
		};
		for (const auto &attribute : data.vattributes().v) {
			attribute.match([&](const MTPDdocumentAttributeAudio &row) {
				result.audio = true;
				result.duration = row.vduration().v;
				result.title = qs(row.vtitle().value_or_empty());
				result.performer = qs(row.vperformer().value_or_empty());
			}, [&](const MTPDdocumentAttributeFilename &row) {
				result.fileName = qs(row.vfile_name());
			}, [&](const MTPDdocumentAttributeImageSize &row) {
				assign(row.vw().v, row.vh().v, false);
			}, [&](const MTPDdocumentAttributeAnimated &) {
				result.animation = true;
			}, [&](const MTPDdocumentAttributeVideo &row) {
				result.video = true;
				assign(row.vw().v, row.vh().v, true);
			}, [&](const auto &) {
			});
		}
	});
	return result;
}

[[nodiscard]] QVector<MTPDocumentAttribute> DocumentAttributesFromPrepared(
		const FilePrepareResult &prepared) {
	auto result = QVector<MTPDocumentAttribute>();
	prepared.document.match([&](const MTPDdocument &data) {
		result = data.vattributes().v;
	}, [](const auto &) {
	});
	return result;
}

[[nodiscard]] QVector<MTPInputDocument> ToInputDocumentVector(
		const std::vector<MTPInputDocument> &stickers) {
	auto result = QVector<MTPInputDocument>();
	result.reserve(int(stickers.size()));
	for (const auto &sticker : stickers) {
		result.push_back(sticker);
	}
	return result;
}

[[nodiscard]] AttachmentMeta BuildAttachmentMeta(const PreparedFile &file) {
	auto result = AttachmentMeta{
		.type = file.type,
		.blockKind = BlockKindForPreparedType(file.type),
		.caption = file.caption.text,
		.displayName = PreparedFileName(file),
		.dimensions = !file.shownDimensions.isEmpty()
			? file.shownDimensions
			: file.originalDimensions,
		.spoiler = file.spoiler,
	};
	if (!file.information) {
		result.audioFileName = result.displayName;
		return result;
	}
	if (const auto song = std::get_if<Ui::PreparedFileInformation::Song>(
			&file.information->media)) {
		result.audioTitle = song->title;
		result.audioPerformer = song->performer;
		result.audioDuration = int(song->duration / 1000);
		result.audioFileName = result.displayName;
	} else if (const auto video = std::get_if<Ui::PreparedFileInformation::Video>(
			&file.information->media)) {
		result.autoplay = video->isGifv;
		result.loop = video->isGifv;
	}
	return result;
}

[[nodiscard]] std::unique_ptr<FileLoadTask> BuildVideoCoverTask(
		not_null<Main::Session*> session,
		PeerId peer,
		std::unique_ptr<PreparedFile> file) {
	if (!file) {
		return nullptr;
	}
	return std::make_unique<FileLoadTask>(FileLoadTask::Args{
		.session = session,
		.filepath = file->path,
		.content = std::move(file->content),
		.information = std::move(file->information),
		.videoCover = nullptr,
		.type = SendMediaType::Photo,
		.to = FileLoadTo(
			peer,
			Api::SendOptions(),
			FullReplyTo(),
			MsgId()),
		.caption = TextWithTags(),
		.spoiler = false,
		.album = nullptr,
		.forceFile = false,
		.sendLargePhotos = false,
		.idOverride = 0,
		.displayName = file->displayName,
	});
}

[[nodiscard]] FileLoadTask::Args BuildPrepareTaskArgs(
		not_null<Main::Session*> session,
		PeerId peer,
		PreparedFile file) {
	const auto sendType = (file.type == PreparedFileType::Photo)
		? SendMediaType::Photo
		: SendMediaType::File;
	return {
		.session = session,
		.filepath = file.path,
		.content = std::move(file.content),
		.information = std::move(file.information),
		.videoCover = BuildVideoCoverTask(
			session,
			peer,
			std::move(file.videoCover)),
		.type = sendType,
		.to = FileLoadTo(
			peer,
			Api::SendOptions(),
			FullReplyTo(),
			MsgId()),
		.caption = TextWithTags(),
		.spoiler = file.spoiler,
		.album = std::make_shared<SendingAlbum>(),
		.forceFile = false,
		.sendLargePhotos = file.sendLargePhotos,
		.idOverride = 0,
		.displayName = file.displayName,
	};
}

class ArticleSession final
	: public std::enable_shared_from_this<ArticleSession>
	, public base::has_weak_ptr {
public:
	static void ShowCompose(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		Api::SendAction action,
		SendMenu::Details sendMenuDetails,
		base::weak_ptr<Window::SessionController> controller) {
		const auto history = action.history;
		const auto topicRootId = action.replyTo.topicRootId;
		const auto monoforumPeerId = action.replyTo.monoforumPeerId;
		const auto composeKey = ComposeKey(
			session,
			history->peer->id,
			topicRootId,
			monoforumPeerId);
		if (const auto entry = LookupComposeThreadEntry(composeKey)) {
			if (const auto existing = entry->articleSession.lock()) {
				SetComposeFieldVisible(composeKey, true);
				existing->focusWindow();
				return;
			}
		}
		const auto cloudDraft = history->cloudDraft(topicRootId, monoforumPeerId);
		const auto hasRichDraft = (cloudDraft && cloudDraft->hasRichMessage());
		const auto page = hasRichDraft
			? std::make_shared<RichPage>(*cloudDraft->richMessage)
			: std::make_shared<RichPage>();
		if (!hasRichDraft) {
			if (const auto entry = LookupComposeThreadEntry(composeKey)) {
				if (entry->readDraft) {
					if (const auto draft = entry->readDraft()) {
						const auto &withTags = draft->textWithTags;
						auto migrated = SplitTextIntoRichPage({
							withTags.text,
							TextUtilities::ConvertTextTagsToEntities(
								withTags.tags),
						});
						if (!migrated.blocks.empty()) {
							*page = std::move(migrated);
							if (entry->migratedAway) {
								entry->migratedAway();
							}
						}
					}
				}
			}
		}
		auto articleSession = std::shared_ptr<ArticleSession>(new ArticleSession(
			session,
			peer,
			Mode::Compose,
			FullMsgId(peer->id, session->data().nextLocalMessageId()),
			std::move(page),
			std::move(action),
			std::move(sendMenuDetails),
			std::nullopt,
			composeKey,
			std::move(controller)));
		articleSession->showWindow();
	}

	static void ShowEdit(
		not_null<HistoryItem*> item,
		std::shared_ptr<const RichPage> richPage,
		base::weak_ptr<Window::SessionController> controller) {
		if (!richPage || !CanEditRichPage(richPage)) {
			if (const auto window = item->history()->session().tryResolveWindow(
					item->history()->peer)) {
				window->showToast(tr::lng_edit_error(tr::now));
			}
			return;
		}
		auto articleSession = std::shared_ptr<ArticleSession>(new ArticleSession(
			&item->history()->session(),
			item->history()->peer,
			Mode::Edit,
			item->fullId(),
			std::make_shared<RichPage>(*richPage),
			std::nullopt,
			{},
			EditedItemSnapshot{
				.item = item,
				.inlinePage = item->richPage(),
				.summary = item->originalText(),
				.fullPage = item->fullRichPage(),
			},
			std::nullopt,
			std::move(controller)));
		articleSession->showWindow();
	}

	static void ShowEditFromField(
			not_null<HistoryItem*> item,
			Api::SendAction action,
			base::weak_ptr<Window::SessionController> controller) {
		const auto session = &item->history()->session();
		const auto topicRootId = action.replyTo.topicRootId;
		const auto monoforumPeerId = action.replyTo.monoforumPeerId;
		const auto composeKey = ComposeKey(
			session,
			item->history()->peer->id,
			topicRootId,
			monoforumPeerId);
		auto page = std::make_shared<RichPage>();
		if (const auto entry = LookupComposeThreadEntry(composeKey)) {
			if (entry->readDraft) {
				if (const auto draft = entry->readDraft()) {
					const auto &withTags = draft->textWithTags;
					*page = SplitTextIntoRichPage({
						withTags.text,
						TextUtilities::ConvertTextTagsToEntities(
							withTags.tags),
					});
				}
			}
			if (entry->migratedAway) {
				entry->migratedAway();
			}
		}
		auto articleSession = std::shared_ptr<ArticleSession>(new ArticleSession(
			session,
			item->history()->peer,
			Mode::Edit,
			item->fullId(),
			std::move(page),
			std::move(action),
			{},
			EditedItemSnapshot{
				.item = item,
				.inlinePage = item->richPage(),
				.summary = item->originalText(),
				.fullPage = item->fullRichPage(),
			},
			std::nullopt,
			std::move(controller)));
		articleSession->showWindow();
	}

	~ArticleSession() {
		_submitDeferred = false;
		for (const auto &attachment : _attachments) {
			_session->uploader().cancel(attachment.uploadId);
		}
	}

private:
	enum class Mode {
		Compose,
		Edit,
	};

	struct AttachmentRecord {
		FullMsgId uploadId;
		PreparedFileType type = PreparedFileType::None;
		RichPage::BlockKind blockKind = RichPage::BlockKind::Unsupported;
		uint64 localMediaId = 0;
		AttachmentState state = AttachmentState::Uploading;
		QString caption;
		QString filename;
		QString filemime;
		QVector<MTPDocumentAttribute> attributes;
		bool forceFile = false;
		QString audioTitle;
		QString audioPerformer;
		QString audioFileName;
		int audioDuration = 0;
		QSize dimensions;
		bool spoiler = false;
		bool autoplay = false;
		bool loop = false;
		std::vector<State::BlockPath> blockLocators;
		MTPInputPhoto inputPhoto;
		MTPInputDocument inputDocument;
		uint64 serverMediaId = 0;
		uint64 accessHash = 0;
		QByteArray fileReference;
		::Data::FileOrigin origin;
		PhotoData *serverPhoto = nullptr;
		DocumentData *serverDocument = nullptr;
	};

	enum class MediaBatchItemState : uchar {
		Waiting,
		Ready,
		Skipped,
		Inserted,
	};

	struct MediaBatchItem {
		MediaBatchItemState state = MediaBatchItemState::Waiting;
		FullMsgId uploadId = FullMsgId();
		RichPage::BlockKind blockKind = RichPage::BlockKind::Unsupported;
	};

	struct MediaBatch {
		uint64 id = 0;
		QPointer<Widget> editor;
		AttachmentInsertMode insertMode = AttachmentInsertMode::Normal;
		std::optional<PreparedMediaPasteTarget> insertTarget;
		std::vector<MediaBatchItem> items;
		int nextIndex = 0;
		std::optional<State::BlockPath> groupAnchor;
		int insertedTopLevel = 0;
	};

	struct QueuedPrepare {
		QPointer<Widget> editor;
		PreparedFile file;
		uint64 batchId = 0;
		int order = 0;
		AttachmentInsertMode insertMode = AttachmentInsertMode::Normal;
		std::optional<State::ReplaceTarget> replaceTarget;
	};

	struct EditedItemSnapshot {
		not_null<HistoryItem*> item;
		std::shared_ptr<const RichPage> inlinePage;
		TextWithEntities summary;
		std::shared_ptr<const RichPage> fullPage;
	};

	struct PendingPhotoEditSource {
		std::shared_ptr<::Data::PhotoMedia> media;
		Fn<void(QImage)> done;
	};

	ArticleSession(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		Mode mode,
		FullMsgId articleId,
		std::shared_ptr<RichPage> page,
		std::optional<Api::SendAction> action,
		SendMenu::Details sendMenuDetails,
		std::optional<EditedItemSnapshot> edited,
		std::optional<ComposeThreadKey> composeThreadKey,
		base::weak_ptr<Window::SessionController> controller = {})
	: _session(session)
	, _peer(peer)
	, _controller(std::move(controller))
	, _mode(mode)
	, _submitType((mode == Mode::Compose)
		? ShowWindowDescriptor::SubmitType::Send
		: ShowWindowDescriptor::SubmitType::Save)
	, _articleId(articleId)
	, _composeAction(std::move(action))
	, _sendMenuDetails(std::move(sendMenuDetails))
	, _edited(std::move(edited))
	, _composeThreadKey(std::move(composeThreadKey))
	, _page(page ? std::move(page) : std::make_shared<RichPage>())
	, _runtime(CreateMessageMediaRuntime(
		_session,
		(_mode == Mode::Compose) ? FullMsgId() : _articleId,
		[](QString) {
		},
		[](QString) {
		},
		composeDraftOrigin(),
		_controller))
	, _limits(ResolveRichMessageLimits(_session))
	, _state(std::make_shared<State>(_page, _runtime, _limits))
	, _submitOptions(_composeAction ? _composeAction->options : Api::SendOptions())
	, _richDraftAutosaveTimer([=] {
		saveRichDraftNow();
	}) {
		subscribeToUploader();
	}

	void setEditorShow(std::shared_ptr<ChatHelpers::Show> show) {
		_editorShow = std::move(show);
	}

	[[nodiscard]] std::shared_ptr<ChatHelpers::Show> resolveShow() const {
		if (_editorShow && _editorShow->valid()) {
			return _editorShow;
		} else if (const auto window = _session->tryResolveWindow(_peer)) {
			return window->uiShow();
		}
		return nullptr;
	}

	void showToast(const QString &text) const {
		if (const auto show = resolveShow()) {
			show->showToast(text);
		}
	}

	void focusWindow() {
		if (const auto show = resolveShow()) {
			show->activate();
		}
		if (_editor) {
			_editor->setFocus(Qt::OtherFocusReason);
		}
	}

	void trackComposeThreadWindow() {
		if (!_composeThreadKey) {
			return;
		}
		auto &entry = ComposeThreadEntryFor(*_composeThreadKey);
		entry.articleSession = weak_from_this();
		entry.fieldVisible.force_assign(true);
	}

	void releaseComposeThreadWindow() {
		if (!_composeThreadKey) {
			return;
		}
		auto &entry = ComposeThreadEntryFor(*_composeThreadKey);
		if (const auto current = entry.articleSession.lock()) {
			if (current.get() != this) {
				return;
			}
		}
		entry.articleSession.reset();
		entry.fieldVisible.force_assign(false);
	}

	[[nodiscard]] ComposeThreadEntry *composeThreadEntry() const {
		return _composeThreadKey
			? LookupComposeThreadEntry(*_composeThreadKey)
			: nullptr;
	}

	[[nodiscard]] ::Data::FileOrigin composeDraftOrigin() const {
		return _composeThreadKey
			? ComposeDraftOrigin(*_composeThreadKey)
			: ::Data::FileOrigin();
	}

	[[nodiscard]] ::Data::FileOrigin photoEditOrigin() const {
		return (_mode == Mode::Edit)
			? ::Data::FileOrigin(_articleId)
			: composeDraftOrigin();
	}

	[[nodiscard]] bool submitWouldBeEphemeral(
			const std::optional<TextWithEntities> &simple) const {
		if (!_composeAction) {
			return false;
		} else if (!simple) {
			const auto id = _composeAction->replyTo.messageId;
			const auto target = _session->data().message(id);
			return target && target->isEphemeral();
		}
		auto message = Api::MessageToSend(*_composeAction);
		message.textWithTags = {
			simple->text,
			TextUtilities::ConvertEntitiesToTextTags(simple->entities),
		};
		return _session->ephemeralMessages().wouldSend(message);
	}

	[[nodiscard]] bool submitPaymentChecked(
			const std::optional<TextWithEntities> &simple,
			Fn<void(int)> resend) {
		if (_mode != Mode::Compose
			|| !_composeAction
			|| _submitOptions.scheduled
			|| submitWouldBeEphemeral(simple)) {
			return true;
		}
		const auto show = resolveShow();
		return !show || _sendPayment.check(
			show,
			_peer,
			_submitOptions,
			1,
			std::move(resend));
	}

	[[nodiscard]] bool submitRequested() {
		if (_submittedPage || _submitApiRequested) {
			return false;
		}
		if (hasPendingPreparation()) {
			// Media is still being prepared/uploaded and is not yet part of
			// the rich page, so SerializeAsSimple() would wrongly treat the
			// page as text-only and drop the in-flight media. Defer until the
			// media block lands, then the submit re-fires.
			_submitDeferred = true;
			return false;
		}
		if (hasVisibleFailedAttachments()) {
			showAttachmentFailedToast();
			return false;
		}
		if (_state->articleEmpty()) {
			showEmptySubmittedPageToast();
			return false;
		}
		auto simple = SerializeAsSimple(_state->richPage(), _session);
		const auto weak = base::make_weak(this);
		const auto withPaymentApproved = [weak](int approved) {
			if (const auto strong = weak.get()) {
				auto options = strong->_submitOptions;
				options.starsApproved = approved;
				strong->requestSubmit(std::move(options));
			}
		};
		if (simple) {
			if (!submitPaymentChecked(simple, withPaymentApproved)) {
				return false;
			}
			return submitSimpleText(std::move(*simple));
		}
		if (_mode == Mode::Compose && _composeAction) {
			const auto replyToId = _composeAction->replyTo.messageId;
			const auto target = _session->data().message(replyToId);
			if (target && target->isEphemeral()) {
				showToast(tr::lng_ephemeral_reply_text_only(tr::now));
				return false;
			}
		}
		if (!CanUseRichMessages(_session)) {
			const auto page = _state->richPage();
			if (!RichPageIsFlattenSafe(page)) {
				ShowRichMessagesPremiumToast(resolveShow());
				return false;
			}
			OfferRichMessagePremiumChoice(
				resolveShow(),
				_session,
				page,
				[=] {
					if (const auto strong = weak.get()) {
						strong->submitWithoutFormatting(page);
					}
				});
			return false;
		}
		auto page = std::shared_ptr<const RichPage>(
			std::make_shared<RichPage>(_state->richPage()));
		if (const auto error = ValidateRichMessage(*page, _limits)) {
			showRichMessageLimitToast(*error);
			return false;
		}
		_submittedPage = page;
		if (submittedAttachmentsReady()
			&& serializeSubmittedPage().status
				== SerializeInputRichMessageStatus::EmptyContent) {
			_submittedPage = nullptr;
			showEmptySubmittedPageToast();
			return false;
		}
		if (!submitPaymentChecked(simple, withPaymentApproved)) {
			_submittedPage = nullptr;
			return false;
		}
		if (!applySubmittedLocalState(page)) {
			_submittedPage = nullptr;
			showToast(tr::lng_edit_error(tr::now));
			return false;
		}
		_submitDeferred = false;
		cancelRichDraftAutosave();
		_backgroundHold = shared_from_this();
		maybeContinueSubmittedRequest();
		return true;
	}

	[[nodiscard]] bool submitSimpleText(TextWithEntities text) {
		if (_mode == Mode::Compose) {
			if (!_composeAction) {
				showToast(tr::lng_edit_error(tr::now));
				return false;
			}
			auto action = *_composeAction;
			action.options = _submitOptions;
			action.clearDraft = true;
			action.history->clearCloudDraft(
				action.replyTo.topicRootId,
				action.replyTo.monoforumPeerId);
			auto message = Api::MessageToSend(action);
			message.textWithTags = {
				text.text,
				TextUtilities::ConvertEntitiesToTextTags(text.entities),
			};
			cancelRichDraftAutosave();
			_session->api().sendMessage(std::move(message));
			return true;
		}
		const auto item = currentSubmittedItem();
		if (!item) {
			showToast(tr::lng_edit_error(tr::now));
			return false;
		}
		Api::EditTextMessage(
			not_null{ item },
			text,
			::Data::WebPageDraft{ .removed = true },
			_submitOptions,
			[weak = base::make_weak(this)](mtpRequestId) {
			},
			[weak = base::make_weak(this)](
					const QString &error,
					mtpRequestId) {
				if (const auto session = weak.get()) {
					session->showToast(error.isEmpty()
						? tr::lng_edit_error(tr::now)
						: error);
				}
			},
			false);
		return true;
	}

	void submitWithoutFormatting(RichPage page) {
		auto plain = FlattenRichPageToSimpleText(page);
		const auto weak = base::make_weak(this);
		const auto withPaymentApproved = [weak, page](int approved) {
			if (const auto strong = weak.get()) {
				strong->_submitOptions.starsApproved = approved;
				if (strong->_composeAction) {
					strong->_composeAction->options
						= strong->_submitOptions;
				}
				strong->submitWithoutFormatting(page);
			}
		};
		if (!submitPaymentChecked(plain, withPaymentApproved)) {
			return;
		}
		if (submitSimpleText(std::move(plain)) && _windowHost) {
			_windowHost->close();
		}
	}

	[[nodiscard]] bool cancelRequested() {
		_submitDeferred = false;
		return true;
	}

	[[nodiscard]] bool changedCancelRequested() {
		_submitDeferred = false;
		if (!_composeAction || !_composeThreadKey) {
			return true;
		}
		startCloseWithDraftSave();
		return false;
	}

	[[nodiscard]] bool discardRequested() {
		_submitDeferred = false;
		cancelRichDraftAutosave();
		if (!_composeAction || !_composeThreadKey) {
			return true;
		}
		const auto topicRootId = _composeThreadKey->draftKey.topicRootId();
		const auto monoforumPeerId = _composeThreadKey->draftKey.monoforumPeerId();
		const auto history = _composeAction->history;
		if (const auto entry = composeThreadEntry()) {
			if (entry->saveDraft) {
				entry->saveDraft(nullptr);
			}
		}
		history->clearCloudDraft(topicRootId, monoforumPeerId);
		if (const auto thread = history->threadFor(topicRootId, monoforumPeerId)) {
			const auto cloudDraft = history->createCloudDraft(
				topicRootId,
				monoforumPeerId,
				nullptr);
			if (cloudDraft) {
				_session->api().saveDraftToCloud(not_null{ thread }, *cloudDraft);
			}
		}
		return true;
	}

	[[nodiscard]] HistoryItem *currentSubmittedItem() const {
		return _session->data().message(_articleId);
	}

	[[nodiscard]] HistoryItem *ensureComposeLocalItem() {
		if (const auto item = currentSubmittedItem()) {
			return item;
		}
		if (!_composeAction) {
			return nullptr;
		}
		auto action = *_composeAction;
		const auto history = action.history;
		const auto peer = history->peer;
		auto flags = NewMessageFlags(peer);
		if (action.replyTo) {
			flags |= MessageFlag::HasReplyInfo;
		}
		Api::FillMessagePostFlags(action, peer, flags);
		if (action.options.scheduled) {
			flags |= MessageFlag::IsOrWasScheduled;
		}
		if (action.options.shortcutId) {
			flags |= MessageFlag::ShortcutMessage;
		}
		const auto starsPaid = std::min(
			peer->starsPerMessageChecked(),
			action.options.starsApproved);
		return history->addNewLocalMessage({
			.id = _articleId.msg,
			.flags = flags,
			.from = NewMessageFromId(action),
			.replyTo = action.replyTo,
			.date = NewMessageDate(action.options),
			.scheduleRepeatPeriod = action.options.scheduleRepeatPeriod,
			.shortcutId = action.options.shortcutId,
			.starsPaid = starsPaid,
			.postAuthor = NewMessagePostAuthor(action),
			.effectId = action.options.effectId,
			.suggest = HistoryMessageSuggestInfo(action.options),
		}, TextWithEntities(), MTP_messageMediaEmpty());
	}

	[[nodiscard]] bool keepsInlineRichPage() const {
		return (_mode == Mode::Edit)
			&& _edited
			&& _edited->inlinePage
			&& _edited->inlinePage->part;
	}

	[[nodiscard]] bool applySubmittedLocalState(
			const std::shared_ptr<const RichPage> &page) {
		const auto item = (_mode == Mode::Compose)
			? ensureComposeLocalItem()
			: currentSubmittedItem();
		if (!item) {
			return false;
		}
		if (keepsInlineRichPage()) {
			item->setFullRichPage(page);
			return true;
		}
		item->applyLocalRichPage(page);
		return true;
	}

	void restoreEditedItem() {
		if (!_edited) {
			return;
		}
		if (const auto item = currentSubmittedItem()) {
			if (keepsInlineRichPage()) {
				if (_edited->fullPage) {
					item->setFullRichPage(_edited->fullPage);
				} else {
					item->clearFullRichPage();
				}
				return;
			}
			item->applyLocalRichPage(_edited->inlinePage, _edited->summary);
			if (_edited->fullPage) {
				item->setFullRichPage(_edited->fullPage);
			}
		}
	}

	void finishSubmittedWork() {
		_submitApiRequested = false;
		_submittedPage = nullptr;
		_backgroundHold = nullptr;
	}

	void failSubmittedWork(bool showToast) {
		if (showToast) {
			showAttachmentFailedToast();
		}
		if (_mode == Mode::Edit) {
			restoreEditedItem();
		} else if (const auto item = currentSubmittedItem()) {
			item->sendFailed();
		}
		finishSubmittedWork();
	}

	void discardSubmittedLocalItem() {
		if (_mode == Mode::Edit) {
			restoreEditedItem();
		} else if (const auto item = currentSubmittedItem()) {
			item->destroy();
		}
	}

	[[nodiscard]] SerializeInputRichMessageMode submittedSerializeMode() const {
		switch (_submitType) {
		case ShowWindowDescriptor::SubmitType::Send:
		case ShowWindowDescriptor::SubmitType::Save:
			return SerializeInputRichMessageMode::FinalSubmit;
		}
		return SerializeInputRichMessageMode::Draft;
	}

	void showEmptySubmittedPageToast() const {
		showToast(tr::lng_article_submit_empty(tr::now));
	}

	[[nodiscard]] bool pageContainsAttachment(
			const std::vector<RichPage::Block> &blocks,
			const AttachmentRecord &attachment) const {
		for (const auto &block : blocks) {
			if (blockMatchesAttachment(block, attachment)
				|| pageContainsAttachment(block.blocks, attachment)) {
				return true;
			}
			for (const auto &item : block.listItems) {
				if (pageContainsAttachment(item.blocks, attachment)) {
					return true;
				}
			}
		}
		return false;
	}

	[[nodiscard]] bool submittedPageContainsAttachment(
			const AttachmentRecord &attachment) const {
		return _submittedPage
			&& pageContainsAttachment(_submittedPage->blocks, attachment);
	}

	[[nodiscard]] bool hasFailedSubmittedAttachments() const {
		for (const auto &attachment : _attachments) {
			if (attachment.state == AttachmentState::Failed
				&& submittedPageContainsAttachment(attachment)) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool submittedAttachmentsReady() const {
		for (const auto &attachment : _attachments) {
			if (submittedPageContainsAttachment(attachment)
				&& attachment.state != AttachmentState::Ready) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] bool patchReadyAttachmentBlock(
			RichPage::Block &block,
			const AttachmentRecord &attachment) const {
		if (attachment.state != AttachmentState::Ready) {
			return false;
		}
		switch (attachment.blockKind) {
		case RichPage::BlockKind::Photo:
			if (!attachment.serverPhoto || !attachment.serverMediaId) {
				return false;
			}
			block.photoId = attachment.serverMediaId;
			block.photo = attachment.serverPhoto;
			return true;
		case RichPage::BlockKind::Video:
		case RichPage::BlockKind::Audio:
			if (!attachment.serverDocument || !attachment.serverMediaId) {
				return false;
			}
			block.documentId = attachment.serverMediaId;
			block.document = attachment.serverDocument;
			return true;
		default:
			return false;
		}
	}

	[[nodiscard]] bool patchReadyGroupedMediaItem(
			RichPage::GroupedMediaItem &item,
			const AttachmentRecord &attachment) const {
		if (attachment.state != AttachmentState::Ready
			|| !groupedMediaItemMatchesAttachment(item, attachment)) {
			return false;
		}
		switch (attachment.blockKind) {
		case RichPage::BlockKind::Photo:
			if (!attachment.serverPhoto || !attachment.serverMediaId) {
				return false;
			}
			item.photoId = attachment.serverMediaId;
			item.photo = attachment.serverPhoto;
			return true;
		case RichPage::BlockKind::Video:
			if (!attachment.serverDocument || !attachment.serverMediaId) {
				return false;
			}
			item.documentId = attachment.serverMediaId;
			item.document = attachment.serverDocument;
			return true;
		default:
			return false;
		}
	}

	[[nodiscard]] bool patchReadyAttachmentInBlock(
			RichPage::Block &block,
			const AttachmentRecord &attachment) const {
		if (block.kind == RichPage::BlockKind::GroupedMedia) {
			auto patchedAny = false;
			auto patchedAll = true;
			for (auto &item : block.mediaItems) {
				if (groupedMediaItemMatchesAttachment(item, attachment)) {
					patchedAny = true;
					if (!patchReadyGroupedMediaItem(item, attachment)) {
						patchedAll = false;
					}
				}
			}
			return patchedAny && patchedAll;
		}
		return patchReadyAttachmentBlock(block, attachment);
	}

	[[nodiscard]] bool patchSubmittedBlocks(
			std::vector<RichPage::Block> &blocks) const {
		for (auto &block : blocks) {
			for (const auto &attachment : _attachments) {
				if (blockMatchesAttachment(block, attachment)
					&& !patchReadyAttachmentInBlock(block, attachment)) {
					return false;
				}
			}
			if (!patchSubmittedBlocks(block.blocks)) {
				return false;
			}
			for (auto &item : block.listItems) {
				if (!patchSubmittedBlocks(item.blocks)) {
					return false;
				}
			}
		}
		return true;
	}

	[[nodiscard]] SerializeInputRichMessageResult serializeSubmittedPage() {
		if (!_submittedPage) {
			return {};
		}
		for (auto &attachment : _attachments) {
			if (attachment.origin) {
				refreshAttachmentInput(attachment);
			}
		}
		auto page = RichPage(*_submittedPage);
		return patchSubmittedBlocks(page.blocks)
			? SerializeInputRichMessage(
				_session,
				page,
				submittedSerializeMode())
			: SerializeInputRichMessageResult();
	}

	void maybeContinueSubmittedRequest() {
		if (!_submittedPage || _submitApiRequested) {
			return;
		}
		if (hasFailedSubmittedAttachments()) {
			failSubmittedWork(false);
			return;
		}
		if (!submittedAttachmentsReady()) {
			return;
		}
		const auto richMessage = serializeSubmittedPage();
		if (richMessage.status == SerializeInputRichMessageStatus::EmptyContent) {
			showEmptySubmittedPageToast();
			discardSubmittedLocalItem();
			finishSubmittedWork();
			restartRichDraftAutosave();
			return;
		} else if (richMessage.status != SerializeInputRichMessageStatus::Success
			|| !richMessage.value) {
			failSubmittedWork(true);
			return;
		}
		const auto item = currentSubmittedItem();
		if (!item) {
			finishSubmittedWork();
			return;
		}
		_submitApiRequested = true;
		if (_mode == Mode::Compose) {
			auto action = *_composeAction;
			action.options = _submitOptions;
			action.clearDraft = true;
			action.history->clearCloudDraft(
				action.replyTo.topicRootId,
				action.replyTo.monoforumPeerId);
			_session->api().sendRichMessage(
				item,
				*richMessage.value,
				std::move(action));
			finishSubmittedWork();
			return;
		}
		Api::EditRichMessage(
			not_null{ item },
			[weak = base::make_weak(this)] {
				if (const auto session = weak.get()) {
					auto richMessage = session->serializeSubmittedPage();
					return (richMessage.status
						== SerializeInputRichMessageStatus::Success)
						? std::move(richMessage.value)
						: std::optional<MTPInputRichMessage>();
				}
				return std::optional<MTPInputRichMessage>();
			},
			_submitOptions,
			[weak = base::make_weak(this)](mtpRequestId) {
				if (const auto session = weak.get()) {
					session->finishSubmittedWork();
				}
			},
			[weak = base::make_weak(this)](const QString &error, mtpRequestId) {
				if (const auto session = weak.get()) {
					session->restoreEditedItem();
					session->showToast(error.isEmpty()
						? tr::lng_edit_error(tr::now)
						: error);
					session->finishSubmittedWork();
				}
			});
	}

	void requestSubmit(Api::SendOptions options) {
		_submitOptions = std::move(options);
		if (_composeAction) {
			_composeAction->options = _submitOptions;
		}
		if (hasPendingPreparation()) {
			_submitDeferred = true;
			return;
		}
		if (hasVisibleFailedAttachments()) {
			showAttachmentFailedToast();
			return;
		}
		simulateSubmitClick();
	}

	void setupSubmitButton(not_null<Ui::RpWidget*> button) {
		_submitButton = button;
		if (_mode != Mode::Compose) {
			return;
		}
		const auto show = _editorShow;
		if (!show) {
			return;
		}
		const auto weak = base::make_weak(this);
		const auto submit = [weak](Api::SendOptions options) {
			if (const auto session = weak.get()) {
				session->requestSubmit(std::move(options));
			}
		};
		SendMenu::SetupMenuAndShortcuts(
			button,
			show,
			[details = _sendMenuDetails] { return details; },
			SendMenu::DefaultCallback(show, submit));
	}

	void requestMedia(
			not_null<Widget*> editor,
			QPointer<QWidget> parent,
			std::optional<State::ReplaceTarget> replaceTarget,
			RequestMediaType type) {
		if (!parent) {
			return;
		}
		_editor = editor;
		const auto weak = base::make_weak(this);
		const auto editorPointer = QPointer<Widget>(editor.get());
		const auto replacing = replaceTarget.has_value();
		const auto filter = (type == RequestMediaType::PhotoVideo)
			? FileDialog::PhotoVideoFilesFilter()
			: (type == RequestMediaType::Audio)
			? FileDialog::AudioFilesFilter()
			: FileDialog::PhotoVideoAudioFilesFilter();
		auto callback = [weak, editorPointer, replaceTarget = std::move(
				replaceTarget)](FileDialog::OpenResult &&result) mutable {
			if (const auto session = weak.get()) {
				session->handleMediaDialogResult(
					editorPointer,
					std::move(result),
					std::move(replaceTarget));
			}
		};
		if (replacing) {
			FileDialog::GetOpenPath(
				std::move(parent),
				tr::lng_choose_file(tr::now),
				filter,
				std::move(callback));
		} else {
			FileDialog::GetOpenPaths(
				std::move(parent),
				tr::lng_choose_files(tr::now),
				filter,
				std::move(callback));
		}
	}

	void requestMap(
			not_null<Widget*> editor,
			QPointer<QWidget> parent,
			rpl::producer<> closeRequests) {
		if (!parent) {
			return;
		}
		_editor = editor;
		const auto config = ResolveMapsConfig(_session);
		if (!Ui::LocationPicker::Available(config)) {
			return;
		}
		const auto weak = base::make_weak(this);
		const auto editorPointer = QPointer<Widget>(editor.get());
		Ui::LocationPicker::Show({
			.parent = static_cast<Ui::RpWidget*>(parent.data()),
			.config = config,
			.chooseLabel = tr::lng_maps_point_send(),
			.session = _session,
			.callback = [weak, editorPointer](::Data::InputVenue venue) {
				if (const auto session = weak.get()) {
					session->applyMapSelection(editorPointer, std::move(venue));
				}
			},
			.quit = [] { Shortcuts::Launch(Shortcuts::Command::Quit); },
			.storageId = _session->local().resolveStorageIdBots(),
			.closeRequests = std::move(closeRequests),
		});
	}

	void showWindow() {
		_backgroundHold = shared_from_this();
		registerLiveAndTrackSession();
		trackComposeThreadWindow();
		auto descriptor = ShowWindowDescriptor{
			.session = _session,
			.peer = _peer,
			.state = _state,
			.submitType = _submitType,
			.discarded = _composeAction
				? Fn<bool()>([session = shared_from_this()] {
					return session->discardRequested();
				})
				: Fn<bool()>(),
			.showCreated = [session = shared_from_this()](
					std::shared_ptr<ChatHelpers::Show> show) {
				session->setEditorShow(std::move(show));
			},
			.editorCreated = [session = shared_from_this()](
					not_null<Widget*> editor) {
				session->editorCreated(editor);
			},
			.cancelled = [session = shared_from_this()] {
				return session->cancelRequested();
			},
			.changedCancelled = _composeAction
				? Fn<bool()>([session = shared_from_this()] {
					return session->changedCancelRequested();
				})
				: Fn<bool()>(),
			.confirmed = [session = shared_from_this()] {
				return session->submitRequested();
			},
			.setupSubmitButton = [session = shared_from_this()](
					not_null<Ui::RpWidget*> button) {
				session->setupSubmitButton(button);
			},
			.requestMedia = [session = shared_from_this()](
					not_null<Widget*> editor,
					QPointer<QWidget> parent,
					std::optional<State::ReplaceTarget> replaceTarget,
					RequestMediaType type) {
				session->requestMedia(
					editor,
					std::move(parent),
					std::move(replaceTarget),
					type);
			},
			.applyPreparedMedia = [session = shared_from_this()](
					not_null<Widget*> editor,
					PreparedList list,
					PreparedMediaPasteTarget target) {
				session->applyPreparedMedia(
					QPointer<Widget>(editor.get()),
					std::move(list),
					std::move(target));
			},
			.requestPhotoEditSource = [session = shared_from_this()](
					uint64 photoId,
					Fn<void(QImage)> done) {
				session->photoEditSource(photoId, std::move(done));
			},
			.replacePhotoWithList = [session = shared_from_this()](
					not_null<Widget*> editor,
					PreparedList list,
					State::ReplaceTarget replaceTarget) {
				session->replaceMediaWithPreparedList(
					QPointer<Widget>(editor.get()),
					std::move(list),
					std::move(replaceTarget));
			},
			.mediaUploadState = [session = shared_from_this()](
					uint64 mediaId) {
				return session->mediaUploadStateForMedia(mediaId);
			},
			.cancelMediaUpload = [session = shared_from_this()](
					not_null<Widget*> editor,
					uint64 mediaId) {
				session->cancelMediaUploadByMediaId(mediaId);
			},
			.addMediaAndGroupWithBlock = [session = shared_from_this()](
					not_null<Widget*> editor,
					State::BlockPath path,
					QPointer<QWidget> parent) {
				session->addMediaAndGroupWithBlock(
					editor,
					std::move(path),
					std::move(parent));
			},
			.requestMap = [session = shared_from_this()](
					not_null<Widget*> editor,
					QPointer<QWidget> parent,
					rpl::producer<> closeRequests) {
				session->requestMap(
					editor,
					std::move(parent),
					std::move(closeRequests));
			},
			.closed = [session = shared_from_this()] {
				session->windowClosed();
			},
			.showLimitToast = [session = shared_from_this()](
					RichMessageLimitError error) {
				session->showRichMessageLimitToast(error);
			},
		};
		_windowHost = ShowWindow(std::move(descriptor));
		restoreComposeDraftAttachments();
	}

	void windowClosed() {
		cancelRichDraftAutosave();
		cancelCloseWithDraftSave(_closeDraftSaveGeneration);
		releaseComposeThreadWindow();
		_editor = nullptr;
		_submitButton = nullptr;
		_windowHost = nullptr;
		_editorShow = nullptr;
		_pendingPhotoEditSources.clear();
		_photoEditSourceLifetime.destroy();
		if (!_submittedPage && !_submitApiRequested) {
			_backgroundHold = nullptr;
			// Sync the local draft and the chat input field with the
			// cloud draft saved on close, like an incoming server draft
			// update: simple-text drafts go back into the message field,
			// rich drafts show the draft preview. Must happen after the
			// compose entry is released above, otherwise the field code
			// still bypasses normal draft handling and skips the update.
			// Skipped when no cloud draft object exists at all: then this
			// editor never wrote one (blank open, blank close), and syncing
			// would wipe an unrelated local draft (e.g. reply-only) through
			// the cloud-to-local clear branch.
			syncFieldWithCloudDraftAfterClose();
		}
		if (const auto continuation = base::take(_onWindowClosedContinuation)) {
			continuation();
		}
	}

public:
	static void CloseAll() {
		auto live = std::vector<std::weak_ptr<ArticleSession>>();
		std::swap(live, Live());
		for (const auto &weak : live) {
			if (const auto strong = weak.lock()) {
				strong->forceClose();
			}
		}
	}

	[[nodiscard]] static bool SaveOpenComposeDraftThen(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		PeerId monoforumPeerId,
		Fn<void()> onSaved);
	[[nodiscard]] static bool RequestCloseOpenEditWindowThen(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		Fn<void()> onClosed);

private:
	// Registry of all editor sessions that currently own a window, so that
	// they can be force-closed on session clear or application shutdown.
	[[nodiscard]] static std::vector<std::weak_ptr<ArticleSession>> &Live() {
		static auto result = std::vector<std::weak_ptr<ArticleSession>>();
		return result;
	}

	void registerLiveAndTrackSession() {
		auto &live = Live();
		live.erase(
			std::remove_if(
				live.begin(),
				live.end(),
				[](const std::weak_ptr<ArticleSession> &weak) {
					return weak.expired();
				}),
			live.end());
		live.push_back(weak_from_this());

		_session->data().sessionDataAboutToBeCleared(
		) | rpl::on_next([weak = weak_from_this()] {
			// Holds a strong reference for the duration of the call, so that
			// dropping the self-hold inside forceClose() doesn't run
			// ~ArticleSession re-entrantly while this handler is on the stack.
			if (const auto strong = weak.lock()) {
				strong->forceClose();
			}
		}, _lifetime);
	}

	// Destroys the editor window synchronously and releases the self-hold.
	// The caller must hold a strong reference (see CloseAll() and the session
	// clear handler) so that the eventual ~ArticleSession runs after this
	// returns rather than re-entrantly.
	//
	// Runs on passcode lock, account switch and application shutdown, so
	// it must stay synchronous and must not start network requests: the
	// current article state is captured into the in-memory cloud draft
	// and mirrored to the local draft / input field, while the server
	// save is only scheduled (it fires after unlock and simply never
	// happens during logout or shutdown).
	void forceClose() {
		if (!_windowHost && !_backgroundHold) {
			return;
		}
		cancelRichDraftAutosave();
		cancelCloseWithDraftSave(_closeDraftSaveGeneration);
		const auto sync = _composeAction
			&& _composeThreadKey
			&& !_submittedPage
			&& !_submitApiRequested;
		if (sync && !hasPendingPreparation()) {
			if (const auto prepared = prepareRichDraftForAutosave()) {
				_composeAction->history->createCloudDraft(
					_composeThreadKey->draftKey.topicRootId(),
					_composeThreadKey->draftKey.monoforumPeerId(),
					&*prepared);
			}
		}
		releaseComposeThreadWindow();
		_editor = nullptr;
		_submitButton = nullptr;
		_windowHost = nullptr;
		_editorShow = nullptr;
		_backgroundHold = nullptr;
		_closeDraftSaveContinuation = nullptr;
		_onWindowClosedContinuation = nullptr;
		if (sync) {
			syncFieldWithCloudDraftAfterClose();
			const auto history = _composeAction->history;
			if (const auto thread = history->threadFor(
					_composeThreadKey->draftKey.topicRootId(),
					_composeThreadKey->draftKey.monoforumPeerId())) {
				_session->api().saveDraftToCloudDelayed(not_null{ thread });
			}
		}
	}

	void handleMediaDialogResult(
		QPointer<Widget> editor,
		FileDialog::OpenResult &&result,
		std::optional<State::ReplaceTarget> replaceTarget) {
		auto showError = [=](tr::phrase<> phrase) {
			showToast(phrase(tr::now));
		};
		auto list = Storage::PreparedFileFromFilesDialog(
			std::move(result),
			[](const PreparedList &) {
				return true;
			},
			showError,
			st::sendMediaPreviewSize,
			_session->premium());
		if (!list) {
			return;
		}
		if (replaceTarget && CountAcceptedPreparedFiles(*list) != 1) {
			showToast(tr::lng_send_media_invalid_files(tr::now));
			return;
		}
		applyPreparedList(
			editor,
			std::move(*list),
			++_prepareBatchId,
			replaceTarget
				? AttachmentInsertMode::ReplaceBlock
				: AttachmentInsertMode::Normal,
			std::nullopt,
			std::move(replaceTarget));
	}

	void applyPreparedMedia(
			QPointer<Widget> editor,
			PreparedList list,
			PreparedMediaPasteTarget target) {
		if (!editor) {
			return;
		}
		if (list.error != PreparedList::Error::None) {
			showToast(tr::lng_send_media_invalid_files(tr::now));
			return;
		}
		applyPreparedList(
			editor,
			std::move(list),
			++_prepareBatchId,
			AttachmentInsertMode::ClipboardPaste,
			std::move(target));
	}

	void replaceMediaWithPreparedList(
			QPointer<Widget> editor,
			PreparedList list,
			State::ReplaceTarget replaceTarget) {
		if (!editor) {
			return;
		}
		if (list.error != PreparedList::Error::None) {
			showToast(tr::lng_send_media_invalid_files(tr::now));
			return;
		}
		if (!list.files.empty()) {
			Storage::UpdateImageDetails(
				list.files.front(),
				st::sendMediaPreviewSize,
				PhotoSideLimit(true));
		}
		applyPreparedList(
			editor,
			std::move(list),
			++_prepareBatchId,
			AttachmentInsertMode::ReplaceBlock,
			std::nullopt,
			std::move(replaceTarget));
	}

	void photoEditSource(uint64 photoId, Fn<void(QImage)> done) {
		if (const auto i = _originalMediaImages.find(photoId);
			i != end(_originalMediaImages)) {
			done(i->second);
			return;
		}
		for (const auto &attachment : _attachments) {
			if ((attachment.blockKind == RichPage::BlockKind::Photo)
				&& mediaIdMatchesAttachment(photoId, attachment)) {
				const auto i = _originalMediaImages.find(
					attachment.localMediaId);
				if (i != end(_originalMediaImages)) {
					done(i->second);
					return;
				}
				break;
			}
		}
		const auto photo = _session->data().photo(PhotoId(photoId));
		const auto media = photo->createMediaView();
		photo->clearFailed(::Data::PhotoSize::Large);
		media->wanted(::Data::PhotoSize::Large, photoEditOrigin());
		if (const auto large = media->image(::Data::PhotoSize::Large)) {
			_pendingPhotoEditSources.remove(photoId);
			done(large->original());
			return;
		}
		const auto i = _pendingPhotoEditSources.find(photoId);
		if (i != end(_pendingPhotoEditSources)) {
			i->second.done = std::move(done);
			return;
		}
		_pendingPhotoEditSources.emplace(photoId, PendingPhotoEditSource{
			.media = media,
			.done = std::move(done),
		});
		if (!_photoEditSourceLifetime) {
			_session->downloaderTaskFinished(
			) | rpl::on_next([=] {
				checkPendingPhotoEditSources();
			}, _photoEditSourceLifetime);
		}
	}

	void checkPendingPhotoEditSources() {
		auto completed = std::vector<std::pair<Fn<void(QImage)>, QImage>>();
		for (auto i = begin(_pendingPhotoEditSources)
			; i != end(_pendingPhotoEditSources);) {
			const auto &media = i->second.media;
			if (const auto large = media->image(::Data::PhotoSize::Large)) {
				completed.emplace_back(
					std::move(i->second.done),
					large->original());
				i = _pendingPhotoEditSources.erase(i);
			} else if (media->owner()->failed(::Data::PhotoSize::Large)) {
				i = _pendingPhotoEditSources.erase(i);
			} else {
				++i;
			}
		}
		for (auto &[done, image] : completed) {
			done(std::move(image));
		}
		if (_pendingPhotoEditSources.empty()) {
			_photoEditSourceLifetime.destroy();
		}
	}

	[[nodiscard]] MediaUploadState mediaUploadStateForMedia(uint64 mediaId) {
		for (const auto &attachment : _attachments) {
			if (mediaIdMatchesAttachment(mediaId, attachment)) {
				const auto uploading
					= (attachment.state == AttachmentState::Uploading)
						|| (attachment.state == AttachmentState::Finalizing);
				return { .uploading = uploading };
			}
		}
		return {};
	}

	void cancelMediaUploadByMediaId(uint64 mediaId) {
		for (const auto &attachment : _attachments) {
			if (mediaIdMatchesAttachment(mediaId, attachment)) {
				eraseAttachment(attachment.uploadId);
				return;
			}
		}
	}

	void addMediaAndGroupWithBlock(
			not_null<Widget*> editor,
			State::BlockPath anchor,
			QPointer<QWidget> parent) {
		if (!parent) {
			return;
		}
		_editor = editor;
		const auto weak = base::make_weak(this);
		const auto editorPointer = QPointer<Widget>(editor.get());
		auto callback = [weak, editorPointer, anchor = std::move(anchor)](
				FileDialog::OpenResult &&result) mutable {
			if (const auto session = weak.get()) {
				session->applyAddToCollageList(
					editorPointer,
					std::move(result),
					std::move(anchor));
			}
		};
		FileDialog::GetOpenPaths(
			std::move(parent),
			tr::lng_choose_files(tr::now),
			FileDialog::PhotoVideoFilesFilter(),
			std::move(callback));
	}

	void applyAddToCollageList(
			QPointer<Widget> editor,
			FileDialog::OpenResult &&result,
			State::BlockPath anchor) {
		if (!editor) {
			return;
		}
		auto showError = [=](tr::phrase<> phrase) {
			showToast(phrase(tr::now));
		};
		auto list = Storage::PreparedFileFromFilesDialog(
			std::move(result),
			[](const PreparedList &) {
				return true;
			},
			showError,
			st::sendMediaPreviewSize,
			_session->premium());
		if (!list) {
			return;
		}
		const auto selection = _state->preparedSelectionForBlock(anchor);
		auto target = PreparedMediaPasteTarget{
			.blockDrop = Markdown::PreparedEditBlockDropTarget{
				.container = selection.blocks.container,
				.insertIndex = anchor.index + 1,
			},
		};
		applyPreparedList(
			editor,
			std::move(*list),
			++_prepareBatchId,
			AttachmentInsertMode::ClipboardPaste,
			std::move(target),
			std::nullopt,
			anchor);
	}

	void applyPreparedList(
		QPointer<Widget> editor,
		PreparedList list,
		uint64 batchId,
		AttachmentInsertMode insertMode = AttachmentInsertMode::Normal,
		std::optional<PreparedMediaPasteTarget> insertTarget = std::nullopt,
		std::optional<State::ReplaceTarget> replaceTarget = std::nullopt,
		std::optional<State::BlockPath> groupAnchor = std::nullopt) {
		const auto effectiveInsertMode = replaceTarget
			? AttachmentInsertMode::ReplaceBlock
			: insertMode;
		const auto replacing = IsReplacing(effectiveInsertMode, replaceTarget);
		if (const auto accepted = CountAcceptedPreparedFiles(list);
			accepted && exceedsMediaLimitWith(replacing ? 0 : accepted)) {
			showRichMessageLimitToast(RichMessageLimitError::Media);
			return;
		}
		if (replacing) {
			if (!list.files.empty()) {
				applyPreparedFile(
					editor,
					std::move(list.files.front()),
					batchId,
					0,
					effectiveInsertMode,
					std::move(replaceTarget));
			} else if (!list.filesToProcess.empty()) {
				_prepareQueue.push_back({
					.editor = editor,
					.file = std::move(list.filesToProcess.front()),
					.batchId = batchId,
					.order = 0,
					.insertMode = effectiveInsertMode,
					.replaceTarget = std::move(replaceTarget),
				});
				enqueueNextPrepare();
			}
			return;
		}
		const auto totalCount = int(
			list.files.size() + list.filesToProcess.size());
		if (totalCount > 0) {
			_mediaBatches.push_back({
				.id = batchId,
				.editor = editor,
				.insertMode = effectiveInsertMode,
				.insertTarget = insertTarget,
				.items = std::vector<MediaBatchItem>(totalCount),
				.groupAnchor = std::move(groupAnchor),
			});
		}
		auto order = 0;
		for (auto &file : list.files) {
			applyPreparedFile(
				editor,
				std::move(file),
				batchId,
				order++,
				effectiveInsertMode,
				replaceTarget);
		}
		for (auto &file : list.filesToProcess) {
			_prepareQueue.push_back({
				.editor = editor,
				.file = std::move(file),
				.batchId = batchId,
				.order = order++,
				.insertMode = effectiveInsertMode,
				.replaceTarget = replaceTarget,
			});
		}
		enqueueNextPrepare();
	}

	void enqueueNextPrepare() {
		if (_preparing) {
			return;
		}
		while (!_prepareQueue.empty()
			&& _prepareQueue.front().file.information) {
			auto queued = std::move(_prepareQueue.front());
			_prepareQueue.pop_front();
			applyPreparedFile(
				queued.editor,
				std::move(queued.file),
				queued.batchId,
				queued.order,
				queued.insertMode,
				std::move(queued.replaceTarget));
		}
		if (_prepareQueue.empty()) {
			maybeContinueDeferredSubmit();
			return;
		}
		auto queued = std::move(_prepareQueue.front());
		_prepareQueue.pop_front();
		const auto weak = base::make_weak(this);
		_preparing = true;
		const auto sideLimit = PhotoSideLimit();
		crl::async([weak, queued = std::move(queued), sideLimit]() mutable {
			Storage::PrepareDetails(
				queued.file,
				st::sendMediaPreviewSize,
				sideLimit);
			crl::on_main([weak, queued = std::move(queued)]() mutable {
				if (const auto session = weak.get()) {
					session->preparedAsyncFile(std::move(queued));
				}
			});
		});
	}

	void preparedAsyncFile(QueuedPrepare queued) {
		_preparing = false;
		applyPreparedFile(
			queued.editor,
			std::move(queued.file),
			queued.batchId,
			queued.order,
			queued.insertMode,
			std::move(queued.replaceTarget));
		enqueueNextPrepare();
	}

	void applyPreparedFile(
		QPointer<Widget> editor,
		PreparedFile file,
		uint64 batchId,
		int order,
		AttachmentInsertMode insertMode,
		std::optional<State::ReplaceTarget> replaceTarget) {
		if (!AcceptedPreparedFileType(file.type)) {
			if (!IsReplacing(insertMode, replaceTarget)) {
				markMediaBatchItemSkipped(batchId, order);
				flushMediaBatch(batchId);
			}
			showUnsupportedMediaToast(batchId);
			return;
		}
		const auto additionalMedia = IsReplacing(insertMode, replaceTarget)
			? 0
			: 1;
		if (exceedsMediaLimitWith(additionalMedia)) {
			if (!IsReplacing(insertMode, replaceTarget)) {
				markMediaBatchItemSkipped(batchId, order);
				flushMediaBatch(batchId);
			}
			showRichMessageLimitToast(RichMessageLimitError::Media);
			return;
		}
		prepareAttachment(
			editor,
			std::move(file),
			batchId,
			order,
			insertMode,
			std::move(replaceTarget));
	}

	void prepareAttachment(
		QPointer<Widget> editor,
		PreparedFile file,
		uint64 batchId,
		int order,
		AttachmentInsertMode insertMode,
		std::optional<State::ReplaceTarget> replaceTarget) {
		const auto meta = BuildAttachmentMeta(file);
		auto originalImage = QImage();
		if (file.information) {
			using ImageInfo = Ui::PreparedFileInformation::Image;
			if (const auto image = std::get_if<ImageInfo>(
					&file.information->media)) {
				originalImage = image->data;
			}
		}
		const auto weak = base::make_weak(this);
		++_pendingAttachmentPrepareCount;
		_attachmentPrepareQueue.addTask(
			std::make_unique<PrepareAttachmentTask>(
				BuildPrepareTaskArgs(_session, _peer->id, std::move(file)),
				[weak, editor, meta, batchId, order, insertMode, replaceTarget,
						originalImage = std::move(originalImage)](
						std::shared_ptr<FilePrepareResult> prepared) mutable {
					if (const auto session = weak.get()) {
						session->attachmentPrepared(
							editor,
							std::move(meta),
							std::move(prepared),
							batchId,
							order,
							insertMode,
							std::move(replaceTarget),
							std::move(originalImage));
					}
				}));
	}

	void attachmentPrepared(
		QPointer<Widget> editor,
		AttachmentMeta meta,
		std::shared_ptr<FilePrepareResult> prepared,
		uint64 batchId,
		int order,
		AttachmentInsertMode insertMode,
		std::optional<State::ReplaceTarget> replaceTarget,
		QImage originalImage) {
		_pendingAttachmentPrepareCount = std::max(
			_pendingAttachmentPrepareCount - 1,
			0);
		if (!prepared) {
			if (!IsReplacing(insertMode, replaceTarget)) {
				markMediaBatchItemSkipped(batchId, order);
				flushMediaBatch(batchId);
			}
			showAttachmentFailedToast();
			maybeContinueDeferredSubmit();
			return;
		}
		if (!editor) {
			if (!IsReplacing(insertMode, replaceTarget)) {
				markMediaBatchItemSkipped(batchId, order);
				flushMediaBatch(batchId);
			}
			maybeContinueDeferredSubmit();
			return;
		}
		if (meta.blockKind != BlockKindForPreparedResult(*prepared)) {
			if (!IsReplacing(insertMode, replaceTarget)) {
				markMediaBatchItemSkipped(batchId, order);
				flushMediaBatch(batchId);
			}
			showUnsupportedMediaToast(batchId);
			maybeContinueDeferredSubmit();
			return;
		}
		const auto replacing = IsReplacing(insertMode, replaceTarget);
		if (exceedsMediaLimitWith(replacing ? 0 : 1)) {
			if (!replacing) {
				markMediaBatchItemSkipped(batchId, order);
				flushMediaBatch(batchId);
			}
			showRichMessageLimitToast(RichMessageLimitError::Media);
			maybeContinueDeferredSubmit();
			return;
		}
		startAttachmentUpload(
			editor,
			std::move(meta),
			std::move(prepared),
			batchId,
			order,
			insertMode,
			std::move(replaceTarget),
			std::move(originalImage));
		maybeContinueDeferredSubmit();
	}

	void startAttachmentUpload(
		QPointer<Widget> editor,
		AttachmentMeta meta,
		std::shared_ptr<FilePrepareResult> prepared,
		uint64 batchId,
		int order,
		AttachmentInsertMode insertMode,
		std::optional<State::ReplaceTarget> replaceTarget,
		QImage originalImage) {
		if (!editor) {
			return;
		}
		const auto replacing = IsReplacing(insertMode, replaceTarget);
		_editor = editor;
		const auto blockKind = meta.blockKind;
		const auto uploadId = createAttachmentUpload(
			std::move(meta),
			std::move(prepared),
			std::move(originalImage));
		if (!uploadId) {
			return;
		}
		const auto attachment = findAttachment(*uploadId);
		if (!attachment) {
			return;
		}
		if (!replacing) {
			markMediaBatchItemReady(
				batchId,
				order,
				*uploadId,
				blockKind);
			flushMediaBatch(batchId);
			return;
		}
		auto block = makeAttachmentBlock(*attachment);
		editor->replacePreparedBlock(
			std::move(*replaceTarget),
			std::move(block));
		refreshAttachmentLocatorsAndDropMissing();
		requestEditorUpdate();
	}

	[[nodiscard]] std::optional<FullMsgId> createAttachmentUpload(
		AttachmentMeta meta,
		std::shared_ptr<FilePrepareResult> prepared,
		QImage originalImage) {
		if (!prepared) {
			return std::nullopt;
		}
		const auto uploadId = FullMsgId(
			_peer->id,
			_session->data().nextLocalMessageId());
		auto record = AttachmentRecord{
			.uploadId = uploadId,
			.type = meta.type,
			.blockKind = meta.blockKind,
			.localMediaId = prepared->id,
			.state = AttachmentState::Uploading,
			.caption = meta.caption,
			.filename = prepared->filename,
			.filemime = prepared->filemime,
			.attributes = DocumentAttributesFromPrepared(*prepared),
			.forceFile = prepared->forceFile,
			.audioTitle = meta.audioTitle,
			.audioPerformer = meta.audioPerformer,
			.audioFileName = meta.audioFileName.isEmpty()
				? meta.displayName
				: meta.audioFileName,
			.audioDuration = meta.audioDuration,
			.dimensions = meta.dimensions,
			.spoiler = meta.spoiler,
			.autoplay = meta.autoplay,
			.loop = meta.loop,
		};
		if (record.blockKind == RichPage::BlockKind::Photo) {
			const auto size = PhotoSizeFromPrepared(prepared->photo);
			if (!size.isEmpty()) {
				record.dimensions = size;
			}
		} else {
			const auto info = DocumentInfoFromPrepared(prepared->document);
			if (!info.dimensions.isEmpty()) {
				record.dimensions = info.dimensions;
			}
			if (record.blockKind == RichPage::BlockKind::Audio) {
				if (record.audioTitle.isEmpty()) {
					record.audioTitle = info.title;
				}
				if (record.audioPerformer.isEmpty()) {
					record.audioPerformer = info.performer;
				}
				if (record.audioFileName.isEmpty()) {
					record.audioFileName = !info.fileName.isEmpty()
						? info.fileName
						: prepared->filename;
				}
				if (!record.audioDuration) {
					record.audioDuration = info.duration;
				}
			} else {
				record.autoplay = record.autoplay || info.animation;
				record.loop = record.loop || info.animation;
			}
		}

		if (record.blockKind == RichPage::BlockKind::Photo
			&& !originalImage.isNull()
			&& record.localMediaId) {
			_originalMediaImages[record.localMediaId] = std::move(originalImage);
		}

		_attachments.push_back(std::move(record));
		_session->uploader().upload(uploadId, prepared);
		return uploadId;
	}

	void applyMapSelection(
		QPointer<Widget> editor,
		::Data::InputVenue venue) {
		if (!editor) {
			return;
		}
		_editor = editor;
		editor->insertPreparedBlock(makeMapBlock(std::move(venue)));
	}

	[[nodiscard]] RichPage::Block makeAttachmentBlock(
			const AttachmentRecord &attachment) const {
		auto block = RichPage::Block();
		block.kind = attachment.blockKind;
		block.caption = ToRichText(attachment.caption);
		if (attachment.blockKind == RichPage::BlockKind::Photo) {
			block.photoId = attachment.localMediaId;
			block.width = attachment.dimensions.width();
			block.height = attachment.dimensions.height();
			block.spoiler = attachment.spoiler;
		} else if (attachment.blockKind == RichPage::BlockKind::Video) {
			block.documentId = attachment.localMediaId;
			block.width = attachment.dimensions.width();
			block.height = attachment.dimensions.height();
			block.spoiler = attachment.spoiler;
			block.autoplay = attachment.autoplay;
			block.loop = attachment.loop;
		} else if (attachment.blockKind == RichPage::BlockKind::Audio) {
			block.documentId = attachment.localMediaId;
			block.audioTitle = attachment.audioTitle;
			block.audioPerformer = attachment.audioPerformer;
			block.audioFileName = attachment.audioFileName;
			block.audioDuration = attachment.audioDuration;
		}
		return block;
	}

	[[nodiscard]] auto makeGroupedAttachmentItem(
			const AttachmentRecord &attachment) const
	-> std::optional<RichPage::GroupedMediaItem> {
		auto item = RichPage::GroupedMediaItem();
		item.kind = attachment.blockKind;
		if (attachment.blockKind == RichPage::BlockKind::Photo) {
			item.photoId = attachment.localMediaId;
		} else if (attachment.blockKind == RichPage::BlockKind::Video) {
			item.documentId = attachment.localMediaId;
		} else {
			return std::nullopt;
		}
		item.width = attachment.dimensions.width();
		item.height = attachment.dimensions.height();
		item.autoplay = attachment.autoplay;
		item.loop = attachment.loop;
		item.spoiler = attachment.spoiler;
		return item;
	}

	[[nodiscard]] RichPage::Block makeGroupedAttachmentBlock(
			const std::vector<FullMsgId> &uploadIds) const {
		auto block = RichPage::Block();
		block.kind = RichPage::BlockKind::GroupedMedia;
		block.mediaIntent = RichPage::GroupedMediaIntent::Collage;
		block.mediaItems.reserve(uploadIds.size());
		auto caption = QString();
		auto captionCount = 0;
		for (const auto &uploadId : uploadIds) {
			const auto attachment = findAttachment(uploadId);
			if (!attachment) {
				continue;
			}
			const auto item = makeGroupedAttachmentItem(*attachment);
			if (!item) {
				continue;
			}
			block.mediaItems.push_back(*item);
			if (caption.isEmpty() && !attachment->caption.isEmpty()) {
				caption = attachment->caption;
			}
			if (!attachment->caption.isEmpty()) {
				++captionCount;
			}
		}
		if (captionCount == 1) {
			block.caption = ToRichText(std::move(caption));
		}
		return block;
	}

	[[nodiscard]] RichPage::Block makeMapBlock(::Data::InputVenue venue) const {
		const auto point = ::Data::LocationPoint(
			venue.lat,
			venue.lon,
			::Data::LocationPoint::NoAccessHash);
		const auto preview = ::Data::ComputeLocation(point);
		auto caption = QString();
		if (!venue.title.isEmpty() && !venue.address.isEmpty()) {
			caption = venue.title + u"\n"_q + venue.address;
		} else {
			caption = !venue.title.isEmpty() ? venue.title : venue.address;
		}
		auto block = RichPage::Block();
		block.kind = RichPage::BlockKind::Map;
		block.latitude = venue.lat;
		block.longitude = venue.lon;
		block.accessHash = point.accessHash();
		block.width = preview.width;
		block.height = preview.height;
		block.zoom = preview.zoom;
		block.caption = ToRichText(std::move(caption));
		return block;
	}

	void subscribeToUploader() {
		_session->uploader().photoReady(
		) | rpl::on_next([=](const Storage::UploadedMedia &data) {
			if (const auto attachment = findAttachment(data.fullId)) {
				finalizeUploadedPhoto(*attachment, data);
			}
		}, _lifetime);
		_session->uploader().documentReady(
		) | rpl::on_next([=](const Storage::UploadedMedia &data) {
			if (const auto attachment = findAttachment(data.fullId)) {
				finalizeUploadedDocument(*attachment, data);
			}
		}, _lifetime);
		_session->uploader().photoProgress(
		) | rpl::on_next([=](const FullMsgId &id) {
			if (findAttachment(id)) {
				requestEditorUpdate();
			}
		}, _lifetime);
		_session->uploader().documentProgress(
		) | rpl::on_next([=](const FullMsgId &id) {
			if (findAttachment(id)) {
				requestEditorUpdate();
			}
		}, _lifetime);
		_session->uploader().photoFailed(
		) | rpl::on_next([=](const FullMsgId &id) {
			markAttachmentFailed(id);
		}, _lifetime);
		_session->uploader().documentFailed(
		) | rpl::on_next([=](const FullMsgId &id) {
			markAttachmentFailed(id);
		}, _lifetime);
	}

	void finalizeUploadedPhoto(
		AttachmentRecord &attachment,
		const Storage::UploadedMedia &data) {
		using Flag = MTPDinputMediaUploadedPhoto::Flag;
		attachment.state = AttachmentState::Finalizing;
		auto flags = MTPDinputMediaUploadedPhoto::Flags();
		const auto stickers = ToInputDocumentVector(data.info.attachedStickers);
		if (attachment.spoiler) {
			flags |= Flag::f_spoiler;
		}
		if (!stickers.isEmpty()) {
			flags |= Flag::f_stickers;
		}
		_session->api().request(MTPmessages_UploadMedia(
			MTP_flags(0),
			MTPstring(),
			_peer->input(),
			MTP_inputMediaUploadedPhoto(
				MTP_flags(flags),
				data.info.file,
				MTP_vector<MTPInputDocument>(std::move(stickers)),
				MTP_int(0),
				MTPInputDocument())
		)).done([weak = base::make_weak(this), uploadId = attachment.uploadId](
				const MTPMessageMedia &result) {
			if (const auto session = weak.get()) {
				session->applyUploadedPhotoResult(uploadId, result);
			}
		}).fail([weak = base::make_weak(this), uploadId = attachment.uploadId](
				const MTP::Error &) {
			if (const auto session = weak.get()) {
				session->markAttachmentFailed(uploadId);
			}
		}).send();
	}

	void finalizeUploadedDocument(
		AttachmentRecord &attachment,
		const Storage::UploadedMedia &data) {
		using Flag = MTPDinputMediaUploadedDocument::Flag;
		attachment.state = AttachmentState::Finalizing;
		auto flags = MTPDinputMediaUploadedDocument::Flags();
		if (attachment.forceFile) {
			flags |= Flag::f_force_file;
		}
		const auto stickers = ToInputDocumentVector(data.info.attachedStickers);
		if (data.info.thumb) {
			flags |= Flag::f_thumb;
		}
		if (attachment.spoiler) {
			flags |= Flag::f_spoiler;
		}
		if (!stickers.isEmpty()) {
			flags |= Flag::f_stickers;
		}
		if (data.info.videoCover) {
			flags |= Flag::f_video_cover;
		}
		if (attachment.blockKind == RichPage::BlockKind::Video
			&& !attachment.forceFile) {
			flags |= Flag::f_nosound_video;
		}
		auto attributes = !attachment.attributes.isEmpty()
			? attachment.attributes
			: QVector<MTPDocumentAttribute>(
				1,
				MTP_documentAttributeFilename(MTP_string(attachment.filename)));
		_session->api().request(MTPmessages_UploadMedia(
			MTP_flags(0),
			MTPstring(),
			_peer->input(),
			MTP_inputMediaUploadedDocument(
				MTP_flags(flags),
				data.info.file,
				data.info.thumb.value_or(MTPInputFile()),
				MTP_string(attachment.filemime),
				MTP_vector<MTPDocumentAttribute>(std::move(attributes)),
				MTP_vector<MTPInputDocument>(std::move(stickers)),
				data.info.videoCover.value_or(MTPInputPhoto()),
				MTP_int(0),
				MTP_int(0))
		)).done([weak = base::make_weak(this), uploadId = attachment.uploadId](
				const MTPMessageMedia &result) {
			if (const auto session = weak.get()) {
				session->applyUploadedDocumentResult(uploadId, result);
			}
		}).fail([weak = base::make_weak(this), uploadId = attachment.uploadId](
				const MTP::Error &) {
			if (const auto session = weak.get()) {
				session->markAttachmentFailed(uploadId);
			}
		}).send();
	}

	void applyUploadedPhotoResult(
		FullMsgId uploadId,
		const MTPMessageMedia &result) {
		const auto attachment = findAttachment(uploadId);
		if (!attachment) {
			return;
		}
		auto ok = false;
		auto failed = false;
		const auto fail = [&] {
			failed = true;
			markAttachmentFailed(uploadId);
		};
		result.match([&](const MTPDmessageMediaPhoto &media) {
			const auto photo = media.vphoto();
			if (!photo || photo->type() != mtpc_photo) {
				fail();
				return;
			}
			const auto localPhoto = _session->data().photo(
				PhotoId(attachment->localMediaId));
			if (!localPhoto.get()) {
				fail();
				return;
			}
			const auto &fields = photo->c_photo();
			_session->data().photoConvert(localPhoto, *photo);
			attachment->state = AttachmentState::Ready;
			attachment->serverMediaId = fields.vid().v;
			attachment->serverPhoto = localPhoto.get();
			attachment->accessHash = fields.vaccess_hash().v;
			attachment->fileReference = fields.vfile_reference().v;
			attachment->inputPhoto = MTP_inputPhoto(
				fields.vid(),
				fields.vaccess_hash(),
				fields.vfile_reference());
			ok = true;
		}, [&](const auto &) {
			fail();
		});
		if (failed) {
			return;
		}
		if (!ok) {
			markAttachmentFailed(uploadId);
			return;
		}
		if (_editor) {
			auto patched = true;
			_editor->applyExternalRichPageMutation([&](RichPage &page) {
				const auto result = patchVisibleAttachmentBlocks(
					page,
					*attachment);
				patched = patched && result;
				return result;
			});
			if (!patched) {
				requestEditorUpdate();
			}
		} else if (!patchVisibleAttachmentBlocks(
			*visibleRichPage(),
			*attachment)) {
			requestEditorUpdate();
		} else {
			_state->resyncAfterExternalRichPageMutation();
			requestEditorUpdate();
		}
		_richDraftAutosaveTimer.cancel();
		saveRichDraftNow();
		retryRichDraftCloseSaveIfNeeded();
		maybeContinueSubmittedRequest();
	}

	void applyUploadedDocumentResult(
		FullMsgId uploadId,
		const MTPMessageMedia &result) {
		const auto attachment = findAttachment(uploadId);
		if (!attachment) {
			return;
		}
		auto ok = false;
		auto failed = false;
		const auto fail = [&] {
			failed = true;
			markAttachmentFailed(uploadId);
		};
		result.match([&](const MTPDmessageMediaDocument &media) {
			const auto document = media.vdocument();
			if (!document || document->type() != mtpc_document) {
				fail();
				return;
			}
			const auto localDocument = _session->data().document(
				DocumentId(attachment->localMediaId));
			if (!localDocument.get()) {
				fail();
				return;
			}
			const auto &fields = document->c_document();
			_session->data().documentConvert(localDocument, *document);
			attachment->state = AttachmentState::Ready;
			attachment->serverMediaId = fields.vid().v;
			attachment->serverDocument = localDocument.get();
			attachment->accessHash = fields.vaccess_hash().v;
			attachment->fileReference = fields.vfile_reference().v;
			attachment->inputDocument = MTP_inputDocument(
				fields.vid(),
				fields.vaccess_hash(),
				fields.vfile_reference());
			ok = true;
		}, [&](const auto &) {
			fail();
		});
		if (failed) {
			return;
		}
		if (!ok) {
			markAttachmentFailed(uploadId);
			return;
		}
		if (_editor) {
			auto patched = true;
			_editor->applyExternalRichPageMutation([&](RichPage &page) {
				const auto result = patchVisibleAttachmentBlocks(
					page,
					*attachment);
				patched = patched && result;
				return result;
			});
			if (!patched) {
				requestEditorUpdate();
			}
		} else if (!patchVisibleAttachmentBlocks(
			*visibleRichPage(),
			*attachment)) {
			requestEditorUpdate();
		} else {
			_state->resyncAfterExternalRichPageMutation();
			requestEditorUpdate();
		}
		_richDraftAutosaveTimer.cancel();
		saveRichDraftNow();
		retryRichDraftCloseSaveIfNeeded();
		maybeContinueSubmittedRequest();
	}

	void markAttachmentFailed(FullMsgId uploadId) {
		if (const auto attachment = findAttachment(uploadId)) {
			attachment->state = AttachmentState::Failed;
			showAttachmentFailedToast();
			requestEditorUpdate();
			retryRichDraftCloseSaveIfNeeded();
			maybeContinueSubmittedRequest();
		}
	}

	void requestEditorUpdate() {
		if (_editor) {
			_editor->update();
		}
	}

	void restoreComposeDraftAttachments() {
		if (!_composeAction || !_composeThreadKey) {
			return;
		}
		const auto origin = composeDraftOrigin();
		for (const auto &block : _state->richPage().blocks) {
			restoreComposeDraftAttachment(block, origin);
		}
		refreshAttachmentLocatorsAndDropMissing();
	}

	void restoreComposeDraftAttachment(
			const RichPage::Block &block,
			const ::Data::FileOrigin &origin) {
		const auto appendPhoto = [&](not_null<PhotoData*> photo, uint64 id) {
			auto existing = std::find_if(
				_attachments.begin(),
				_attachments.end(),
				[=](const AttachmentRecord &attachment) {
					return (attachment.blockKind == RichPage::BlockKind::Photo)
						&& (attachment.serverMediaId == id);
				});
			if (existing != end(_attachments)) {
				if (!existing->origin) {
					existing->origin = origin;
				}
				refreshAttachmentInput(*existing);
				return;
			}
			auto attachment = AttachmentRecord{
				.type = PreparedFileType::Photo,
				.blockKind = RichPage::BlockKind::Photo,
				.state = AttachmentState::Ready,
				.caption = block.caption.text.text,
				.dimensions = QSize(block.width, block.height),
				.spoiler = block.spoiler,
				.serverMediaId = id,
				.origin = origin,
				.serverPhoto = photo.get(),
			};
			refreshAttachmentInput(attachment);
			_attachments.push_back(std::move(attachment));
		};
		const auto appendDocument = [&](not_null<DocumentData*> document,
				uint64 id,
				RichPage::BlockKind kind) {
			auto existing = std::find_if(
				_attachments.begin(),
				_attachments.end(),
				[=](const AttachmentRecord &attachment) {
					return (attachment.blockKind == kind)
						&& (attachment.serverMediaId == id);
				});
			if (existing != end(_attachments)) {
				if (!existing->origin) {
					existing->origin = origin;
				}
				refreshAttachmentInput(*existing);
				return;
			}
			auto attachment = AttachmentRecord{
				.type = (kind == RichPage::BlockKind::Audio)
					? PreparedFileType::Music
					: PreparedFileType::Video,
				.blockKind = kind,
				.state = AttachmentState::Ready,
				.caption = block.caption.text.text,
				.dimensions = QSize(block.width, block.height),
				.spoiler = block.spoiler,
				.autoplay = block.autoplay,
				.loop = block.loop,
				.serverMediaId = id,
				.origin = origin,
				.serverDocument = document.get(),
			};
			if (kind == RichPage::BlockKind::Audio) {
				attachment.audioTitle = block.audioTitle;
				attachment.audioPerformer = block.audioPerformer;
				attachment.audioFileName = block.audioFileName;
				attachment.audioDuration = block.audioDuration;
			}
			refreshAttachmentInput(attachment);
			_attachments.push_back(std::move(attachment));
		};
		switch (block.kind) {
		case RichPage::BlockKind::Photo: {
			const auto photo = block.photo
				? block.photo
				: _session->data().photo(block.photoId).get();
			if (photo && block.photoId) {
				appendPhoto(not_null{ photo }, block.photoId);
			}
		} break;
		case RichPage::BlockKind::Video:
		case RichPage::BlockKind::Audio: {
			const auto document = block.document
				? block.document
				: _session->data().document(block.documentId).get();
			if (document && block.documentId) {
				appendDocument(
					not_null{ document },
					block.documentId,
					block.kind);
			}
		} break;
		case RichPage::BlockKind::GroupedMedia:
			for (const auto &item : block.mediaItems) {
				restoreComposeDraftAttachment(item, origin);
			}
			break;
		default:
			break;
		}
		for (const auto &child : block.blocks) {
			restoreComposeDraftAttachment(child, origin);
		}
		for (const auto &item : block.listItems) {
			for (const auto &child : item.blocks) {
				restoreComposeDraftAttachment(child, origin);
			}
		}
	}

	void restoreComposeDraftAttachment(
			const RichPage::GroupedMediaItem &item,
			const ::Data::FileOrigin &origin) {
		if (item.kind == RichPage::BlockKind::Photo) {
			const auto photo = item.photo
				? item.photo
				: _session->data().photo(item.photoId).get();
			if (!photo || !item.photoId) {
				return;
			}
			auto existing = std::find_if(
				_attachments.begin(),
				_attachments.end(),
				[=](const AttachmentRecord &attachment) {
					return (attachment.blockKind == RichPage::BlockKind::Photo)
						&& (attachment.serverMediaId == item.photoId);
				});
			if (existing != end(_attachments)) {
				if (!existing->origin) {
					existing->origin = origin;
				}
				refreshAttachmentInput(*existing);
				return;
			}
			auto attachment = AttachmentRecord{
				.type = PreparedFileType::Photo,
				.blockKind = RichPage::BlockKind::Photo,
				.state = AttachmentState::Ready,
				.dimensions = QSize(item.width, item.height),
				.spoiler = item.spoiler,
				.serverMediaId = item.photoId,
				.origin = origin,
				.serverPhoto = photo,
			};
			refreshAttachmentInput(attachment);
			_attachments.push_back(std::move(attachment));
		} else if (item.kind == RichPage::BlockKind::Video) {
			const auto document = item.document
				? item.document
				: _session->data().document(item.documentId).get();
			if (!document || !item.documentId) {
				return;
			}
			auto existing = std::find_if(
				_attachments.begin(),
				_attachments.end(),
				[=](const AttachmentRecord &attachment) {
					return (attachment.blockKind == RichPage::BlockKind::Video)
						&& (attachment.serverMediaId == item.documentId);
				});
			if (existing != end(_attachments)) {
				if (!existing->origin) {
					existing->origin = origin;
				}
				refreshAttachmentInput(*existing);
				return;
			}
			auto attachment = AttachmentRecord{
				.type = PreparedFileType::Video,
				.blockKind = RichPage::BlockKind::Video,
				.state = AttachmentState::Ready,
				.dimensions = QSize(item.width, item.height),
				.spoiler = item.spoiler,
				.autoplay = item.autoplay,
				.loop = item.loop,
				.serverMediaId = item.documentId,
				.origin = origin,
				.serverDocument = document,
			};
			refreshAttachmentInput(attachment);
			_attachments.push_back(std::move(attachment));
		}
	}

	void refreshAttachmentInput(AttachmentRecord &attachment) {
		if (attachment.serverPhoto) {
			const auto input = attachment.serverPhoto->mtpInput();
			if (input.type() == mtpc_inputPhoto) {
				attachment.inputPhoto = input;
				attachment.accessHash = input.c_inputPhoto().vaccess_hash().v;
				attachment.fileReference = attachment.serverPhoto->fileReference();
			}
		} else if (attachment.serverDocument) {
			const auto input = attachment.serverDocument->mtpInput();
			if (input.type() == mtpc_inputDocument) {
				attachment.inputDocument = input;
				attachment.accessHash = input.c_inputDocument().vaccess_hash().v;
				attachment.fileReference = attachment.serverDocument->fileReference();
			}
		}
	}
	void editorCreated(not_null<Widget*> editor);
	void cancelRichDraftAutosave();
	void restartRichDraftAutosave();
	void handleRichDraftAutosave(Widget::AutosaveEvent event);
	[[nodiscard]] std::optional<::Data::Draft> prepareRichDraftForAutosave() const;
	void saveRichDraftNow();
	void startCloseWithDraftSave();
	void startCloseWithDraftSaveThen(Fn<void()> continuation);
	void saveRichDraftForClose(uint64 generation);
	void retryRichDraftCloseSaveIfNeeded();
	void closeWithDraftSaveDone(uint64 generation);
	void closeWithDraftSaveFailed(uint64 generation, QString error = QString());
	void closeNowWithoutDraftSave(uint64 generation);
	void cancelCloseWithDraftSave(uint64 generation);
	void showCloseDraftSavingBox(uint64 generation);
	void showCloseDraftSaveFailedBox(uint64 generation, const QString &error);
	void syncFieldWithCloudDraftAfterClose();

	[[nodiscard]] AttachmentRecord *findAttachment(FullMsgId uploadId) {
		for (auto &attachment : _attachments) {
			if (attachment.uploadId == uploadId) {
				return &attachment;
			}
		}
		return nullptr;
	}

	[[nodiscard]] const AttachmentRecord *findAttachment(
			FullMsgId uploadId) const {
		for (const auto &attachment : _attachments) {
			if (attachment.uploadId == uploadId) {
				return &attachment;
			}
		}
		return nullptr;
	}

	[[nodiscard]] MediaBatch *findMediaBatch(uint64 batchId) {
		for (auto &batch : _mediaBatches) {
			if (batch.id == batchId) {
				return &batch;
			}
		}
		return nullptr;
	}

	[[nodiscard]] bool eraseFinishedMediaBatch(uint64 batchId) {
		auto erased = false;
		_mediaBatches.erase(
			std::remove_if(
				_mediaBatches.begin(),
				_mediaBatches.end(),
				[=, &erased](const MediaBatch &batch) {
					const auto done = (batch.id == batchId)
						&& std::all_of(
							batch.items.begin(),
							batch.items.end(),
							[](const MediaBatchItem &item) {
								return (item.state
										== MediaBatchItemState::Inserted)
									|| (item.state
										== MediaBatchItemState::Skipped);
							});
					if (done) {
						erased = true;
					}
					return done;
				}),
			_mediaBatches.end());
		return erased;
	}

	void markMediaBatchItemSkipped(uint64 batchId, int order) {
		const auto batch = findMediaBatch(batchId);
		if (!batch || order < 0 || order >= int(batch->items.size())) {
			return;
		}
		auto &item = batch->items[order];
		if (item.state != MediaBatchItemState::Inserted) {
			item.state = MediaBatchItemState::Skipped;
		}
	}

	void markMediaBatchItemReady(
			uint64 batchId,
			int order,
			FullMsgId uploadId,
			RichPage::BlockKind blockKind) {
		const auto batch = findMediaBatch(batchId);
		if (!batch
			|| order < 0
			|| order >= int(batch->items.size())
			|| !findAttachment(uploadId)) {
			return;
		}
		auto &item = batch->items[order];
		item.state = MediaBatchItemState::Ready;
		item.uploadId = uploadId;
		item.blockKind = blockKind;
	}

	void eraseAttachment(FullMsgId uploadId) {
		const auto i = std::find_if(
			_attachments.begin(),
			_attachments.end(),
			[=](const AttachmentRecord &attachment) {
				return attachment.uploadId == uploadId;
			});
		if (i == _attachments.end()) {
			return;
		}
		if (i->state != AttachmentState::Ready) {
			_session->uploader().cancel(i->uploadId);
		}
		_attachments.erase(i);
	}

	[[nodiscard]] bool hasUninsertedMediaBatchUpload(
			FullMsgId uploadId) const {
		for (const auto &batch : _mediaBatches) {
			for (const auto &item : batch.items) {
				if (item.state == MediaBatchItemState::Ready
					&& item.uploadId == uploadId) {
					return true;
				}
			}
		}
		return false;
	}

	void abandonMediaBatch(uint64 batchId) {
		const auto batch = findMediaBatch(batchId);
		if (!batch) {
			return;
		}
		for (auto &item : batch->items) {
			if (item.state == MediaBatchItemState::Ready
				&& item.uploadId) {
				eraseAttachment(item.uploadId);
			}
			if (item.state != MediaBatchItemState::Inserted) {
				item.state = MediaBatchItemState::Skipped;
			}
		}
		_mediaBatches.erase(
			std::remove_if(
				_mediaBatches.begin(),
				_mediaBatches.end(),
				[=](const MediaBatch &batch) {
					return batch.id == batchId;
				}),
			_mediaBatches.end());
		maybeContinueDeferredSubmit();
	}

	void flushMediaBatch(uint64 batchId) {
		if (eraseFinishedMediaBatch(batchId)) {
			maybeContinueDeferredSubmit();
			return;
		}
		const auto batch = findMediaBatch(batchId);
		if (!batch) {
			return;
		}
		if (!batch->editor) {
			abandonMediaBatch(batchId);
			return;
		}
		auto blocks = std::vector<RichPage::Block>();
		auto emittedUploadIds = std::vector<FullMsgId>();
		const auto skipFinished = [&] {
			while (batch->nextIndex < int(batch->items.size())) {
				const auto state = batch->items[batch->nextIndex].state;
				if (state != MediaBatchItemState::Skipped
					&& state != MediaBatchItemState::Inserted) {
					return;
				}
				++batch->nextIndex;
			}
		};
		const auto appendSubrun = [&](
				const std::vector<FullMsgId> &uploadIds) {
			if (uploadIds.empty()) {
				return;
			}
			if (batch->groupAnchor || uploadIds.size() == 1) {
				for (const auto &uploadId : uploadIds) {
					if (const auto attachment = findAttachment(uploadId)) {
						blocks.push_back(makeAttachmentBlock(*attachment));
					}
				}
			} else {
				blocks.push_back(makeGroupedAttachmentBlock(uploadIds));
			}
			emittedUploadIds.insert(
				emittedUploadIds.end(),
				uploadIds.begin(),
				uploadIds.end());
		};
		const auto appendPhotoVideoRun = [&](
				const std::vector<FullMsgId> &uploadIds) {
			auto subrun = std::vector<FullMsgId>();
			auto hasCaption = false;
			for (const auto &uploadId : uploadIds) {
				const auto attachment = findAttachment(uploadId);
				if (!attachment) {
					continue;
				}
				const auto itemHasCaption = !attachment->caption.isEmpty();
				if (itemHasCaption && hasCaption) {
					appendSubrun(subrun);
					subrun.clear();
					hasCaption = false;
				}
				subrun.push_back(uploadId);
				hasCaption = hasCaption || itemHasCaption;
			}
			appendSubrun(subrun);
		};

		while (true) {
			skipFinished();
			if (batch->nextIndex >= int(batch->items.size())) {
				break;
			}
			auto &item = batch->items[batch->nextIndex];
			if (item.state == MediaBatchItemState::Waiting) {
				break;
			}
			if (item.state != MediaBatchItemState::Ready) {
				break;
			}
			const auto attachment = findAttachment(item.uploadId);
			if (!attachment) {
				item.state = MediaBatchItemState::Skipped;
				++batch->nextIndex;
				continue;
			}
			if (item.blockKind == RichPage::BlockKind::Audio) {
				blocks.push_back(makeAttachmentBlock(*attachment));
				emittedUploadIds.push_back(item.uploadId);
				item.state = MediaBatchItemState::Inserted;
				++batch->nextIndex;
				continue;
			}
			if (!IsPhotoVideoRichMessageKind(item.blockKind)) {
				item.state = MediaBatchItemState::Skipped;
				++batch->nextIndex;
				continue;
			}
			auto cursor = batch->nextIndex;
			auto waitingBeforeBoundary = false;
			auto runUploadIds = std::vector<FullMsgId>();
			auto runIndexes = std::vector<int>();
			while (cursor < int(batch->items.size())) {
				auto &candidate = batch->items[cursor];
				if (candidate.state == MediaBatchItemState::Skipped
					|| candidate.state == MediaBatchItemState::Inserted) {
					++cursor;
					continue;
				}
				if (candidate.state == MediaBatchItemState::Waiting) {
					waitingBeforeBoundary = true;
					break;
				}
				if (candidate.state != MediaBatchItemState::Ready) {
					waitingBeforeBoundary = true;
					break;
				}
				if (candidate.blockKind == RichPage::BlockKind::Audio) {
					break;
				}
				if (!IsPhotoVideoRichMessageKind(candidate.blockKind)) {
					candidate.state = MediaBatchItemState::Skipped;
					++cursor;
					continue;
				}
				if (findAttachment(candidate.uploadId)) {
					runUploadIds.push_back(candidate.uploadId);
					runIndexes.push_back(cursor);
				} else {
					candidate.state = MediaBatchItemState::Skipped;
				}
				++cursor;
			}
			if (waitingBeforeBoundary) {
				break;
			}
			if (runUploadIds.empty()) {
				batch->nextIndex = cursor;
				continue;
			}
			appendPhotoVideoRun(runUploadIds);
			for (const auto index : runIndexes) {
				batch->items[index].state = MediaBatchItemState::Inserted;
			}
			batch->nextIndex = cursor;
		}
		if (blocks.empty()) {
			groupBatchIntoCollageIfFinished(batchId);
			if (eraseFinishedMediaBatch(batchId)) {
				maybeContinueDeferredSubmit();
			}
			return;
		}
		const auto editor = batch->editor;
		_editor = editor;
		const auto addedTopLevel = int(blocks.size());
		batch->insertedTopLevel += addedTopLevel;
		if (batch->insertMode == AttachmentInsertMode::ClipboardPaste
			&& batch->insertTarget
			&& batch->insertTarget->blockDrop) {
			auto target = *batch->insertTarget;
			editor->pastePreparedBlocks(std::move(blocks), std::move(target));
			// A batch can flush more than once as its items become ready at
			// different times. Keep the drop position and advance it past the
			// blocks we just inserted so later flushes land contiguously here
			// instead of falling back to the current text cursor.
			batch->insertTarget->blockDrop->insertIndex += addedTopLevel;
		} else if (batch->insertMode == AttachmentInsertMode::ClipboardPaste
			&& batch->insertTarget) {
			auto target = std::move(*batch->insertTarget);
			batch->insertTarget = std::nullopt;
			editor->pastePreparedBlocks(std::move(blocks), std::move(target));
		} else {
			editor->insertPreparedBlocks(std::move(blocks));
		}
		refreshAttachmentLocatorsAndDropMissing();
		for (const auto &uploadId : emittedUploadIds) {
			if (const auto attachment = findAttachment(uploadId)) {
				if (attachment->state == AttachmentState::Ready && _editor) {
					auto patched = true;
					_editor->applyExternalRichPageMutation([&](
							RichPage &page) {
						const auto result = patchVisibleAttachmentBlocks(
							page,
							*attachment);
						patched = patched && result;
						return result;
					});
					if (!patched) {
						requestEditorUpdate();
					}
				}
			}
		}
		requestEditorUpdate();
		groupBatchIntoCollageIfFinished(batchId);
		if (eraseFinishedMediaBatch(batchId)) {
			maybeContinueDeferredSubmit();
		}
	}

	void groupBatchIntoCollageIfFinished(uint64 batchId) {
		const auto batch = findMediaBatch(batchId);
		if (!batch || !batch->groupAnchor || !batch->editor) {
			return;
		}
		const auto finished = std::all_of(
			batch->items.begin(),
			batch->items.end(),
			[](const MediaBatchItem &item) {
				return (item.state == MediaBatchItemState::Inserted)
					|| (item.state == MediaBatchItemState::Skipped);
			});
		if (!finished) {
			return;
		}
		const auto anchor = *batch->groupAnchor;
		const auto insertedCount = batch->insertedTopLevel;
		batch->groupAnchor = std::nullopt;
		batch->editor->groupBlocksIntoGroup(anchor, insertedCount);
	}

	[[nodiscard]] bool mediaIdMatchesAttachment(
			uint64 id,
			const AttachmentRecord &attachment) const {
		return id
			&& ((id == attachment.localMediaId)
				|| (attachment.serverMediaId
					&& (id == attachment.serverMediaId)));
	}

	[[nodiscard]] bool groupedMediaItemMatchesAttachment(
			const RichPage::GroupedMediaItem &item,
			const AttachmentRecord &attachment) const {
		switch (attachment.blockKind) {
		case RichPage::BlockKind::Photo:
			return (item.kind == RichPage::BlockKind::Photo)
				&& mediaIdMatchesAttachment(item.photoId, attachment);
		case RichPage::BlockKind::Video:
			return (item.kind == RichPage::BlockKind::Video)
				&& mediaIdMatchesAttachment(item.documentId, attachment);
		default:
			return false;
		}
	}

	[[nodiscard]] bool groupedMediaBlockMatchesAttachment(
			const RichPage::Block &block,
			const AttachmentRecord &attachment) const {
		return (block.kind == RichPage::BlockKind::GroupedMedia)
			&& std::any_of(
				block.mediaItems.begin(),
				block.mediaItems.end(),
				[&](const RichPage::GroupedMediaItem &item) {
					return groupedMediaItemMatchesAttachment(
						item,
						attachment);
				});
	}

	[[nodiscard]] bool blockMatchesAttachment(
		const RichPage::Block &block,
		const AttachmentRecord &attachment) const {
		switch (attachment.blockKind) {
		case RichPage::BlockKind::Photo:
			return ((block.kind == RichPage::BlockKind::Photo)
					&& mediaIdMatchesAttachment(block.photoId, attachment))
				|| groupedMediaBlockMatchesAttachment(block, attachment);
		case RichPage::BlockKind::Video:
			return ((block.kind == RichPage::BlockKind::Video)
					&& mediaIdMatchesAttachment(block.documentId, attachment))
				|| groupedMediaBlockMatchesAttachment(block, attachment);
		case RichPage::BlockKind::Audio:
			return (block.kind == RichPage::BlockKind::Audio)
				&& mediaIdMatchesAttachment(block.documentId, attachment);
		default:
			return false;
		}
	}

	void collectBlockLocators(
		const std::vector<RichPage::Block> &blocks,
		const State::BlockContainerPath &container,
		const AttachmentRecord &attachment,
		std::vector<State::BlockPath> &result) const {
		for (auto i = 0, count = int(blocks.size()); i != count; ++i) {
			const auto path = State::BlockPath{
				.container = container,
				.index = i,
			};
			const auto &block = blocks[i];
			if (blockMatchesAttachment(block, attachment)) {
				result.push_back(path);
			}
			if (!block.blocks.empty()) {
				auto child = container;
				child.steps.push_back({
					.kind = State::BlockContainerKind::BlockChildren,
					.blockIndex = i,
				});
				collectBlockLocators(
					block.blocks,
					child,
					attachment,
					result);
			}
			for (auto itemIndex = 0, items = int(block.listItems.size());
				itemIndex != items;
				++itemIndex) {
				const auto &itemBlocks = block.listItems[itemIndex].blocks;
				if (itemBlocks.empty()) {
					continue;
				}
				auto child = container;
				child.steps.push_back({
					.kind = State::BlockContainerKind::ListItemChildren,
					.blockIndex = i,
					.listItemIndex = itemIndex,
				});
				collectBlockLocators(
					itemBlocks,
					child,
					attachment,
					result);
			}
		}
	}

	void refreshAttachmentLocators(
		const RichPage &page,
		AttachmentRecord &attachment) {
		auto locators = std::vector<State::BlockPath>();
		collectBlockLocators(
			page.blocks,
			State::BlockContainerPath(),
			attachment,
			locators);
		attachment.blockLocators = std::move(locators);
	}

	void refreshAttachmentLocatorsAndDropMissing() {
		const auto &page = _state->richPage();
		for (auto &attachment : _attachments) {
			refreshAttachmentLocators(page, attachment);
		}
		for (auto i = _attachments.begin(); i != _attachments.end();) {
			if (!i->blockLocators.empty()) {
				++i;
				continue;
			}
			if (hasUninsertedMediaBatchUpload(i->uploadId)) {
				++i;
				continue;
			}
			if (i->state != AttachmentState::Ready) {
				_session->uploader().cancel(i->uploadId);
			}
			i = _attachments.erase(i);
		}
	}

	[[nodiscard]] RichPage *visibleRichPage() const {
		return &const_cast<RichPage&>(_state->richPage());
	}

	[[nodiscard]] std::vector<RichPage::Block> *visibleBlockContainer(
		RichPage &page,
		const State::BlockContainerPath &path) const {
		auto result = &page.blocks;
		for (const auto &step : path.steps) {
			switch (step.kind) {
			case State::BlockContainerKind::Root:
				break;
			case State::BlockContainerKind::BlockChildren:
				if (step.blockIndex < 0 || step.blockIndex >= int(result->size())) {
					return nullptr;
				}
				result = &(*result)[step.blockIndex].blocks;
				break;
			case State::BlockContainerKind::ListItemChildren: {
				if (step.blockIndex < 0 || step.blockIndex >= int(result->size())) {
					return nullptr;
				}
				auto &block = (*result)[step.blockIndex];
				if (step.listItemIndex < 0
					|| step.listItemIndex >= int(block.listItems.size())) {
					return nullptr;
				}
				result = &block.listItems[step.listItemIndex].blocks;
			} break;
			}
		}
		return result;
	}

	[[nodiscard]] RichPage::Block *visibleBlock(
		RichPage &page,
		const State::BlockPath &path) const {
		const auto blocks = visibleBlockContainer(page, path.container);
		if (!blocks || path.index < 0 || path.index >= int(blocks->size())) {
			return nullptr;
		}
		return &(*blocks)[path.index];
	}

	[[nodiscard]] bool patchVisibleAttachmentBlocks(
		RichPage &page,
		AttachmentRecord &attachment) {
		refreshAttachmentLocators(page, attachment);
		for (const auto &locator : attachment.blockLocators) {
			const auto block = visibleBlock(page, locator);
			if (!block || !blockMatchesAttachment(*block, attachment)) {
				continue;
			}
			if (!patchReadyAttachmentInBlock(*block, attachment)) {
				return false;
			}
		}
		refreshAttachmentLocators(page, attachment);
		return true;
	}

	[[nodiscard]] int pendingAttachmentPlaceholders() const {
		auto result = _pendingAttachmentPrepareCount;
		if (_preparing) {
			++result;
		}
		for (const auto &queued : _prepareQueue) {
			if (AcceptedPreparedFileType(queued.file.type)
				|| !queued.file.information) {
				++result;
			}
		}
		for (const auto &batch : _mediaBatches) {
			for (const auto &item : batch.items) {
				if (item.state == MediaBatchItemState::Ready) {
					++result;
				}
			}
		}
		return result;
	}

	[[nodiscard]] bool exceedsMediaLimitWith(int additionalMedia) const {
		return (CountRichPageMedia(_state->richPage())
			+ pendingAttachmentPlaceholders()
			+ additionalMedia) > _limits.maxMedia;
	}

	[[nodiscard]] bool hasVisibleAttachmentBlock(AttachmentRecord &attachment) {
		refreshAttachmentLocators(_state->richPage(), attachment);
		return !attachment.blockLocators.empty();
	}

	[[nodiscard]] bool hasVisibleFailedAttachments() {
		for (auto &attachment : _attachments) {
			if (attachment.state == AttachmentState::Failed
				&& hasVisibleAttachmentBlock(attachment)) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool hasVisiblePendingAttachments() {
		for (auto &attachment : _attachments) {
			if (((attachment.state == AttachmentState::Uploading)
					|| (attachment.state == AttachmentState::Finalizing))
				&& hasVisibleAttachmentBlock(attachment)) {
				return true;
			}
		}
		return false;
	}

	void showAttachmentFailedToast() {
		showToast(tr::lng_attach_failed(tr::now));
	}

	void showRichMessageLimitToast(RichMessageLimitError error) const {
		switch (error) {
		case RichMessageLimitError::Length:
			showToast(tr::lng_article_limit_length(tr::now));
			return;
		case RichMessageLimitError::Blocks:
			showToast(tr::lng_article_limit_blocks(tr::now));
			return;
		case RichMessageLimitError::Depth:
			showToast(tr::lng_article_limit_depth(tr::now));
			return;
		case RichMessageLimitError::Media:
			showToast(tr::lng_article_limit_media(tr::now));
			return;
		case RichMessageLimitError::TableColumns:
			showToast(tr::lng_article_limit_columns(tr::now));
			return;
		}
		showToast(tr::lng_edit_error(tr::now));
	}

	void showUnsupportedMediaToast(uint64 batchId) {
		if (_rejectedToastBatchId == batchId) {
			return;
		}
		_rejectedToastBatchId = batchId;
		showToast(tr::lng_iv_editor_media_invalid_file(tr::now));
	}

	[[nodiscard]] bool hasPendingPreparation() const {
		return _preparing
			|| !_prepareQueue.empty()
			|| (_pendingAttachmentPrepareCount > 0)
			|| !_mediaBatches.empty();
	}

	void maybeContinueDeferredSubmit() {
		retryRichDraftCloseSaveIfNeeded();
		if (!_submitDeferred || hasPendingPreparation()) {
			return;
		}
		_submitDeferred = false;
		simulateSubmitClick();
	}

	void simulateSubmitClick() {
		if (!_submitButton) {
			return;
		}
		const auto post = [button = _submitButton](QEvent::Type type) {
			if (!button) {
				return;
			}
			QApplication::postEvent(
				button,
				new QMouseEvent(
					type,
					QPointF(0, 0),
					Qt::LeftButton,
					Qt::LeftButton,
					Qt::NoModifier));
		};
		post(QEvent::MouseButtonPress);
		post(QEvent::MouseButtonRelease);
	}

	const not_null<Main::Session*> _session;
	const not_null<PeerData*> _peer;
	const base::weak_ptr<Window::SessionController> _controller;
	const Mode _mode;
	const ShowWindowDescriptor::SubmitType _submitType;
	const FullMsgId _articleId;
	std::optional<Api::SendAction> _composeAction;
	const SendMenu::Details _sendMenuDetails;
	const std::optional<EditedItemSnapshot> _edited;
	const std::optional<ComposeThreadKey> _composeThreadKey;
	const std::shared_ptr<RichPage> _page;
	const std::shared_ptr<Markdown::MediaRuntime> _runtime;
	const RichMessageLimits _limits;
	const std::shared_ptr<State> _state;
	Api::SendOptions _submitOptions;
	SendPaymentHelper _sendPayment;
	std::shared_ptr<ChatHelpers::Show> _editorShow;
	QPointer<Ui::RpWidget> _submitButton;
	QPointer<Widget> _editor;
	std::unique_ptr<WindowHost> _windowHost;
	std::shared_ptr<ArticleSession> _backgroundHold;
	Fn<void()> _closeDraftSaveContinuation;
	Fn<void()> _onWindowClosedContinuation;
	std::shared_ptr<const RichPage> _submittedPage;
	std::vector<AttachmentRecord> _attachments;
	base::flat_map<uint64, QImage> _originalMediaImages;
	base::flat_map<uint64, PendingPhotoEditSource> _pendingPhotoEditSources;
	std::deque<QueuedPrepare> _prepareQueue;
	std::vector<MediaBatch> _mediaBatches;
	TaskQueue _attachmentPrepareQueue;
	base::Timer _richDraftAutosaveTimer;
	base::weak_qptr<Ui::GenericBox> _closeDraftSaveBox;
	rpl::lifetime _editorAutosaveLifetime;
	rpl::lifetime _photoEditSourceLifetime;
	rpl::lifetime _lifetime;
	uint64 _prepareBatchId = 0;
	uint64 _rejectedToastBatchId = 0;
	uint64 _closeDraftSaveGeneration = 0;
	mtpRequestId _closeDraftSaveRequestId = 0;
	int _pendingAttachmentPrepareCount = 0;
	bool _preparing = false;
	bool _submitDeferred = false;
	bool _submitApiRequested = false;
	bool _closeDraftSaveActive = false;
	bool _closeDraftSaveWaiting = false;
	bool _richDraftAutosaveRetryPending = false;

};

void ArticleSession::editorCreated(not_null<Widget*> editor) {
	_editor = editor;
	if (!_composeAction || !_composeThreadKey) {
		return;
	}
	_editorAutosaveLifetime.destroy();
	editor->autosaveEvents(
	) | rpl::on_next([weak = weak_from_this()](Widget::AutosaveEvent event) {
		if (const auto session = weak.lock()) {
			session->handleRichDraftAutosave(event);
		}
	}, _editorAutosaveLifetime);
}

void ArticleSession::cancelRichDraftAutosave() {
	_richDraftAutosaveTimer.cancel();
	_editorAutosaveLifetime.destroy();
	_richDraftAutosaveRetryPending = false;
}

void ArticleSession::restartRichDraftAutosave() {
	_richDraftAutosaveTimer.callOnce(kRichDraftAutosaveTimeout);
}

void ArticleSession::handleRichDraftAutosave(Widget::AutosaveEvent event) {
	if (!_composeAction
		|| !_composeThreadKey
		|| !_windowHost
		|| _submittedPage
		|| _submitApiRequested
		|| _closeDraftSaveActive) {
		return;
	}
	switch (event.type) {
	case Widget::AutosaveEventType::TextIdle:
		restartRichDraftAutosave();
		return;
	case Widget::AutosaveEventType::StructuralMutation:
		_richDraftAutosaveTimer.cancel();
		saveRichDraftNow();
		return;
	}
}

std::optional<::Data::Draft> ArticleSession::prepareRichDraftForAutosave() const {
	if (!_composeAction || !_composeThreadKey) {
		return std::nullopt;
	}
	const auto topicRootId = _composeThreadKey->draftKey.topicRootId();
	const auto monoforumPeerId = _composeThreadKey->draftKey.monoforumPeerId();
	const auto history = _composeAction->history;
	const auto cloudDraft = history->cloudDraft(topicRootId, monoforumPeerId);
	auto draft = cloudDraft
		? *cloudDraft
		: ::Data::Draft(
			TextWithTags(),
			_composeAction->replyTo,
			SuggestOptions(),
			MessageCursor(),
			::Data::WebPageDraft());
	draft.textWithTags = TextWithTags();
	draft.cursor = MessageCursor();
	draft.webpage = ::Data::WebPageDraft();
	draft.reply.topicRootId = topicRootId;
	draft.reply.monoforumPeerId = monoforumPeerId;
	if (_state->articleEmpty()) {
		draft.richMessage = nullptr;
		draft.richMessageSummary = {};
		return draft;
	}
	if (auto simple = SerializeAsSimple(_state->richPage(), _session)) {
		draft.textWithTags = {
			simple->text,
			TextUtilities::ConvertEntitiesToTextTags(simple->entities),
		};
		draft.cursor = MessageCursor(
			int(simple->text.size()),
			int(simple->text.size()),
			Ui::kQFixedMax);
		draft.richMessage = nullptr;
		draft.richMessageSummary = {};
		return draft;
	}
	auto richMessage = std::make_shared<RichPage>(_state->richPage());
	const auto serialized = SerializeInputRichMessage(
		_session,
		*richMessage,
		SerializeInputRichMessageMode::Draft);
	if (serialized.status == SerializeInputRichMessageStatus::Failed) {
		return std::nullopt;
	}
	draft.richMessage = std::move(richMessage);
	draft.richMessageSummary = FlattenRichPageSummary(*draft.richMessage);
	return draft;
}

void ArticleSession::saveRichDraftNow() {
	if (!_composeAction
		|| !_composeThreadKey
		|| !_windowHost
		|| _submittedPage
		|| _submitApiRequested
		|| _closeDraftSaveActive) {
		return;
	}
	const auto prepared = prepareRichDraftForAutosave();
	if (!prepared) {
		_richDraftAutosaveRetryPending = hasVisiblePendingAttachments();
		return;
	}
	const auto topicRootId = _composeThreadKey->draftKey.topicRootId();
	const auto monoforumPeerId = _composeThreadKey->draftKey.monoforumPeerId();
	const auto history = _composeAction->history;
	const auto thread = history->threadFor(topicRootId, monoforumPeerId);
	if (!thread) {
		return;
	}
	const auto cloudDraft = history->createCloudDraft(
		topicRootId,
		monoforumPeerId,
		&*prepared);
	if (!cloudDraft) {
		return;
	}
	_richDraftAutosaveRetryPending = (_session->api().saveDraftToCloud(
		not_null{ thread },
		*cloudDraft) == 0);
}

void ArticleSession::startCloseWithDraftSave() {
	if (_closeDraftSaveActive) {
		return;
	}
	_closeDraftSaveActive = true;
	_closeDraftSaveWaiting = false;
	_closeDraftSaveRequestId = 0;
	_richDraftAutosaveTimer.cancel();
	const auto generation = ++_closeDraftSaveGeneration;
	showCloseDraftSavingBox(generation);
	saveRichDraftForClose(generation);
}

void ArticleSession::startCloseWithDraftSaveThen(Fn<void()> continuation) {
	_closeDraftSaveContinuation = std::move(continuation);
	startCloseWithDraftSave();
}

void ArticleSession::saveRichDraftForClose(uint64 generation) {
	if (!_closeDraftSaveActive
		|| generation != _closeDraftSaveGeneration
		|| !_composeAction
		|| !_composeThreadKey
		|| !_windowHost
		|| _submittedPage
		|| _submitApiRequested) {
		return;
	}
	if (hasVisibleFailedAttachments()) {
		closeWithDraftSaveFailed(generation);
		return;
	} else if (hasPendingPreparation() || hasVisiblePendingAttachments()) {
		_closeDraftSaveWaiting = true;
		return;
	}
	const auto prepared = prepareRichDraftForAutosave();
	if (!prepared) {
		closeWithDraftSaveFailed(generation);
		return;
	}
	const auto topicRootId = _composeThreadKey->draftKey.topicRootId();
	const auto monoforumPeerId = _composeThreadKey->draftKey.monoforumPeerId();
	const auto history = _composeAction->history;
	const auto thread = history->threadFor(topicRootId, monoforumPeerId);
	if (!thread) {
		closeWithDraftSaveFailed(generation);
		return;
	}
	const auto cloudDraft = history->createCloudDraft(
		topicRootId,
		monoforumPeerId,
		&*prepared);
	if (!cloudDraft) {
		closeWithDraftSaveFailed(generation);
		return;
	}
	_closeDraftSaveWaiting = false;
	_richDraftAutosaveRetryPending = false;
	_closeDraftSaveRequestId = _session->api().saveDraftToCloud(
		not_null{ thread },
		*cloudDraft,
		[weak = weak_from_this(), generation] {
			if (const auto session = weak.lock()) {
				session->closeWithDraftSaveDone(generation);
			}
		},
		[weak = weak_from_this(), generation](const MTP::Error &error) {
			if (const auto session = weak.lock()) {
				session->closeWithDraftSaveFailed(generation, error.type());
			}
		});
	if (!_closeDraftSaveRequestId) {
		closeWithDraftSaveFailed(generation);
	}
}

void ArticleSession::retryRichDraftCloseSaveIfNeeded() {
	if (_closeDraftSaveActive && _closeDraftSaveWaiting) {
		saveRichDraftForClose(_closeDraftSaveGeneration);
	}
}

void ArticleSession::closeWithDraftSaveDone(uint64 generation) {
	if (!_closeDraftSaveActive || generation != _closeDraftSaveGeneration) {
		return;
	}
	_closeDraftSaveActive = false;
	_closeDraftSaveWaiting = false;
	_closeDraftSaveRequestId = 0;
	++_closeDraftSaveGeneration;
	if (_closeDraftSaveBox) {
		_closeDraftSaveBox->closeBox();
	}
	if (_windowHost) {
		_windowHost->close();
	}
	if (const auto continuation = base::take(_closeDraftSaveContinuation)) {
		continuation();
	}
}

void ArticleSession::closeWithDraftSaveFailed(uint64 generation, QString error) {
	if (!_closeDraftSaveActive || generation != _closeDraftSaveGeneration) {
		return;
	}
	_closeDraftSaveWaiting = false;
	_closeDraftSaveRequestId = 0;
	if (_closeDraftSaveBox) {
		_closeDraftSaveBox->closeBox();
	}
	showCloseDraftSaveFailedBox(generation, error);
}

void ArticleSession::closeNowWithoutDraftSave(uint64 generation) {
	if (!_closeDraftSaveActive || generation != _closeDraftSaveGeneration) {
		return;
	}
	_closeDraftSaveContinuation = nullptr;
	_closeDraftSaveActive = false;
	_closeDraftSaveWaiting = false;
	_closeDraftSaveRequestId = 0;
	++_closeDraftSaveGeneration;
	if (_windowHost) {
		_windowHost->close();
	}
}

void ArticleSession::cancelCloseWithDraftSave(uint64 generation) {
	if (!_closeDraftSaveActive || generation != _closeDraftSaveGeneration) {
		return;
	}
	_closeDraftSaveContinuation = nullptr;
	_closeDraftSaveActive = false;
	_closeDraftSaveWaiting = false;
	_closeDraftSaveRequestId = 0;
	++_closeDraftSaveGeneration;
	if (_closeDraftSaveBox) {
		_closeDraftSaveBox->closeBox();
	}
}

void ArticleSession::syncFieldWithCloudDraftAfterClose() {
	if (!_composeAction || !_composeThreadKey) {
		return;
	}
	const auto history = _composeAction->history;
	const auto topicRootId = _composeThreadKey->draftKey.topicRootId();
	const auto monoforumPeerId = _composeThreadKey->draftKey.monoforumPeerId();
	if (history->cloudDraft(topicRootId, monoforumPeerId)) {
		history->applyCloudDraft(topicRootId, monoforumPeerId);
	}
}

void ArticleSession::showCloseDraftSavingBox(uint64 generation) {
	if (_closeDraftSaveBox) {
		return;
	}
	const auto show = resolveShow();
	if (!show) {
		return;
	}
	_closeDraftSaveBox = show->show(Ui::MakeConfirmBox({
		.text = tr::lng_iv_editor_saving_draft(),
		.confirmed = [weak = weak_from_this(), generation](
				Fn<void()> closeBox) {
			closeBox();
			if (const auto session = weak.lock()) {
				session->closeNowWithoutDraftSave(generation);
			}
		},
		.cancelled = [weak = weak_from_this(), generation](
				Fn<void()> closeBox) {
			closeBox();
			if (const auto session = weak.lock()) {
				session->cancelCloseWithDraftSave(generation);
			}
		},
		.confirmText = tr::lng_iv_editor_close_now(),
		.cancelText = tr::lng_cancel(),
		.confirmStyle = &st::attentionBoxButton,
		.strictCancel = true,
	}));
}

void ArticleSession::showCloseDraftSaveFailedBox(
		uint64 generation,
		const QString &error) {
	const auto show = resolveShow();
	if (!show) {
		return;
	}
	const auto text = error.isEmpty()
		? tr::lng_iv_editor_save_draft_failed(tr::now)
		: tr::lng_iv_editor_save_draft_failed_error(
			tr::now,
			lt_error,
			error);
	_closeDraftSaveBox = show->show(Ui::MakeConfirmBox({
		.text = text,
		.confirmed = [weak = weak_from_this(), generation](
				Fn<void()> closeBox) {
			closeBox();
			if (const auto session = weak.lock()) {
				session->closeNowWithoutDraftSave(generation);
			}
		},
		.cancelled = [weak = weak_from_this(), generation](
				Fn<void()> closeBox) {
			closeBox();
			if (const auto session = weak.lock()) {
				session->cancelCloseWithDraftSave(generation);
			}
		},
		.confirmText = tr::lng_iv_editor_close_now(),
		.cancelText = tr::lng_cancel(),
		.confirmStyle = &st::attentionBoxButton,
		.strictCancel = true,
	}));
}

bool ArticleSession::SaveOpenComposeDraftThen(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		PeerId monoforumPeerId,
		Fn<void()> onSaved) {
	const auto entry = LookupComposeThreadEntry(
		ComposeKey(session, peerId, topicRootId, monoforumPeerId));
	const auto strong = entry
		? entry->articleSession.lock()
		: nullptr;
	if (!strong) {
		return false;
	}
	strong->startCloseWithDraftSaveThen(std::move(onSaved));
	return true;
}

bool ArticleSession::RequestCloseOpenEditWindowThen(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		Fn<void()> onClosed) {
	for (const auto &weak : Live()) {
		const auto strong = weak.lock();
		if (!strong
			|| strong->_mode != Mode::Edit
			|| strong->_session != session
			|| strong->_peer != peer
			|| !strong->_windowHost) {
			continue;
		}
		strong->_onWindowClosedContinuation = std::move(onClosed);
		strong->_windowHost->activateClose();
		return true;
	}
	return false;
}

} // namespace

void ShowRichMessagesPremiumToast(std::shared_ptr<ChatHelpers::Show> show) {
	if (!show) {
		return;
	}
	const auto session = &show->session();
	show->showToast({
		.text = tr::lng_article_premium_required(
			tr::now,
			lt_link,
			tr::link(tr::bold(
				tr::lng_article_premium_required_link(tr::now))),
			tr::marked),
		.filter = [=](
				const ClickHandlerPtr &handler,
				Qt::MouseButton button) {
			if (button != Qt::LeftButton) {
				return false;
			}
			if (show && show->valid()) {
				ShowPremiumPreviewToBuy(
					show,
					PremiumFeature::RichFormatting);
			} else if (const auto window
					= session->tryResolveWindow(nullptr)) {
				ShowPremiumPreviewToBuy(
					window,
					PremiumFeature::RichFormatting);
			}
			return true;
		},
		.icon = &st::settingsToastStarIcon,
		.adaptive = true,
		.duration = Ui::Toast::kDefaultDuration * 2,
	});
}

void SetupSendLockBadge(
		not_null<Ui::SendButton*> button,
		QPoint position,
		rpl::producer<bool> locked) {
	const auto lockIcon = &st::ivEditorSendLockIcon;
	const auto lockPadding = st::ivEditorSendLockBadgePadding;
	const auto lock = Ui::CreateChild<Ui::RpWidget>(button.get());
	lock->setAttribute(Qt::WA_TransparentForMouseEvents);
	lock->resize(
		lockIcon->width() + 2 * lockPadding,
		lockIcon->height() + 2 * lockPadding);
	lock->move(position);
	lock->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(lock);
		auto hq = PainterHighQualityEnabler(p);
		const auto border = st::ivEditorSendLockBadgeStroke;
		auto pen = QPen(st::windowBg);
		pen.setWidth(border);
		p.setPen(pen);
		p.setBrush(st::windowBgActive);
		const auto half = border / 2.;
		p.drawEllipse(QRectF(lock->rect()).marginsRemoved(
			QMarginsF(half, half, half, half)));
		lockIcon->paint(p, lockPadding, lockPadding, lock->width());
	}, lock->lifetime());
	lock->hide();
	std::move(locked) | rpl::on_next([=](bool shown) {
		lock->setVisible(shown);
		if (shown) {
			lock->raise();
		}
	}, lock->lifetime());
}

void OfferRichMessagePremiumChoice(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Main::Session*> session,
		const RichPage &page,
		Fn<void()> sendWithoutFormatting) {
	if (!show) {
		return;
	}
	const auto flattened = FlattenRichPageToSimpleText(page);
	const auto lengthLimit = ::Data::PremiumLimits(session)
		.messageLengthCurrent();
	const auto sendable = (CountRichPageMedia(page) == 0)
		&& !flattened.text.isEmpty()
		&& (int(flattened.text.size()) <= lengthLimit);
	if (!sendable) {
		ShowRichMessagesPremiumToast(std::move(show));
		return;
	}
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWidth);
		box->setStyle(st::ivEditorPremiumChoiceBox);

		const auto icon = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			st::ivEditorPremiumChoiceIconPadding,
			style::al_top);
		const auto size = st::ivEditorPremiumChoiceIconCircle;
		icon->resize(size, size);
		icon->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(icon);
			auto hq = PainterHighQualityEnabler(p);
			const auto left = (icon->width() - size) / 2;
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			p.drawEllipse(left, 0, size, size);
			const auto &glyph = st::ivEditorPremiumChoiceIcon;
			glyph.paint(
				p,
				left + (size - glyph.width()) / 2,
				(size - glyph.height()) / 2,
				icon->width());
		}, icon->lifetime());

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_article_premium_choice_title(),
				st::boxTitle),
			st::boxRowPadding,
			style::al_top);

		Ui::AddSkip(
			box->verticalLayout(),
			st::ivEditorPremiumChoiceAboutSkip);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_article_premium_choice_about(
					lt_link,
					tr::lng_article_premium_choice_about_link(tr::bold),
					tr::marked),
				st::boxLabel),
			st::boxRowPadding,
			style::al_top);

		Ui::AddSkip(box->verticalLayout());
		Ui::AddSkip(box->verticalLayout());
		Ui::AddSkip(box->verticalLayout());
		const auto subscribe = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				tr::lng_posts_subscribe(),
				st::defaultActiveButton),
			st::boxRowPadding,
			style::al_justify);
		subscribe->setClickedCallback([=] {
			box->closeBox();
			Settings::ShowPremium(session, u"rich_message"_q);
		});
		Ui::AddSkip(box->verticalLayout());
		const auto plain = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				tr::lng_article_premium_choice_plain(),
				st::defaultLightButton),
			st::boxRowPadding,
			style::al_justify);
		plain->setClickedCallback([=] {
			box->closeBox();
			if (sendWithoutFormatting) {
				sendWithoutFormatting();
			}
		});
		Ui::AddSkip(box->verticalLayout());
		const auto cancel = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				tr::lng_cancel(),
				st::defaultLightButton),
			st::boxRowPadding,
			style::al_justify);
		cancel->setClickedCallback([=] {
			box->closeBox();
		});
		for (const auto &button : { subscribe, plain, cancel }) {
			button->setFullRadius(true);
		}
		Ui::AddSkip(
			box->verticalLayout(),
			st::defaultVerticalListSkip / 2);
	}));
}

bool CanAuthorRichMessages(not_null<Main::Session*> session) {
	return RichMessagePostingMode(session) != RichMessagePosting::Disabled;
}

bool CheckRichMessagesPremium(
		not_null<Window::SessionController*> controller) {
	if (!CanAuthorRichMessages(&controller->session())) {
		return false;
	}
	if (CanUseRichMessages(&controller->session())) {
		return true;
	}
	ShowRichMessagesPremiumToast(controller->uiShow());
	return false;
}

void ShowComposeBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Api::SendAction action,
		SendMenu::Details sendMenuDetails) {
	ArticleSession::ShowCompose(
		&controller->session(),
		peer,
		std::move(action),
		std::move(sendMenuDetails),
		base::make_weak(controller));
}

void ShowEditBox(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	if (!CanAuthorRichMessages(&controller->session())) {
		return;
	}
	if (!CanUseRichMessages(&controller->session())) {
		ShowRichMessagesPremiumToast(controller->uiShow());
		return;
	}
	const auto weak = base::make_weak(controller);
	const auto itemId = item->fullId();
	Core::App().iv().resolveRichMessage(controller, item, [=](
			std::shared_ptr<const RichPage> page) {
		const auto strong = weak.get();
		const auto current = strong
			? strong->session().data().message(itemId)
			: nullptr;
		if (!strong || !current) {
			return;
		}
		if (!page || !CanEditRichPage(page)) {
			strong->showToast(tr::lng_edit_error(tr::now));
			return;
		}
		ArticleSession::ShowEdit(
			not_null{ current },
			std::move(page),
			base::make_weak(strong));
	});
}

void ShowEditFromFieldBox(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		Api::SendAction action) {
	ArticleSession::ShowEditFromField(
		item,
		std::move(action),
		base::make_weak(controller));
}

bool IsComposeBoxOpen(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		PeerId monoforumPeerId) {
	const auto entry = LookupComposeThreadEntry(
		ComposeKey(session, peerId, topicRootId, monoforumPeerId));
	return entry && !entry->articleSession.expired();
}

bool SaveOpenComposeDraftThenEdit(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		PeerId monoforumPeerId,
		Fn<void()> onSaved) {
	return ArticleSession::SaveOpenComposeDraftThen(
		session,
		peerId,
		topicRootId,
		monoforumPeerId,
		std::move(onSaved));
}

bool RequestCloseOpenEditWindowThenCompose(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		Fn<void()> onClosed) {
	return ArticleSession::RequestCloseOpenEditWindowThen(
		session,
		peer,
		std::move(onClosed));
}

rpl::producer<bool> FieldVisibleValue(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		PeerId monoforumPeerId) {
	return ComposeThreadEntryFor(
		ComposeKey(session, peerId, topicRootId, monoforumPeerId)
	).fieldVisible.value();
}

void RegisterThreadFieldBridge(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		PeerId monoforumPeerId,
		ThreadFieldDraftReader readDraft,
		ThreadFieldDraftSaver saveDraft,
		ThreadFieldMigratedAway migratedAway) {
	auto &entry = ComposeThreadEntryFor(
		ComposeKey(session, peerId, topicRootId, monoforumPeerId));
	entry.readDraft = std::move(readDraft);
	entry.saveDraft = std::move(saveDraft);
	entry.migratedAway = std::move(migratedAway);
}

void UnregisterThreadFieldBridge(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		PeerId monoforumPeerId) {
	auto &entry = ComposeThreadEntryFor(
		ComposeKey(session, peerId, topicRootId, monoforumPeerId));
	entry.readDraft = nullptr;
	entry.saveDraft = nullptr;
	entry.migratedAway = nullptr;
}

void CloseAllWindows() {
	ArticleSession::CloseAll();
}

} // namespace Iv::Editor
