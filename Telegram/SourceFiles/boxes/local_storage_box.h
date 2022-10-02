/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "storage/cache/storage_cache_database.h"

namespace Main {
class Session;
} // namespace Main

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
class MediaSlider;
} // namespace Ui

class LocalStorageBox : public Ui::BoxContent {
	struct CreateTag {
	};

public:
	using Database = Storage::Cache::Database;

	LocalStorageBox(
		QWidget*,
		not_null<Main::Session*> session,
		CreateTag);

	static void Show(not_null<Main::Session*> session);

protected:
	void prepare() override;

private:
	class Row;

	void clearByTag(uint16 tag);
	void update(Database::Stats &&stats, Database::Stats &&statsBig);
	void updateRow(
		not_null<Ui::SlideWrap<Row>*> row,
		const Database::TaggedSummary *data);
	void setupControls();
	void setupLimits(not_null<Ui::VerticalLayout*> container);
	void updateMediaLimit();
	void updateTotalLimit();
	void updateTotalLabel();
	void updateMediaLabel();
	void limitsChanged();
	void save();

	Database::TaggedSummary summary() const;

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
	not_null<Ui::MediaSlider*> createLimitsSlider(
		not_null<Ui::VerticalLayout*> container,
		int valuesCount,
		Convert &&convert,
		Value currentValue,
		Callback &&callback);

	const not_null<Main::Session*> _session;
	const not_null<Storage::Cache::Database*> _db;
	const not_null<Storage::Cache::Database*> _dbBig;

	Database::Stats _stats;
	Database::Stats _statsBig;

	base::flat_map<uint16, not_null<Ui::SlideWrap<Row>*>> _rows;
	Ui::MediaSlider *_totalSlider = nullptr;
	Ui::LabelSimple *_totalLabel = nullptr;
	Ui::MediaSlider *_mediaSlider = nullptr;
	Ui::LabelSimple *_mediaLabel = nullptr;

	int64 _totalSizeLimit = 0;
	int64 _mediaSizeLimit = 0;
	size_type _timeLimit = 0;
	bool _limitsChanged = false;

};
