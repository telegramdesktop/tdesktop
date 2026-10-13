/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_document.h"

#include "base/options.h"
#include "core/file_location.h"
#include "core/update_channel.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_groups.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/file_download.h"

namespace Tgw {
namespace {

constexpr auto kReadChunk = 64 * 1024;
constexpr auto kRetainedSnapshots = 8;

#ifdef _DEBUG
constexpr auto kDefaultEnabled = true;
#else
constexpr auto kDefaultEnabled = Core::BuildIsCanary;
#endif // _DEBUG

enum class Verdict : uchar {
	Unknown,
	Checking,
	Valid,
	Invalid,
};

struct Source {
	QString path;
	QDateTime modified;
	int64 size = 0;
	QByteArray bytes;
	std::weak_ptr<Data::DocumentMedia> media;
};

struct Content {
	Source source;
	Core::FileLocation location;
};

struct Entry {
	base::flat_set<FullMsgId> pending;
	Source source;
	int generation = 0;
	Verdict verdict = Verdict::Unknown;
	bool unavailable = false;
};

struct State {
	base::flat_map<not_null<DocumentData*>, Entry> entries;
	std::vector<not_null<DocumentData*>> retained;
	rpl::lifetime lifetime;
};

struct Utf8State {
	int needed = 0;
	int lower = 0x80;
	int upper = 0xBF;
};

using StateMap = base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<State>>;

base::options::toggle OptionWidgetMessages({
	.id = kOptionWidgetMessages,
	.name = "HTML widget messages",
	.description = "Recognize .tgw files in messages as HTML widgets.",
	.defaultValue = kDefaultEnabled,
	.restartRequired = true,
});

auto EnabledAtStartup = false;
auto WidgetViewsDisabled = false;

[[nodiscard]] QString WidgetMimeType() {
	return u"application/x-tgwidget"_q;
}

[[nodiscard]] bool FeedUtf8(
		Utf8State &state,
		const char *data,
		int64 size) {
	for (auto i = int64(); i != size; ++i) {
		const auto byte = int(uchar(data[i]));
		if (state.needed) {
			if (byte < state.lower || byte > state.upper) {
				return false;
			}
			state.lower = 0x80;
			state.upper = 0xBF;
			--state.needed;
		} else if (byte >= 0xC2 && byte <= 0xDF) {
			state.needed = 1;
		} else if (byte >= 0xE0 && byte <= 0xEF) {
			state.needed = 2;
			state.lower = (byte == 0xE0) ? 0xA0 : 0x80;
			state.upper = (byte == 0xED) ? 0x9F : 0xBF;
		} else if (byte >= 0xF0 && byte <= 0xF4) {
			state.needed = 3;
			state.lower = (byte == 0xF0) ? 0x90 : 0x80;
			state.upper = (byte == 0xF4) ? 0x8F : 0xBF;
		} else if (byte >= 0x80) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] Verdict CheckBytes(const QByteArray &bytes) {
	auto state = Utf8State();
	return (!bytes.isEmpty()
		&& FeedUtf8(state, bytes.constData(), bytes.size())
		&& !state.needed)
		? Verdict::Valid
		: Verdict::Invalid;
}

[[nodiscard]] Verdict CheckFile(const QString &path, int64 size) {
	if (!size) {
		return Verdict::Invalid;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() != size) {
		return Verdict::Unknown;
	}
	auto state = Utf8State();
	auto buffer = QByteArray(kReadChunk, Qt::Uninitialized);
	auto left = size;
	while (left > 0) {
		const auto read = file.read(
			buffer.data(),
			std::min(left, int64(kReadChunk)));
		if (read <= 0) {
			return Verdict::Unknown;
		} else if (!FeedUtf8(state, buffer.constData(), read)) {
			return Verdict::Invalid;
		}
		left -= read;
	}
	return state.needed ? Verdict::Invalid : Verdict::Valid;
}

[[nodiscard]] QByteArray ReadLocation(const Core::FileLocation &location) {
	if (!location.check()) {
		return QByteArray();
	}
	auto file = QFile(location.name());
	if (!file.open(QIODevice::ReadOnly) || file.size() != location.size) {
		return QByteArray();
	}
	auto result = file.read(location.size);
	file.close();
	return location.check() ? result : QByteArray();
}

[[nodiscard]] QByteArray ReadValidated(
		const QByteArray &bytes,
		const std::shared_ptr<const Core::FileLocation> &access,
		int64 size) {
	if (size <= 0 || size > Storage::kMaxFileInMemory) {
		return QByteArray();
	}
	auto result = access ? ReadLocation(*access) : bytes;
	const auto valid = (result.size() == size)
		&& (CheckBytes(result) == Verdict::Valid);
	return valid ? result : QByteArray();
}

[[nodiscard]] bool SameSource(const Source &a, const Source &b) {
	if (a.bytes.isEmpty() || b.bytes.isEmpty()) {
		return a.bytes.isEmpty()
			&& b.bytes.isEmpty()
			&& (a.path == b.path)
			&& (a.modified == b.modified)
			&& (a.size == b.size);
	}
	// WHY: the entry owns its snapshot, so that storage cannot be freed and
	// reused by another body while compared; any replacement, even of equal
	// length, lives elsewhere, and the owner check catches media recreation.
	return (a.bytes.constData() == b.bytes.constData())
		&& (a.bytes.size() == b.bytes.size())
		&& !a.media.owner_before(b.media)
		&& !b.media.owner_before(a.media);
}

[[nodiscard]] bool SnapshotCurrent(const Source &source) {
	const auto media = source.media.lock();
	if (!media) {
		return false;
	}
	const auto bytes = media->bytes();
	return (bytes.constData() == source.bytes.constData())
		&& (bytes.size() == source.bytes.size());
}

[[nodiscard]] bool Usable(const Entry &entry) {
	return (entry.verdict == Verdict::Valid) && !entry.unavailable;
}

void Release(Entry &entry) {
	entry.source = Source();
	entry.verdict = Verdict::Unknown;
}

[[nodiscard]] bool LeavesRetained(
		not_null<State*> state,
		not_null<DocumentData*> document) {
	const auto i = state->entries.find(document);
	if (i == end(state->entries)) {
		return true;
	}
	auto &entry = i->second;
	if (entry.source.bytes.isEmpty()
		|| entry.verdict == Verdict::Checking) {
		return true;
	} else if (SnapshotCurrent(entry.source)) {
		return false;
	}
	Release(entry);
	return true;
}

void Sweep(not_null<State*> state) {
	const auto leaves = [&](not_null<DocumentData*> document) {
		return LeavesRetained(state, document);
	};
	auto &retained = state->retained;
	retained.erase(ranges::remove_if(retained, leaves), end(retained));
}

void Retain(not_null<State*> state, not_null<DocumentData*> document) {
	auto &retained = state->retained;
	retained.erase(ranges::remove(retained, document), end(retained));
	Sweep(state);
	retained.push_back(document);
	while (int(retained.size()) > kRetainedSnapshots) {
		const auto i = state->entries.find(retained.front());
		if (i != end(state->entries)) {
			Release(i->second);
		}
		retained.erase(begin(retained));
	}
}

[[nodiscard]] StateMap &States() {
	static auto result = StateMap();
	return result;
}

[[nodiscard]] State *FindState(not_null<Main::Session*> session) {
	auto &states = States();
	const auto i = states.find(session);
	return (i != end(states)) ? i->second.get() : nullptr;
}

[[nodiscard]] Entry *FindEntry(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document) {
	const auto state = FindState(session);
	if (!state) {
		return nullptr;
	}
	auto &entries = state->entries;
	const auto i = entries.find(document);
	return (i != end(entries)) ? &i->second : nullptr;
}

[[nodiscard]] std::optional<Content> CurrentContent(
		not_null<DocumentData*> document) {
	if (document->cancelled()) {
		return std::nullopt;
	}
	const auto media = document->activeMediaView();
	auto bytes = media ? media->bytes() : QByteArray();
	if (!bytes.isEmpty()) {
		const auto size = int64(bytes.size());
		return Content{
			.source = {
				.size = size,
				.bytes = std::move(bytes),
				.media = media,
			},
		};
	} else if (document->filepath(true).isEmpty()) {
		return std::nullopt;
	}
	const auto &location = document->location();
	return Content{
		.source = {
			.path = location.name(),
			.modified = location.modified,
			.size = location.size,
		},
		.location = location,
	};
}

[[nodiscard]] std::shared_ptr<const Core::FileLocation> EnableAccess(
		const Core::FileLocation &location) {
	if (!location.accessEnable()) {
		return nullptr;
	}
	return std::shared_ptr<const Core::FileLocation>(
		new Core::FileLocation(location),
		[](const Core::FileLocation *copy) {
			copy->accessDisable();
			delete copy;
		});
}

void RefreshItems(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document,
		base::flat_set<FullMsgId> ids) {
	const auto owner = &session->data();
	for (const auto &id : ids) {
		const auto item = owner->message(id);
		const auto media = item ? item->media() : nullptr;
		if (media && media->document() == document) {
			owner->requestItemViewRefresh(item);
		}
	}
}

void Apply(
	not_null<Main::Session*> session,
	not_null<DocumentData*> document,
	int generation,
	Verdict verdict);

void StartCheck(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document,
		Entry &entry,
		Content content) {
	entry.source = std::move(content.source);
	entry.verdict = Verdict::Checking;
	entry.unavailable = false;
	const auto generation = ++entry.generation;
	auto access = std::shared_ptr<const Core::FileLocation>();
	if (entry.source.bytes.isEmpty()) {
		access = EnableAccess(content.location);
		if (!access) {
			entry.verdict = Verdict::Unknown;
			return;
		}
	}
	const auto bytes = entry.source.bytes;
	const auto size = entry.source.size;
	const auto weak = base::make_weak(session);
	crl::async([=] {
		const auto verdict = access
			? CheckFile(access->name(), size)
			: CheckBytes(bytes);
		crl::on_main(weak, [=] {
			Apply(session, document, generation, verdict);
		});
	});
}

void Apply(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document,
		int generation,
		Verdict verdict) {
	const auto state = FindState(session);
	if (!state) {
		return;
	}
	const auto i = state->entries.find(document);
	if (i == end(state->entries) || i->second.generation != generation) {
		return;
	}
	auto &entry = i->second;
	auto content = CurrentContent(document);
	if (content && SameSource(content->source, entry.source)) {
		entry.verdict = verdict;
		if (!entry.source.bytes.isEmpty()) {
			Retain(state, document);
		}
		if (verdict == Verdict::Valid) {
			RefreshItems(session, document, base::take(entry.pending));
		}
		return;
	}
	Release(entry);
	if (content && !entry.pending.empty()) {
		StartCheck(session, document, entry, std::move(*content));
	}
}

[[nodiscard]] bool CheckCurrent(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document,
		Entry &entry) {
	auto content = CurrentContent(document);
	if (!content) {
		return false;
	} else if (entry.verdict == Verdict::Unknown
		|| !SameSource(content->source, entry.source)) {
		StartCheck(session, document, entry, std::move(*content));
		return false;
	}
	return Usable(entry);
}

void PostRefresh(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document) {
	crl::on_main(session, [=] {
		const auto entry = FindEntry(session, document);
		if (entry && Usable(*entry)) {
			RefreshItems(session, document, base::take(entry->pending));
		}
	});
}

void Subscribe(not_null<Main::Session*> session, not_null<State*> state) {
	const auto owner = &session->data();
	owner->documentLoadProgress(
	) | rpl::filter([](not_null<DocumentData*> document) {
		return !document->loading();
	}) | rpl::on_next([=](not_null<DocumentData*> document) {
		const auto i = state->entries.find(document);
		if (i == end(state->entries)) {
			return;
		}
		Sweep(state);
		if (CheckCurrent(session, document, i->second)
			&& !i->second.pending.empty()) {
			PostRefresh(session, document);
		}
	}, state->lifetime);

	owner->itemIdChanged(
	) | rpl::on_next([=](Data::Session::IdChange change) {
		const auto was = FullMsgId(change.newId.peer, change.oldId);
		for (auto &[document, entry] : state->entries) {
			if (entry.pending.remove(was)) {
				entry.pending.emplace(change.newId);
			}
		}
	}, state->lifetime);
}

[[nodiscard]] not_null<State*> ResolveState(
		not_null<Main::Session*> session) {
	if (const auto state = FindState(session)) {
		return state;
	}
	const auto state = States().emplace(
		session,
		std::make_unique<State>()).first->second.get();
	session->lifetime().add([=] {
		States().remove(session);
	});
	Subscribe(session, state);
	return state;
}

} // namespace

const char kOptionWidgetMessages[] = "html-widget-messages";

void InitEnabled() {
	EnabledAtStartup = OptionWidgetMessages.value();
}

bool Enabled() {
	return EnabledAtStartup;
}

bool IsWidgetFileName(const QString &name) {
	return name.endsWith(u".tgw"_q, Qt::CaseInsensitive);
}

bool IsWidgetDocument(not_null<const DocumentData*> document) {
	return IsWidgetFileName(document->filename())
		|| document->hasMimeType(WidgetMimeType());
}

bool ShouldAutoDownload(not_null<const DocumentData*> document) {
	return Enabled()
		&& IsWidgetDocument(document)
		&& (document->size >= 0)
		&& (document->size <= Storage::kMaxStickerBytesSize);
}

QString UploadMimeType(const QString &fileName, const QString &mimeType) {
	return (Enabled() && IsWidgetFileName(fileName))
		? WidgetMimeType()
		: mimeType;
}

bool IsWidgetViewCandidate(
		not_null<const DocumentData*> document,
		not_null<const HistoryItem*> item) {
	return Enabled()
		&& IsWidgetDocument(document)
		&& !item->history()->owner().groups().find(item);
}

bool ShowWidgetView(
		not_null<DocumentData*> document,
		not_null<const HistoryItem*> item) {
	if (WidgetViewsDisabled) {
		return false;
	}
	const auto session = &document->session();
	const auto state = ResolveState(session);
	Sweep(state);
	auto &entry = state->entries[document];
	if (CheckCurrent(session, document, entry)) {
		return true;
	}
	entry.pending.emplace(item->fullId());
	return false;
}

bool HasValidatedContent(not_null<DocumentData*> document) {
	const auto entry = FindEntry(&document->session(), document);
	if (WidgetViewsDisabled || !entry || !Usable(*entry)) {
		return false;
	}
	const auto content = CurrentContent(document);
	return content && SameSource(content->source, entry->source);
}

std::optional<ContentReader> PrepareContentReader(
		not_null<DocumentData*> document) {
	const auto entry = FindEntry(&document->session(), document);
	if (WidgetViewsDisabled || !entry || !Usable(*entry)) {
		return std::nullopt;
	}
	const auto content = CurrentContent(document);
	if (!content || !SameSource(content->source, entry->source)) {
		return std::nullopt;
	}
	const auto bytes = entry->source.bytes;
	const auto access = bytes.isEmpty()
		? EnableAccess(content->location)
		: nullptr;
	const auto size = entry->source.size;
	return ContentReader{
		.read = [=] { return ReadValidated(bytes, access, size); },
		.generation = entry->generation,
	};
}

void MarkContentUnavailable(
		not_null<DocumentData*> document,
		int generation,
		FullMsgId itemId) {
	const auto session = &document->session();
	const auto entry = FindEntry(session, document);
	if (entry && entry->generation == generation) {
		entry->unavailable = true;
	}
	crl::on_main(session, [=] {
		RefreshItems(session, document, { itemId });
	});
}

void DisableWidgetViews(const std::vector<WidgetItemId> &items) {
	WidgetViewsDisabled = true;
	for (const auto &widget : items) {
		const auto session = widget.session;
		const auto id = widget.id;
		crl::on_main(session, [=] {
			const auto owner = &session->data();
			if (const auto item = owner->message(id)) {
				owner->requestItemViewRefresh(item);
			}
		});
	}
}

} // namespace Tgw
