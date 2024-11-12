/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_downloads.h"

#include "data/data_document.h"
#include "data/data_peer_id.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/file_download_web.h"
#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "styles/style_chat.h"

#include <QtCore/QBuffer>
#include <QtCore/QDataStream>

namespace InlineBots {
namespace {

constexpr auto kDownloadsVersion = 1;
constexpr auto kMaxDownloadsBots = 4096;
constexpr auto kMaxDownloadsPerBot = 16384;

} // namespace

Downloads::Downloads(not_null<Main::Session*> session)
: _session(session) {
}

Downloads::~Downloads() = default;

DownloadId Downloads::start(StartArgs &&args) {
	read();

	const auto botId = args.bot->id;
	const auto id = ++_autoIncrementId;
	auto &list = _lists[botId].list;
	list.push_back({
		.id = id,
		.url = std::move(args.url),
		.path = std::move(args.path),
	});
	auto &entry = list.back();
	auto &loader = _loaders[id];
	loader.botId = botId;
	loader.loader = std::make_unique<webFileLoader>(
		_session,
		entry.url,
		entry.path,
		WebRequestType::FullLoad);
	loader.loader->updates(
	) | rpl::start_with_next_error_done([=] {
		progress(botId, id);
	}, [=](FileLoader::Error) {
		fail(botId, id);
	}, [=] {
		done(botId, id);
	}, loader.loader->lifetime());
	loader.loader->start();

	return id;
}

void Downloads::progress(PeerId botId, DownloadId id) {
	const auto i = _loaders.find(id);
	if (i == end(_loaders)) {
		return;
	}
	const auto &loader = i->second.loader;
	const auto total = loader->fullSize();
	const auto ready = loader->currentOffset();

	auto &list = _lists[botId].list;
	const auto j = ranges::find(
		list,
		id,
		&DownloadsEntry::id);
	Assert(j != end(list));

	if (total < 0
		|| ready > total
		|| (j->total && j->total != total)) {
		fail(botId, id);
		return;
	} else if (ready > total) {
		fail(botId, id);
		return;
	} else if (ready == total) {
		// Wait for 'done' signal.
		return;
	}
	applyProgress(botId, id, total, ready);
}

void Downloads::fail(PeerId botId, DownloadId id) {
	const auto i = _loaders.find(id);
	if (i == end(_loaders)) {
		return;
	}
	auto loader = std::move(i->second.loader);
	_loaders.erase(i);
	loader = nullptr;

	auto &list = _lists[botId].list;
	const auto k = ranges::find(
		list,
		id,
		&DownloadsEntry::id);
	Assert(k != end(list));
	k->ready = -1;
}

void Downloads::done(PeerId botId, DownloadId id) {
	const auto i = _loaders.find(id);
	if (i == end(_loaders)) {
		return;
	}
	auto &list = _lists[botId].list;
	const auto j = ranges::find(
		list,
		id,
		&DownloadsEntry::id);
	Assert(j != end(list));

	const auto total = i->second.loader->fullSize();
	if (total <= 0 || (j->total && j->total != total)) {
		fail(botId, id);
		return;
	}
	_loaders.erase(i);

	applyProgress(botId, id, total, total);
}

void Downloads::applyProgress(
		PeerId botId,
		DownloadId id,
		int64 total,
		int64 ready) {
	Expects(total > 0);
	Expects(ready >= 0 && ready <= total);

	auto &list = _lists[botId].list;
	const auto j = ranges::find(
		list,
		id,
		&DownloadsEntry::id);
	Assert(j != end(list));

	auto &progress = _progressView[botId];
	auto current = progress.current();
	if (!j->total) {
		j->total = total;
		current.total += total;
	}
	if (j->ready != ready) {
		const auto delta = ready - j->ready;
		j->ready = ready;
		current.ready += delta;
	}

	if (total == ready) {
		write();
	}

	progress = current;
	if (current.ready == current.total) {
		progress = DownloadsProgress();
	}
}

void Downloads::cancel(DownloadId id) {
	const auto i = _loaders.find(id);
	if (i == end(_loaders)) {
		return;
	}
	const auto botId = i->second.botId;
	fail(botId, id);

	auto &list = _lists[botId].list;
	list.erase(
		ranges::remove(list, id, &DownloadsEntry::id),
		end(list));

	auto &progress = _progressView[botId];
	progress.force_assign(progress.current());
}

[[nodiscard]] auto Downloads::downloadsProgress(not_null<UserData*> bot)
->rpl::producer<DownloadsProgress> {
	read();

	return _progressView[bot->id].value();
}

void Downloads::read() {
	auto bytes = _session->local().readInlineBotsDownloads();
	if (bytes.isEmpty()) {
		return;
	}

	Assert(_lists.empty());

	auto stream = QDataStream(&bytes, QIODevice::ReadOnly);
	stream.setVersion(QDataStream::Qt_5_1);

	quint32 version = 0, count = 0;
	stream >> version;
	if (version != kDownloadsVersion) {
		return;
	}
	stream >> count;
	if (!count || count > kMaxDownloadsBots) {
		return;
	}
	auto lists = base::flat_map<PeerId, List>();
	for (auto i = 0; i != count; ++i) {
		quint64 rawBotId = 0;
		quint32 count = 0;
		stream >> rawBotId >> count;
		const auto botId = DeserializePeerId(rawBotId);
		if (!botId
			|| !peerIsUser(botId)
			|| count > kMaxDownloadsPerBot
			|| lists.contains(botId)) {
			return;
		}
		auto &list = lists[botId];
		list.list.reserve(count);
		for (auto j = 0; j != count; ++j) {
			auto entry = DownloadsEntry();
			stream >> entry.url >> entry.path >> entry.total;
			entry.ready = entry.total;
			entry.id = ++_autoIncrementId;
			list.list.push_back(std::move(entry));
		}
	}
	_lists = std::move(lists);
}

void Downloads::write() {
	auto size = sizeof(quint32) // version
		+ sizeof(quint32); // lists count

	for (const auto &[botId, list] : _lists) {
		size += sizeof(quint64) // botId
			+ sizeof(quint32); // list count
		for (const auto &entry : list.list) {
			if (entry.total > 0 && entry.ready == entry.total) {
				size += Serialize::stringSize(entry.url)
					+ Serialize::stringSize(entry.path)
					+ sizeof(quint64); // total
			}
		}
	}

	auto bytes = QByteArray();
	bytes.reserve(size);
	auto buffer = QBuffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	auto stream = QDataStream(&buffer);
	stream.setVersion(QDataStream::Qt_5_1);

	stream << quint32(kDownloadsVersion) << quint32(_lists.size());

	for (const auto &[botId, list] : _lists) {
		stream << SerializePeerId(botId) << quint32(list.list.size());
		for (const auto &entry : list.list) {
			if (entry.total > 0 && entry.ready == entry.total) {
				stream << entry.url << entry.path << entry.total;
			}
		}
	}
	buffer.close();

	_session->local().writeInlineBotsDownloads(bytes);
}

void DownloadFileBox(not_null<Ui::GenericBox*> box, DownloadBoxArgs args) {
	Expects(!args.name.isEmpty());

	box->setTitle(tr::lng_bot_download_file());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_bot_download_file_sure(
			lt_bot,
			rpl::single(Ui::Text::Bold(args.bot)),
			Ui::Text::RichLangValue),
		st::botDownloadLabel));
	//box->addRow(MakeFilePreview(box, args));
	const auto done = std::move(args.done);
	const auto name = args.name;
	const auto session = args.session;
	box->addButton(tr::lng_bot_download_file_button(), [=] {
		const auto path = FileNameForSave(
			session,
			tr::lng_save_file(tr::now),
			QString(),
			u"file"_q,
			name,
			false,
			QDir());
		if (!path.isEmpty()) {
			box->closeBox();
			done(path);
		}
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
		done(QString());
	});
}

} // namespace InlineBots
