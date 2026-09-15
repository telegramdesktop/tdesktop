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
	[[nodiscard]] rpl::producer<> themeToggles() const;

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintFrame(QPainter &p);
	void paintContent(QPainter &p);
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
	QImage _image;
	QSize _size;
	QImage _from;
	Ui::Animations::Simple _progress;
	rpl::event_stream<> _themeToggles;
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
		}
		applyHeight();
	}, lifetime());
}

rpl::producer<> PreviewWidget::themeToggles() const {
	return _themeToggles.events();
}

int PreviewWidget::innerWidth() const {
	return std::max(width() - 2 * st::photoEditorLinkPreviewPadding, 1);
}

bool PreviewWidget::hasContent() const {
	return !_size.isEmpty() && (!_image.isNull() || _pill.has_value());
}

void PreviewWidget::setSource(
		std::shared_ptr<MessageSource> source,
		bool dark) {
	if (hasContent()) {
		beginTransition(_renderer && (_dark != dark), dark);
	}
	_dark = dark;
	_pill.reset();
	_pillLink.reset();
	_source = std::move(source);
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
	if (_pill) {
		_pill->paint(p, origin, scale);
	} else {
		p.drawImage(QRectF(origin, size), _image);
	}
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
			const auto webpage = state->resolved
				? state->resolved->webpage
				: nullptr;
			if (result.message) {
				preview->entity()->setSource(result.message, result.dark);
			} else if (result.pill) {
				preview->entity()->setPill(*result.pill);
			}
			const auto shown = result.message || result.pill;
			preview->toggle(shown, anim::type::normal);
			options->toggle(shown, anim::type::normal);
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
			refreshPreview();
			state->loading = !validated.isEmpty();
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
