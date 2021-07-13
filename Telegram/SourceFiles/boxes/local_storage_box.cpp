/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/local_storage_box.h"

#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/effects/radial_animation.h"
#include "ui/text/format_values.h"
#include "ui/emoji_config.h"
#include "storage/storage_account.h"
#include "storage/cache/storage_cache_database.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kMegabyte = int64(1024 * 1024);
constexpr auto kTotalSizeLimitsCount = 18;
constexpr auto kMediaSizeLimitsCount = 18;
constexpr auto kMinimalSizeLimit = 100 * kMegabyte;
constexpr auto kTimeLimitsCount = 16;
constexpr auto kMaxTimeLimitValue = std::numeric_limits<size_type>::max();
constexpr auto kFakeMediaCacheTag = uint16(0xFFFF);

int64 TotalSizeLimitInMB(int index) {
	if (index < 8) {
		return int64(index + 2) * 100;
	}
	return int64(index - 7) * 1024;
}

int64 TotalSizeLimit(int index) {
	return TotalSizeLimitInMB(index) * kMegabyte;
}

int64 MediaSizeLimitInMB(int index) {
	if (index < 9) {
		return int64(index + 1) * 100;
	}
	return int64(index - 8) * 1024;
}

int64 MediaSizeLimit(int index) {
	return MediaSizeLimitInMB(index) * kMegabyte;
}

QString SizeLimitText(int64 limit) {
	const auto mb = (limit / (1024 * 1024));
	const auto gb = (mb / 1024);
	return (gb > 0)
		? (QString::number(gb) + " GB")
		: (QString::number(mb) + " MB");
}

size_type TimeLimitInDays(int index) {
	if (index < 3) {
		const auto weeks = (index + 1);
		return size_type(weeks) * 7;
	} else if (index < 15) {
		const auto month = (index - 2);
		return (size_type(month) * 30)
			+ ((month >= 12) ? 5 :
				(month >= 10) ? 4 :
				(month >= 8) ? 3 :
				(month >= 7) ? 2 :
				(month >= 5) ? 1 :
				(month >= 3) ? 0 :
				(month >= 2) ? -1 :
				(month >= 1) ? 1 : 0);
			//+ (month >= 1 ? 1 : 0)
			//- (month >= 2 ? 2 : 0)
			//+ (month >= 3 ? 1 : 0)
			//+ (month >= 5 ? 1 : 0)
			//+ (month >= 7 ? 1 : 0)
			//+ (month >= 8 ? 1 : 0)
			//+ (month >= 10 ? 1 : 0)
			//+ (month >= 12 ? 1 : 0);
	}
	return 0;
}

size_type TimeLimit(int index) {
	const auto days = TimeLimitInDays(index);
	return days
		? (days * 24 * 60 * 60)
		: kMaxTimeLimitValue;
}

QString TimeLimitText(size_type limit) {
	const auto days = (limit / (24 * 60 * 60));
	const auto weeks = (days / 7);
	const auto months = (days / 29);
	return (months > 0)
		? tr::lng_local_storage_limit_months(tr::now, lt_count, months)
		: (limit > 0)
		? tr::lng_local_storage_limit_weeks(tr::now, lt_count, weeks)
		: tr::lng_local_storage_limit_never(tr::now);
}

size_type LimitToValue(size_type timeLimit) {
	return timeLimit ? timeLimit : kMaxTimeLimitValue;
}

size_type ValueToLimit(size_type timeLimit) {
	return (timeLimit != kMaxTimeLimitValue) ? timeLimit : 0;
}

} // namespace

class LocalStorageBox::Row : public Ui::RpWidget {
public:
	Row(
		QWidget *parent,
		Fn<QString(size_type)> title,
		rpl::producer<QString> clear,
		const Database::TaggedSummary &data);

	void update(const Database::TaggedSummary &data);
	void toggleProgress(bool shown);

	rpl::producer<> clearRequests() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	QString titleText(const Database::TaggedSummary &data) const;
	QString sizeText(const Database::TaggedSummary &data) const;
	void radialAnimationCallback();

	Fn<QString(size_type)> _titleFactory;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _description;
	object_ptr<Ui::FlatLabel> _clearing = { nullptr };
	object_ptr<Ui::RoundButton> _clear;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _progress;

};

LocalStorageBox::Row::Row(
	QWidget *parent,
	Fn<QString(size_type)> title,
	rpl::producer<QString> clear,
	const Database::TaggedSummary &data)
: RpWidget(parent)
, _titleFactory(std::move(title))
, _title(
	this,
	titleText(data),
	st::localStorageRowTitle)
, _description(
	this,
	sizeText(data),
	st::localStorageRowSize)
, _clear(this, std::move(clear), st::localStorageClear) {
	_clear->setVisible(data.count != 0);
}

void LocalStorageBox::Row::update(const Database::TaggedSummary &data) {
	if (data.count != 0) {
		_title->setText(titleText(data));
	}
	_description->setText(sizeText(data));
	_clear->setVisible(data.count != 0);
}

void LocalStorageBox::Row::toggleProgress(bool shown) {
	if (!shown) {
		_progress = nullptr;
		_description->show();
		_clearing.destroy();
	} else if (!_progress) {
		_progress = std::make_unique<Ui::InfiniteRadialAnimation>(
			[=] { radialAnimationCallback(); },
			st::proxyCheckingAnimation);
		_progress->start();
		_clearing = object_ptr<Ui::FlatLabel>(
			this,
			tr::lng_local_storage_clearing(tr::now),
			st::localStorageRowSize);
		_clearing->show();
		_description->hide();
		resizeToWidth(width());
		RpWidget::update();
	}
}

void LocalStorageBox::Row::radialAnimationCallback() {
	if (!anim::Disabled()) {
		RpWidget::update();
	}
}

rpl::producer<> LocalStorageBox::Row::clearRequests() const {
	return _clear->clicks() | rpl::to_empty;
}

int LocalStorageBox::Row::resizeGetHeight(int newWidth) {
	const auto height = st::localStorageRowHeight;
	const auto padding = st::localStorageRowPadding;
	const auto available = newWidth - padding.left() - padding.right();
	_title->resizeToWidth(available);
	_description->resizeToWidth(available);
	_title->moveToLeft(padding.left(), padding.top(), newWidth);
	_description->moveToLeft(
		padding.left(),
		height - padding.bottom() - _description->height(),
		newWidth);
	if (_clearing) {
		const auto progressShift = st::proxyCheckingPosition.x()
			+ st::proxyCheckingAnimation.size.width()
			+ st::proxyCheckingSkip;
		_clearing->resizeToWidth(available - progressShift);
		_clearing->moveToLeft(
			padding.left(),// + progressShift,
			_description->y(),
			newWidth);
	}
	_clear->moveToRight(
		st::layerBox.buttonPadding.right(),
		(height - _clear->height()) / 2,
		newWidth);
	return height;
}

void LocalStorageBox::Row::paintEvent(QPaintEvent *e) {
	if (!_progress || true) {
		return;
	}
	Painter p(this);
	const auto padding = st::localStorageRowPadding;
	const auto height = st::localStorageRowHeight;
	const auto bottom = height - padding.bottom() - _description->height();
	_progress->draw(
		p,
		{
			st::proxyCheckingPosition.x() + padding.left(),
			st::proxyCheckingPosition.y() + bottom
		},
		width());
}

QString LocalStorageBox::Row::titleText(const Database::TaggedSummary &data) const {
	return _titleFactory(data.count);
}

QString LocalStorageBox::Row::sizeText(const Database::TaggedSummary &data) const {
	return data.totalSize
		? Ui::FormatSizeText(data.totalSize)
		: tr::lng_local_storage_empty(tr::now);
}

LocalStorageBox::LocalStorageBox(
	QWidget*,
	not_null<Main::Session*> session,
	CreateTag)
: _session(session)
, _db(&session->data().cache())
, _dbBig(&session->data().cacheBigFile()) {
	const auto &settings = session->local().cacheSettings();
	const auto &settingsBig = session->local().cacheBigFileSettings();
	_totalSizeLimit = settings.totalSizeLimit + settingsBig.totalSizeLimit;
	_mediaSizeLimit = settingsBig.totalSizeLimit;
	_timeLimit = settings.totalTimeLimit;
}

void LocalStorageBox::Show(not_null<::Main::Session*> session) {
	auto shared = std::make_shared<object_ptr<LocalStorageBox>>(
		Box<LocalStorageBox>(session, CreateTag()));
	const auto weak = shared->data();
	rpl::combine(
		session->data().cache().statsOnMain(),
		session->data().cacheBigFile().statsOnMain()
	) | rpl::start_with_next([=](
			Database::Stats &&stats,
			Database::Stats &&statsBig) {
		weak->update(std::move(stats), std::move(statsBig));
		if (auto &strong = *shared) {
			Ui::show(std::move(strong));
		}
	}, weak->lifetime());
}

void LocalStorageBox::prepare() {
	setTitle(tr::lng_local_storage_title());

	addButton(tr::lng_box_ok(), [this] { closeBox(); });

	setupControls();
}

void LocalStorageBox::updateRow(
		not_null<Ui::SlideWrap<Row>*> row,
		const Database::TaggedSummary *data) {
	const auto summary = (_rows.find(0)->second == row);
	const auto shown = (data && data->count && data->totalSize) || summary;
	if (shown) {
		row->entity()->update(*data);
	}
	row->toggle(shown, anim::type::normal);
}

void LocalStorageBox::update(
		Database::Stats &&stats,
		Database::Stats &&statsBig) {
	_stats = std::move(stats);
	_statsBig = std::move(statsBig);
	if (const auto i = _rows.find(0); i != end(_rows)) {
		i->second->entity()->toggleProgress(
			_stats.clearing || _statsBig.clearing);
	}
	for (const auto &entry : _rows) {
		if (entry.first == kFakeMediaCacheTag) {
			updateRow(entry.second, &_statsBig.full);
		} else if (entry.first) {
			const auto i = _stats.tagged.find(entry.first);
			updateRow(
				entry.second,
				(i != end(_stats.tagged)) ? &i->second : nullptr);
		} else {
			const auto full = summary();
			updateRow(entry.second, &full);
		}
	}
}

auto LocalStorageBox::summary() const -> Database::TaggedSummary {
	auto result = _stats.full;
	result.count += _statsBig.full.count;
	result.totalSize += _statsBig.full.totalSize;
	return result;
}

void LocalStorageBox::clearByTag(uint16 tag) {
	if (tag == kFakeMediaCacheTag) {
		_dbBig->clear();
	} else if (tag) {
		_db->clearByTag(tag);
	} else {
		_db->clear();
		_dbBig->clear();
		Ui::Emoji::ClearIrrelevantCache();
	}
}

void LocalStorageBox::setupControls() {
	const auto container = setInnerWidget(
		object_ptr<Ui::VerticalLayout>(this),
		st::defaultMultiSelect.scroll);
	const auto createRow = [&](
			uint16 tag,
			Fn<QString(size_type)> title,
			rpl::producer<QString> clear,
			const Database::TaggedSummary &data) {
		auto result = container->add(object_ptr<Ui::SlideWrap<Row>>(
			container,
			object_ptr<Row>(
				container,
				std::move(title),
				std::move(clear),
				data)));
		const auto shown = (data.count && data.totalSize) || !tag;
		result->toggle(shown, anim::type::instant);
		result->entity()->clearRequests(
		) | rpl::start_with_next([=] {
			clearByTag(tag);
		}, result->lifetime());
		_rows.emplace(tag, result);
		return result;
	};
	auto tracker = Ui::MultiSlideTracker();
	const auto createTagRow = [&](uint8 tag, auto &&titleFactory) {
		static const auto empty = Database::TaggedSummary();
		const auto i = _stats.tagged.find(tag);
		const auto &data = (i != end(_stats.tagged)) ? i->second : empty;
		auto factory = std::forward<decltype(titleFactory)>(titleFactory);
		auto title = [factory = std::move(factory)](size_type count) {
			return factory(tr::now, lt_count, count);
		};
		tracker.track(createRow(
			tag,
			std::move(title),
			tr::lng_local_storage_clear_some(),
			data));
	};
	auto summaryTitle = [](size_type) {
		return tr::lng_local_storage_summary(tr::now);
	};
	auto mediaCacheTitle = [](size_type) {
		return tr::lng_local_storage_media(tr::now);
	};
	createRow(
		0,
		std::move(summaryTitle),
		tr::lng_local_storage_clear(),
		summary());
	setupLimits(container);
	const auto shadow = container->add(object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::PlainShadow>(container),
		st::localStorageRowPadding));
	createTagRow(Data::kImageCacheTag, tr::lng_local_storage_image);
	createTagRow(Data::kStickerCacheTag, tr::lng_local_storage_sticker);
	createTagRow(Data::kVoiceMessageCacheTag, tr::lng_local_storage_voice);
	createTagRow(Data::kVideoMessageCacheTag, tr::lng_local_storage_round);
	createTagRow(Data::kAnimationCacheTag, tr::lng_local_storage_animation);
	tracker.track(createRow(
		kFakeMediaCacheTag,
		std::move(mediaCacheTitle),
		tr::lng_local_storage_clear_some(),
		_statsBig.full));
	shadow->toggleOn(
		std::move(tracker).atLeastOneShownValue()
	);
	container->resizeToWidth(st::boxWidth);
	container->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWidth, height);
	}, container->lifetime());
}

template <
	typename Value,
	typename Convert,
	typename Callback,
	typename>
not_null<Ui::MediaSlider*> LocalStorageBox::createLimitsSlider(
		not_null<Ui::VerticalLayout*> container,
		int valuesCount,
		Convert &&convert,
		Value currentValue,
		Callback &&callback) {
	const auto label = container->add(
		object_ptr<Ui::LabelSimple>(container, st::localStorageLimitLabel),
		st::localStorageLimitLabelMargin);
	callback(label, currentValue);
	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::localStorageLimitSlider),
		st::localStorageLimitMargin);
	slider->resize(st::localStorageLimitSlider.seekSize);
	slider->setPseudoDiscrete(
		valuesCount,
		std::forward<Convert>(convert),
		currentValue,
		[=, callback = std::forward<Callback>(callback)](Value value) {
			callback(label, value);
		});
	return slider;
}

void LocalStorageBox::updateMediaLimit() {
	const auto good = [&](int64 mediaLimit) {
		return (_totalSizeLimit - mediaLimit >= kMinimalSizeLimit);
	};
	if (good(_mediaSizeLimit) || !_mediaSlider || !_mediaLabel) {
		return;
	}
	auto index = 1;
	while ((index < kMediaSizeLimitsCount)
		&& (MediaSizeLimit(index) * 2 <= _totalSizeLimit)) {
		++index;
	}
	--index;
	_mediaSizeLimit = MediaSizeLimit(index);
	_mediaSlider->setValue(index / float64(kMediaSizeLimitsCount - 1));
	updateMediaLabel();

	Ensures(good(_mediaSizeLimit));
}

void LocalStorageBox::updateTotalLimit() {
	const auto good = [&](int64 totalLimit) {
		return (totalLimit - _mediaSizeLimit >= kMinimalSizeLimit);
	};
	if (good(_totalSizeLimit) || !_totalSlider || !_totalLabel) {
		return;
	}
	auto index = kTotalSizeLimitsCount - 1;
	while ((index > 0)
		&& (TotalSizeLimit(index - 1) >= 2 * _mediaSizeLimit)) {
		--index;
	}
	_totalSizeLimit = TotalSizeLimit(index);
	_totalSlider->setValue(index / float64(kTotalSizeLimitsCount - 1));
	updateTotalLabel();

	Ensures(good(_totalSizeLimit));
}

void LocalStorageBox::updateTotalLabel() {
	Expects(_totalLabel != nullptr);

	const auto text = SizeLimitText(_totalSizeLimit);
	_totalLabel->setText(tr::lng_local_storage_size_limit(tr::now, lt_size, text));
}

void LocalStorageBox::updateMediaLabel() {
	Expects(_mediaLabel != nullptr);

	const auto text = SizeLimitText(_mediaSizeLimit);
	_mediaLabel->setText(tr::lng_local_storage_media_limit(tr::now, lt_size, text));
}

void LocalStorageBox::setupLimits(not_null<Ui::VerticalLayout*> container) {
	container->add(
		object_ptr<Ui::PlainShadow>(container),
		st::localStorageRowPadding);

	_totalSlider = createLimitsSlider(
		container,
		kTotalSizeLimitsCount,
		TotalSizeLimit,
		_totalSizeLimit,
		[=](not_null<Ui::LabelSimple*> label, int64 limit) {
			_totalSizeLimit = limit;
			_totalLabel = label;
			updateTotalLabel();
			updateMediaLimit();
			limitsChanged();
		});

	_mediaSlider = createLimitsSlider(
		container,
		kMediaSizeLimitsCount,
		MediaSizeLimit,
		_mediaSizeLimit,
		[=](not_null<Ui::LabelSimple*> label, int64 limit) {
			_mediaSizeLimit = limit;
			_mediaLabel = label;
			updateMediaLabel();
			updateTotalLimit();
			limitsChanged();
		});

	createLimitsSlider(
		container,
		kTimeLimitsCount,
		TimeLimit,
		LimitToValue(_timeLimit),
		[=](not_null<Ui::LabelSimple*> label, size_type limit) {
			_timeLimit = ValueToLimit(limit);
			const auto text = TimeLimitText(_timeLimit);
			label->setText(tr::lng_local_storage_time_limit(tr::now, lt_limit, text));
			limitsChanged();
		});
}

void LocalStorageBox::limitsChanged() {
	const auto &settings = _session->local().cacheSettings();
	const auto &settingsBig = _session->local().cacheBigFileSettings();
	const auto sizeLimit = _totalSizeLimit - _mediaSizeLimit;
	const auto changed = (settings.totalSizeLimit != sizeLimit)
		|| (settingsBig.totalSizeLimit != _mediaSizeLimit)
		|| (settings.totalTimeLimit != _timeLimit)
		|| (settingsBig.totalTimeLimit != _timeLimit);
	if (_limitsChanged != changed) {
		_limitsChanged = changed;
		clearButtons();
		if (_limitsChanged) {
			addButton(tr::lng_settings_save(), [=] { save(); });
			addButton(tr::lng_cancel(), [=] { closeBox(); });
		} else {
			addButton(tr::lng_box_ok(), [=] { closeBox(); });
		}
	}
}

void LocalStorageBox::save() {
	if (!_limitsChanged) {
		closeBox();
		return;
	}
	auto update = Storage::Cache::Database::SettingsUpdate();
	update.totalSizeLimit = _totalSizeLimit - _mediaSizeLimit;
	update.totalTimeLimit = _timeLimit;
	auto updateBig = Storage::Cache::Database::SettingsUpdate();
	updateBig.totalSizeLimit = _mediaSizeLimit;
	updateBig.totalTimeLimit = _timeLimit;
	_session->local().updateCacheSettings(update, updateBig);
	_session->data().cache().updateSettings(update);
	closeBox();
}

void LocalStorageBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setFont(st::boxTextFont);
	p.setPen(st::windowFg);
}
