// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// Mokagram: защищённые чаты без запретов и пересылка копией.
#include "mokagram/forward/mokagram_copy_forward.h"

#include "mokagram/lang/mokagram_lang.h"
#include "mokagram/settings/mokagram_settings.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/random.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/image/image.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "styles/style_boxes.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>

namespace Mokagram::Forward {
namespace {

constexpr auto kOldTempFiles = 3600;

int RawDepth = 0;

[[nodiscard]] QString U(const char *text) {
	return QString::fromUtf8(Mokagram::Lang::Tr(text));
}

[[nodiscard]] QString TempDir() {
	return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
		+ u"/mokagram-copy/"_q;
}

// Файлы от прошлых пересылок: к этому времени их давно отправили.
void CleanTemp() {
	const auto dir = QDir(TempDir());
	const auto now = QDateTime::currentDateTime();
	for (const auto &info : dir.entryInfoList(QDir::Files)) {
		if (info.lastModified().secsTo(now) > kOldTempFiles) {
			QFile::remove(info.absoluteFilePath());
		}
	}
}

[[nodiscard]] QString TempPath(const QString &name) {
	QDir().mkpath(TempDir());
	// Копии защищённых файлов: в общем /tmp их видит только владелец.
	QFile::setPermissions(
		TempDir(),
		QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
	const auto clean = QString(name).replace('/', '_').replace('\\', '_');
	return TempDir()
		+ QString::number(base::RandomValue<uint32>(), 16)
		+ '_'
		+ (clean.isEmpty() ? u"file"_q : clean);
}

[[nodiscard]] TextWithTags Caption(not_null<HistoryItem*> item) {
	const auto &text = item->originalText();
	return {
		text.text,
		TextUtilities::ConvertEntitiesToTextTags(text.entities),
	};
}

enum class Kind {
	Skip,
	Text,
	File,
	Voice,
	Sticker,
};

struct Prepared {
	Kind kind = Kind::Skip;
	Ui::PreparedList list;
	QByteArray bytes;
	bool media = false; // фото или видео, а не файл
	bool waiting = false;
};

struct Job {
	not_null<Main::Session*> session;
	std::vector<FullMsgId> items;
	Api::SendAction action;
	bool dropCaptions = false;
	int index = 0;
	int sent = 0;
	std::vector<std::shared_ptr<Data::PhotoMedia>> photos;
	std::vector<std::shared_ptr<Data::DocumentMedia>> documents;
	base::flat_map<not_null<DocumentData*>, QString> saving;
	rpl::lifetime waiting;
};

void Process(std::shared_ptr<Job> job);

[[nodiscard]] Prepared PreparePhoto(
		const std::shared_ptr<Job> &job,
		not_null<HistoryItem*> item,
		not_null<PhotoData*> photo) {
	auto view = photo->createMediaView();
	job->photos.push_back(view);
	view->wanted(Data::PhotoSize::Large, item->fullId());
	const auto image = view->image(Data::PhotoSize::Large);
	if (!image) {
		return { .waiting = true };
	}
	auto list = Storage::PrepareMediaFromImage(
		QImage(image->original()),
		view->imageBytes(Data::PhotoSize::Large),
		st::sendMediaPreviewSize);
	return { .kind = Kind::File, .list = std::move(list), .media = true };
}

[[nodiscard]] Prepared PrepareDocument(
		const std::shared_ptr<Job> &job,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	if (document->sticker()) {
		return { .kind = Kind::Sticker };
	}
	auto view = document->createMediaView();
	job->documents.push_back(view);
	const auto voice = document->isVoiceMessage()
		|| document->isVideoMessage();
	auto path = document->filepath(true);
	if (path.isEmpty() && !view->bytes().isEmpty()) {
		path = TempPath(document->filename());
		auto file = QFile(path);
		if (!file.open(QIODevice::WriteOnly)) {
			return {};
		}
		file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
		file.write(view->bytes());
	}
	if (path.isEmpty()) {
		auto &target = job->saving[document];
		if (target.isEmpty()) {
			target = TempPath(document->filename());
			document->save(item->fullId(), target);
		}
		const auto info = QFileInfo(target);
		if (!info.isFile() || info.size() != document->size) {
			return { .waiting = true };
		}
		QFile::setPermissions(
			target,
			QFileDevice::ReadOwner | QFileDevice::WriteOwner);
		path = target;
	}
	if (voice) {
		auto file = QFile(path);
		if (!file.open(QIODevice::ReadOnly)) {
			return {};
		}
		return { .kind = Kind::Voice, .bytes = file.readAll() };
	}
	auto list = Storage::PrepareMediaList(
		QStringList{ path },
		st::sendMediaPreviewSize,
		job->session->premium());
	if (list.error != Ui::PreparedList::Error::None || list.files.empty()) {
		return {};
	}
	for (auto &file : list.files) {
		file.displayName = document->filename();
	}
	const auto type = list.files.front().type;
	const auto media = (type == Ui::PreparedFile::Type::Video)
		&& (document->isVideoFile() || document->isAnimation());
	return { .kind = Kind::File, .list = std::move(list), .media = media };
}

[[nodiscard]] Prepared Prepare(
		const std::shared_ptr<Job> &job,
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media || media->webpage()) {
		return { .kind = item->originalText().text.isEmpty()
			? Kind::Skip
			: Kind::Text };
	} else if (const auto photo = media->photo()) {
		return PreparePhoto(job, item, photo);
	} else if (const auto document = media->document()) {
		return PrepareDocument(job, item, document);
	}
	// Опросы, геоточки и прочее не перезаливаются: шлём хотя бы текст.
	return { .kind = item->originalText().text.isEmpty()
		? Kind::Skip
		: Kind::Text };
}

void WaitForDownloads(std::shared_ptr<Job> job) {
	job->waiting.destroy();
	job->session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		job->waiting.destroy();
		Process(job);
	}, job->waiting);
}

void Send(
		const std::shared_ptr<Job> &job,
		not_null<HistoryItem*> item,
		Prepared &&prepared) {
	auto action = job->action;
	action.clearDraft = false;
	auto caption = job->dropCaptions ? TextWithTags() : Caption(item);
	switch (prepared.kind) {
	case Kind::Skip:
		return;
	case Kind::Text: {
		auto message = Api::MessageToSend(action);
		message.textWithTags = std::move(caption);
		job->session->api().sendMessage(std::move(message));
	} break;
	case Kind::Sticker:
		Api::SendExistingDocument(
			Api::MessageToSend(action),
			item->media()->document());
		break;
	case Kind::Voice: {
		const auto document = item->media()->document();
		const auto voice = document->voice();
		job->session->api().sendVoiceMessage(
			std::move(prepared.bytes),
			voice ? voice->waveform : VoiceWaveform(),
			document->duration(),
			document->isVideoMessage(),
			action,
			std::move(caption));
	} break;
	case Kind::File:
		for (auto &file : prepared.list.files) {
			file.caption = std::move(caption);
			caption = TextWithTags();
		}
		job->session->api().sendFiles(
			std::move(prepared.list),
			prepared.media ? SendMediaType::Photo : SendMediaType::File,
			nullptr,
			action);
		break;
	}
	++job->sent;
}

void SendAlbum(
		const std::shared_ptr<Job> &job,
		const std::vector<not_null<HistoryItem*>> &items,
		std::vector<Prepared> &&prepared) {
	auto action = job->action;
	action.clearDraft = false;
	auto list = Ui::PreparedList();
	auto media = true;
	for (auto i = 0; i != int(items.size()); ++i) {
		auto &one = prepared[i];
		media = media && one.media;
		for (auto &file : one.list.files) {
			if (!job->dropCaptions) {
				file.caption = Caption(items[i]);
			}
			list.files.push_back(std::move(file));
		}
	}
	if (list.files.empty()) {
		return;
	}
	job->session->api().sendFiles(
		std::move(list),
		media ? SendMediaType::Photo : SendMediaType::File,
		std::make_shared<SendingAlbum>(),
		action);
	job->sent += int(items.size());
}

void Process(std::shared_ptr<Job> job) {
	auto &items = job->items;
	while (job->index < int(items.size())) {
		const auto first = job->session->data().message(items[job->index]);
		if (!first) {
			++job->index;
			continue;
		}
		auto batch = std::vector<not_null<HistoryItem*>>{ first };
		if (const auto group = first->groupId()) {
			for (auto i = job->index + 1; i < int(items.size()); ++i) {
				const auto item = job->session->data().message(items[i]);
				if (!item || item->groupId() != group) {
					break;
				}
				batch.push_back(item);
			}
		}
		auto prepared = std::vector<Prepared>();
		for (const auto item : batch) {
			prepared.push_back(Prepare(job, item));
			if (prepared.back().waiting) {
				WaitForDownloads(job);
				return;
			}
		}
		const auto files = ranges::count(
			prepared,
			Kind::File,
			&Prepared::kind);
		if (batch.size() > 1 && files == int(batch.size())) {
			SendAlbum(job, batch, std::move(prepared));
		} else {
			for (auto i = 0; i != int(batch.size()); ++i) {
				Send(job, batch[i], std::move(prepared[i]));
			}
		}
		job->index += int(batch.size());
	}
	job->waiting.destroy();
	Ui::Toast::Show(U("Переслано копией: %1").arg(job->sent));
}

} // namespace

bool Bypass() {
	return !RawDepth && Settings::Bool(u"chats.bypass_protection"_q);
}

RawScope::RawScope() {
	++RawDepth;
}

RawScope::~RawScope() {
	--RawDepth;
}

bool NeedsCopy(not_null<HistoryItem*> item) {
	const auto raw = RawScope();
	const auto media = item->media();
	return item->forbidsForward()
		|| !item->history()->peer->allowsForwarding()
		|| (media && media->ttlSeconds());
}

void CopyForward(
		not_null<Main::Session*> session,
		std::vector<not_null<HistoryItem*>> items,
		Api::SendAction action,
		bool dropCaptions) {
	if (items.empty()) {
		return;
	}
	CleanTemp();
	ranges::sort(items, [](not_null<HistoryItem*> a, not_null<HistoryItem*> b) {
		return a->date() < b->date()
			|| (a->date() == b->date() && a->id < b->id);
	});
	auto ids = std::vector<FullMsgId>();
	ids.reserve(items.size());
	for (const auto item : items) {
		ids.push_back(item->fullId());
	}
	Ui::Toast::Show(U("Чат защищён: пересылаю копией…"));
	Process(std::make_shared<Job>(Job{
		.session = session,
		.items = std::move(ids),
		.action = std::move(action),
		.dropCaptions = dropCaptions,
	}));
}

void RegisterSettings() {
	Settings::Register({
		.key = u"chats.bypass_protection"_q,
		.section = U("Защищённые чаты"),
		.title = U("Снять запреты в защищённых чатах"),
		.description = U("Можно делать скриншоты, копировать текст, сохранять фото, видео и голосовые, пересылать. Пересылка идёт копией: файлы заливаются заново."),
		.kind = Settings::Kind::Bool,
		.defaultValue = false,
		.category = u"chats"_q,
		.sectionIcon = u"copy"_q,
		.icon = u"copy"_q,
	});
}

} // namespace Mokagram::Forward
