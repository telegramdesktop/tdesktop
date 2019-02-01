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
	static auto id = ServerMaxMsgId + (ServerMaxMsgId / 3);
	const auto flags = Flag::f_entities
		| Flag::f_from_id
		| (out ? Flag::f_out : Flag(0));
	const auto replyTo = 0;
	const auto viaBotId = 0;
	const auto item = new HistoryMessage(
		history,
		++id,
		flags,
		replyTo,
		viaBotId,
		unixtime(),
		out ? history->session().userId() : peerToUser(history->peer->id),
		QString(),
		TextWithEntities{ TextUtilities::Clean(text) });
	return AdminLog::OwnedItem(delegate, item);
}

QImage PrepareScaledNonPattern(
		const QImage &image,
		Images::Option blur) {
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
		takeWidth * cIntRetinaFactor(),
		takeHeight * cIntRetinaFactor(),
		Images::Option::Smooth
		| Images::Option::TransparentBackground
		| blur,
		size,
		size);
}

QImage ColorizePattern(QImage image, QColor color) {
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	// Similar to style::colorizeImage.
	// But style::colorizeImage takes pattern with all pixels having the
	// same components value, from (0, 0, 0, 0) to (255, 255, 255, 255).
	//
	// While in patterns we have different value ranges, usually they are
	// from (0, 0, 0, 0) to (0, 0, 0, 255), so we should use only 'alpha'.

	const auto width = image.width();
	const auto height = image.height();
	const auto pattern = anim::shifted(color);

	const auto resultBytesPerPixel = (image.depth() >> 3);
	constexpr auto resultIntsPerPixel = 1;
	const auto resultIntsPerLine = (image.bytesPerLine() >> 2);
	const auto resultIntsAdded = resultIntsPerLine - width * resultIntsPerPixel;
	auto resultInts = reinterpret_cast<uint32*>(image.bits());
	Assert(resultIntsAdded >= 0);
	Assert(image.depth() == static_cast<int>((resultIntsPerPixel * sizeof(uint32)) << 3));
	Assert(image.bytesPerLine() == (resultIntsPerLine << 2));

	const auto maskBytesPerPixel = (image.depth() >> 3);
	const auto maskBytesPerLine = image.bytesPerLine();
	const auto maskBytesAdded = maskBytesPerLine - width * maskBytesPerPixel;

	// We want to read the last byte of four available.
	// This is the difference with style::colorizeImage.
	auto maskBytes = image.constBits() + (maskBytesPerPixel - 1);
	Assert(maskBytesAdded >= 0);
	Assert(image.depth() == (maskBytesPerPixel << 3));
	for (auto y = 0; y != height; ++y) {
		for (auto x = 0; x != width; ++x) {
			auto maskOpacity = static_cast<anim::ShiftedMultiplier>(*maskBytes) + 1;
			*resultInts = anim::unshifted(pattern * maskOpacity);
			maskBytes += maskBytesPerPixel;
			resultInts += resultIntsPerPixel;
		}
		maskBytes += maskBytesAdded;
		resultInts += resultIntsAdded;
	}
	return image;
}

QImage PrepareScaledFromFull(
		const QImage &image,
		std::optional<QColor> patternBackground,
		Images::Option blur = Images::Option(0)) {
	auto result = PrepareScaledNonPattern(image, blur);
	if (patternBackground) {
		result = ColorizePattern(
			std::move(result),
			Data::PatternColor(*patternBackground));
	}
	return std::move(result).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
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
	void paintPaper(
		Painter &p,
		const Data::WallPaper &paper,
		int column,
		int row) const;

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
		paper.loadThumbnail();
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
		} else if ((st::backgroundSize.height() + st::backgroundPadding) * row >= r.top() + r.height()) {
			break;
		}
		paintPaper(p, paper, column, row);
	}
}

void BackgroundBox::Inner::paintPaper(
		Painter &p,
		const Data::WallPaper &paper,
		int column,
		int row) const {
	Expects(paper.thumbnail() != nullptr);

	const auto x = st::backgroundPadding + column * (st::backgroundSize.width() + st::backgroundPadding);
	const auto y = st::backgroundPadding + row * (st::backgroundSize.height() + st::backgroundPadding);
	const auto &pixmap = paper.thumbnail()->pix(
		paper.fileOrigin(),
		st::backgroundSize.width(),
		st::backgroundSize.height());
	p.drawPixmap(x, y, pixmap);

	if (paper.id() == Window::Theme::Background()->id()) {
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
	Expects(_paper.thumbnail() != nullptr);

	subscribe(Auth().downloaderTaskFinished(), [=] { update(); });
}

void BackgroundPreviewBox::prepare() {
	setTitle(langFactory(lng_background_header));

	addButton(langFactory(lng_background_apply), [=] { apply(); });
	addButton(langFactory(lng_cancel), [=] { closeBox(); });
	if (_paper.hasShareUrl()) {
		addLeftButton(langFactory(lng_background_share), [=] { share(); });
	}
	updateServiceBg(_paper.backgroundColor());

	_paper.loadThumbnail();
	_paper.loadDocument();
	if (_paper.document() && _paper.document()->loading()) {
		_radial.start(_paper.document()->progress());
	}
	setScaledFromThumb();
	checkLoadedDocument();

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
	QApplication::clipboard()->setText(_paper.shareUrl());
	Ui::Toast::Show(lang(lng_background_link_copied));
}

void BackgroundPreviewBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto ms = getms();
	const auto color = _paper.backgroundColor();
	if (color) {
		p.fillRect(e->rect(), *color);
	}
	if (!color || _paper.isPattern()) {
		if (_scaled.isNull() && !setScaledFromThumb()) {
			p.fillRect(e->rect(), st::boxBg);
			return;
		}
		paintImage(p);
		paintRadial(p, ms);
	}
	paintTexts(p, ms);
}

void BackgroundPreviewBox::paintImage(Painter &p) {
	Expects(!_scaled.isNull());

	p.setOpacity(_paper.isPattern()
		? std::clamp(_paper.patternIntensity() / 100., 0., 1.)
		: 1.);
	const auto guard = gsl::finally([&] { p.setOpacity(1.); });

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
	paintDate(p);
	_text1->draw(p, rect(), TextSelection(), ms);
	p.translate(0, height1);
	_text2->draw(p, rect(), TextSelection(), ms);
	p.translate(0, height2);
}

void BackgroundPreviewBox::paintDate(Painter &p) {
	const auto date = _text1->Get<HistoryView::DateBadge>();
	if (!date || !_serviceBg) {
		return;
	}
	const auto text = date->text;
	const auto bubbleHeight = st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	const auto bubbleTop = st::msgServiceMargin.top();
	const auto textWidth = st::msgServiceFont->width(text);
	const auto bubbleWidth = st::msgServicePadding.left() + textWidth + st::msgServicePadding.right();
	const auto bubbleLeft = (width() - bubbleWidth) / 2;
	const auto radius = bubbleHeight / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(*_serviceBg);
	p.drawRoundedRect(bubbleLeft, bubbleTop, bubbleWidth, bubbleHeight, radius, radius);
	p.setPen(st::msgServiceFg);
	p.setFont(st::msgServiceFont);
	p.drawText(bubbleLeft + st::msgServicePadding.left(), bubbleTop + st::msgServicePadding.top() + st::msgServiceFont->ascent, text);
}

void BackgroundPreviewBox::step_radial(TimeMs ms, bool timer) {
	Expects(_paper.document() != nullptr);

	const auto document = _paper.document();
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

bool BackgroundPreviewBox::setScaledFromThumb() {
	Expects(_paper.thumbnail() != nullptr);

	const auto thumbnail = _paper.thumbnail();
	if (!thumbnail->loaded()) {
		return false;
	}
	setScaledFromImage(PrepareScaledFromFull(
		thumbnail->original(),
		patternBackgroundColor(),
		_paper.document() ? Images::Option::Blurred : Images::Option(0)));
	return true;
}

void BackgroundPreviewBox::setScaledFromImage(QImage &&image) {
	updateServiceBg(Window::Theme::CountAverageColor(image));
	_scaled = App::pixmapFromImageInPlace(std::move(image));
}

void BackgroundPreviewBox::updateServiceBg(std::optional<QColor> background) {
	if (background) {
		_serviceBg = Window::Theme::AdjustedColor(
			st::msgServiceBg->c,
			*background);
	}
}

std::optional<QColor> BackgroundPreviewBox::patternBackgroundColor() const {
	return _paper.isPattern() ? _paper.backgroundColor() : std::nullopt;
}

void BackgroundPreviewBox::checkLoadedDocument() {
	const auto document = _paper.document();
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
			patternBackground = patternBackgroundColor(),
			guard = std::move(right)
		]() mutable {
			auto scaled = PrepareScaledFromFull(image, patternBackground);
			crl::on_main([
				this,
				image = std::move(image),
				scaled = std::move(scaled),
				guard = std::move(guard)
			]() mutable {
				if (!guard) {
					return;
				}
				setScaledFromImage(std::move(scaled));
				_full = std::move(image);
				update();
			});
		});
	});
}

bool BackgroundPreviewBox::Start(
		const QString &slug,
		const QMap<QString, QString> &params) {
	if (const auto paper = Data::WallPaper::FromColorSlug(slug)) {
		Ui::show(Box<BackgroundPreviewBox>(paper->withUrlParams(params)));
		return true;
	}
	if (!IsValidWallPaperSlug(slug)) {
		Ui::show(Box<InformBox>(lang(lng_background_bad_link)));
		return false;
	}
	Auth().api().requestWallPaper(slug, [=](const Data::WallPaper &result) {
		Ui::show(Box<BackgroundPreviewBox>(result.withUrlParams(params)));
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
