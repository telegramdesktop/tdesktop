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
#include "ui/toast/toast.h"
#include "ui/image/image.h"
#include "history/history.h"
#include "history/history_message.h"
#include "history/view/history_view_message.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "core/application.h"
#include "boxes/confirm_box.h"
#include "styles/style_overview.h"
#include "styles/style_history.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kBackgroundsInRow = 3;
constexpr auto kMaxWallPaperSlugLength = 255;

[[nodiscard]] bool IsValidWallPaperSlug(const QString &slug) {
	if (slug.isEmpty() || slug.size() > kMaxWallPaperSlugLength) {
		return false;
	}
	return ranges::find_if(slug, [](QChar ch) {
		return (ch != '.')
			&& (ch != '_')
			&& (ch != '-')
			&& (ch < '0' || ch > '9')
			&& (ch < 'a' || ch > 'z')
			&& (ch < 'A' || ch > 'Z');
	}) == slug.end();
}

AdminLog::OwnedItem GenerateTextItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const QString &text,
		bool out) {
	Expects(history->peer->isUser());

	using Flag = MTPDmessage::Flag;
	const auto id = ServerMaxMsgId + (ServerMaxMsgId / 3) + (out ? 1 : 0);
	const auto flags = Flag::f_entities
		| Flag::f_from_id
		| (out ? Flag::f_out : Flag(0));
	const auto replyTo = 0;
	const auto viaBotId = 0;
	const auto item = new HistoryMessage(
		history,
		id,
		flags,
		replyTo,
		viaBotId,
		unixtime(),
		out ? history->session().userId() : peerToUser(history->peer->id),
		QString(),
		TextWithEntities{ TextUtilities::Clean(text) });
	return AdminLog::OwnedItem(delegate, item);
}

QImage PrepareScaledFromFull(
		const QImage &image,
		Images::Option blur = Images::Option(0)) {
	const auto size = st::boxWideWidth;
	const auto width = std::max(image.width(), 1);
	const auto height = std::max(image.height(), 1);
	const auto takeWidth = (width > height)
		? (width * size / height)
		: size;
	const auto takeHeight = (width > height)
		? size
		: (height * size / width);
	return Images::prepare(
		image,
		takeWidth,
		takeHeight,
		Images::Option::Smooth | blur,
		size,
		size);
}

QPixmap PrepareScaledFromThumb(ImagePtr thumb) {
	return thumb->loaded()
		? App::pixmapFromImageInPlace(PrepareScaledFromFull(
			thumb->original(),
			Images::Option::Blurred))
		: QPixmap();
}

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
		Ui::show(
			Box<BackgroundPreviewBox>(papers[index]),
			LayerOption::KeepOther);
	}
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

BackgroundPreviewBox::BackgroundPreviewBox(
	QWidget*,
	const Data::WallPaper &paper)
: _text1(GenerateTextItem(
	this,
	Auth().data().history(peerFromUser(ServiceUserId)),
	lang(lng_background_text1),
	false))
, _text2(GenerateTextItem(
	this,
	Auth().data().history(peerFromUser(ServiceUserId)),
	lang(lng_background_text2),
	true))
, _paper(paper)
, _radial(animation(this, &BackgroundPreviewBox::step_radial)) {
	subscribe(Auth().downloaderTaskFinished(), [=] { update(); });
}

void BackgroundPreviewBox::prepare() {
	setTitle(langFactory(lng_background_header));

	addButton(langFactory(lng_background_apply), [=] { apply(); });
	addButton(langFactory(lng_cancel), [=] { closeBox(); });
	if (!_paper.slug.isEmpty()) {
		addLeftButton(langFactory(lng_background_share), [=] { share(); });
	}

	_scaled = PrepareScaledFromThumb(_paper.thumb);
	checkLoadedDocument();

	if (_paper.thumb && !_paper.thumb->loaded()) {
		_paper.thumb->loadEvenCancelled(Data::FileOriginWallpaper(
			_paper.id,
			_paper.accessHash));
	}
	if (_paper.document) {
		_paper.document->save(Data::FileOriginWallpaper(
			_paper.id,
			_paper.accessHash), QString());
		if (_paper.document->loading()) {
			_radial.start(_paper.document->progress());
		}
	}

	_text1->setDisplayDate(true);
	_text1->initDimensions();
	_text1->resizeGetHeight(st::boxWideWidth);
	_text2->initDimensions();
	_text2->resizeGetHeight(st::boxWideWidth);

	setDimensions(st::boxWideWidth, st::boxWideWidth);
}

void BackgroundPreviewBox::apply() {
	App::main()->setChatBackground(_paper, std::move(_full));
	closeBox();
}

void BackgroundPreviewBox::share() {
	Expects(!_paper.slug.isEmpty());

	QApplication::clipboard()->setText(
		Core::App().createInternalLinkFull("bg/" + _paper.slug));
	Ui::Toast::Show(lang(lng_background_link_copied));
}

void BackgroundPreviewBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto ms = getms();

	if (const auto color = Window::Theme::GetWallPaperColor(_paper.slug)) {
		p.fillRect(e->rect(), *color);
	} else {
		if (_scaled.isNull()) {
			_scaled = PrepareScaledFromThumb(_paper.thumb);
			if (_scaled.isNull()) {
				p.fillRect(e->rect(), st::boxBg);
				return;
			}
		}
		paintImage(p);
		paintRadial(p, ms);
	}
	paintTexts(p, ms);
}

void BackgroundPreviewBox::paintImage(Painter &p) {
	Expects(!_scaled.isNull());

	const auto factor = cIntRetinaFactor();
	const auto size = st::boxWideWidth;
	const auto from = QRect(
		0,
		(size - height()) / 2 * factor,
		size * factor,
		height() * factor);
	p.drawPixmap(rect(), _scaled, from);
}

void BackgroundPreviewBox::paintRadial(Painter &p, TimeMs ms) {
	bool radial = false;
	float64 radialOpacity = 0;
	if (_radial.animating()) {
		_radial.step(ms);
		radial = _radial.animating();
		radialOpacity = _radial.opacity();
	}
	if (!radial) {
		return;
	}
	auto inner = radialRect();

	p.setPen(Qt::NoPen);
	p.setOpacity(radialOpacity);
	p.setBrush(st::radialBg);

	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}

	p.setOpacity(1);
	QRect arc(inner.marginsRemoved(QMargins(st::radialLine, st::radialLine, st::radialLine, st::radialLine)));
	_radial.draw(p, arc, st::radialLine, st::radialFg);
}

QRect BackgroundPreviewBox::radialRect() const {
	const auto available = height()
		- st::historyPaddingBottom
		- _text1->height()
		- _text2->height()
		- st::historyPaddingBottom;
	return QRect(
		QPoint(
			(width() - st::radialSize.width()) / 2,
			(available - st::radialSize.height()) / 2),
		st::radialSize);
}

void BackgroundPreviewBox::paintTexts(Painter &p, TimeMs ms) {
	const auto height1 = _text1->height();
	const auto height2 = _text2->height();
	const auto top = height()
		- height1
		- height2
		- st::historyPaddingBottom;
	p.translate(0, top);
	_text1->draw(p, rect(), TextSelection(), ms);
	p.translate(0, height1);
	_text2->draw(p, rect(), TextSelection(), ms);
	p.translate(0, height2);
}

void BackgroundPreviewBox::step_radial(TimeMs ms, bool timer) {
	Expects(_paper.document != nullptr);

	const auto document = _paper.document;
	const auto wasAnimating = _radial.animating();
	const auto updated = _radial.update(
		document->progress(),
		!document->loading(),
		ms);
	if (timer
		&& (wasAnimating || _radial.animating())
		&& (!anim::Disabled() || updated)) {
		update(radialRect());
	}
	checkLoadedDocument();
}

void BackgroundPreviewBox::checkLoadedDocument() {
	const auto document = _paper.document;
	if (!document
		|| !document->loaded(DocumentData::FilePathResolveChecked)
		|| _generating) {
		return;
	}
	_generating = Data::ReadImageAsync(document, [=](
			QImage &&image) mutable {
		auto [left, right] = base::make_binary_guard();
		_generating = std::move(left);
		crl::async([
			this,
			image = std::move(image),
			guard = std::move(right)
		]() mutable {
			auto scaled = PrepareScaledFromFull(image);
			crl::on_main([
				this,
				image = std::move(image),
				scaled = std::move(scaled),
				guard = std::move(guard)
			]() mutable {
				if (!guard) {
					return;
				}
				_scaled = App::pixmapFromImageInPlace(std::move(scaled));
				_full = std::move(image);
				update();
			});
		});
	});
}

bool BackgroundPreviewBox::Start(const QString &slug, const QString &mode) {
	if (Window::Theme::GetWallPaperColor(slug)) {
		Ui::show(Box<BackgroundPreviewBox>(Data::WallPaper{
			Window::Theme::kCustomBackground,
			0ULL, // accessHash
			MTPDwallPaper::Flags(0),
			slug,
		}));
		return true;
	}
	if (!IsValidWallPaperSlug(slug)) {
		Ui::show(Box<InformBox>(lang(lng_background_bad_link)));
		return false;
	}
	Auth().api().requestWallPaper(slug, [](const Data::WallPaper &result) {
		Ui::show(Box<BackgroundPreviewBox>(result));
	}, [](const RPCError &error) {
		Ui::show(Box<InformBox>(lang(lng_background_bad_link)));
	});
	return true;
}

HistoryView::Context BackgroundPreviewBox::elementContext() {
	return HistoryView::Context::ContactPreview;
}

std::unique_ptr<HistoryView::Element> BackgroundPreviewBox::elementCreate(
		not_null<HistoryMessage*> message) {
	return std::make_unique<HistoryView::Message>(this, message);
}

std::unique_ptr<HistoryView::Element> BackgroundPreviewBox::elementCreate(
		not_null<HistoryService*> message) {
	Unexpected("Service message in BackgroundPreviewBox.");
}

bool BackgroundPreviewBox::elementUnderCursor(
		not_null<const Element*> view) {
	return false;
}

void BackgroundPreviewBox::elementAnimationAutoplayAsync(
	not_null<const Element*> element) {
}

TimeMs BackgroundPreviewBox::elementHighlightTime(
		not_null<const Element*> element) {
	return TimeMs();
}

bool BackgroundPreviewBox::elementInSelectionMode() {
	return false;
}
