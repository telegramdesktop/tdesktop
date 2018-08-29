/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "storage/cache/storage_cache_database.h"

namespace Storage {
namespace Cache {
class Database;
} // namespace Cache
} // namespace Storage

namespace Ui {
class VerticalLayout;
template <typename Widget>
class SlideWrap;
} // namespace Ui

class LocalStorageBox : public BoxContent {
	struct CreateTag {
	};

public:
	using Database = Storage::Cache::Database;

	LocalStorageBox(QWidget*, not_null<Database*> db, CreateTag);

	static void Show(not_null<Database*> db);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;

private:
	class Row;

	void clearByTag(uint8 tag);
	void update(Database::Stats &&stats);
	void updateRow(
		not_null<Ui::SlideWrap<Row>*> row,
		Database::TaggedSummary *data);
	void setupControls();

	not_null<Storage::Cache::Database*> _db;
	Database::Stats _stats;

	object_ptr<Ui::VerticalLayout> _content = { nullptr };
	base::flat_map<uint8, not_null<Ui::SlideWrap<Row>*>> _rows;

};
