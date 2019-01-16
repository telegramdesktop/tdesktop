/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/background_box.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "window/themes/window_theme.h"
#include "ui/effects/round_checkbox.h"
#include "ui/image/image.h"
#include "auth_session.h"
#include "data/data_session.h"
#include "styles/style_overview.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kBackgroundsInRow = 3;

} // namespace

class BackgroundBox::Inner
	: public Ui::RpWidget
	, private MTP::Sender
	, private base::Subscriber {
public:
	Inner(QWidget *parent);

	void setBackgroundChosenCallback(Fn<void(int index)> callback) {
		_backgroundChosenCallback = std::move(callback);
	}

	~Inner();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void updateWallpapers();

	Fn<void(int index)> _backgroundChosenCallback;

	int _over = -1;
	int _overDown = -1;

	std::unique_ptr<Ui::RoundCheckbox> _check; // this is not a widget

};

BackgroundBox::BackgroundBox(QWidget*) {
}

void BackgroundBox::prepare() {
	setTitle(langFactory(lng_backgrounds_header));

	addButton(langFactory(lng_close), [=] { closeBox(); });

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);

	_inner = setInnerWidget(object_ptr<Inner>(this), st::backgroundScroll);
	_inner->setBackgroundChosenCallback([=](int index) {
		backgroundChosen(index);
	});
}

void BackgroundBox::backgroundChosen(int index) {
	const auto &papers = Auth().data().wallpapers();
	if (index >= 0 && index < papers.size()) {
		App::main()->setChatBackground(papers[index]);
	}
	closeBox();
}

BackgroundBox::Inner::Inner(QWidget *parent) : RpWidget(parent)
, _check(std::make_unique<Ui::RoundCheckbox>(st::overviewCheck, [=] { update(); })) {
	_check->setChecked(true, Ui::RoundCheckbox::SetStyle::Fast);
	if (Auth().data().wallpapers().empty()) {
		resize(kBackgroundsInRow * (st::backgroundSize.width() + st::backgroundPadding) + st::backgroundPadding, 2 * (st::backgroundSize.height() + st::backgroundPadding) + st::backgroundPadding);
	} else {
		updateWallpapers();
	}
	request(MTPaccount_GetWallPapers(
		MTP_int(Auth().data().wallpapersHash())
	)).done([=](const MTPaccount_WallPapers &result) {
		if (Auth().data().updateWallpapers(result)) {
			updateWallpapers();
		}
	}).send();

	subscribe(Auth().downloaderTaskFinished(), [=] { update(); });
	subscribe(Window::Theme::Background(), [=](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			_check->invalidateCache();
		}
	});
	setMouseTracking(true);
}

void BackgroundBox::Inner::updateWallpapers() {
	const auto &papers = Auth().data().wallpapers();
	const auto count = papers.size();
	const auto rows = (count / kBackgroundsInRow)
		+ (count % kBackgroundsInRow ? 1 : 0);

	resize(kBackgroundsInRow * (st::backgroundSize.width() + st::backgroundPadding) + st::backgroundPadding, rows * (st::backgroundSize.height() + st::backgroundPadding) + st::backgroundPadding);

	const auto preload = kBackgroundsInRow * 3;
	for (const auto &paper : papers | ranges::view::take(preload)) {
		paper.thumb->load(Data::FileOrigin());
	}
}

void BackgroundBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	const auto &papers = Auth().data().wallpapers();
	if (papers.empty()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
		return;
	}
	auto row = 0;
	auto column = 0;
	for (const auto &paper : papers) {
		const auto increment = gsl::finally([&] {
			++column;
			if (column == kBackgroundsInRow) {
				column = 0;
				++row;
			}
		});
		if ((st::backgroundSize.height() + st::backgroundPadding) * (row + 1) <= r.top()) {
			continue;
		}

		paper.thumb->load(Data::FileOrigin());

		int x = st::backgroundPadding + column * (st::backgroundSize.width() + st::backgroundPadding);
		int y = st::backgroundPadding + row * (st::backgroundSize.height() + st::backgroundPadding);

		const auto &pix = paper.thumb->pix(
			Data::FileOrigin(),
			st::backgroundSize.width(),
			st::backgroundSize.height());
		p.drawPixmap(x, y, pix);

		if (paper.id == Window::Theme::Background()->id()) {
			auto checkLeft = x + st::backgroundSize.width() - st::overviewCheckSkip - st::overviewCheck.size;
			auto checkTop = y + st::backgroundSize.height() - st::overviewCheckSkip - st::overviewCheck.size;
			_check->paint(p, getms(), checkLeft, checkTop, width());
		}
	}
}

void BackgroundBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	const auto newOver = [&] {
		const auto x = e->pos().x();
		const auto y = e->pos().y();
		const auto width = st::backgroundSize.width();
		const auto height = st::backgroundSize.height();
		const auto skip = st::backgroundPadding;
		const auto row = int((y - skip) / (height + skip));
		const auto column = int((x - skip) / (width + skip));
		if (y - row * (height + skip) > skip + height) {
			return -1;
		} else if (x - column * (width + skip) > skip + width) {
			return -1;
		}
		const auto result = row * kBackgroundsInRow + column;
		return (result < Auth().data().wallpapers().size()) ? result : -1;
	}();
	if (_over != newOver) {
		_over = newOver;
		setCursor((_over >= 0 || _overDown >= 0)
			? style::cur_pointer
			: style::cur_default);
	}
}

void BackgroundBox::Inner::mousePressEvent(QMouseEvent *e) {
	_overDown = _over;
}

void BackgroundBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	if (_overDown == _over && _over >= 0) {
		if (_backgroundChosenCallback) {
			_backgroundChosenCallback(_over);
		}
	} else if (_over < 0) {
		setCursor(style::cur_default);
	}
}

BackgroundBox::Inner::~Inner() = default;
