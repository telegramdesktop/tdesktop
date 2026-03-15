#include "custom_db.h"
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include "history/history_item.h"
#include "history/history.h"
#include "data/data_peer.h"

namespace CustomDB {

void Init() {
    if (QSqlDatabase::contains("tdesktop_custom")) return;

    auto db = QSqlDatabase::addDatabase("QSQLITE", "tdesktop_custom");
    auto dataLocation = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataLocation);
    db.setDatabaseName(dataLocation + "/tdesktop_custom.sqlite");

    if (!db.open()) {
        qDebug() << "Failed to open custom DB:" << db.lastError().text();
        return;
    }

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
}

void SaveMessage(not_null<HistoryItem*> item) {
    auto db = QSqlDatabase::database("tdesktop_custom");
    if (!db.isOpen()) {
        return;
    }

    const auto text = item->originalText().text;
    if (text.isEmpty()) return;

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO messages (msg_id, peer_id, peer_name, date, text, is_out) "
        "VALUES (:msg_id, :peer_id, :peer_name, :date, :text, :is_out)"
    );
    query.bindValue(":msg_id", (qint64)item->id.bare);
    query.bindValue(":peer_id", QString::number(item->history()->peer->id.value));
    query.bindValue(":peer_name", item->history()->peer->name());
    query.bindValue(":date", item->date());
    query.bindValue(":text", text);
    query.bindValue(":is_out", item->out() ? 1 : 0);
    query.exec();
}

} // namespace CustomDB
