/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common_session.h"
#include "storage/cache/storage_cache_database.h"
#include "ui/round_rect.h"

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

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
class BoxContent;
} // namespace Ui

namespace Settings {

inline constexpr auto kChartPartsCount = 6;

[[nodiscard]] Type LocalStorageId();

class LocalStorage : public Section<LocalStorage> {
public:
	using Database = Storage::Cache::Database;

	LocalStorage(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;
	void showFinished() override;
	bool paintOuter(
		not_null<QWidget*> outer,
		int maxVisibleHeight,
		QRect clip) override;
	const Ui::RoundRect *bottomSkipRounding() const override {
		return &_bottomSkipRounding;
	}

private:
	class Row;
	class Chart;
	class DeviceBar;
	class ClearButton;

	void setupContent();
	void updateChart();
	void updateCategoryPercents();
	void updateRowCorners();
	void updateDeviceBar();
	void updateClearButton();
	void updateCategoriesWrap();
	void showClearingBox();
	void clearSelected();
	void startClearing();
	void finishClearing();
	void toggleSelected(int chartIndex, bool selected, not_null<Row*> row);
	[[nodiscard]] std::array<int64, kChartPartsCount> chartedSizes() const;
	void update(Database::Stats &&stats, Database::Stats &&statsBig);
	void updateRow(
		not_null<Ui::SlideWrap<Row>*> row,
		const Database::TaggedSummary *data);
	void setupControls(not_null<Ui::VerticalLayout*> container);
	void setupLimits(not_null<Ui::VerticalLayout*> container);
	void updateMediaLimit();
	void updateTotalLimit();
	void updateTotalLabel();
	void updateMediaLabel();
	void applyLimits();

	[[nodiscard]] Database::TaggedSummary summary() const;

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
		const QString &name,
		Convert &&convert,
		Value currentValue,
		Callback &&callback);

	const not_null<Main::Session*> _session;
	const not_null<Storage::Cache::Database*> _db;
	const not_null<Storage::Cache::Database*> _dbBig;

	Database::Stats _stats;
	Database::Stats _statsBig;

	base::flat_map<uint16, not_null<Ui::SlideWrap<Row>*>> _rows;
	std::array<bool, kChartPartsCount> _selected
		= { { true, true, true, true, true, true } };
	rpl::variable<bool> _allSelected = true;
	Chart *_chart = nullptr;
	Ui::SlideWrap<Ui::VerticalLayout> *_categoriesWrap = nullptr;
	bool _categoriesInited = false;
	ClearButton *_clearButton = nullptr;
	DeviceBar *_deviceBar = nullptr;
	Ui::MediaSlider *_totalSlider = nullptr;
	Ui::LabelSimple *_totalLabel = nullptr;
	Ui::MediaSlider *_mediaSlider = nullptr;
	Ui::LabelSimple *_mediaLabel = nullptr;

	Ui::RoundRect _bottomSkipRounding;

	int64 _totalSizeLimit = 0;
	int64 _mediaSizeLimit = 0;
	size_type _timeLimit = 0;

	bool _clearRequested = false;
	int64 _clearFreedBase = 0;
	bool _minDurationPassed = false;
	bool _clearingStarted = false;
	base::weak_qptr<Ui::BoxContent> _clearingBox;

};

} // namespace Settings
