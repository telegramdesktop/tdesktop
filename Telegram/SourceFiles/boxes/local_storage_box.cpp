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
#include "ui/effects/radial_animation.h"
#include "ui/widgets/shadow.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "layout.h"

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
		st::boxButtonPadding.right(),
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
	_content.create(this);

	const auto createRow = [&](
			uint8 tag,
			Fn<QString(size_type)> title,
			Fn<QString()> clear,
			const Database::TaggedSummary &data) {
		auto result = _content->add(object_ptr<Ui::SlideWrap<Row>>(
			_content,
			object_ptr<Row>(
				_content,
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
	const auto shadow = _content->add(object_ptr<Ui::SlideWrap<>>(
		_content,
		object_ptr<Ui::PlainShadow>(_content),
		st::localStorageRowPadding)
	);
	createTagRow(Data::kImageCacheTag, lng_local_storage_image);
	createTagRow(Data::kStickerCacheTag, lng_local_storage_sticker);
	createTagRow(Data::kVoiceMessageCacheTag, lng_local_storage_voice);
	createTagRow(Data::kVideoMessageCacheTag, lng_local_storage_round);
	createTagRow(Data::kAnimationCacheTag, lng_local_storage_animation);
	shadow->toggleOn(
		std::move(tracker).atLeastOneShownValue()
	);
	_content->resizeToWidth(st::boxWidth);
	_content->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWidth, height);
	}, _content->lifetime());
}

void LocalStorageBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setFont(st::boxTextFont);
	p.setPen(st::windowFg);
}
