/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/local_storage_box.h"

#include "styles/style_boxes.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/effects/radial_animation.h"
#include "storage/localstorage.h"
#include "storage/cache/storage_cache_database.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "layout.h"

namespace {

constexpr auto kSizeLimitsCount = 20;
constexpr auto kTimeLimitsCount = 16;
constexpr auto kMaxTimeLimitValue = std::numeric_limits<size_type>::max();

int64 SizeLimitInMB(int index) {
	if (index < 10) {
		return int64(index + 1) * 100;
	}
	return int64(index - 9) * 1024;
}

int64 SizeLimit(int index) {
	return SizeLimitInMB(index) * 1024 * 1024;
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
		? lng_local_storage_limit_months(lt_count, months)
		: (limit > 0)
		? lng_local_storage_limit_weeks(lt_count, weeks)
		: lang(lng_local_storage_limit_never);
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
		Fn<QString()> clear,
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
	void step_radial(TimeMs ms, bool timer);

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
	Fn<QString()> clear,
	const Database::TaggedSummary &data)
: RpWidget(parent)
, _titleFactory(std::move(title))
, _title(
	this,
	titleText(data),
	Ui::FlatLabel::InitType::Simple,
	st::localStorageRowTitle)
, _description(
	this,
	sizeText(data),
	Ui::FlatLabel::InitType::Simple,
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
			animation(this, &Row::step_radial),
			st::proxyCheckingAnimation);
		_progress->start();
		_clearing = object_ptr<Ui::FlatLabel>(
			this,
			lang(lng_local_storage_clearing),
			Ui::FlatLabel::InitType::Simple,
			st::localStorageRowSize);
		_clearing->show();
		_description->hide();
		resizeToWidth(width());
		RpWidget::update();
	}
}

void LocalStorageBox::Row::step_radial(TimeMs ms, bool timer) {
	if (timer) {
		RpWidget::update();
	}
}

rpl::producer<> LocalStorageBox::Row::clearRequests() const {
	return _clear->clicks();
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
		st::boxLayerButtonPadding.right(),
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
	_progress->step(crl::time());
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
		? formatSizeText(data.totalSize)
		: lang(lng_local_storage_empty);
}

LocalStorageBox::LocalStorageBox(
	QWidget*,
	not_null<Database*> db,
	CreateTag)
: _db(db) {
	const auto &settings = Local::cacheSettings();
	_sizeLimit = settings.totalSizeLimit;
	_timeLimit = settings.totalTimeLimit;
}

void LocalStorageBox::Show(not_null<Database*> db) {
	auto shared = std::make_shared<object_ptr<LocalStorageBox>>(
		Box<LocalStorageBox>(db, CreateTag()));
	const auto weak = shared->data();
	db->statsOnMain(
	) | rpl::start_with_next([=](Database::Stats &&stats) {
		weak->update(std::move(stats));
		if (auto &strong = *shared) {
			Ui::show(std::move(strong));
		}
	}, weak->lifetime());
}

void LocalStorageBox::prepare() {
	setTitle(langFactory(lng_local_storage_title));

	addButton(langFactory(lng_box_ok), [this] { closeBox(); });

	setupControls();
}

void LocalStorageBox::updateRow(
		not_null<Ui::SlideWrap<Row>*> row,
		Database::TaggedSummary *data) {
	const auto summary = (_rows.find(0)->second == row);
	const auto shown = (data && data->count && data->totalSize) || summary;
	if (shown) {
		row->entity()->update(*data);
	}
	row->toggle(shown, anim::type::normal);
}

void LocalStorageBox::update(Database::Stats &&stats) {
	_stats = std::move(stats);
	if (const auto i = _rows.find(0); i != end(_rows)) {
		i->second->entity()->toggleProgress(_stats.clearing);
	}
	for (const auto &entry : _rows) {
		if (entry.first) {
			const auto i = _stats.tagged.find(entry.first);
			updateRow(
				entry.second,
				(i != end(_stats.tagged)) ? &i->second : nullptr);
		} else {
			updateRow(entry.second, &_stats.full);
		}
	}
}

void LocalStorageBox::clearByTag(uint8 tag) {
	if (tag) {
		_db->clearByTag(tag);
	} else {
		_db->clear();
	}
}

void LocalStorageBox::setupControls() {
	const auto container = setInnerWidget(
		object_ptr<Ui::VerticalLayout>(this));
	const auto createRow = [&](
			uint8 tag,
			Fn<QString(size_type)> title,
			Fn<QString()> clear,
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
			return factory(lt_count, count);
		};
		tracker.track(createRow(
			tag,
			std::move(title),
			langFactory(lng_local_storage_clear_some),
			data));
	};
	auto summaryTitle = [](size_type) {
		return lang(lng_local_storage_summary);
	};
	createRow(
		0,
		std::move(summaryTitle),
		langFactory(lng_local_storage_clear),
		_stats.full);
	setupLimits(container);
	const auto shadow = container->add(object_ptr<Ui::SlideWrap<>>(
		container,
		object_ptr<Ui::PlainShadow>(container),
		st::localStorageRowPadding));
	createTagRow(Data::kImageCacheTag, lng_local_storage_image);
	createTagRow(Data::kStickerCacheTag, lng_local_storage_sticker);
	createTagRow(Data::kVoiceMessageCacheTag, lng_local_storage_voice);
	createTagRow(Data::kVideoMessageCacheTag, lng_local_storage_round);
	createTagRow(Data::kAnimationCacheTag, lng_local_storage_animation);
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
void LocalStorageBox::createLimitsSlider(
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
}

void LocalStorageBox::setupLimits(not_null<Ui::VerticalLayout*> container) {
	const auto shadow = container->add(
		object_ptr<Ui::PlainShadow>(container),
		st::localStorageRowPadding);

	createLimitsSlider(
		container,
		kSizeLimitsCount,
		SizeLimit,
		_sizeLimit,
		[=](not_null<Ui::LabelSimple*> label, int64 limit) {
			const auto text = SizeLimitText(limit);
			label->setText(lng_local_storage_size_limit(lt_size, text));
			_sizeLimit = limit;
			limitsChanged();
		});

	createLimitsSlider(
		container,
		kTimeLimitsCount,
		TimeLimit,
		LimitToValue(_timeLimit),
		[=](not_null<Ui::LabelSimple*> label, size_type limit) {
			const auto text = TimeLimitText(ValueToLimit(limit));
			label->setText(lng_local_storage_time_limit(lt_limit, text));
			_timeLimit = limit;
			limitsChanged();
		});
}

void LocalStorageBox::limitsChanged() {
	const auto &settings = Local::cacheSettings();
	const auto changed = (settings.totalSizeLimit != _sizeLimit)
		|| (settings.totalTimeLimit != _timeLimit);
	if (_limitsChanged != changed) {
		_limitsChanged = changed;
		clearButtons();
		if (_limitsChanged) {
			addButton(langFactory(lng_settings_save), [=] { save(); });
			addButton(langFactory(lng_cancel), [=] { closeBox(); });
		} else {
			addButton(langFactory(lng_box_ok), [=] { closeBox(); });
		}
	}
}

void LocalStorageBox::save() {
	if (!_limitsChanged) {
		closeBox();
		return;
	}
	auto update = Storage::Cache::Database::SettingsUpdate();
	update.totalSizeLimit = _sizeLimit;
	update.totalTimeLimit = _timeLimit;
	Local::updateCacheSettings(update);
	Auth().data().cache().updateSettings(update);
	closeBox();
}

void LocalStorageBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setFont(st::boxTextFont);
	p.setPen(st::windowFg);
}
