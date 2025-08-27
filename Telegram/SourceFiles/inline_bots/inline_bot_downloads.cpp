/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_downloads.h"

#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_peer_id.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/file_download_web.h"
#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "ui/chat/attach/attach_bot_downloads.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "styles/style_chat.h"

#include <QtCore/QBuffer>
#include <QtCore/QDataStream>

#include "base/call_delayed.h"

namespace InlineBots {
namespace {

constexpr auto kDownloadsVersion = 1;
constexpr auto kMaxDownloadsBots = 4096;
constexpr auto kMaxDownloadsPerBot = 16384;

} // namespace

Downloads::Downloads(not_null<Main::Session*> session)
: _session(session) {
}

Downloads::~Downloads() {
	base::take(_loaders);
	base::take(_lists);
}

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
	load(botId, id, list.back());
	return id;
}

void Downloads::load(
		PeerId botId,
		DownloadId id,
		DownloadsEntry &entry) {
	entry.loading = 1;
	entry.failed = 0;

	auto &loader = _loaders[id];
	Assert(!loader.loader);
	loader.botId = botId;
	loader.loader = std::make_unique<webFileLoader>(
		_session,
		entry.url,
		entry.path,
		WebRequestType::FullLoad);

	applyProgress(botId, id, 0, 0);

	loader.loader->updates(
	) | rpl::start_with_next_error_done([=] {
		progress(botId, id);
	}, [=](FileLoader::Error) {
		fail(botId, id);
	}, [=] {
		done(botId, id);
	}, loader.loader->lifetime());

	loader.loader->start();
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

	if (total < 0 || ready > total) {
		fail(botId, id);
		return;
	} else if (ready == total) {
		// Wait for 'done' signal.
		return;
	}

	applyProgress(botId, id, total, ready);
}

void Downloads::fail(PeerId botId, DownloadId id, bool cancel) {
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
	k->loading = 0;
	k->failed = 1;

	if (cancel) {
		auto copy = *k;
		list.erase(k);
		applyProgress(botId, copy, 0, 0);
	} else {
		applyProgress(botId, *k, 0, 0);
	}
}

void Downloads::done(PeerId botId, DownloadId id) {
	const auto i = _loaders.find(id);
	if (i == end(_loaders)) {
		return;
	}
	const auto total = i->second.loader->fullSize();
	if (total <= 0) {
		fail(botId, id);
		return;
	}
	_loaders.erase(i);

	auto &list = _lists[botId].list;
	const auto j = ranges::find(
		list,
		id,
		&DownloadsEntry::id);
	Assert(j != end(list));
	j->loading = 0;

	applyProgress(botId, id, total, total);
}

void Downloads::applyProgress(
		PeerId botId,
		DownloadId id,
		int64 total,
		int64 ready) {
	Expects(total >= 0);
	Expects(ready >= 0 && ready <= total);

	auto &list = _lists[botId].list;
	const auto j = ranges::find(
		list,
		id,
		&DownloadsEntry::id);
	Assert(j != end(list));

	applyProgress(botId, *j, total, ready);
}

void Downloads::applyProgress(
		PeerId botId,
		DownloadsEntry &entry,
		int64 total,
		int64 ready) {
	auto &progress = _progressView[botId];
	auto current = progress.current();
	auto subtract = int64(0);
	if (current.ready == current.total) {
		subtract = current.ready;
	}
	if (entry.total != total) {
		const auto delta = total - entry.total;
		entry.total = total;
		current.total += delta;
	}
	if (entry.ready != ready) {
		const auto delta = ready - entry.ready;
		entry.ready = ready;
		current.ready += delta;
	}
	if (subtract > 0
		&& current.ready >= subtract
		&& current.total >= subtract) {
		current.ready -= subtract;
		current.total -= subtract;
	}
	if (entry.loading || current.ready < current.total) {
		current.loading = 1;
	} else {
		current.loading = 0;
	}

	if (total > 0 && total == ready) {
		write();
	}

	progress = current;
}

void Downloads::action(
		not_null<UserData*> bot,
		DownloadId id,
		DownloadsAction type) {
	switch (type) {
	case DownloadsAction::Open: {
		const auto i = ranges::find(
			_lists[bot->id].list,
			id,
			&DownloadsEntry::id);
		if (i == end(_lists[bot->id].list)) {
			return;
		}
		File::ShowInFolder(i->path);
	} break;
	case DownloadsAction::Cancel: {
		const auto i = _loaders.find(id);
		if (i == end(_loaders)) {
			return;
		}
		const auto botId = i->second.botId;
		fail(botId, id, true);
	} break;
	case DownloadsAction::Retry: {
		const auto i = ranges::find(
			_lists[bot->id].list,
			id,
			&DownloadsEntry::id);
		if (i == end(_lists[bot->id].list)) {
			return;
		}
		load(bot->id, id, *i);
	} break;
	}
}

[[nodiscard]] auto Downloads::progress(not_null<UserData*> bot)
->rpl::producer<DownloadsProgress> {
	read();

	return _progressView[bot->id].value();
}

const std::vector<DownloadsEntry> &Downloads::list(
		not_null<UserData*> bot,
		bool forceCheck) {
	read();

	auto &entry = _lists[bot->id];
	if (forceCheck) {
		const auto was = int(entry.list.size());
		for (auto i = begin(entry.list); i != end(entry.list);) {
			if (i->loading || i->failed) {
				++i;
			} else if (auto info = QFileInfo(i->path)
				; !info.exists() || info.size() != i->total) {
				i = entry.list.erase(i);
			} else {
				++i;
			}
		}
		if (int(entry.list.size()) != was) {
			write();
		}
	}
	return entry.list;
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
			auto size = int64();
			stream >> entry.url >> entry.path >> size;
			entry.total = entry.ready = size;
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
					+ sizeof(quint64); // size
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
	const auto chosen = std::make_shared<bool>();
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
			*chosen = true;
			box->closeBox();
			done(path);
		}
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	box->boxClosing() | rpl::start_with_next([=] {
		if (!*chosen) {
			done(QString());
		}
	}, box->lifetime());
}

} // namespace InlineBots
