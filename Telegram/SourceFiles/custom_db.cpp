#include "custom_db.h"
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtConcurrent/QtConcurrent>
#include "history/history_item.h"
#include "history/history.h"
#include "data/data_peer.h"

namespace CustomDB {

namespace {
	QString gDataLocation;
	bool gInitialized = false;
}

void Init() {
    if (gInitialized) return;

    gDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(gDataLocation);
	
	// Create the DB once in the main thread to ensure the table exists
	auto db = QSqlDatabase::addDatabase("QSQLITE", "tdesktop_custom_init");
	db.setDatabaseName(gDataLocation + "/tdesktop_custom.sqlite");
	if (db.open()) {
		QSqlQuery query(db);
		query.exec(
			"CREATE TABLE IF NOT EXISTS messages ("
			"id INTEGER PRIMARY KEY AUTOINCREMENT, "
			"msg_id INTEGER, "
			"peer_id TEXT, "
			"peer_name TEXT, "
			"date INTEGER, "
			"text TEXT, "
			"is_out INTEGER"
			")"
		);
		db.close();
	}
	QSqlDatabase::removeDatabase("tdesktop_custom_init");
	gInitialized = true;
}

void SaveMessage(not_null<HistoryItem*> item) {
    if (!gInitialized) return;

    const auto text = item->originalText().text;
    if (text.isEmpty()) return;

	// Extract data needed for DB before jumping to background thread
	struct MsgData {
		qint64 msgId;
		QString peerId;
		QString peerName;
		int date;
		QString text;
		int isOut;
	};

	MsgData data{
		(qint64)item->id.bare,
		QString::number(item->history()->peer->id.value),
		item->history()->peer->name(),
		item->date(),
		text,
		item->out() ? 1 : 0
	};

	// Run DB operation in background to prevent UI lag
	QtConcurrent::run([data = std::move(data)]() {
		// Each thread needs its own unique connection name
		const QString connectionName = "tdesktop_custom_" + QString::number((quintptr)QThread::currentThreadId());
		
		{
			QSqlDatabase db;
			if (QSqlDatabase::contains(connectionName)) {
				db = QSqlDatabase::database(connectionName);
			} else {
				db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
				db.setDatabaseName(gDataLocation + "/tdesktop_custom.sqlite");
			}

			if (db.open()) {
				QSqlQuery query(db);
				query.prepare(
					"INSERT INTO messages (msg_id, peer_id, peer_name, date, text, is_out) "
					"VALUES (:msg_id, :peer_id, :peer_name, :date, :text, :is_out)"
				);
				query.bindValue(":msg_id", data.msgId);
				query.bindValue(":peer_id", data.peerId);
				query.bindValue(":peer_name", data.peerName);
				query.bindValue(":date", data.date);
				query.bindValue(":text", data.text);
				query.bindValue(":is_out", data.isOut);
				query.exec();
				db.close();
			}
		}
		// Clean up the connection after we are done
		// QSqlDatabase::removeDatabase(connectionName); // Optional, but helps avoid connection leaks if threads die. We'll leave it out for connection pooling effect in Qt.
	});
}

} // namespace CustomDB
