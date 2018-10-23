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
#include "styles/style_overview.h"
#include "styles/style_boxes.h"

class BackgroundBox::Inner : public TWidget, public RPCSender, private base::Subscriber {
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
	void gotWallpapers(const MTPVector<MTPWallPaper> &result);
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

	addButton(langFactory(lng_close), [this] { closeBox(); });

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);

	_inner = setInnerWidget(object_ptr<Inner>(this), st::backgroundScroll);
	_inner->setBackgroundChosenCallback([this](int index) { backgroundChosen(index); });
}

void BackgroundBox::backgroundChosen(int index) {
	if (index >= 0 && index < App::cServerBackgrounds().size()) {
		auto &paper = App::cServerBackgrounds()[index];
		if (App::main()) App::main()->setChatBackground(paper);

		using Update = Window::Theme::BackgroundUpdate;
		Window::Theme::Background()->notify(Update(Update::Type::Start, !paper.id));
	}
	closeBox();
}

BackgroundBox::Inner::Inner(QWidget *parent) : TWidget(parent)
, _check(std::make_unique<Ui::RoundCheckbox>(st::overviewCheck, [this] { update(); })) {
	_check->setChecked(true, Ui::RoundCheckbox::SetStyle::Fast);
	if (App::cServerBackgrounds().isEmpty()) {
		resize(BackgroundsInRow * (st::backgroundSize.width() + st::backgroundPadding) + st::backgroundPadding, 2 * (st::backgroundSize.height() + st::backgroundPadding) + st::backgroundPadding);
		MTP::send(MTPaccount_GetWallPapers(), rpcDone(&Inner::gotWallpapers));
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

void BackgroundBox::Inner::gotWallpapers(const MTPVector<MTPWallPaper> &result) {
	App::WallPapers wallpapers;

	auto oldBackground = Images::Create(qsl(":/gui/art/bg_initial.jpg"), "JPG");
	wallpapers.push_back(App::WallPaper(Window::Theme::kInitialBackground, oldBackground, oldBackground));
	auto &v = result.v;
	for_const (auto &w, v) {
		switch (w.type()) {
		case mtpc_wallPaper: {
			auto &d = w.c_wallPaper();
			auto &sizes = d.vsizes.v;
			const MTPPhotoSize *thumb = 0, *full = 0;
			int32 thumbLevel = -1, fullLevel = -1;
			for (QVector<MTPPhotoSize>::const_iterator j = sizes.cbegin(), e = sizes.cend(); j != e; ++j) {
				char size = 0;
				int32 w = 0, h = 0;
				switch (j->type()) {
				case mtpc_photoSize: {
					auto &s = j->c_photoSize().vtype.v;
					if (s.size()) size = s[0];
					w = j->c_photoSize().vw.v;
					h = j->c_photoSize().vh.v;
				} break;

				case mtpc_photoCachedSize: {
					auto &s = j->c_photoCachedSize().vtype.v;
					if (s.size()) size = s[0];
					w = j->c_photoCachedSize().vw.v;
					h = j->c_photoCachedSize().vh.v;
				} break;
				}
				if (!size || !w || !h) continue;

				int32 newThumbLevel = qAbs((st::backgroundSize.width() * cIntRetinaFactor()) - w), newFullLevel = qAbs(2560 - w);
				if (thumbLevel < 0 || newThumbLevel < thumbLevel) {
					thumbLevel = newThumbLevel;
					thumb = &(*j);
				}
				if (fullLevel < 0 || newFullLevel < fullLevel) {
					fullLevel = newFullLevel;
					full = &(*j);
				}
			}
			if (thumb && full && full->type() != mtpc_photoSizeEmpty) {
				wallpapers.push_back(App::WallPaper(d.vid.v ? d.vid.v : INT_MAX, App::image(*thumb), App::image(*full)));
			}
		} break;

		case mtpc_wallPaperSolid: {
			auto &d = w.c_wallPaperSolid();
		} break;
		}
	}

	App::cSetServerBackgrounds(wallpapers);
	updateWallpapers();
}

void BackgroundBox::Inner::updateWallpapers() {
	_bgCount = App::cServerBackgrounds().size();
	_rows = _bgCount / BackgroundsInRow;
	if (_bgCount % BackgroundsInRow) ++_rows;

	resize(BackgroundsInRow * (st::backgroundSize.width() + st::backgroundPadding) + st::backgroundPadding, _rows * (st::backgroundSize.height() + st::backgroundPadding) + st::backgroundPadding);
	for (int i = 0; i < BackgroundsInRow * 3; ++i) {
		if (i >= _bgCount) break;

		App::cServerBackgrounds()[i].thumb->load(Data::FileOrigin());
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

				const auto &paper = App::cServerBackgrounds()[index];
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
