/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_link_box.h"

#include "api/api_single_message_search.h"
#include "base/qthelp_url.h"
#include "base/timer.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "editor/editor_link_pill.h"
#include "editor/editor_message_render.h"
#include "history/history_item.h"
#include "history/view/controls/history_view_webpage_processor.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/animations.h"
#include "ui/effects/glare.h"
#include "ui/effects/loading_element.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"
#include "styles/style_calls.h"
#include "styles/style_editor.h"
#include "styles/style_layers.h"

namespace Editor {
namespace {

constexpr auto kResolveDelay = crl::time(700);
constexpr auto kLoadingTitleRatio = 0.45;
constexpr auto kLoadingLineRatios = std::array{ 0.32, 0.58, 0.86 };
constexpr auto kGlareTimeout = crl::time(1000);
constexpr auto kGlareDuration = crl::time(1000);

[[nodiscard]] const style::palette &DefaultPalette(bool dark) {
	static auto cache = std::array<std::unique_ptr<style::palette>, 2>();
	auto &result = cache[dark ? 1 : 0];
	if (!result) {
		result = std::make_unique<style::palette>();
		Window::Theme::PreparePaletteCallback(dark, std::nullopt)(*result);
	}
	return *result;
}

class LoadingMessage final : public Ui::LoadingElement {
public:
	LoadingMessage(const style::palette &palette, bool captionAbove);

	[[nodiscard]] bool captionAbove() const;

	[[nodiscard]] int height() const override;
	void paint(QPainter &p, int width) override;

private:
	void paintCaption(QPainter &p, int inner);
	void paintPreview(QPainter &p, int inner);

	const style::palette &_palette;
	Ui::LoadingLine _line;
	bool _captionAbove = true;

};

LoadingMessage::LoadingMessage(
	const style::palette &palette,
	bool captionAbove)
: _palette(palette)
, _line(
	st::photoEditorLinkLoadingLine,
	st::photoEditorLinkLoadingSkip,
	palette.windowBgRipple()->c)
, _captionAbove(captionAbove) {
}

bool LoadingMessage::captionAbove() const {
	return _captionAbove;
}

int LoadingMessage::height() const {
	const auto padding = st::photoEditorLinkLoadingPadding;
	const auto lines = 1 + int(kLoadingLineRatios.size());
	return padding.top()
		+ lines * _line.height()
		- st::photoEditorLinkLoadingSkip
		+ padding.bottom();
}

void LoadingMessage::paint(QPainter &p, int width) {
	const auto radius = st::photoEditorLinkLoadingRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(_palette.msgInBg()->c);
	p.drawRoundedRect(QRect(0, 0, width, height()), radius, radius);

	const auto padding = st::photoEditorLinkLoadingPadding;
	const auto inner = width - padding.left() - padding.right();
	p.translate(padding.left(), padding.top());
	if (_captionAbove) {
		paintCaption(p, inner);
		paintPreview(p, inner);
	} else {
		paintPreview(p, inner);
		paintCaption(p, inner);
	}
}

void LoadingMessage::paintCaption(QPainter &p, int inner) {
	_line.paint(p, int(inner * kLoadingTitleRatio));
	p.translate(0, _line.height());
}

void LoadingMessage::paintPreview(QPainter &p, int inner) {
	const auto quote = st::photoEditorLinkLoadingQuote;
	const auto quoteSkip = quote + st::photoEditorLinkLoadingQuoteSkip;
	const auto line = _line.height();
	const auto lines = int(kLoadingLineRatios.size());
	p.setPen(Qt::NoPen);
	p.setBrush(_palette.windowBgRipple()->c);
	p.drawRoundedRect(
		QRect(0, 0, quote, lines * line - st::photoEditorLinkLoadingSkip),
		quote / 2.,
		quote / 2.);
	p.translate(quoteSkip, 0);
	for (const auto ratio : kLoadingLineRatios) {
		_line.paint(p, int((inner - quoteSkip) * ratio));
		p.translate(0, line);
	}
	p.translate(-quoteSkip, 0);
}

struct Resolved {
	FullMsgId messageId;
	WebPageData *webpage = nullptr;
};

class LinkResolver final : public base::has_weak_ptr {
public:
	explicit LinkResolver(not_null<Main::Session*> session);

	void resolve(const QString &url);
	void cancel();
	[[nodiscard]] rpl::producer<Resolved> resolved() const;

private:
	void resolveMessage();
	void resolveWebpage();
	void finish(Resolved result);

	const not_null<Main::Session*> _session;
	Api::SingleMessageSearch _search;
	HistoryView::Controls::WebpageResolver _webpages;
	QString _url;
	int _generation = 0;
	rpl::event_stream<Resolved> _resolved;
	rpl::lifetime _webpageLifetime;

};
LinkResolver::LinkResolver(not_null<Main::Session*> session)
: _session(session)
, _search(session)
, _webpages(session) {
}

void LinkResolver::resolve(const QString &url) {
	cancel();
	_url = url;
	resolveMessage();
}

void LinkResolver::cancel() {
	++_generation;
	_search.clear();
	_webpages.cancel(_url);
	_webpageLifetime.destroy();
	_url = QString();
}

rpl::producer<Resolved> LinkResolver::resolved() const {
	return _resolved.events();
}

void LinkResolver::resolveMessage() {
	const auto generation = _generation;
	const auto item = _search.lookup(_url, crl::guard(this, [=] {
		if (generation == _generation) {
			resolveMessage();
		}
	}));
	if (!item) {
		return;
	} else if (*item && CanRenderMessage(*item)) {
		finish({ .messageId = MessageToRender(*item)->fullId() });
	} else {
		resolveWebpage();
	}
}

void LinkResolver::resolveWebpage() {
	const auto url = _url;
	const auto generation = _generation;
	const auto finishWith = [=](WebPageData *page) {
		if (generation == _generation) {
			finish({ .webpage = page });
		}
	};
	const auto waitForPending = [=](not_null<WebPageData*> page) {
		_session->data().webPageUpdates(
		) | rpl::filter([=](not_null<WebPageData*> updated) {
			return (updated == page) && !page->pendingTill;
		}) | rpl::on_next([=] {
			finishWith(page->failed ? nullptr : page.get());
		}, _webpageLifetime);
	};
	_webpages.resolved(
	) | rpl::filter([=](const QString &link) {
		return (link == url);
	}) | rpl::on_next([=] {
		const auto page = _webpages.lookup(url).value_or(nullptr);
		if (page && page->pendingTill > 0) {
			waitForPending(page);
		} else {
			finishWith(page);
		}
	}, _webpageLifetime);
	_webpages.request(url, true);
}

void LinkResolver::finish(Resolved result) {
	_url = QString();
	_resolved.fire(std::move(result));
	++_generation;
}

constexpr auto kToDarkDuration = crl::time(450);
constexpr auto kToLightDuration = crl::time(320);

class ThemeButton final : public Ui::AbstractButton {
public:
	ThemeButton(QWidget *parent, bool dark);

	void setDark(bool dark);

private:
	void paintEvent(QPaintEvent *e) override;

	std::unique_ptr<Lottie::Icon> _icon;
	bool _dark = false;

};

ThemeButton::ThemeButton(QWidget *parent, bool dark)
: AbstractButton(parent)
, _icon(Lottie::MakeIcon({
	.name = u"sun_outline"_q,
	.color = &st::groupCallMembersFg,
	.sizeOverride = Size(st::photoEditorLinkThemeIconSize),
}))
, _dark(dark) {
	setObjectName(u"photoEditorLinkThemeToggle"_q);
	resize(st::photoEditorLinkThemeSize, st::photoEditorLinkThemeSize);
	if (_icon->valid() && _dark) {
		_icon->jumpTo(_icon->framesCount() - 1, [=] { update(); });
	}
}

void ThemeButton::setDark(bool dark) {
	if (_dark == dark) {
		return;
	}
	_dark = dark;
	if (_icon->valid()) {
		_icon->animate(
			[=] { update(); },
			_icon->frameIndex(),
			dark ? (_icon->framesCount() - 1) : 0);
	}
	update();
}

void ThemeButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::photoEditorLinkThemeBg);
	p.drawEllipse(rect());
	if (_icon->valid()) {
		_icon->paintInCenter(p, rect());
	}
}

class PreviewWidget final : public Ui::RpWidget {
public:
	explicit PreviewWidget(QWidget *parent);

	void setSource(std::shared_ptr<MessageSource> source, bool dark);
	void setPill(const LinkPreview &link);
	void setLoading(bool dark, bool captionAbove);
	void clear();
	[[nodiscard]] rpl::producer<> themeToggles() const;
	[[nodiscard]] rpl::producer<> sourceRemovals() const;

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintFrame(QPainter &p);
	void paintContent(QPainter &p);
	void paintLoading(QPainter &p, QRectF plate);
	void paintGlare(QPainter &p, QRectF plate, int radius);
	void clearLoading();
	void validateGlare();
	[[nodiscard]] QSize loadingSize() const;
	[[nodiscard]] QImage snapshot();
	void beginTransition(bool radial, bool dark);
	void scheduleRefresh();
	void refresh();
	[[nodiscard]] not_null<Ui::ChatTheme*> background(bool dark);
	[[nodiscard]] float64 contentScale() const;
	[[nodiscard]] int targetHeight() const;
	void applyHeight();
	[[nodiscard]] int innerWidth() const;
	[[nodiscard]] bool hasContent() const;

	const not_null<ThemeButton*> _theme;
	std::array<std::unique_ptr<Ui::ChatTheme>, 2> _backgrounds;
	std::shared_ptr<MessageSource> _source;
	std::unique_ptr<MessageRenderer> _renderer;
	std::optional<LinkPreview> _pillLink;
	std::optional<LinkPill> _pill;
	std::optional<LoadingMessage> _loading;
	QImage _image;
	QSize _size;
	QImage _from;
	Ui::Animations::Simple _progress;
	Ui::GlareEffect _glare;
	rpl::event_stream<> _themeToggles;
	rpl::event_stream<> _sourceRemovals;
	rpl::lifetime _sourceLifetime;
	int _heightFrom = 0;
	int _heightTo = 0;
	bool _radial = false;
	bool _dark = false;
	bool _refreshScheduled = false;

};

PreviewWidget::PreviewWidget(QWidget *parent)
: RpWidget(parent)
, _theme(Ui::CreateChild<ThemeButton>(this, false)) {
	_theme->hide();
	_theme->setClickedCallback([=] { _themeToggles.fire({}); });
	widthValue() | rpl::on_next([=] {
		if (_pillLink) {
			_pill.emplace(
				*_pillLink,
				LinkPill::DensityFor(innerWidth()),
				innerWidth());
			_size = _pill->size().toSize();
		} else if (_loading) {
			_size = loadingSize();
			validateGlare();
		}
		applyHeight();
	}, lifetime());
}

rpl::producer<> PreviewWidget::themeToggles() const {
	return _themeToggles.events();
}

rpl::producer<> PreviewWidget::sourceRemovals() const {
	return _sourceRemovals.events();
}

int PreviewWidget::innerWidth() const {
	return std::max(width() - 2 * st::photoEditorLinkPreviewPadding, 1);
}

bool PreviewWidget::hasContent() const {
	return !_size.isEmpty()
		&& (_loading || !_image.isNull() || _pill.has_value());
}

QSize PreviewWidget::loadingSize() const {
	return QSize(innerWidth(), _loading->height());
}

void PreviewWidget::clearLoading() {
	_loading.reset();
	_glare.animation.stop();
}

void PreviewWidget::validateGlare() {
	_glare.width = _size.width();
	_glare.validate(
		DefaultPalette(_dark).msgInBg()->c,
		[=] { update(); },
		kGlareTimeout,
		kGlareDuration);
}

void PreviewWidget::setLoading(bool dark, bool captionAbove) {
	if (_loading
		&& (_dark == dark)
		&& (_loading->captionAbove() == captionAbove)) {
		return;
	}
	if (hasContent()) {
		beginTransition(false, dark);
	}
	_dark = dark;
	_loading.emplace(DefaultPalette(dark), captionAbove);
	_sourceLifetime.destroy();
	_source = nullptr;
	_renderer = nullptr;
	_image = QImage();
	_pill.reset();
	_pillLink.reset();
	_size = loadingSize();
	_theme->hide();
	validateGlare();
	applyHeight();
	update();
}

void PreviewWidget::setSource(
		std::shared_ptr<MessageSource> source,
		bool dark) {
	if (hasContent()) {
		beginTransition(_renderer && (_dark != dark), dark);
	}
	_dark = dark;
	clearLoading();
	_pill.reset();
	_pillLink.reset();
	_sourceLifetime.destroy();
	_source = std::move(source);
	_source->removed(
	) | rpl::on_next([=] {
		_sourceRemovals.fire({});
	}, _sourceLifetime);
	_renderer = std::make_unique<MessageRenderer>(_source);
	_renderer->setDark(dark);
	_renderer->setRepaintCallback([=] { scheduleRefresh(); });
	_theme->setDark(dark);
	_theme->show();
	refresh();
}

void PreviewWidget::setPill(const LinkPreview &link) {
	if (hasContent()) {
		beginTransition(false, _dark);
	}
	_dark = link.dark;
	clearLoading();
	_sourceLifetime.destroy();
	_source = nullptr;
	_renderer = nullptr;
	_image = QImage();
	_pillLink = link;
	_pill.emplace(link, LinkPill::DensityFor(innerWidth()), innerWidth());
	_size = _pill->size().toSize();
	_theme->hide();
	applyHeight();
	update();
}

void PreviewWidget::clear() {
	if (hasContent()) {
		beginTransition(false, _dark);
	}
	clearLoading();
	_sourceLifetime.destroy();
	_source = nullptr;
	_renderer = nullptr;
	_image = QImage();
	_pill.reset();
	_pillLink.reset();
	_size = QSize();
	_theme->hide();
	applyHeight();
	update();
}

void PreviewWidget::beginTransition(bool radial, bool dark) {
	_from = snapshot();
	_heightFrom = height();
	_radial = radial;
	_progress = {};
	_progress.start(
		[=] { applyHeight(); update(); },
		0.,
		1.,
		(!radial
			? st::slideWrapDuration
			: dark
			? kToDarkDuration
			: kToLightDuration),
		(!radial
			? anim::linear
			: dark
			? anim::easeOutQuint
			: anim::easeInCubic));
}

QImage PreviewWidget::snapshot() {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	paintFrame(p);
	return result;
}

void PreviewWidget::scheduleRefresh() {
	if (_refreshScheduled) {
		return;
	}
	_refreshScheduled = true;
	crl::on_main(this, [=] {
		_refreshScheduled = false;
		refresh();
	});
}

void PreviewWidget::refresh() {
	if (!_renderer) {
		return;
	}
	_image = _renderer->render(style::DevicePixelRatio());
	_size = _renderer->size();
	applyHeight();
	update();
}

not_null<Ui::ChatTheme*> PreviewWidget::background(bool dark) {
	auto &theme = _backgrounds[dark ? 1 : 0];
	if (!theme) {
		theme = std::make_unique<Ui::ChatTheme>();
		theme->setBackground(Window::Theme::PrepareDefaultBackground(dark));
		theme->repaintBackgroundRequests(
		) | rpl::on_next([=] { update(); }, lifetime());
	}
	return theme.get();
}

float64 PreviewWidget::contentScale() const {
	return std::min({
		1.,
		innerWidth() / float64(_size.width()),
		st::photoEditorLinkPreviewMaxHeight / float64(_size.height()),
	});
}

int PreviewWidget::targetHeight() const {
	if (_size.isEmpty() || !width()) {
		return 0;
	}
	const auto padding = st::photoEditorLinkPreviewPadding;
	return int(std::ceil(_size.height() * contentScale())) + 2 * padding;
}

void PreviewWidget::applyHeight() {
	_heightTo = targetHeight();
	const auto animated = _progress.animating();
	const auto progress = animated ? _progress.value(1.) : 1.;
	const auto shown = animated
		? anim::interpolate(_heightFrom, _heightTo, progress)
		: _heightTo;
	if (height() != shown) {
		resize(width(), shown);
	}
	if (!animated && !_from.isNull()) {
		_from = QImage();
	}
}

void PreviewWidget::resizeEvent(QResizeEvent *e) {
	const auto skip = st::photoEditorLinkThemeSkip;
	_theme->moveToRight(skip, skip, width());
}

void PreviewWidget::paintContent(QPainter &p) {
	auto hq = PainterHighQualityEnabler(p);
	Window::SectionWidget::PaintBackground(
		p,
		background(_dark),
		QSize(width(), height() * 3),
		rect());
	if (!hasContent()) {
		return;
	}
	const auto scale = contentScale();
	const auto size = QSizeF(_size) * scale;
	const auto origin = QPointF(
		(width() - size.width()) / 2.,
		st::photoEditorLinkPreviewPadding);
	if (_loading) {
		paintLoading(p, QRectF(origin, size));
	} else if (_pill) {
		_pill->paint(p, origin, scale);
	} else {
		p.drawImage(QRectF(origin, size), _image);
	}
}

void PreviewWidget::paintLoading(QPainter &p, QRectF plate) {
	p.save();
	p.translate(plate.topLeft());
	_loading->paint(p, _size.width());
	p.restore();
	paintGlare(p, plate, st::photoEditorLinkLoadingRadius);
}

void PreviewWidget::paintGlare(QPainter &p, QRectF plate, int radius) {
	if (!_glare.glare.birthTime) {
		return;
	}
	const auto progress = _glare.progress(crl::now());
	if (progress < 0. || progress > 1.) {
		return;
	}
	const auto width = float64(_glare.width);
	const auto shift = plate.x()
		- width
		+ (plate.width() + width * 2) * progress;
	auto path = QPainterPath();
	path.addRoundedRect(plate, radius, radius);
	p.save();
	p.setClipPath(path);
	p.drawTiledPixmap(
		QRectF(shift, plate.y(), width, plate.height()),
		_glare.pixmap,
		QPointF());
	p.restore();
}

void PreviewWidget::paintFrame(QPainter &p) {
	auto clip = QPainterPath();
	clip.addRoundedRect(rect(), st::boxRadius, st::boxRadius);
	p.setClipPath(clip);
	paintContent(p);
	if (!_progress.animating() || _from.isNull()) {
		return;
	}
	const auto progress = _progress.value(1.);
	auto hq = PainterHighQualityEnabler(p);
	if (_radial) {
		const auto full = std::hypot(width(), height());
		const auto radius = full * (_dark ? (1. - progress) : progress);
		const auto center = QPointF(rect::center(_theme->geometry()));
		auto circle = QPainterPath();
		circle.addEllipse(center, radius, radius);
		p.setClipPath(_dark
			? clip.intersected(circle)
			: clip.subtracted(circle));
	} else {
		p.setOpacity(1. - progress);
	}
	p.drawImage(0, 0, _from);
}

void PreviewWidget::paintEvent(QPaintEvent *e) {
	if (!hasContent() && _from.isNull()) {
		return;
	}
	auto p = QPainter(this);
	paintFrame(p);
}

[[nodiscard]] QString StripDoubledPrefix(const QString &text) {
	const auto https = u"https://"_q;
	const auto doubled = {
		u"https://https://"_q,
		u"https://http://"_q,
	};
	for (const auto &prefix : doubled) {
		if (text.startsWith(prefix, Qt::CaseInsensitive)) {
			return text.mid(https.size());
		}
	}
	return QString();
}

[[nodiscard]] bool HasPhoto(WebPageData *webpage) {
	return webpage
		&& webpage->hasLargeMedia
		&& (webpage->photo || webpage->document);
}

[[nodiscard]] bool HasVideo(WebPageData *webpage) {
	return webpage
		&& !webpage->photo
		&& webpage->document
		&& webpage->document->isVideoFile();
}

} // namespace

object_ptr<Ui::BoxContent> LinkBox(LinkBoxArgs &&args) {
	return Box([args = std::move(args)](not_null<Ui::GenericBox*> box) {
		const auto session = &args.show->session();
		struct State {
			explicit State(not_null<Main::Session*> session)
			: resolver(session) {
			}

			LinkResolver resolver;
			base::Timer timer;
			std::optional<Resolved> resolved;
			QString url;
			rpl::variable<bool> valid = false;
			rpl::variable<bool> loading = false;
			rpl::variable<bool> captionAbove = true;
			rpl::variable<bool> largePhoto = false;
			rpl::variable<bool> video = false;
			rpl::variable<bool> withoutPreview = false;
			rpl::variable<bool> dark = false;
			rpl::variable<bool> customName = false;
		};
		const auto state = box->lifetime().make_state<State>(session);
		const auto &editing = args.editing;
		state->dark = editing
			? editing->dark
			: Window::Theme::IsNightMode();
		if (editing) {
			state->captionAbove = editing->captionAbove;
			state->largePhoto = editing->largePhoto;
			state->withoutPreview = !editing->preview;
			state->customName = !editing->name.isEmpty();
		}

		box->setTitle(editing
			? tr::lng_formatting_link_edit_title()
			: tr::lng_formatting_link_create_title());
		box->setWidth(st::boxWideWidth);

		const auto url = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::groupCallField,
			tr::lng_formatting_link_url(),
			(editing
				? editing->url
				: !args.url.isEmpty()
				? args.url
				: u"https://"_q)));
		box->setFocusCallback([=] { url->setFocusFast(); });

		const auto preview = box->addRow(
			object_ptr<Ui::SlideWrap<PreviewWidget>>(
				box,
				object_ptr<PreviewWidget>(box)),
			st::photoEditorLinkPreviewMargin);
		preview->hide(anim::type::instant);

		const auto layout = box->verticalLayout();
		const auto options = layout->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				box,
				object_ptr<Ui::VerticalLayout>(box)));
		options->hide(anim::type::instant);
		const auto optionsInner = options->entity();
		const auto addRow = [&](rpl::producer<QString> text) {
			return optionsInner->add(
				object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
					optionsInner,
					object_ptr<Ui::SettingsButton>(
						optionsInner,
						std::move(text),
						st::groupCallSettingsButton)));
		};
		const auto above = addRow(
			state->captionAbove.value() | rpl::map([](bool above) {
				return above
					? tr::lng_link_move_up(tr::now)
					: tr::lng_link_move_down(tr::now);
			}));
		const auto photo = addRow(rpl::combine(
			state->largePhoto.value(),
			state->video.value()
		) | rpl::map([](bool large, bool video) {
			return large
				? (video
					? tr::lng_link_shrink_video(tr::now)
					: tr::lng_link_shrink_photo(tr::now))
				: (video
					? tr::lng_link_enlarge_video(tr::now)
					: tr::lng_link_enlarge_photo(tr::now));
		}));
		const auto previewRow = addRow(
			tr::lng_shortcuts_toggle_link_preview());
		previewRow->entity()->toggleOn(
			state->withoutPreview.value() | rpl::map([](bool without) {
				return !without;
			}));
		const auto customize = addRow(
			tr::lng_photo_editor_link_customize());
		customize->entity()->toggleOn(state->customName.value());
		const auto nameWrap = optionsInner->add(
			object_ptr<Ui::SlideWrap<Ui::InputField>>(
				optionsInner,
				object_ptr<Ui::InputField>(
					optionsInner,
					st::groupCallField,
					tr::lng_formatting_link_text(),
					editing ? editing->name : QString()),
				st::boxRowPadding));
		const auto name = nameWrap->entity();
		nameWrap->toggle(state->customName.current(), anim::type::instant);

		const auto currentLink = [=] {
			return LinkPreview{
				.url = state->url,
				.name = (state->customName.current()
					? name->getLastText().trimmed()
					: QString()),
				.captionAbove = state->captionAbove.current(),
				.largePhoto = state->largePhoto.current(),
				.preview = !state->withoutPreview.current(),
				.dark = state->dark.current(),
			};
		};
		const auto makeResult = [=] {
			auto result = LinkBoxResult{ .dark = state->dark.current() };
			const auto &resolved = state->resolved;
			if (!resolved) {
				return result;
			} else if (resolved->messageId) {
				const auto item = session->data().message(
					resolved->messageId);
				if (item && CanRenderMessage(item)) {
					result.message = std::make_shared<MessageSource>(item);
					return result;
				} else if (item && !MessageForbidsRender(item)) {
					auto link = currentLink();
					link.preview = false;
					result.pill = std::move(link);
					return result;
				}
			}
			auto link = currentLink();
			const auto webpage = link.preview ? resolved->webpage : nullptr;
			if (webpage) {
				result.message = std::make_shared<MessageSource>(
					session,
					std::move(link),
					webpage);
			} else {
				result.pill = std::move(link);
			}
			return result;
		};
		const auto refreshPreview = [=] {
			const auto result = makeResult();
			const auto message = result.message && !result.message->link();
			const auto bubble = result.message && !message;
			const auto loading = state->loading.current();
			const auto webpage = state->resolved
				? state->resolved->webpage
				: nullptr;
			if (result.message) {
				preview->entity()->setSource(result.message, result.dark);
			} else if (result.pill) {
				preview->entity()->setPill(*result.pill);
			} else if (loading) {
				preview->entity()->setLoading(
					state->dark.current(),
					state->captionAbove.current());
			} else {
				preview->entity()->clear();
			}
			const auto shown = result.message || result.pill || loading;
			preview->toggle(shown, anim::type::normal);
			options->toggle(shown && !loading, anim::type::normal);
			above->toggle(bubble, anim::type::normal);
			photo->toggle(bubble && HasPhoto(webpage), anim::type::normal);
			previewRow->toggle(webpage != nullptr, anim::type::normal);
			customize->toggle(!message, anim::type::normal);
			nameWrap->toggle(
				!message && state->customName.current(),
				anim::type::normal);
		};

		above->entity()->setClickedCallback([=] {
			state->captionAbove = !state->captionAbove.current();
			refreshPreview();
		});
		photo->entity()->setClickedCallback([=] {
			state->largePhoto = !state->largePhoto.current();
			refreshPreview();
		});
		previewRow->entity()->toggledChanges(
		) | rpl::on_next([=](bool shown) {
			state->withoutPreview = !shown;
			refreshPreview();
		}, previewRow->lifetime());
		preview->entity()->themeToggles(
		) | rpl::on_next([=] {
			state->dark = !state->dark.current();
			refreshPreview();
		}, preview->lifetime());
		customize->entity()->toggledChanges(
		) | rpl::on_next([=](bool enabled) {
			state->customName = enabled;
			if (enabled) {
				name->setFocusFast();
			}
			refreshPreview();
		}, customize->lifetime());
		name->changes() | rpl::on_next([=] {
			if (state->customName.current()) {
				refreshPreview();
			}
		}, name->lifetime());

		const auto check = [=] {
			const auto text = url->getLastText().trimmed();
			const auto stripped = StripDoubledPrefix(text);
			if (!stripped.isEmpty()) {
				InvokeQueued(url, [=] { url->setText(stripped); });
				return;
			}
			const auto validated = qthelp::validate_url(text);
			state->valid = !validated.isEmpty();
			if (validated == state->url) {
				return;
			}
			state->url = validated;
			state->resolved = std::nullopt;
			state->resolver.cancel();
			state->timer.cancel();
			state->loading = !validated.isEmpty();
			refreshPreview();
			if (state->loading.current()) {
				state->timer.callOnce(kResolveDelay);
			}
		};
		state->timer.setCallback([=] {
			state->resolver.resolve(state->url);
		});
		state->resolver.resolved(
		) | rpl::on_next([=](Resolved resolved) {
			state->loading = false;
			state->resolved = resolved;
			state->video = HasVideo(resolved.webpage);
			refreshPreview();
		}, box->lifetime());
		url->changes() | rpl::on_next(check, url->lifetime());
		preview->entity()->sourceRemovals(
		) | rpl::on_next([=] {
			state->url = QString();
			check();
		}, preview->lifetime());
		check();

		const auto submit = [=] {
			if (!state->valid.current() || state->loading.current()) {
				return;
			}
			auto result = makeResult();
			if (!result.message && !result.pill) {
				result.pill = currentLink();
			}
			const auto done = args.done;
			box->closeBox();
			done(std::move(result));
		};
		const auto button = box->addButton(
			(editing
				? tr::lng_settings_save()
				: tr::lng_formatting_link_create()),
			submit);
		rpl::combine(
			state->valid.value(),
			state->loading.value()
		) | rpl::on_next([=](bool valid, bool loading) {
			button->setDisabled(!valid || loading);
		}, button->lifetime());
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

		rpl::merge(
			url->submits() | rpl::to_empty,
			name->submits() | rpl::to_empty
		) | rpl::on_next(submit, box->lifetime());
	});
}

} // namespace Editor
