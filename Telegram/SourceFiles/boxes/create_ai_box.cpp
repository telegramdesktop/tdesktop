/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/create_ai_box.h"

#include "apiwrap.h"
#include "base/object_ptr.h"
#include "base/weak_ptr.h"
#include "boxes/create_ai_tone_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_file_origin.h"
#include "data/data_msg_id.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "iv/iv_cached_media.h"
#include "iv/iv_rich_page.h"
#include "iv/markdown/iv_markdown_article.h"
#include "iv/markdown/iv_markdown_article_scroll_forwarder.h"
#include "iv/markdown/iv_markdown_common.h"
#include "iv/markdown/iv_markdown_prepare.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/boxes/about_cocoon_box.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "window/themes/window_theme.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_iv.h"
#include "styles/style_layers.h"

namespace Iv::Editor {
namespace {

constexpr auto kPromptRemainingThreshold = 100;

[[nodiscard]] LanguageId DefaultAiTranslateTo(LanguageId offeredFrom) {
	const auto current = LanguageId{
		QLocale(Lang::LanguageIdOrDefault(Lang::Id())).language()
	};
	if (current && (current != offeredFrom)) {
		return current;
	}
	const auto english = LanguageId{ QLocale::English };
	if (english != offeredFrom) {
		return english;
	}
	return LanguageId{ QLocale::Spanish };
}

[[nodiscard]] TextWithEntities SelectorTitle(LanguageId id) {
	return tr::lng_ai_compose_in_language(
		tr::now,
		lt_language,
		tr::link(Ui::LanguageName(id)),
		tr::marked);
}

class ResponseIsland final : public Ui::RpWidget {
public:
	ResponseIsland(
		QWidget *parent,
		not_null<Main::Session*> session,
		std::shared_ptr<const RichPage> page,
		LanguageId language,
		bool emojify,
		Fn<void()> chooseLanguage,
		Fn<void(bool)> emojifyChanged);
	~ResponseIsland();

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	bool eventHook(QEvent *e) override;

	void paintArticle(Painter &p, QRect clip);
	void requestArticleRepaint(QRect articleRect);
	void detachArticleBindings();

	[[nodiscard]] int controlRowHeight() const;
	[[nodiscard]] QRect articleRect() const;
	[[nodiscard]] Iv::Markdown::MarkdownArticle *scrollTarget();

	const not_null<Main::Session*> _session;
	const object_ptr<Ui::FlatLabel> _selector;
	const object_ptr<Ui::RpWidget> _arrows;
	const object_ptr<Ui::Checkbox> _emojify;
	std::shared_ptr<Iv::Markdown::MediaRuntime> _mediaRuntime;
	Iv::Markdown::MarkdownArticle _article;
	std::unique_ptr<Ui::ChatTheme> _theme;
	std::unique_ptr<Ui::ChatStyle> _style;
	Iv::Markdown::MarkdownArticleScrollForwarder _scrollForwarder;
	int _articleHeight = 0;
	int _paletteVersion = -1;
	bool _hasArticle = false;

};

ResponseIsland::ResponseIsland(
	QWidget *parent,
	not_null<Main::Session*> session,
	std::shared_ptr<const RichPage> page,
	LanguageId language,
	bool emojify,
	Fn<void()> chooseLanguage,
	Fn<void(bool)> emojifyChanged)
: RpWidget(parent)
, _session(session)
, _selector(this, st::aiComposeCardTitle)
, _arrows(this)
, _emojify(
	this,
	tr::lng_ai_compose_emojify(tr::now),
	st::aiComposeEmojifyCheckbox,
	std::make_unique<Ui::RoundCheckView>(st::defaultCheck, emojify))
, _article(st::aiComposeCardMarkdown)
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _style(std::make_unique<Ui::ChatStyle>(session->colorIndicesValue())) {
	_style->apply(_theme.get());
	_paletteVersion = _style->paletteVersion();
	setAttribute(Qt::WA_AcceptTouchEvents);

	_selector->setMarkedText(SelectorTitle(language));
	_selector->setClickHandlerFilter([=](const auto &...) {
		if (chooseLanguage) {
			chooseLanguage();
		}
		return false;
	});

	const auto &icon = st::createAiSelectorArrowsIcon;
	_arrows->setAttribute(Qt::WA_TransparentForMouseEvents);
	_arrows->resize(icon.size());
	_arrows->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_arrows.data());
		st::createAiSelectorArrowsIcon.paint(p, 0, 0, _arrows->width());
	}, _arrows->lifetime());

	_emojify->checkedChanges() | rpl::on_next([=](bool checked) {
		if (emojifyChanged) {
			emojifyChanged(checked);
		}
	}, _emojify->lifetime());

	const auto weak = base::make_weak(this);
	_article.setTextRepaintCallbacks(
		[weak] {
			if (const auto owner = weak.get()) {
				owner->requestArticleRepaint(QRect());
			}
		},
		[weak](QRect rect) {
			if (const auto owner = weak.get()) {
				owner->requestArticleRepaint(rect);
			}
		});

	const auto richLimits = Iv::ResolveRichMessageLimits(session);
	_mediaRuntime = Iv::CreateMessageMediaRuntime(
		session,
		FullMsgId(),
		[](QString) {},
		[](QString) {},
		::Data::FileOrigin());
	auto prepared = Iv::Markdown::TryPrepareNativeInstantView({
		.richPage = page,
		.mediaRuntime = _mediaRuntime,
		.dimensionsOverride = Iv::Markdown::CaptureMarkdownPrepareDimensions(
			st::aiComposeCardMarkdown),
		.tableRenderLimits
			= Iv::Markdown::PrepareTableRenderLimitsForRichMessage(richLimits),
	});
	if (prepared.supported()) {
		_article.setContent(std::move(prepared.content));
		_hasArticle = true;
	}
}

ResponseIsland::~ResponseIsland() {
	detachArticleBindings();
}

void ResponseIsland::detachArticleBindings() {
	_article.setTextRepaintCallbacks(nullptr, nullptr);
}

void ResponseIsland::requestArticleRepaint(QRect rect) {
	crl::on_main(this, [=] {
		if (rect.isEmpty()) {
			update();
		} else {
			update(rect.translated(articleRect().topLeft()));
		}
	});
}

QRect ResponseIsland::articleRect() const {
	const auto top = st::aiComposeCardPadding.top()
		+ controlRowHeight()
		+ st::aiComposeCardSectionSkip;
	return QRect(0, top, width(), _articleHeight);
}

Iv::Markdown::MarkdownArticle *ResponseIsland::scrollTarget() {
	return _hasArticle ? &_article : nullptr;
}

void ResponseIsland::wheelEvent(QWheelEvent *e) {
	_scrollForwarder.handleWheel(scrollTarget(), e, articleRect().topLeft());
}

void ResponseIsland::mousePressEvent(QMouseEvent *e) {
	if (!_scrollForwarder.handleMousePress(
			scrollTarget(),
			e,
			articleRect().topLeft())) {
		RpWidget::mousePressEvent(e);
	}
}

void ResponseIsland::mouseMoveEvent(QMouseEvent *e) {
	if (!_scrollForwarder.handleMouseMove(
			scrollTarget(),
			e,
			articleRect().topLeft())) {
		RpWidget::mouseMoveEvent(e);
	}
}

void ResponseIsland::mouseReleaseEvent(QMouseEvent *e) {
	if (!_scrollForwarder.handleMouseRelease(
			scrollTarget(),
			e,
			articleRect().topLeft())) {
		RpWidget::mouseReleaseEvent(e);
	}
}

bool ResponseIsland::eventHook(QEvent *e) {
	if (Iv::Markdown::MarkdownArticleScrollForwarder::IsTouchEvent(e)
		&& _scrollForwarder.handleTouchHook(
			scrollTarget(),
			this,
			e,
			articleRect().topLeft())) {
		return true;
	}
	return RpWidget::eventHook(e);
}

void ResponseIsland::paintArticle(Painter &p, QRect clip) {
	if (!_hasArticle) {
		return;
	}
	if (_paletteVersion != _style->paletteVersion()) {
		_paletteVersion = _style->paletteVersion();
		_article.invalidatePaletteCache();
	}
	const auto content = articleRect();
	if (content.isEmpty()) {
		return;
	}
	const auto articleClip = content.intersected(clip).translated(
		-content.topLeft());
	if (articleClip.isEmpty()) {
		return;
	}
	auto context = Iv::Markdown::MarkdownArticlePaintContext(
		_theme->preparePaintContext(
			_style.get(),
			QRect(QPoint(), content.size()),
			QRect(QPoint(), content.size()),
			articleClip,
			false));
	const auto messageStyle = context.messageStyle();
	context.caches = {
		.pre = messageStyle->preCache.get(),
		.blockquote = context.quoteCache({}, 0),
		.colors = _style->highlightColors(),
		.st = &messageStyle->richPageStyle,
		.repaint = [weak = base::make_weak(this)] {
			if (const auto owner = weak.get()) {
				owner->requestArticleRepaint(QRect());
			}
		},
		.repaintRect = [weak = base::make_weak(this)](QRect rect) {
			if (const auto owner = weak.get()) {
				owner->requestArticleRepaint(rect);
			}
		},
	};
	_article.setVisibleTopBottom(0, content.height());
	p.save();
	p.setClipRect(content.intersected(clip));
	p.translate(content.topLeft());
	_article.paint(p, context);
	p.restore();
}

int ResponseIsland::controlRowHeight() const {
	return std::max(_selector->height(), _emojify->height());
}

void ResponseIsland::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::aiComposeCardBg);
		p.drawRoundedRect(
			rect(),
			st::aiComposeCardRadius,
			st::aiComposeCardRadius);
	}
	paintArticle(p, e->rect());
}

void ResponseIsland::resizeEvent(QResizeEvent *e) {
	const auto &padding = st::aiComposeCardPadding;
	const auto inner = width() - padding.left() - padding.right();
	const auto rowHeight = controlRowHeight();

	_emojify->resizeToNaturalWidth(inner);
	_emojify->moveToRight(
		padding.left() - _emojify->getMargins().left(),
		padding.top(),
		width());

	const auto arrowsSkip = st::aiComposeCardControlSkip;
	const auto selectorWidth = std::max(
		inner
			- _emojify->width()
			- st::aiComposeCardControlSkip
			- _arrows->width()
			- arrowsSkip,
		0);
	_selector->resizeToWidth(selectorWidth);
	_selector->moveToLeft(padding.left(), padding.top());
	_arrows->moveToLeft(
		padding.left() + _selector->textMaxWidth() + arrowsSkip,
		padding.top() + (_selector->height() - _arrows->height()) / 2);

	auto y = padding.top() + rowHeight;
	if (_hasArticle && width() > 0) {
		y += st::aiComposeCardSectionSkip;
		_articleHeight = _article.resizeGetHeight(width());
		y += _articleHeight;
	}
	y += padding.bottom();
	if (height() != y) {
		resize(width(), y);
	}
}

struct State {
	not_null<Main::Session*> session;
	Fn<void(std::shared_ptr<const RichPage>)> applyToPage;
	mtpRequestId requestId = 0;
	Ui::InputField *prompt = nullptr;
	LanguageId language;
	bool emojify = false;
	enum class Phase {
		Initial,
		Loading,
		HasResult,
	};
	Phase phase = Phase::Initial;
	rpl::variable<bool> loading = false;
	std::shared_ptr<const RichPage> page;
	Ui::SlideWrap<ResponseIsland> *responseWrap = nullptr;
	Ui::RoundButton *primaryButton = nullptr;
	Fn<void()> generate;
	Fn<void()> rebuildButtons;
	Fn<void()> rebuildResponseIsland;
	Fn<void()> enterLoading;
	Fn<void()> updateGenerateEnabled;
};

} // namespace

void CreateAiBox(not_null<Ui::GenericBox*> box, CreateAiBoxArgs &&args) {
	const auto state = box->lifetime().make_state<State>(State{
		.session = args.session,
		.applyToPage = std::move(args.applyToPage),
		.language = DefaultAiTranslateTo(LanguageId()),
	});

	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::lng_ai_compose_create_title());
	box->setStyle(st::aiComposeBox);

	const auto prompt = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::aiTonePromptField,
			Ui::InputField::Mode::MultiLine,
			rpl::producer<QString>()),
		st::aiToneFieldsMargin);
	prompt->setSubmitSettings(Ui::InputField::SubmitSettings::None);
	const auto promptLimit = state->session->appConfig().get<int>(
		u"aicompose_tone_prompt_length_max"_q,
		1024);
	Ui::AddLengthLimitLabel(prompt, promptLimit, {
		.customThreshold = kPromptRemainingThreshold,
	});
	state->prompt = prompt;

	const auto promptPad = st::aiToneFieldPadding;
	const auto promptLineHeight = st::aiTonePromptField.style.font->height;
	const auto promptMaxLines = 7;
	prompt->setMaxHeight((promptLineHeight * promptMaxLines) / 2
		+ promptPad.top()
		+ promptPad.bottom());

	const auto promptPlaceholder = AddAiComposeFieldDecor(
		prompt,
		tr::lng_ai_compose_create_placeholder());

	promptPlaceholder->heightValue(
	) | rpl::on_next([=](int phHeight) {
		const auto pad = st::aiToneFieldPadding;
		prompt->setMinHeight(phHeight + pad.top() + pad.bottom());
	}, prompt->lifetime());

	state->updateGenerateEnabled = [=] {
		const auto pill = state->primaryButton;
		if (!pill || state->phase != State::Phase::Initial) {
			return;
		}
		const auto text = state->prompt->getLastText();
		const auto disabled = text.trimmed().isEmpty()
			|| (text.size() > promptLimit);
		pill->setDisabled(disabled);
		pill->setAttribute(Qt::WA_TransparentForMouseEvents, disabled);
		pill->setTextFgOverride(disabled
			? anim::color(st::activeButtonBg, st::activeButtonFg, 0.5)
			: std::optional<QColor>());
	};

	state->prompt->changes(
	) | rpl::on_next([=] {
		state->updateGenerateEnabled();
		if (!state->page) {
			return;
		}
		if (const auto wrap = state->responseWrap) {
			wrap->setDirectionUp(true);
			wrap->toggle(false, anim::type::normal);
			wrap->setFinishedCallback([=] {
				if (state->responseWrap == wrap && !wrap->toggled()) {
					delete state->responseWrap;
					state->responseWrap = nullptr;
				}
			});
		}
		state->page = nullptr;
		state->phase = State::Phase::Initial;
		if (state->requestId) {
			state->session->api().request(state->requestId).cancel();
			state->requestId = 0;
		}
		state->loading = false;
		state->rebuildButtons();
	}, prompt->lifetime());

	const auto chooseLanguage = [=] {
		box->uiShow()->showBox(Box([=](not_null<Ui::GenericBox*> chooser) {
			Ui::ChooseLanguageBox(
				chooser,
				tr::lng_languages(),
				[=](std::vector<LanguageId> ids) {
					if (ids.empty()) {
						return;
					}
					state->language = ids.front();
					state->generate();
				},
				{ state->language },
				false,
				nullptr);
		}));
	};

	state->rebuildResponseIsland = [=] {
		if (state->responseWrap) {
			delete state->responseWrap;
			state->responseWrap = nullptr;
		}
		if (!state->page) {
			return;
		}
		const auto content = box->verticalLayout();
		auto wrap = object_ptr<Ui::SlideWrap<ResponseIsland>>(
			content,
			object_ptr<ResponseIsland>(
				content,
				state->session,
				state->page,
				state->language,
				state->emojify,
				chooseLanguage,
				[=](bool checked) {
					state->emojify = checked;
					state->generate();
				}),
			style::margins(
				st::aiComposeContentMargin.left(),
				st::aiComposeCardSectionSkip,
				st::aiComposeContentMargin.right(),
				0));
		const auto ptr = wrap.data();
		content->add(std::move(wrap));
		state->responseWrap = ptr;
		ptr->toggle(false, anim::type::instant);
		ptr->toggle(true, anim::type::normal);
	};

	const auto addPill = [=](
			rpl::producer<QString> text,
			Fn<void()> callback) {
		const auto pill = box->addButton(
			rpl::conditional(
				state->loading.value(),
				rpl::single(QString()),
				std::move(text)),
			std::move(callback));
		pill->setFullRadius(true);
		using namespace Info::Statistics;
		const auto animation = InfiniteRadialAnimationWidget(
			pill,
			pill->height() / 2);
		AddChildToWidgetCenter(pill, animation);
		animation->showOn(state->loading.value());
		state->primaryButton = pill;
	};

	state->rebuildButtons = [=] {
		box->clearButtons();
		box->addTopButton(st::aiComposeBoxClose, [=] {
			box->closeBox();
		})->setAccessibleName(tr::lng_close(tr::now));
		box->addTopButton(st::aiComposeBoxInfoButton, [=] {
			box->uiShow()->show(Box(Ui::AboutCocoonBox));
		})->setAccessibleName(tr::lng_sr_ai_compose_info(tr::now));
		state->primaryButton = nullptr;
		if (state->phase == State::Phase::HasResult) {
			addPill(tr::lng_ai_compose_add_to_page(), [=] {
				if (state->applyToPage) {
					state->applyToPage(state->page);
				}
				box->closeBox();
			});
		} else {
			addPill(tr::lng_ai_compose_generate(), [=] {
				state->generate();
			});
			state->updateGenerateEnabled();
		}
	};

	state->enterLoading = [=] {
		if (!state->primaryButton) {
			state->rebuildButtons();
		}
		const auto pill = state->primaryButton;
		pill->setDisabled(true);
		pill->setAttribute(Qt::WA_TransparentForMouseEvents);
		pill->setClickedCallback([] {});
	};

	state->generate = [=] {
		const auto prompt = state->prompt->getLastText();
		if (prompt.trimmed().isEmpty()
			|| prompt.size() > promptLimit) {
			return;
		}
		if (state->requestId) {
			state->session->api().request(state->requestId).cancel();
			state->requestId = 0;
		}
		state->phase = State::Phase::Loading;
		state->loading = true;
		state->enterLoading();

		using Flag = MTPmessages_composeRichMessageWithAI::Flag;
		auto flags = MTPmessages_composeRichMessageWithAI::Flags(0)
			| Flag::f_tone;
		if (state->emojify) {
			flags |= Flag::f_emojify;
		}
		const auto lang = state->language
			? state->language.twoLetterCode()
			: QString();
		if (!lang.isEmpty()) {
			flags |= Flag::f_translate_to_lang;
		}
		state->requestId = state->session->api().request(
			MTPmessages_ComposeRichMessageWithAI(
				MTP_flags(flags),
				MTPInputRichMessage(),
				lang.isEmpty() ? MTPstring() : MTP_string(lang),
				MTP_inputAiComposeToneSingleUse(MTP_string(prompt)))
		).done([=](const MTPmessages_ComposedRichMessageWithAI &result) {
			state->requestId = 0;
			state->loading = false;
			state->page = Iv::ParseRichPage(
				state->session,
				result.data().vresult());
			state->phase = State::Phase::HasResult;
			state->rebuildResponseIsland();
			state->rebuildButtons();
			state->prompt->clearFocus();
		}).fail([=](const MTP::Error &error) {
			state->requestId = 0;
			state->loading = false;
			state->phase = state->page
				? State::Phase::HasResult
				: State::Phase::Initial;
			state->rebuildButtons();
			if (MTP::IgnoreError(error)) {
				return;
			}
			box->showToast(error.type());
		}).handleFloodErrors().send();
	};

	state->rebuildButtons();
}

void ShowCreateAiBox(
		std::shared_ptr<ChatHelpers::Show> show,
		CreateAiBoxArgs &&args) {
	show->show(Box(CreateAiBox, std::move(args)));
}

} // namespace Iv::Editor
