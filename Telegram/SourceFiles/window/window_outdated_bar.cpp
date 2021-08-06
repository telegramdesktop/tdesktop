/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_outdated_bar.h"

#include "ui/widgets/labels.h" // Ui::FlatLabel
#include "ui/widgets/buttons.h" // Ui::IconButton
#include "ui/wrap/slide_wrap.h" // Ui::SlideWrap
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "base/platform/base_platform_info.h"
#include "lang/lang_keys.h"
#include "styles/style_window.h"

namespace Window {
namespace {

#ifdef DESKTOP_APP_SPECIAL_TARGET
constexpr auto kMinimalSkip = 7;
constexpr auto kSoonSkip = 30;
constexpr auto kNowSkip = 90;

class Bar : public Ui::RpWidget {
public:
	Bar(not_null<QWidget*> parent, QDate date);

	int resizeGetHeight(int newWidth) override;

	rpl::producer<> hideClicks() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QDate _date;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _details;
	object_ptr<Ui::IconButton> _close;
	bool _soon = false;

};

[[nodiscard]] rpl::producer<QString> OutdatedReasonPhrase() {
	const auto why = Platform::WhySystemBecomesOutdated();
	return (why == Platform::OutdateReason::Is32Bit)
		? tr::lng_outdated_title_bits()
		: tr::lng_outdated_title();
}

Bar::Bar(not_null<QWidget*> parent, QDate date)
: _date(date)
, _title(
	this,
	OutdatedReasonPhrase() | Ui::Text::ToUpper(),
	st::windowOutdatedTitle)
, _details(this,
	QString(),
	st::windowOutdatedDetails)
, _close(this, st::windowOutdatedClose)
, _soon(_date >= QDate::currentDate()) {
	_title->setTryMakeSimilarLines(true);
	_details->setTryMakeSimilarLines(true);
	_details->setText(_soon
		? tr::lng_outdated_soon(tr::now, lt_date, langDayOfMonthFull(date))
		: tr::lng_outdated_now(tr::now));
}

rpl::producer<> Bar::hideClicks() const {
	return _close->clicks() | rpl::to_empty;
}

int Bar::resizeGetHeight(int newWidth) {
	const auto padding = st::windowOutdatedPadding;
	const auto skip = _close->width();
	const auto available = newWidth - 2 * skip;

	_title->resizeToWidth(available);
	_title->moveToLeft(skip, padding.top(), newWidth);

	_details->resizeToWidth(available);
	_details->moveToLeft(
		skip,
		_title->y() + _title->height() + st::windowOutdatedSkip,
		newWidth);

	_close->moveToRight(0, 0, newWidth);

	return _details->y() + _details->height() + padding.bottom();
}

void Bar::paintEvent(QPaintEvent *e) {
	QPainter(this).fillRect(
		e->rect(),
		_soon ? st::outdateSoonBg : st::outdatedBg);
}

QString LastHiddenPath() {
	return cWorkingDir() + qsl("tdata/outdated_hidden");
}

[[nodiscard]] bool Skip(const QDate &date) {
	auto file = QFile(LastHiddenPath());
	if (!file.open(QIODevice::ReadOnly) || file.size() != sizeof(qint32)) {
		return false;
	}
	const auto content = file.readAll();
	if (content.size() != sizeof(qint32)) {
		return false;
	}
	const auto value = *reinterpret_cast<const qint32*>(content.constData());
	const auto year = (value / 10000);
	const auto month = (value % 10000) / 100;
	const auto day = (value % 100);
	const auto last = QDate(year, month, day);
	if (!last.isValid()) {
		return false;
	}
	const auto today = QDate::currentDate();
	if (last > today) {
		return false;
	}
	const auto skipped = last.daysTo(today);
	if (today > date && last <= date) {
		return (skipped < kMinimalSkip);
	} else if (today <= date) {
		return (skipped < kSoonSkip);
	} else {
		return (skipped < kNowSkip);
	}
}

void Closed() {
	auto file = QFile(LastHiddenPath());
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	const auto today = QDate::currentDate();
	const auto value = qint32(0
		+ today.year() * 10000
		+ today.month() * 100
		+ today.day());
	file.write(QByteArray::fromRawData(
		reinterpret_cast<const char*>(&value),
		sizeof(qint32)));
}
#endif // DESKTOP_APP_SPECIAL_TARGET

} // namespace

object_ptr<Ui::RpWidget> CreateOutdatedBar(not_null<QWidget*> parent) {
#ifdef DESKTOP_APP_SPECIAL_TARGET
	const auto date = Platform::WhenSystemBecomesOutdated();
	if (date.isNull()) {
		return { nullptr };
	} else if (Skip(date)) {
		return { nullptr };
	}

	auto result = object_ptr<Ui::SlideWrap<Bar>>(
		parent.get(),
		object_ptr<Bar>(parent.get(), date));
	const auto wrap = result.data();

	wrap->entity()->hideClicks(
	) | rpl::start_with_next([=] {
		wrap->toggle(false, anim::type::normal);
		Closed();
	}, wrap->lifetime());

	return result;
#else // DESKTOP_APP_SPECIAL_TARGET
	return { nullptr };
#endif // DESKTOP_APP_SPECIAL_TARGET
}

} // namespace Window
