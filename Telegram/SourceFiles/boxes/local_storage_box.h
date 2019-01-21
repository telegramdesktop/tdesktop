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
class LabelSimple;
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
	void setupLimits(not_null<Ui::VerticalLayout*> container);
	void limitsChanged();
	void save();

	template <
		typename Value,
		typename Convert,
		typename Callback,
		typename = std::enable_if_t<
			rpl::details::is_callable_plain_v<
				Callback,
				not_null<Ui::LabelSimple*>,
				Value>
			&& std::is_same_v<Value, decltype(std::declval<Convert>()(1))>>>
	void createLimitsSlider(
		not_null<Ui::VerticalLayout*> container,
		int valuesCount,
		Convert &&convert,
		Value currentValue,
		Callback &&callback);

	not_null<Storage::Cache::Database*> _db;
	Database::Stats _stats;

	base::flat_map<uint8, not_null<Ui::SlideWrap<Row>*>> _rows;

	int64 _sizeLimit = 0;
	size_type _timeLimit = 0;
	bool _limitsChanged = false;

};
