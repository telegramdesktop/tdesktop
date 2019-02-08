/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/background_box.h"

#include "lang/lang_keys.h"
#include "ui/effects/round_checkbox.h"
#include "ui/image/image.h"
#include "auth_session.h"
#include "mtproto/sender.h"
#include "data/data_session.h"
#include "boxes/background_preview_box.h"
#include "styles/style_overview.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kBackgroundsInRow = 3;

QImage TakeMiddleSample(QImage original, QSize size) {
	size *= cIntRetinaFactor();
	const auto from = original.size();
	if (from.isEmpty()) {
		auto result = original.scaled(size);
		result.setDevicePixelRatio(cRetinaFactor());
		return result;
	}

	const auto take = (from.width() * size.height()
		> from.height() * size.width())
		? QSize(size.width() * from.height() / size.height(), from.height())
		: QSize(from.width(), size.height() * from.width() / size.width());
	auto result = original.copy(
		(from.width() - take.width()) / 2,
		(from.height() - take.height()) / 2,
		take.width(),
		take.height()
	).scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	result.setDevicePixelRatio(cRetinaFactor());
	return result;
}

} // namespace

class BackgroundBox::Inner
	: public Ui::RpWidget
	, private MTP::Sender
	, private base::Subscriber {
public:
	Inner(QWidget *parent);

	void setBackgroundChosenCallback(
		Fn<void(const Data::WallPaper &)> callback);

	~Inner();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct Paper {
		Data::WallPaper data;
		mutable QPixmap thumbnail;
	};
	void updatePapers();
	void sortPapers();
	void paintPaper(
		Painter &p,
		const Paper &paper,
		int column,
		int row) const;
	void validatePaperThumbnail(const Paper &paper) const;

	Fn<void(const Data::WallPaper &)> _backgroundChosenCallback;
	std::vector<Paper> _papers;

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
	_inner->setBackgroundChosenCallback([](const Data::WallPaper &paper) {
		Ui::show(
			Box<BackgroundPreviewBox>(paper),
			LayerOption::KeepOther);
	});
}

BackgroundBox::Inner::Inner(QWidget *parent) : RpWidget(parent)
, _check(std::make_unique<Ui::RoundCheckbox>(st::overviewCheck, [=] { update(); })) {
	_check->setChecked(true, Ui::RoundCheckbox::SetStyle::Fast);
	if (Auth().data().wallpapers().empty()) {
		resize(st::boxWideWidth, 2 * (st::backgroundSize.height() + st::backgroundPadding) + st::backgroundPadding);
	} else {
		updatePapers();
	}
	request(MTPaccount_GetWallPapers(
		MTP_int(Auth().data().wallpapersHash())
	)).done([=](const MTPaccount_WallPapers &result) {
		if (Auth().data().updateWallpapers(result)) {
			updatePapers();
		}
	}).send();

	subscribe(Auth().downloaderTaskFinished(), [=] { update(); });
	using Update = Window::Theme::BackgroundUpdate;
	subscribe(Window::Theme::Background(), [=](const Update &update) {
		if (update.paletteChanged()) {
			_check->invalidateCache();
		} else if (update.type == Update::Type::New) {
			sortPapers();
			this->update();
		}
	});
	setMouseTracking(true);
}

void BackgroundBox::Inner::setBackgroundChosenCallback(
		Fn<void(const Data::WallPaper &)> callback) {
	_backgroundChosenCallback = std::move(callback);
}

void BackgroundBox::Inner::sortPapers() {
	const auto current = Window::Theme::Background()->id();
	const auto night = Window::Theme::IsNightMode();
	ranges::stable_sort(_papers, std::greater<>(), [&](const Paper &paper) {
		const auto &data = paper.data;
		return std::make_tuple(
			data.id() == current,
			night ? data.isDark() : !data.isDark(),
			!data.isDefault() && !data.isLocal(),
			!data.isDefault() && data.isLocal());
	});
	if (!_papers.empty() && _papers.front().data.id() == current) {
		_papers.front().data = _papers.front().data.withParamsFrom(
			Window::Theme::Background()->paper());
	}
}

void BackgroundBox::Inner::updatePapers() {
	_papers = Auth().data().wallpapers(
	) | ranges::view::filter([](const Data::WallPaper &paper) {
		return !paper.isPattern() || paper.backgroundColor().has_value();
	}) | ranges::view::transform([](const Data::WallPaper &paper) {
		return Paper{ paper };
	}) | ranges::to_vector;
	sortPapers();
	const auto count = _papers.size();
	const auto rows = (count / kBackgroundsInRow)
		+ (count % kBackgroundsInRow ? 1 : 0);

	resize(st::boxWideWidth, rows * (st::backgroundSize.height() + st::backgroundPadding) + st::backgroundPadding);

	const auto preload = kBackgroundsInRow * 3;
	for (const auto &paper : _papers | ranges::view::take(preload)) {
		paper.data.loadThumbnail();
	}
}

void BackgroundBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	if (_papers.empty()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
		return;
	}
	auto row = 0;
	auto column = 0;
	for (const auto &paper : _papers) {
		const auto increment = gsl::finally([&] {
			++column;
			if (column == kBackgroundsInRow) {
				column = 0;
				++row;
			}
		});
		if ((st::backgroundSize.height() + st::backgroundPadding) * (row + 1) <= r.top()) {
			continue;
		} else if ((st::backgroundSize.height() + st::backgroundPadding) * row >= r.top() + r.height()) {
			break;
		}
		paintPaper(p, paper, column, row);
	}
}

void BackgroundBox::Inner::validatePaperThumbnail(
		const Paper &paper) const {
	Expects(paper.data.thumbnail() != nullptr);

	const auto thumbnail = paper.data.thumbnail();
	if (!paper.thumbnail.isNull()) {
		return;
	} else if (!thumbnail->loaded()) {
		thumbnail->load(paper.data.fileOrigin());
		return;
	}
	auto original = thumbnail->original();
	if (paper.data.isPattern()) {
		const auto color = *paper.data.backgroundColor();
		original = Data::PreparePatternImage(
			std::move(original),
			color,
			Data::PatternColor(color),
			paper.data.patternIntensity());
	}
	paper.thumbnail = App::pixmapFromImageInPlace(TakeMiddleSample(
		original,
		st::backgroundSize));
	paper.thumbnail.setDevicePixelRatio(cRetinaFactor());
}

void BackgroundBox::Inner::paintPaper(
		Painter &p,
		const Paper &paper,
		int column,
		int row) const {
	const auto x = st::backgroundPadding + column * (st::backgroundSize.width() + st::backgroundPadding);
	const auto y = st::backgroundPadding + row * (st::backgroundSize.height() + st::backgroundPadding);
	validatePaperThumbnail(paper);
	if (!paper.thumbnail.isNull()) {
		p.drawPixmap(x, y, paper.thumbnail);
	}

	if (paper.data.id() == Window::Theme::Background()->id()) {
		const auto checkLeft = x + st::backgroundSize.width() - st::overviewCheckSkip - st::overviewCheck.size;
		const auto checkTop = y + st::backgroundSize.height() - st::overviewCheckSkip - st::overviewCheck.size;
		_check->paint(p, getms(), checkLeft, checkTop, width());
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
		return (result < _papers.size()) ? result : -1;
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
	if (_overDown == _over && _over >= 0 && _over < _papers.size()) {
		if (_backgroundChosenCallback) {
			_backgroundChosenCallback(_papers[_over].data);
		}
	} else if (_over < 0) {
		setCursor(style::cur_default);
	}
}

BackgroundBox::Inner::~Inner() = default;
