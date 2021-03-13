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
#include "ui/ui_utility.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "mtproto/sender.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "boxes/background_preview_box.h"
#include "boxes/confirm_box.h"
#include "window/window_session_controller.h"
#include "app.h"
#include "styles/style_overview.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

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

class BackgroundBox::Inner final
	: public Ui::RpWidget
	, private base::Subscriber {
public:
	Inner(
		QWidget *parent,
		not_null<Main::Session*> session);
	~Inner();

	rpl::producer<Data::WallPaper> chooseEvents() const;
	rpl::producer<Data::WallPaper> removeRequests() const;

	void removePaper(const Data::WallPaper &data);

private:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	struct Paper {
		Data::WallPaper data;
		mutable std::shared_ptr<Data::DocumentMedia> dataMedia;
		mutable QPixmap thumbnail;
	};
	struct Selected {
		int index = 0;
		inline bool operator==(const Selected &other) const {
			return index == other.index;
		}
		inline bool operator!=(const Selected &other) const {
			return !(*this == other);
		}
	};
	struct DeleteSelected {
		int index = 0;
		inline bool operator==(const DeleteSelected &other) const {
			return index == other.index;
		}
		inline bool operator!=(const DeleteSelected &other) const {
			return !(*this == other);
		}
	};
	using Selection = std::variant<v::null_t, Selected, DeleteSelected>;

	int getSelectionIndex(const Selection &selection) const;
	void repaintPaper(int index);
	void resizeToContentAndPreload();
	void updatePapers();
	void requestPapers();
	void sortPapers();
	void paintPaper(
		Painter &p,
		const Paper &paper,
		int column,
		int row) const;
	void validatePaperThumbnail(const Paper &paper) const;

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	std::vector<Paper> _papers;

	Selection _over;
	Selection _overDown;

	std::unique_ptr<Ui::RoundCheckbox> _check; // this is not a widget
	rpl::event_stream<Data::WallPaper> _backgroundChosen;
	rpl::event_stream<Data::WallPaper> _backgroundRemove;

};

BackgroundBox::BackgroundBox(
	QWidget*,
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

void BackgroundBox::prepare() {
	setTitle(tr::lng_backgrounds_header());

	addButton(tr::lng_close(), [=] { closeBox(); });

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);

	_inner = setInnerWidget(
		object_ptr<Inner>(this, &_controller->session()),
		st::backgroundScroll);

	_inner->chooseEvents(
	) | rpl::start_with_next([=](const Data::WallPaper &paper) {
		Ui::show(
			Box<BackgroundPreviewBox>(_controller, paper),
			Ui::LayerOption::KeepOther);
	}, _inner->lifetime());

	_inner->removeRequests(
	) | rpl::start_with_next([=](const Data::WallPaper &paper) {
		removePaper(paper);
	}, _inner->lifetime());
}

void BackgroundBox::removePaper(const Data::WallPaper &paper) {
	const auto session = &_controller->session();
	const auto remove = [=, weak = Ui::MakeWeak(this)](Fn<void()> &&close) {
		close();
		if (weak) {
			weak->_inner->removePaper(paper);
		}
		session->data().removeWallpaper(paper);
		session->api().request(MTPaccount_SaveWallPaper(
			paper.mtpInput(session),
			MTP_bool(true),
			paper.mtpSettings()
		)).send();
	};
	Ui::show(
		Box<ConfirmBox>(
			tr::lng_background_sure_delete(tr::now),
			tr::lng_selected_delete(tr::now),
			tr::lng_cancel(tr::now),
			remove),
		Ui::LayerOption::KeepOther);
}

BackgroundBox::Inner::Inner(
	QWidget *parent,
	not_null<Main::Session*> session)
: RpWidget(parent)
, _session(session)
, _api(&_session->mtp())
, _check(std::make_unique<Ui::RoundCheckbox>(st::overviewCheck, [=] { update(); })) {
	_check->setChecked(true, anim::type::instant);
	if (_session->data().wallpapers().empty()) {
		resize(st::boxWideWidth, 2 * (st::backgroundSize.height() + st::backgroundPadding) + st::backgroundPadding);
	} else {
		updatePapers();
	}
	requestPapers();

	_session->downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());
	using Update = Window::Theme::BackgroundUpdate;
	subscribe(Window::Theme::Background(), [=](const Update &update) {
		if (update.paletteChanged()) {
			_check->invalidateCache();
		} else if (update.type == Update::Type::New) {
			sortPapers();
			requestPapers();
			this->update();
		}
	});
	setMouseTracking(true);
}

void BackgroundBox::Inner::requestPapers() {
	_api.request(MTPaccount_GetWallPapers(
		MTP_int(_session->data().wallpapersHash())
	)).done([=](const MTPaccount_WallPapers &result) {
		if (_session->data().updateWallpapers(result)) {
			updatePapers();
		}
	}).send();
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
	_over = _overDown = Selection();

	_papers = _session->data().wallpapers(
	) | ranges::views::filter([](const Data::WallPaper &paper) {
		return !paper.isPattern() || paper.backgroundColor().has_value();
	}) | ranges::views::transform([](const Data::WallPaper &paper) {
		return Paper{ paper };
	}) | ranges::to_vector;
	sortPapers();
	resizeToContentAndPreload();
}

void BackgroundBox::Inner::resizeToContentAndPreload() {
	const auto count = _papers.size();
	const auto rows = (count / kBackgroundsInRow)
		+ (count % kBackgroundsInRow ? 1 : 0);

	resize(
		st::boxWideWidth,
		(rows * (st::backgroundSize.height() + st::backgroundPadding)
			+ st::backgroundPadding));

	const auto preload = kBackgroundsInRow * 3;
	for (const auto &paper : _papers | ranges::views::take(preload)) {
		if (!paper.data.localThumbnail() && !paper.dataMedia) {
			if (const auto document = paper.data.document()) {
				paper.dataMedia = document->createMediaView();
				paper.dataMedia->thumbnailWanted(paper.data.fileOrigin());
			}
		}
	}
	update();
}

void BackgroundBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	if (_papers.empty()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), tr::lng_contacts_loading(tr::now), style::al_center);
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
	if (!paper.thumbnail.isNull()) {
		return;
	}
	const auto localThumbnail = paper.data.localThumbnail();
	if (!localThumbnail) {
		if (const auto document = paper.data.document()) {
			if (!paper.dataMedia) {
				paper.dataMedia = document->createMediaView();
				paper.dataMedia->thumbnailWanted(paper.data.fileOrigin());
			}
		}
		if (!paper.dataMedia || !paper.dataMedia->thumbnail()) {
			return;
		}
	}
	const auto thumbnail = localThumbnail
		? localThumbnail
		: paper.dataMedia->thumbnail();
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

	const auto over = !v::is_null(_overDown) ? _overDown : _over;
	if (paper.data.id() == Window::Theme::Background()->id()) {
		const auto checkLeft = x + st::backgroundSize.width() - st::overviewCheckSkip - st::overviewCheck.size;
		const auto checkTop = y + st::backgroundSize.height() - st::overviewCheckSkip - st::overviewCheck.size;
		_check->paint(p, checkLeft, checkTop, width());
	} else if (Data::IsCloudWallPaper(paper.data)
		&& !Data::IsDefaultWallPaper(paper.data)
		&& !v::is_null(over)
		&& (&paper == &_papers[getSelectionIndex(over)])) {
		const auto deleteSelected = v::is<DeleteSelected>(over);
		const auto deletePos = QPoint(x + st::backgroundSize.width() - st::stickerPanDeleteIconBg.width(), y);
		p.setOpacity(deleteSelected ? st::stickerPanDeleteOpacityBgOver : st::stickerPanDeleteOpacityBg);
		st::stickerPanDeleteIconBg.paint(p, deletePos, width());
		p.setOpacity(deleteSelected ? st::stickerPanDeleteOpacityFgOver : st::stickerPanDeleteOpacityFg);
		st::stickerPanDeleteIconFg.paint(p, deletePos, width());
		p.setOpacity(1.);
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
		const auto result = row * kBackgroundsInRow + column;
		if (y - row * (height + skip) > skip + height) {
			return Selection();
		} else if (x - column * (width + skip) > skip + width) {
			return Selection();
		} else if (result >= _papers.size()) {
			return Selection();
		}
		const auto deleteLeft = (column + 1) * (width + skip)
			- st::stickerPanDeleteIconBg.width();
		const auto deleteBottom = row * (height + skip) + skip
			+ st::stickerPanDeleteIconBg.height();
		const auto currentId = Window::Theme::Background()->id();
		const auto inDelete = (x >= deleteLeft)
			&& (y < deleteBottom)
			&& Data::IsCloudWallPaper(_papers[result].data)
			&& !Data::IsDefaultWallPaper(_papers[result].data)
			&& (currentId != _papers[result].data.id());
		return (result >= _papers.size())
			? Selection()
			: inDelete
			? Selection(DeleteSelected{ result })
			: Selection(Selected{ result });
	}();
	if (_over != newOver) {
		repaintPaper(getSelectionIndex(_over));
		_over = newOver;
		repaintPaper(getSelectionIndex(_over));
		setCursor((!v::is_null(_over) || !v::is_null(_overDown))
			? style::cur_pointer
			: style::cur_default);
	}
}

void BackgroundBox::Inner::repaintPaper(int index) {
	if (index < 0 || index >= _papers.size()) {
		return;
	}
	const auto row = (index / kBackgroundsInRow);
	const auto column = (index % kBackgroundsInRow);
	const auto width = st::backgroundSize.width();
	const auto height = st::backgroundSize.height();
	const auto skip = st::backgroundPadding;
	update(
		(width + skip) * column + skip,
		(height + skip) * row + skip,
		width,
		height);
}

void BackgroundBox::Inner::mousePressEvent(QMouseEvent *e) {
	_overDown = _over;
}

int BackgroundBox::Inner::getSelectionIndex(
		const Selection &selection) const {
	return v::match(selection, [](const Selected &data) {
		return data.index;
	}, [](const DeleteSelected &data) {
		return data.index;
	}, [](v::null_t) {
		return -1;
	});
}

void BackgroundBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	if (base::take(_overDown) == _over && !v::is_null(_over)) {
		const auto index = getSelectionIndex(_over);
		if (index >= 0 && index < _papers.size()) {
			if (std::get_if<DeleteSelected>(&_over)) {
				_backgroundRemove.fire_copy(_papers[index].data);
			} else if (std::get_if<Selected>(&_over)) {
				auto &paper = _papers[index];
				if (!paper.dataMedia) {
					if (const auto document = paper.data.document()) {
						// Keep it alive while it is on the screen.
						paper.dataMedia = document->createMediaView();
					}
				}
				_backgroundChosen.fire_copy(paper.data);
			}
		}
	} else if (v::is_null(_over)) {
		setCursor(style::cur_default);
	}
}

void BackgroundBox::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	for (auto i = 0, count = int(_papers.size()); i != count; ++i) {
		const auto row = (i / kBackgroundsInRow);
		const auto height = st::backgroundSize.height();
		const auto skip = st::backgroundPadding;
		const auto top = skip + row * (height + skip);
		const auto bottom = top + height;
		if ((bottom <= visibleTop || top >= visibleBottom)
			&& !_papers[i].thumbnail.isNull()) {
			_papers[i].dataMedia = nullptr;
		}
	}
}

rpl::producer<Data::WallPaper> BackgroundBox::Inner::chooseEvents() const {
	return _backgroundChosen.events();
}

auto BackgroundBox::Inner::removeRequests() const
-> rpl::producer<Data::WallPaper> {
	return _backgroundRemove.events();
}

void BackgroundBox::Inner::removePaper(const Data::WallPaper &data) {
	const auto i = ranges::find(
		_papers,
		data.id(),
		[](const Paper &paper) { return paper.data.id(); });
	if (i != end(_papers)) {
		_papers.erase(i);
		_over = _overDown = Selection();
		resizeToContentAndPreload();
	}
}

BackgroundBox::Inner::~Inner() = default;
