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

	int _bgCount = 0;
	int _rows = 0;
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
	if (index >= 0 && index < Auth().data().wallpapersCount()) {
		const auto &paper = Auth().data().wallpaper(index);
		App::main()->setChatBackground(paper);

		using Update = Window::Theme::BackgroundUpdate;
		Window::Theme::Background()->notify(Update(Update::Type::Start, !paper.id));
	}
	closeBox();
}

BackgroundBox::Inner::Inner(QWidget *parent) : RpWidget(parent)
, _check(std::make_unique<Ui::RoundCheckbox>(st::overviewCheck, [=] { update(); })) {
	_check->setChecked(true, Ui::RoundCheckbox::SetStyle::Fast);
	if (!Auth().data().wallpapersCount()) {
		resize(BackgroundsInRow * (st::backgroundSize.width() + st::backgroundPadding) + st::backgroundPadding, 2 * (st::backgroundSize.height() + st::backgroundPadding) + st::backgroundPadding);
		request(MTPaccount_GetWallPapers(
			MTP_int(0)
		)).done([=](const MTPaccount_WallPapers &result) {
			result.match([&](const MTPDaccount_wallPapers &data) {
				Auth().data().setWallpapers(data.vwallpapers.v);
				updateWallpapers();
			}, [&](const MTPDaccount_wallPapersNotModified &) {
				LOG(("API Error: account.wallPapersNotModified received."));
			});
		}).send();
	} else {
		updateWallpapers();
	}

	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });
	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			_check->invalidateCache();
		}
	});
	setMouseTracking(true);
}

void BackgroundBox::Inner::updateWallpapers() {
	_bgCount = Auth().data().wallpapersCount();
	_rows = _bgCount / BackgroundsInRow;
	if (_bgCount % BackgroundsInRow) ++_rows;

	resize(BackgroundsInRow * (st::backgroundSize.width() + st::backgroundPadding) + st::backgroundPadding, _rows * (st::backgroundSize.height() + st::backgroundPadding) + st::backgroundPadding);
	for (int i = 0; i < BackgroundsInRow * 3; ++i) {
		if (i >= _bgCount) break;

		Auth().data().wallpaper(i).thumb->load(Data::FileOrigin());
	}
}

void BackgroundBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	if (_rows) {
		for (int i = 0; i < _rows; ++i) {
			if ((st::backgroundSize.height() + st::backgroundPadding) * (i + 1) <= r.top()) continue;
			for (int j = 0; j < BackgroundsInRow; ++j) {
				int index = i * BackgroundsInRow + j;
				if (index >= _bgCount) break;

				const auto &paper = Auth().data().wallpaper(index);
				paper.thumb->load(Data::FileOrigin());

				int x = st::backgroundPadding + j * (st::backgroundSize.width() + st::backgroundPadding);
				int y = st::backgroundPadding + i * (st::backgroundSize.height() + st::backgroundPadding);

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
	} else {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
	}
}

void BackgroundBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	int x = e->pos().x(), y = e->pos().y();
	int row = int((y - st::backgroundPadding) / (st::backgroundSize.height() + st::backgroundPadding));
	if (y - row * (st::backgroundSize.height() + st::backgroundPadding) > st::backgroundPadding + st::backgroundSize.height()) row = _rows + 1;

	int col = int((x - st::backgroundPadding) / (st::backgroundSize.width() + st::backgroundPadding));
	if (x - col * (st::backgroundSize.width() + st::backgroundPadding) > st::backgroundPadding + st::backgroundSize.width()) row = _rows + 1;

	int newOver = row * BackgroundsInRow + col;
	if (newOver >= _bgCount) newOver = -1;
	if (newOver != _over) {
		_over = newOver;
		setCursor((_over >= 0 || _overDown >= 0) ? style::cur_pointer : style::cur_default);
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
