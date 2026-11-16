// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: 37e007c0
// Mokagram: хранилище истории, реализация (SQLite, два фоновых потока).
#include "mokagram/history/mokagram_history_store.h"

#include "base/unixtime.h"
#include "data/data_peer_id.h"
#include "logs.h"

#include <crl/crl_on_main.h>

#include <sqlite3.h>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace Mokagram::History {
namespace {

constexpr auto kSchemaVersion = 1;
constexpr auto kBatchSize = 256;
constexpr auto kPruneEvery = 500;

using Job = std::function<void(sqlite3*)>;

class Statement {
public:
	Statement(sqlite3 *db, const char *sql) {
		if (sqlite3_prepare_v2(db, sql, -1, &_stmt, nullptr) != SQLITE_OK) {
			_stmt = nullptr;
		}
	}
	~Statement() {
		if (_stmt) {
			sqlite3_finalize(_stmt);
		}
	}
	[[nodiscard]] bool ok() const {
		return _stmt != nullptr;
	}
	[[nodiscard]] sqlite3_stmt *get() const {
		return _stmt;
	}
	void bind(int index, int64 value) {
		sqlite3_bind_int64(_stmt, index, value);
	}
	void bind(int index, const QString &value) {
		const auto utf8 = value.toUtf8();
		sqlite3_bind_text(
			_stmt,
			index,
			utf8.constData(),
			int(utf8.size()),
			SQLITE_TRANSIENT);
	}
	void bind(int index, const QByteArray &value) {
		sqlite3_bind_blob(
			_stmt,
			index,
			value.constData(),
			int(value.size()),
			SQLITE_TRANSIENT);
	}
	[[nodiscard]] bool step() {
		return sqlite3_step(_stmt) == SQLITE_ROW;
	}
	void run() {
		sqlite3_step(_stmt);
	}
	[[nodiscard]] int64 int64At(int column) const {
		return sqlite3_column_int64(_stmt, column);
	}
	[[nodiscard]] QString textAt(int column) const {
		return QString::fromUtf8(
			reinterpret_cast<const char*>(sqlite3_column_text(_stmt, column)),
			sqlite3_column_bytes(_stmt, column));
	}
	[[nodiscard]] QByteArray blobAt(int column) const {
		return QByteArray(
			reinterpret_cast<const char*>(sqlite3_column_blob(_stmt, column)),
			sqlite3_column_bytes(_stmt, column));
	}

private:
	sqlite3_stmt *_stmt = nullptr;

};

void Exec(sqlite3 *db, const char *sql) {
	sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
}

[[nodiscard]] int64 UserVersion(sqlite3 *db) {
	auto stmt = Statement(db, "PRAGMA user_version");
	return (stmt.ok() && stmt.step()) ? stmt.int64At(0) : -1;
}

[[nodiscard]] bool CreateSchema(sqlite3 *db) {
	if (sqlite3_exec(
		db,
		"PRAGMA auto_vacuum=INCREMENTAL;"
		"PRAGMA journal_mode=WAL;"
		"PRAGMA synchronous=NORMAL;"
		"PRAGMA temp_store=MEMORY;"
		"PRAGMA mmap_size=33554432;"
		"PRAGMA cache_size=-8192;"
		"PRAGMA journal_size_limit=16777216;"
		"PRAGMA wal_autocheckpoint=250;"
		"CREATE TABLE IF NOT EXISTS msg("
		"peer INTEGER NOT NULL,"
		"topic INTEGER NOT NULL DEFAULT 0,"
		"id INTEGER NOT NULL,"
		"kind INTEGER NOT NULL,"
		"rev INTEGER NOT NULL DEFAULT 0,"
		"date INTEGER NOT NULL DEFAULT 0,"
		"event INTEGER NOT NULL DEFAULT 0,"
		"from_id INTEGER NOT NULL DEFAULT 0,"
		"text TEXT,"
		"ents BLOB,"
		"media TEXT,"
		"PRIMARY KEY(peer, kind, id, rev)) WITHOUT ROWID;"
		"CREATE INDEX IF NOT EXISTS msg_by_time"
		" ON msg(peer, kind, event DESC, id DESC);"
		"CREATE INDEX IF NOT EXISTS msg_by_event ON msg(event);",
		nullptr,
		nullptr,
		nullptr) != SQLITE_OK) {
		return false;
	}
	sqlite3_exec(db, "ALTER TABLE msg ADD COLUMN tl BLOB", nullptr, nullptr, nullptr);
	sqlite3_exec(db, "ALTER TABLE msg ADD COLUMN reply_to INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
	sqlite3_exec(db, "ALTER TABLE msg ADD COLUMN reply_peer INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
	sqlite3_exec(
		db,
		"CREATE TABLE IF NOT EXISTS seen("
		"peer INTEGER NOT NULL,"
		"id INTEGER NOT NULL,"
		"topic INTEGER NOT NULL DEFAULT 0,"
		"date INTEGER NOT NULL DEFAULT 0,"
		"from_id INTEGER NOT NULL DEFAULT 0,"
		"text TEXT,"
		"ents BLOB,"
		"media TEXT,"
		"tl BLOB,"
		"reply_to INTEGER NOT NULL DEFAULT 0,"
		"reply_peer INTEGER NOT NULL DEFAULT 0,"
		"PRIMARY KEY(peer, id)) WITHOUT ROWID;"
		"CREATE INDEX IF NOT EXISTS seen_by_id ON seen(id);",
		nullptr,
		nullptr,
		nullptr);
	const auto version = UserVersion(db);
	if (version < 0) {
		return false;
	} else if (version < kSchemaVersion) {
		Exec(db, "PRAGMA user_version=1");
	}
	return true;
}

// При ошибке оставляем базу и журнал нетронутыми для восстановления.
[[nodiscard]] sqlite3 *OpenWriter(const QString &path) {
	auto db = (sqlite3*)nullptr;
	const auto utf8 = path.toUtf8();
	if (sqlite3_open_v2(
		utf8.constData(),
		&db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
		nullptr) == SQLITE_OK
		&& CreateSchema(db)) {
		return db;
	}
	if (db) {
		LOG(("[mokagram] History: cannot open database: %1"
			).arg(QString::fromUtf8(sqlite3_errmsg(db))));
		sqlite3_close(db);
	}
	return nullptr;
}

[[nodiscard]] sqlite3 *OpenReader(const QString &path) {
	auto db = (sqlite3*)nullptr;
	const auto utf8 = path.toUtf8();
	if (sqlite3_open_v2(
		utf8.constData(),
		&db,
		SQLITE_OPEN_READONLY,
		nullptr) != SQLITE_OK) {
		if (db) {
			sqlite3_close(db);
		}
		return nullptr;
	}
	Exec(db, "PRAGMA cache_size=-4096; PRAGMA mmap_size=33554432;");
	return db;
}

void WriteSnapshot(sqlite3 *db, const Snapshot &snapshot) {
	const auto edited = (snapshot.kind == Kind::Edited);
	auto stmt = Statement(
		db,
		edited
		? "INSERT OR REPLACE INTO msg"
			"(peer,topic,id,kind,rev,date,event,from_id,text,ents,media,tl,reply_to,reply_peer)"
			" VALUES(?1,?2,?3,2,"
			"(SELECT COALESCE(MAX(rev),0)+1 FROM msg"
			" WHERE peer=?1 AND kind=2 AND id=?3),"
			"?4,?5,?6,?7,?8,?9,?10,?11,?12)"
		: "INSERT OR REPLACE INTO msg"
			"(peer,topic,id,kind,rev,date,event,from_id,text,ents,media,tl,reply_to,reply_peer)"
			" VALUES(?1,?2,?3,1,0,?4,?5,?6,?7,?8,?9,?10,?11,?12)");
	if (!stmt.ok()) {
		return;
	}
	stmt.bind(1, int64(snapshot.peer));
	stmt.bind(2, snapshot.topic);
	stmt.bind(3, snapshot.id);
	stmt.bind(4, int64(snapshot.date));
	stmt.bind(5, int64(snapshot.event));
	stmt.bind(6, int64(snapshot.from));
	stmt.bind(7, snapshot.text);
	stmt.bind(8, snapshot.entities);
	stmt.bind(9, snapshot.media);
	stmt.bind(10, snapshot.tl);
	stmt.bind(11, snapshot.replyTo);
	stmt.bind(12, int64(snapshot.replyPeer));
	stmt.run();
}

void WriteSeen(sqlite3 *db, const Snapshot &snapshot, bool replace) {
	auto stmt = Statement(
		db,
		replace
		? "INSERT OR REPLACE INTO seen"
			"(peer,id,topic,date,from_id,text,ents,media,tl,reply_to,reply_peer)"
			" VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)"
		: "INSERT OR IGNORE INTO seen"
			"(peer,id,topic,date,from_id,text,ents,media,tl,reply_to,reply_peer)"
			" VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)");
	if (!stmt.ok()) {
		return;
	}
	stmt.bind(1, int64(snapshot.peer));
	stmt.bind(2, snapshot.id);
	stmt.bind(3, snapshot.topic);
	stmt.bind(4, int64(snapshot.date));
	stmt.bind(5, int64(snapshot.from));
	stmt.bind(6, snapshot.text);
	stmt.bind(7, snapshot.entities);
	stmt.bind(8, snapshot.media);
	stmt.bind(9, snapshot.tl);
	stmt.bind(10, snapshot.replyTo);
	stmt.bind(11, int64(snapshot.replyPeer));
	stmt.run();
}

[[nodiscard]] int64 DatabaseBytes(sqlite3 *db) {
	auto pages = int64(0);
	auto size = int64(0);
	{
		auto stmt = Statement(db, "PRAGMA page_count");
		if (stmt.ok() && stmt.step()) {
			pages = stmt.int64At(0);
		}
	}
	{
		auto stmt = Statement(db, "PRAGMA page_size");
		if (stmt.ok() && stmt.step()) {
			size = stmt.int64At(0);
		}
	}
	return pages * size;
}

// Сохранённые файлы лежат в <база>/files/<peer>/<id>_<документ>.<ext>.
void RemoveSavedFiles(const QString &files, int64 peer, int64 id) {
	auto dir = QDir(files + u"/%1"_q.arg(peer));
	for (const auto &name : dir.entryList({ u"%1_*"_q.arg(id) }, QDir::Files)) {
		dir.remove(name);
	}
}

// Стирает файлы сообщений из SELECT peer, id: вызывать до DELETE.
void EraseSavedFiles(
		sqlite3 *db,
		const QString &files,
		const char *select,
		std::initializer_list<int64> binds) {
	auto stmt = Statement(db, select);
	if (!stmt.ok()) {
		return;
	}
	auto index = 1;
	for (const auto value : binds) {
		stmt.bind(index++, value);
	}
	while (stmt.step()) {
		RemoveSavedFiles(files, stmt.int64At(0), stmt.int64At(1));
	}
}

void Prune(sqlite3 *db, const QString &files, Limits limits) {
	if (limits.days > 0) {
		const auto cutoff = int64(base::unixtime::now())
			- int64(limits.days) * 86400;
		EraseSavedFiles(
			db,
			files,
			"SELECT peer,id FROM msg WHERE event < ?1 AND kind=1",
			{ cutoff });
		auto stmt = Statement(db, "DELETE FROM msg WHERE event < ?1");
		if (stmt.ok()) {
			stmt.bind(1, cutoff);
			stmt.run();
		}
		auto seen = Statement(db, "DELETE FROM seen WHERE date < ?1");
		if (seen.ok()) {
			seen.bind(1, cutoff);
			seen.run();
		}
	}
	if (limits.perChat > 0) {
		auto peers = std::vector<std::pair<int64, int64>>();
		{
			auto stmt = Statement(
				db,
				"SELECT peer, count(*) FROM msg GROUP BY peer"
				" HAVING count(*) > ?1");
			if (stmt.ok()) {
				stmt.bind(1, int64(limits.perChat));
				while (stmt.step()) {
					peers.emplace_back(stmt.int64At(0), stmt.int64At(1));
				}
			}
		}
		for (const auto &[peer, count] : peers) {
			EraseSavedFiles(
				db,
				files,
				"SELECT peer,id FROM msg WHERE peer=?1 AND kind=1"
				" AND (kind,id,rev) IN (SELECT kind,id,rev FROM msg"
				" WHERE peer=?1 ORDER BY event ASC LIMIT ?2)",
				{ peer, count - limits.perChat });
			auto stmt = Statement(
				db,
				"DELETE FROM msg WHERE peer=?1 AND (kind,id,rev) IN"
				" (SELECT kind,id,rev FROM msg WHERE peer=?1"
				" ORDER BY event ASC LIMIT ?2)");
			if (stmt.ok()) {
				stmt.bind(1, peer);
				stmt.bind(2, count - limits.perChat);
				stmt.run();
			}
		}
	}
	if (limits.megabytes > 0) {
		const auto maxBytes = int64(limits.megabytes) * 1024 * 1024;
		for (auto i = 0; i != 8; ++i) {
			Exec(db, "PRAGMA incremental_vacuum");
			if (DatabaseBytes(db) <= maxBytes) {
				break;
			}
			EraseSavedFiles(
				db,
				files,
				"SELECT peer,id FROM msg WHERE kind=1 AND (peer,kind,id,rev) IN"
				" (SELECT peer,kind,id,rev FROM msg ORDER BY event ASC"
				" LIMIT MAX(1, (SELECT count(*) FROM msg) / 10))",
				{});
			Exec(
				db,
				"DELETE FROM msg WHERE (peer,kind,id,rev) IN"
				" (SELECT peer,kind,id,rev FROM msg ORDER BY event ASC"
				" LIMIT MAX(1, (SELECT count(*) FROM msg) / 10))");
			Exec(
				db,
				"DELETE FROM seen WHERE (peer,id) IN"
				" (SELECT peer,id FROM seen ORDER BY date ASC"
				" LIMIT MAX(1, (SELECT count(*) FROM seen) / 10))");
		}
	}
	Exec(db, "PRAGMA incremental_vacuum");
	Exec(db, "PRAGMA wal_checkpoint(TRUNCATE)");
}

} // namespace

// Фоновый поток со своей очередью и своим соединением.
class Worker final {
public:
	Worker(QString path, bool writer, std::shared_ptr<std::atomic<int>> ready)
	: _path(std::move(path))
	, _writer(writer)
	, _ready(std::move(ready))
	, _thread([=] { run(); }) {
	}
	~Worker() {
		{
			const auto lock = std::scoped_lock(_mutex);
			_stop = true;
		}
		_cv.notify_all();
		_thread.join();
	}

	void post(Job job) {
		{
			const auto lock = std::scoped_lock(_mutex);
			_jobs.push_back(std::move(job));
		}
		_cv.notify_one();
	}

private:
	void run() {
		auto db = (sqlite3*)nullptr;
		if (_writer) {
			db = OpenWriter(_path);
			_ready->store(db ? 1 : 2);
			_ready->notify_all();
		} else {
			_ready->wait(0);
			db = (_ready->load() == 1) ? OpenReader(_path) : nullptr;
		}
		auto lock = std::unique_lock(_mutex);
		while (true) {
			_cv.wait(lock, [&] { return _stop || !_jobs.empty(); });
			if (_jobs.empty()) {
				break; // Очередь сброшена, можно выходить.
			}
			auto batch = std::vector<Job>();
			while (!_jobs.empty() && batch.size() < kBatchSize) {
				batch.push_back(std::move(_jobs.front()));
				_jobs.pop_front();
			}
			lock.unlock();
			if (db) {
				if (_writer) {
					Exec(db, "BEGIN");
				}
				for (const auto &job : batch) {
					job(db);
				}
				if (_writer) {
					Exec(db, "COMMIT");
				}
			}
			lock.lock();
		}
		lock.unlock();
		if (db) {
			sqlite3_close(db);
		}
	}

	const QString _path;
	const bool _writer;
	const std::shared_ptr<std::atomic<int>> _ready;
	std::mutex _mutex;
	std::condition_variable _cv;
	std::deque<Job> _jobs;
	bool _stop = false;
	std::thread _thread;

};

struct Store::Impl {
	Impl(const QString &path)
	: ready(std::make_shared<std::atomic<int>>(0))
	, files(QFileInfo(path).absolutePath() + u"/files"_q)
	, writer(path, true, ready)
	, reader(path, false, ready) {
	}

	std::shared_ptr<std::atomic<int>> ready;
	const QString files;
	Worker writer;
	Worker reader;
};

Store::Store(const QString &path)
: _impl(std::make_unique<Impl>(path)) {
	reloadCounts();
}

Store::~Store() {
	// Деструкторы Worker дописывают очередь и закрывают базу.
}

void Store::setLimitsProvider(Fn<Limits()> provider) {
	_limits = std::move(provider);
}

void Store::push(Snapshot snapshot) {
	auto &counts = _counts[snapshot.peer];
	if (snapshot.kind == Kind::Deleted) {
		++counts.deleted;
	} else {
		++counts.edited;
		_revisions.emplace(FullMsgId(
			PeerId(snapshot.peer),
			MsgId(snapshot.id)));
	}
	_impl->writer.post([snapshot = std::move(snapshot)](sqlite3 *db) {
		WriteSnapshot(db, snapshot);
	});
	_changes.fire({});
	if (++_sincePrune >= kPruneEvery) {
		_sincePrune = 0;
		if (_limits) {
			pruneNow(_limits());
		}
	}
}

void Store::pruneNow(Limits limits) {
	const auto weak = base::make_weak(this);
	_impl->writer.post([=, files = _impl->files](sqlite3 *db) {
		Prune(db, files, limits);
		crl::on_main(weak, [=] {
			if (const auto strong = weak.get()) {
				strong->reloadCounts();
			}
		});
	});
}

void Store::pruneIfDue(int added) {
	_sincePrune += added;
	if (_sincePrune >= kPruneEvery) {
		_sincePrune = 0;
		if (_limits) {
			pruneNow(_limits());
		}
	}
}

void Store::journal(Snapshot snapshot, bool replace) {
	_impl->writer.post([snapshot = std::move(snapshot), replace](sqlite3 *db) {
		WriteSeen(db, snapshot, replace);
	});
	pruneIfDue(1);
}

void Store::claimDeleted(
		uint64 peer,
		std::vector<int64> ids,
		Fn<void(std::vector<Snapshot>)> done) {
	if (ids.empty()) {
		return;
	}
	const auto weak = base::make_weak(this);
	const auto now = base::unixtime::now();
	_impl->writer.post([=](sqlite3 *db) {
		auto result = std::vector<Snapshot>();
		for (const auto id : ids) {
			auto stmt = Statement(
				db,
				"SELECT peer,topic,id,date,from_id,text,ents,media,tl,"
				"reply_to,reply_peer FROM seen"
				" WHERE id=?1 AND (?2=0 OR peer=?2)");
			if (!stmt.ok()) {
				continue;
			}
			stmt.bind(1, id);
			stmt.bind(2, int64(peer));
			while (stmt.step()) {
				const auto row = uint64(stmt.int64At(0));
				if (!peer && peerIsChannel(PeerId(row))) {
					continue;
				}
				auto &snapshot = result.emplace_back();
				snapshot.peer = row;
				snapshot.topic = stmt.int64At(1);
				snapshot.id = stmt.int64At(2);
				snapshot.kind = Kind::Deleted;
				snapshot.date = int32(stmt.int64At(3));
				snapshot.event = now;
				snapshot.from = uint64(stmt.int64At(4));
				snapshot.text = stmt.textAt(5);
				snapshot.entities = stmt.blobAt(6);
				snapshot.media = stmt.textAt(7);
				snapshot.tl = stmt.blobAt(8);
				snapshot.replyTo = stmt.int64At(9);
				snapshot.replyPeer = uint64(stmt.int64At(10));
			}
		}
		for (const auto &snapshot : result) {
			WriteSnapshot(db, snapshot);
		}
		if (result.empty()) {
			return;
		}
		crl::on_main(weak, [=, result = std::move(result)]() mutable {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			for (const auto &snapshot : result) {
				++strong->_counts[snapshot.peer].deleted;
			}
			strong->_changes.fire({});
			strong->pruneIfDue(int(result.size()));
			done(std::move(result));
		});
	});
}

void Store::removeOne(uint64 peer, int64 id) {
	_impl->writer.post([=](sqlite3 *db) {
		auto stmt = Statement(
			db,
			"DELETE FROM msg WHERE peer=?1 AND kind=1 AND id=?2");
		if (stmt.ok()) {
			stmt.bind(1, int64(peer));
			stmt.bind(2, id);
			stmt.run();
		}
	});
	_changes.fire({});
}

void Store::clearPeer(uint64 peer) {
	_counts.remove(peer);
	_revisionsLoaded.remove(peer);
	for (auto i = _revisions.begin(); i != _revisions.end();) {
		if (i->peer.value == peer) {
			i = _revisions.erase(i);
		} else {
			++i;
		}
	}
	_impl->writer.post([=, files = _impl->files](sqlite3 *db) {
		QDir(files + u"/%1"_q.arg(peer)).removeRecursively();
		auto stmt = Statement(db, "DELETE FROM msg WHERE peer=?1");
		if (stmt.ok()) {
			stmt.bind(1, int64(peer));
			stmt.run();
		}
		auto seen = Statement(db, "DELETE FROM seen WHERE peer=?1");
		if (seen.ok()) {
			seen.bind(1, int64(peer));
			seen.run();
		}
	});
	_changes.fire({});
}

void Store::clearAll() {
	_counts.clear();
	_revisionsLoaded.clear();
	_revisions.clear();
	_impl->writer.post([files = _impl->files](sqlite3 *db) {
		QDir(files).removeRecursively();
		Exec(db, "DELETE FROM msg");
		Exec(db, "DELETE FROM seen");
		Exec(db, "PRAGMA incremental_vacuum");
		Exec(db, "PRAGMA wal_checkpoint(TRUNCATE)");
	});
	_changes.fire({});
}

Counts Store::counts(uint64 peer) const {
	const auto i = _counts.find(peer);
	return (i != end(_counts)) ? i->second : Counts();
}

const base::flat_map<uint64, Counts> &Store::allCounts() const {
	return _counts;
}

bool Store::hasRevisions(FullMsgId id) const {
	return _revisions.contains(id);
}

void Store::ensureRevisionsLoaded(uint64 peer) {
	if (!_revisionsLoaded.emplace(peer).second) {
		return;
	}
	const auto weak = base::make_weak(this);
	_impl->reader.post([=](sqlite3 *db) {
		auto ids = std::vector<int64>();
		auto stmt = Statement(
			db,
			"SELECT DISTINCT id FROM msg WHERE peer=?1 AND kind=2");
		if (!stmt.ok()) {
			return;
		}
		stmt.bind(1, int64(peer));
		while (stmt.step()) {
			ids.push_back(stmt.int64At(0));
		}
		crl::on_main(weak, [=, ids = std::move(ids)] {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			for (const auto id : ids) {
				strong->_revisions.emplace(FullMsgId(
					PeerId(peer),
					MsgId(id)));
			}
			strong->_changes.fire({});
		});
	});
}

void Store::reloadCounts() {
	const auto weak = base::make_weak(this);
	_impl->reader.post([=](sqlite3 *db) {
		auto result = base::flat_map<uint64, Counts>();
		auto stmt = Statement(
			db,
			"SELECT peer, kind, count(*) FROM msg GROUP BY peer, kind");
		if (!stmt.ok()) {
			return;
		}
		while (stmt.step()) {
			auto &counts = result[uint64(stmt.int64At(0))];
			(stmt.int64At(1) == int(Kind::Deleted)
				? counts.deleted
				: counts.edited) = int(stmt.int64At(2));
		}
		crl::on_main(weak, [=, result = std::move(result)]() mutable {
			if (const auto strong = weak.get()) {
				strong->_counts = std::move(result);
				strong->_changes.fire({});
			}
		});
	});
}

void Store::loadPage(
		PageQuery query,
		Fn<void(std::vector<Snapshot>)> done) {
	const auto weak = base::make_weak(this);
	_impl->reader.post([=](sqlite3 *db) {
		auto result = std::vector<Snapshot>();
		auto sql = QByteArray(
			"SELECT peer,topic,id,kind,date,event,from_id,text,ents,media,tl,reply_to,reply_peer"
			" FROM msg WHERE (?1=0 OR peer=?1) AND kind=?2");
		if (query.since) {
			sql += " AND event>=?8";
		}
		if (!query.mediaLike.isEmpty()) {
			sql += " AND media LIKE ?9";
		}
		if (query.id) {
			sql += " AND id=?5";
		}
		if (query.minId) {
			sql += " AND id>=?7";
		}
		if (query.beforeEvent) {
			sql += " AND (event < ?3 OR (event = ?3 AND id < ?4))";
		}
		sql += " ORDER BY event DESC, id DESC, rev DESC LIMIT ?6";
		auto stmt = Statement(db, sql.constData());
		if (stmt.ok()) {
			stmt.bind(1, int64(query.peer));
			stmt.bind(2, int64(query.kind));
			stmt.bind(3, int64(query.beforeEvent));
			stmt.bind(4, query.beforeId);
			stmt.bind(5, query.id);
			stmt.bind(6, int64(query.limit));
			if (query.minId) {
				stmt.bind(7, query.minId);
			}
			if (query.since) {
				stmt.bind(8, int64(query.since));
			}
			if (!query.mediaLike.isEmpty()) {
				stmt.bind(9, query.mediaLike);
			}
			while (stmt.step()) {
				auto &row = result.emplace_back();
				row.peer = uint64(stmt.int64At(0));
				row.topic = stmt.int64At(1);
				row.id = stmt.int64At(2);
				row.kind = Kind(stmt.int64At(3));
				row.date = int32(stmt.int64At(4));
				row.event = int32(stmt.int64At(5));
				row.from = uint64(stmt.int64At(6));
				row.text = stmt.textAt(7);
				row.entities = stmt.blobAt(8);
				row.media = stmt.textAt(9);
				row.tl = stmt.blobAt(10);
				row.replyTo = stmt.int64At(11);
				row.replyPeer = uint64(stmt.int64At(12));
			}
		}
		crl::on_main(weak, [=, result = std::move(result)]() mutable {
			if (weak.get()) {
				done(std::move(result));
			}
		});
	});
}

rpl::producer<> Store::changes() const {
	return _changes.events();
}

} // namespace Mokagram::History
