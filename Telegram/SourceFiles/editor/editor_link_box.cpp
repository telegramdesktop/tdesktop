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
#include "editor/editor_message_render.h"
#include "history/history_item.h"
#include "history/view/controls/history_view_webpage_processor.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/section_widget.h"
#include "styles/style_calls.h"
#include "styles/style_editor.h"
#include "styles/style_layers.h"

#include <QtGui/QPainter>

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

class PreviewWidget final : public Ui::RpWidget {
public:
	explicit PreviewWidget(QWidget *parent);

	void setSource(std::shared_ptr<MessageSource> source);

private:
	void paintEvent(QPaintEvent *e) override;
	void refresh();
	void updateHeight();

	std::shared_ptr<MessageSource> _source;
	std::unique_ptr<MessageRenderer> _renderer;
	QImage _image;
	QSize _size;

};

PreviewWidget::PreviewWidget(QWidget *parent) : RpWidget(parent) {
	widthValue() | rpl::on_next([=] {
		updateHeight();
	}, lifetime());
}

void PreviewWidget::setSource(std::shared_ptr<MessageSource> source) {
	_source = std::move(source);
	_renderer = std::make_unique<MessageRenderer>(_source);
	_renderer->setRepaintCallback([=] { refresh(); });
	refresh();
}

void PreviewWidget::refresh() {
	_image = _renderer->render(style::DevicePixelRatio());
	_size = _renderer->size();
	updateHeight();
	update();
}

void PreviewWidget::updateHeight() {
	if (_size.isEmpty() || !width()) {
		resize(width(), 0);
		return;
	}
	const auto padding = st::photoEditorLinkPreviewPadding;
	const auto scale = std::min({
		1.,
		(width() - 2 * padding) / float64(_size.width()),
		st::photoEditorLinkPreviewMaxHeight / float64(_size.height()),
	});
	resize(width(), int(std::ceil(_size.height() * scale)) + 2 * padding);
}

void PreviewWidget::paintEvent(QPaintEvent *e) {
	if (_image.isNull() || _size.isEmpty() || !_renderer) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	auto clip = QPainterPath();
	clip.addRoundedRect(rect(), st::boxRadius, st::boxRadius);
	p.setClipPath(clip);
	Window::SectionWidget::PaintBackground(
		p,
		_renderer->theme(),
		QSize(width(), height() * 3),
		rect());
	const auto padding = st::photoEditorLinkPreviewPadding;
	const auto scale = (height() - 2 * padding) / float64(_size.height());
	const auto size = QSizeF(_size) * scale;
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(
		QRectF(QPointF((width() - size.width()) / 2., padding), size),
		_image);
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
			rpl::variable<bool> customName = false;
		};
		const auto state = box->lifetime().make_state<State>(session);
		const auto &editing = args.editing;
		if (editing) {
			state->captionAbove = editing->captionAbove;
			state->largePhoto = editing->largePhoto;
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
		const auto above = optionsInner->add(object_ptr<Ui::SettingsButton>(
			optionsInner,
			state->captionAbove.value() | rpl::map([](bool above) {
				return above
					? tr::lng_link_move_up(tr::now)
					: tr::lng_link_move_down(tr::now);
			}),
			st::groupCallSettingsButton));
		const auto photo = optionsInner->add(
			object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
				optionsInner,
				object_ptr<Ui::SettingsButton>(
					optionsInner,
					state->largePhoto.value() | rpl::map([=](bool large) {
						const auto video = state->resolved
							&& HasVideo(state->resolved->webpage);
						return large
							? (video
								? tr::lng_link_shrink_video(tr::now)
								: tr::lng_link_shrink_photo(tr::now))
							: (video
								? tr::lng_link_enlarge_video(tr::now)
								: tr::lng_link_enlarge_photo(tr::now));
					}),
					st::groupCallSettingsButton)));
		const auto customize = optionsInner->add(
			object_ptr<Ui::SettingsButton>(
				optionsInner,
				tr::lng_photo_editor_link_customize(),
				st::groupCallSettingsButton));
		customize->toggleOn(state->customName.value());
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
			};
		};
		const auto makeSource = [=]() -> std::shared_ptr<MessageSource> {
			const auto &resolved = state->resolved;
			if (!resolved) {
				return nullptr;
			} else if (resolved->messageId) {
				const auto item = session->data().message(
					resolved->messageId);
				return (item && CanRenderMessage(item))
					? std::make_shared<MessageSource>(item)
					: nullptr;
			}
			return std::make_shared<MessageSource>(
				session,
				currentLink(),
				resolved->webpage);
		};
		const auto refreshPreview = [=] {
			const auto source = makeSource();
			const auto message = source && !source->link();
			const auto webpage = source ? source->webpage() : nullptr;
			if (source) {
				preview->entity()->setSource(source);
			}
			preview->toggle(source != nullptr, anim::type::normal);
			options->toggle(source && !message, anim::type::normal);
			above->setVisible(webpage != nullptr);
			photo->toggle(HasPhoto(webpage), anim::type::normal);
		};

		above->setClickedCallback([=] {
			state->captionAbove = !state->captionAbove.current();
			refreshPreview();
		});
		photo->entity()->setClickedCallback([=] {
			state->largePhoto = !state->largePhoto.current();
			refreshPreview();
		});
		customize->toggledChanges(
		) | rpl::on_next([=](bool enabled) {
			state->customName = enabled;
			nameWrap->toggle(enabled, anim::type::normal);
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
			auto source = makeSource();
			if (!source) {
				source = std::make_shared<MessageSource>(
					session,
					currentLink(),
					nullptr);
			}
			const auto done = args.done;
			box->closeBox();
			done(std::move(source));
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
