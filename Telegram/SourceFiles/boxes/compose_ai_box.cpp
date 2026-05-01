/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/compose_ai_box.h"

#include "api/api_compose_with_ai.h"
#include "apiwrap.h"
#include "boxes/create_ai_tone_box.h"
#include "boxes/premium_preview_box.h"
#include "boxes/share_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "core/ui_integration.h"
#include "data/data_ai_compose_tones.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "settings/sections/settings_premium.h"
#include "spellcheck/platform/platform_language.h"
#include "ui/boxes/about_cocoon_box.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/labeled_emoji_tabs.h"
#include "ui/controls/send_button.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/skeleton_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/custom_emoji_text_badge.h"
#include "ui/text/text_extended_data.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/tooltip.h"
#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <algorithm>
#include <array>

namespace HistoryView::Controls {
namespace {

constexpr auto kAiComposeStyleTooltipHiddenPref = "ai_compose_style_tooltip_hidden"_cs;

enum class ComposeAiMode {
	Translate,
	Style,
	Fix,
};

enum class CardState {
	Waiting,
	Loading,
	Ready,
	Failed,
};

[[nodiscard]] QColor ComposeAiColorWithAlpha(
		const style::color &color,
		float64 alpha) {
	auto result = color->c;
	result.setAlphaF(result.alphaF() * alpha);
	return result;
}

[[nodiscard]] TextWithEntities HighlightDiff(TextWithEntities text) {
	return Ui::Text::Colorized(
		Ui::Text::Wrapped(std::move(text), EntityType::Underline), 1);
}

[[nodiscard]] TextWithEntities StrikeOutDiff(TextWithEntities text) {
	return Ui::Text::Colorized(
		Ui::Text::Wrapped(std::move(text), EntityType::StrikeOut), 2);
}

[[nodiscard]] TextWithEntities BuildDiffDisplay(
		const Api::ComposeWithAi::Diff &diff) {
	auto result = TextWithEntities();
	auto entities = diff.entities;
	std::stable_sort(
		entities.begin(),
		entities.end(),
		[](const auto &a, const auto &b) {
			return a.offset < b.offset;
		});
	const auto size = int(diff.text.text.size());
	auto taken = 0;
	for (const auto &entity : entities) {
		const auto offset = std::clamp(entity.offset, 0, size);
		const auto length = std::clamp(entity.length, 0, size - offset);
		if (offset > taken) {
			result.append(Ui::Text::Mid(diff.text, taken, offset - taken));
		}
		auto part = Ui::Text::Mid(diff.text, offset, length);
		switch (entity.type) {
		case Api::ComposeWithAi::DiffEntity::Type::Insert:
			result.append(HighlightDiff(std::move(part)));
			break;
		case Api::ComposeWithAi::DiffEntity::Type::Replace:
			if (!entity.oldText.isEmpty()) {
				result.append(
					StrikeOutDiff(
						TextWithEntities::Simple(entity.oldText)));
			}
			result.append(HighlightDiff(std::move(part)));
			break;
		case Api::ComposeWithAi::DiffEntity::Type::Delete:
			result.append(StrikeOutDiff(std::move(part)));
			break;
		}
		taken = std::max(taken, offset + length);
	}
	if (taken < size) {
		result.append(Ui::Text::Mid(diff.text, taken));
	}
	return result;
}

[[nodiscard]] QString FromTitle(LanguageId id) {
	return tr::lng_ai_compose_original(tr::now);
}

[[nodiscard]] TextWithEntities ToTitle(
		LanguageId id,
		const QString &style) {
	const auto name = style.isEmpty()
		? tr::link(Ui::LanguageName(id))
		: tr::link(tr::lng_ai_compose_name_style(
			tr::now,
			lt_name,
			tr::marked(Ui::LanguageName(id)),
			lt_style,
			tr::marked(style),
			tr::marked));
	return tr::lng_ai_compose_to_language(
		tr::now,
		lt_language,
		name,
		tr::marked);
}

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

[[nodiscard]] const style::icon &ModeIcon(
		ComposeAiMode mode,
		bool active) {
	switch (mode) {
	case ComposeAiMode::Translate:
		return active
			? st::aiComposeTabTranslateIconActive
			: st::aiComposeTabTranslateIcon;
	case ComposeAiMode::Style:
		return active
			? st::aiComposeTabStyleIconActive
			: st::aiComposeTabStyleIcon;
	case ComposeAiMode::Fix:
		return active
			? st::aiComposeTabFixIconActive
			: st::aiComposeTabFixIcon;
	}
	return active
		? st::aiComposeTabTranslateIconActive
		: st::aiComposeTabTranslateIcon;
}

[[nodiscard]] qreal ComposeAiPillRadius(int height) {
	return height / 2.;
}

[[nodiscard]] QColor ComposeAiActiveBackgroundColor(
		const style::color &color) {
	return ComposeAiColorWithAlpha(
		color,
		st::aiComposeButtonBgActiveOpacity);
}

[[nodiscard]] QColor ComposeAiRippleColor(
		const style::RippleAnimation &ripple,
		float64 opacity) {
	return ComposeAiColorWithAlpha(
		ripple.color,
		opacity);
}

[[nodiscard]] Ui::LabeledEmojiTab ResolveStyleDescriptor(
		const Data::AiComposeTone &tone) {
	return {
		.id = tone.isDefault ? tone.defaultType : QString::number(tone.id),
		.label = tone.title,
		.customEmojiData = tone.emojiId
			? Data::SerializeCustomEmojiId(tone.emojiId)
			: QString(),
	};
}

[[nodiscard]] std::vector<Ui::LabeledEmojiTab> ResolveStyleDescriptors(
		const std::vector<Data::AiComposeTone> &tones) {
	auto result = std::vector<Ui::LabeledEmojiTab>();
	result.reserve(tones.size());
	for (const auto &tone : tones) {
		result.push_back(ResolveStyleDescriptor(tone));
	}
	return result;
}

[[nodiscard]] std::vector<Ui::LabeledEmojiTab> ResolveTranslateStyleDescriptors(
		not_null<Main::Session*> session,
		const std::vector<Ui::LabeledEmojiTab> &styles) {
	const auto neutral = ChatHelpers::GenerateLocalTgsSticker(
		session,
		u"chat/white_flag_emoji"_q);
	auto result = std::vector<Ui::LabeledEmojiTab>();
	result.reserve(styles.size() + 1);
	result.push_back({
		.id = QString(),
		.label = tr::lng_ai_compose_style_neutral(tr::now),
		.customEmojiData = Data::SerializeCustomEmojiId(neutral->id),
	});
	result.insert(end(result), begin(styles), end(styles));
	return result;
}

[[nodiscard]] auto WithAddStyleTab(std::vector<Ui::LabeledEmojiTab> tabs)
-> std::vector<Ui::LabeledEmojiTab> {
	tabs.push_back({
		.id = u"_add_style"_q,
		.label = tr::lng_ai_compose_tone_create(tr::now),
		.icon = &st::aiComposeAddStyleIcon,
		.iconActive = &st::aiComposeAddStyleIconOver,
	});
	return tabs;
}

[[nodiscard]] TextWithEntities LoadingTitleSparkle(
		not_null<Main::Session*> session) {
	const auto sparkles = ChatHelpers::GenerateLocalTgsSticker(
		session,
		u"chat/sparkles_emoji"_q);
	return tr::marked(u" "_q)
		.append(Data::SingleCustomEmoji(sparkles->id));
}

class ComposeAiModeButton final : public Ui::RippleButton {
public:
	ComposeAiModeButton(
		QWidget *parent,
		ComposeAiMode mode,
		QString label);

	void setSelected(bool selected);
	[[nodiscard]] ComposeAiMode mode() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	[[nodiscard]] QImage prepareRippleMask() const override;

private:
	const ComposeAiMode _mode;
	const QString _label;
	bool _selected = false;

};

class ComposeAiModeTabs final : public Ui::RpWidget {
public:
	ComposeAiModeTabs(QWidget *parent);

	void setActive(ComposeAiMode mode);
	void setChangedCallback(Fn<void(ComposeAiMode)> callback);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const not_null<ComposeAiModeButton*> _translate;
	const not_null<ComposeAiModeButton*> _style;
	const not_null<ComposeAiModeButton*> _fix;
	Fn<void(ComposeAiMode)> _changed;
	ComposeAiMode _active = ComposeAiMode::Style;

};

class ComposeAiPreviewCard final : public Ui::RpWidget {
public:
	ComposeAiPreviewCard(
		QWidget *parent,
		not_null<Main::Session*> session,
		TextWithEntities original,
		std::shared_ptr<Ui::ChatStyle> chatStyle);

	void setResizeCallback(Fn<void()> callback);
	void setChooseCallback(Fn<void()> callback);
	void setCopyCallback(Fn<void()> callback);
	void setEmojifyChangedCallback(Fn<void(bool)> callback);
	void setOriginalTitle(const QString &title);
	void setOriginalVisible(bool visible);
	void setResultTitle(const TextWithEntities &title);
	void setEmojifyVisible(bool visible);
	void setEmojifyChecked(bool checked);
	void setState(CardState state);
	void setResultText(TextWithEntities text);
	void setShow(std::shared_ptr<Ui::Show> show);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void refreshGeometry();
	void updateOriginalToggleIcon();

	const Ui::Text::MarkedContext _context;
	const TextWithEntities _original;
	const not_null<Ui::FlatLabel*> _originalTitle;
	const not_null<Ui::FlatLabel*> _originalBody;
	const not_null<Ui::IconButton*> _originalToggle;
	const not_null<Ui::FlatLabel*> _resultTitle;
	const not_null<Ui::FlatLabel*> _resultBody;
	const not_null<Ui::IconButton*> _copy;
	const not_null<Ui::Checkbox*> _emojify;
	Fn<void()> _resized;
	Fn<void()> _chooseCallback;
	Fn<void()> _copyCallback;
	Fn<void(bool)> _emojifyChanged;
	bool _ignoreResizedCallback = false;
	bool _originalExpanded = false;
	bool _originalVisible = true;
	bool _emojifyVisible = false;
	bool _dividerVisible = false;
	int _dividerTop = 0;
	CardState _state = CardState::Waiting;
	Ui::SkeletonAnimation _skeleton;
	std::array<Ui::Text::SpecialColor, 2> _diffColors;

};

class ComposeAiContent final : public Ui::RpWidget {
public:
	ComposeAiContent(
		QWidget *parent,
		not_null<Ui::GenericBox*> box,
		ComposeAiBoxArgs args);
	~ComposeAiContent();

	[[nodiscard]] bool hasResult() const;
	[[nodiscard]] const TextWithEntities &result() const;
	[[nodiscard]] const std::vector<Ui::LabeledEmojiTab> &stylesData() const;
	[[nodiscard]] const std::vector<Data::AiComposeTone> &tones() const;
	void setReadyChangedCallback(Fn<void(bool)> callback);
	void setLoadingChangedCallback(Fn<void(bool)> callback);
	void setPremiumFloodCallback(Fn<void()> callback);
	void setModeChangedCallback(Fn<void(ComposeAiMode)> callback);
	void setStyleSelectedCallback(Fn<void()> callback);
	[[nodiscard]] ComposeAiMode mode() const;
	[[nodiscard]] bool hasStyleSelection() const;
	void setModeTabs(not_null<ComposeAiModeTabs*> tabs);
	void setStyleTabs(not_null<Ui::SlideWrap<Ui::LabeledEmojiScrollTabs>*> stylesWrap);
	void refreshTones();
	void selectToneById(uint64 id);
	void start();

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void refreshLayout();
	void chooseLanguage();
	void copyResult();
	void setMode(ComposeAiMode mode);
	void updateTitles();
	void updatePinnedTabs(anim::type animated);
	void cancelRequest();
	void request();
	void resetState(CardState state);
	void applyResult(Api::ComposeWithAi::Result &&result);
	void showError(const QString &error = {});
	void setAuthorId(UserId authorId);
	void notifyLoadingChanged();
	void notifyReadyChanged();
	[[nodiscard]] QString currentTranslateStyle() const;
	[[nodiscard]] QString currentTranslateStyleLabel() const;

	const not_null<Ui::GenericBox*> _box;
	const not_null<Main::Session*> _session;
	const TextWithEntities _original;
	const LanguageId _detectedFrom;
	LanguageId _to;
	std::vector<Data::AiComposeTone> _tones;
	std::vector<Ui::LabeledEmojiTab> _stylesData;
	std::vector<Ui::LabeledEmojiTab> _translateStylesData;
	QPointer<ComposeAiModeTabs> _tabs;
	QPointer<Ui::LabeledEmojiScrollTabs> _styles;
	QPointer<Ui::SlideWrap<Ui::LabeledEmojiScrollTabs>> _stylesWrap;
	const not_null<ComposeAiPreviewCard*> _preview;
	const not_null<Ui::FlatLabel*> _authorLabel;
	Fn<void(bool)> _readyChanged;
	Fn<void(bool)> _loadingChanged;
	Fn<void()> _premiumFlood;
	Fn<void(ComposeAiMode)> _modeChanged;
	Fn<void()> _styleSelected;
	ComposeAiMode _mode = ComposeAiMode::Style;
	int _styleIndex = -1;
	int _translateStyleIndex = 0;
	UserId _authorId = UserId(0);
	bool _emojify = false;
	CardState _state = CardState::Waiting;
	mtpRequestId _requestId = 0;
	int _requestToken = 0;
	TextWithEntities _result;

};

// ComposeAiModeButton

ComposeAiModeButton::ComposeAiModeButton(
	QWidget *parent,
	ComposeAiMode mode,
	QString label)
: RippleButton(parent, st::aiComposeButtonRippleInactive)
, _mode(mode)
, _label(std::move(label)) {
	setCursor(style::cur_pointer);
	setAccessibleName(_label);
}

void ComposeAiModeButton::setSelected(bool selected) {
	if (_selected == selected) {
		return;
	}
	_selected = selected;
	update();
}

ComposeAiMode ComposeAiModeButton::mode() const {
	return _mode;
}

void ComposeAiModeButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);

	const auto radius = ComposeAiPillRadius(height());
	if (_selected) {
		p.setPen(Qt::NoPen);
		p.setBrush(ComposeAiActiveBackgroundColor(
			st::aiComposeTabButtonBgActive));
		p.drawRoundedRect(
			rect(),
			radius,
			radius);
	}
	const auto ripple = ComposeAiRippleColor(
		_selected
			? st::aiComposeButtonRippleActive
			: st::aiComposeButtonRippleInactive,
		_selected
			? st::aiComposeButtonRippleActiveOpacity
			: st::aiComposeButtonRippleInactiveOpacity);
	paintRipple(p, 0, 0, &ripple);

	const auto &icon = ModeIcon(_mode, _selected);
	const auto iconLeft = (width() - icon.width()) / 2;
	icon.paint(p, iconLeft, st::aiComposeTabIconTop, width());

	p.setPen(_selected
		? st::aiComposeTabLabelFgActive
		: st::aiComposeTabLabelFg);
	p.setFont(st::aiComposeTabLabelFont);
	p.drawText(
		QRect(
			0,
			st::aiComposeTabLabelTop,
			width(),
			height() - st::aiComposeTabLabelTop),
		Qt::AlignHCenter | Qt::AlignTop,
		_label);
}

QImage ComposeAiModeButton::prepareRippleMask() const {
	return Ui::RippleAnimation::MaskByDrawer(size(), false, [&](QPainter &p) {
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		const auto radius = ComposeAiPillRadius(height());
		p.drawRoundedRect(
			rect(),
			radius,
			radius);
	});
}

// ComposeAiModeTabs

ComposeAiModeTabs::ComposeAiModeTabs(QWidget *parent)
: RpWidget(parent)
, _translate(Ui::CreateChild<ComposeAiModeButton>(
	this,
	ComposeAiMode::Translate,
	tr::lng_ai_compose_tab_translate(tr::now)))
, _style(Ui::CreateChild<ComposeAiModeButton>(
	this,
	ComposeAiMode::Style,
	tr::lng_ai_compose_tab_style(tr::now)))
, _fix(Ui::CreateChild<ComposeAiModeButton>(
	this,
	ComposeAiMode::Fix,
	tr::lng_ai_compose_tab_fix(tr::now))) {
	const auto bind = [=](not_null<ComposeAiModeButton*> button) {
		button->setClickedCallback([=] {
			setActive(button->mode());
			if (_changed) {
				_changed(button->mode());
			}
		});
	};
	bind(_translate);
	bind(_style);
	bind(_fix);
	setActive(ComposeAiMode::Style);
}

void ComposeAiModeTabs::setActive(ComposeAiMode mode) {
	_active = mode;
	_translate->setSelected(mode == ComposeAiMode::Translate);
	_style->setSelected(mode == ComposeAiMode::Style);
	_fix->setSelected(mode == ComposeAiMode::Fix);
}

void ComposeAiModeTabs::setChangedCallback(Fn<void(ComposeAiMode)> callback) {
	_changed = std::move(callback);
}

int ComposeAiModeTabs::resizeGetHeight(int newWidth) {
	const auto padding = st::aiComposeTabsPadding;
	const auto skip = st::aiComposeTabsSkip;
	const auto innerWidth = newWidth - padding.left() - padding.right();
	const auto buttonWidth = (innerWidth - (2 * skip)) / 3;
	const auto buttonHeight = st::aiComposeTabsHeight
		- padding.top()
		- padding.bottom();
	const auto top = padding.top();
	auto left = padding.left();
	for (const auto &button : { _translate, _style, _fix }) {
		button->setGeometry(left, top, buttonWidth, buttonHeight);
		left += buttonWidth + skip;
	}
	return st::aiComposeTabsHeight;
}

void ComposeAiModeTabs::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::aiComposeTabsBg);
	const auto radius = st::aiComposeTabsRadius;
	p.drawRoundedRect(
		rect(),
		radius,
		radius);
}

// ComposeAiPreviewCard

ComposeAiPreviewCard::ComposeAiPreviewCard(
	QWidget *parent,
	not_null<Main::Session*> session,
	TextWithEntities original,
	std::shared_ptr<Ui::ChatStyle> chatStyle)
: RpWidget(parent)
, _context(Core::TextContext({ .session = session }))
, _original(std::move(original))
, _originalTitle(Ui::CreateChild<Ui::FlatLabel>(
	this,
	st::aiComposeCardTitle))
, _originalBody(Ui::CreateChild<Ui::FlatLabel>(
	this,
	st::aiComposeBodyLabel))
, _originalToggle(Ui::CreateChild<Ui::IconButton>(
	this,
	st::aiComposeExpandButton))
, _resultTitle(Ui::CreateChild<Ui::FlatLabel>(
	this,
	st::aiComposeCardTitle))
, _resultBody(Ui::CreateChild<Ui::FlatLabel>(
	this,
	st::aiComposeBodyLabel))
, _copy(Ui::CreateChild<Ui::IconButton>(
	this,
	st::aiComposeCopyButton))
, _emojify(
	Ui::CreateChild<Ui::Checkbox>(
		this,
		tr::lng_ai_compose_emojify(tr::now),
		st::aiComposeEmojifyCheckbox,
		std::make_unique<Ui::RoundCheckView>(st::defaultCheck,false)))
, _skeleton(_resultBody) {
	_originalBody->setSelectable(true);
	_originalBody->setMarkedText(_original, _context);
	_resultTitle->setClickHandlerFilter([=](const auto &...) {
		if (_chooseCallback) {
			_chooseCallback();
		}
		return false;
	});
	_resultBody->setSelectable(true);
	const auto watchHeight = [=](not_null<Ui::FlatLabel*> label) {
		label->heightValue(
		) | rpl::skip(1) | rpl::on_next([=] {
			if (_resized && !_ignoreResizedCallback) {
				_resized();
			}
		}, lifetime());
	};
	watchHeight(_originalBody);
	watchHeight(_resultBody);
	_diffColors[0] = { &st::boxTextFgGood->p, &st::boxTextFgGood->p };
	_diffColors[1] = { &st::attentionButtonFg->p, &st::attentionButtonFg->p };
	_resultBody->setColors(_diffColors);
	_originalToggle->setClickedCallback([=] {
		_originalExpanded = !_originalExpanded;
		updateOriginalToggleIcon();
		if (_resized) {
			_resized();
		}
	});
	_copy->setClickedCallback([=] {
		if (_copyCallback) {
			_copyCallback();
		}
	});
	_copy->setAccessibleName(tr::lng_sr_ai_compose_copy_result(tr::now));
	_emojify->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		if (_emojifyChanged) {
			_emojifyChanged(checked);
		}
	}, _emojify->lifetime());
	setOriginalTitle(tr::lng_ai_compose_original(tr::now));
	setResultTitle(tr::lng_ai_compose_result(tr::now, tr::marked));
	_resultBody->setMarkedText(_original, _context);
	_copy->setVisible(false);
	updateOriginalToggleIcon();
	if (chatStyle) {
		const auto style = chatStyle;
		const auto s = session.get();
		const auto setupCaches = [=](not_null<Ui::FlatLabel*> label) {
			label->setPreCache([=] {
				return style->messageStyle(false, false).preCache.get();
			});
			label->setBlockquoteCache([=] {
				return style->coloredQuoteCache(
					false,
					s->user()->colorIndex());
			});
		};
		setupCaches(_originalBody);
		setupCaches(_resultBody);
	}
}

void ComposeAiPreviewCard::setResizeCallback(Fn<void()> callback) {
	_resized = std::move(callback);
}

void ComposeAiPreviewCard::setChooseCallback(Fn<void()> callback) {
	_chooseCallback = std::move(callback);
}

void ComposeAiPreviewCard::setCopyCallback(Fn<void()> callback) {
	_copyCallback = std::move(callback);
}

void ComposeAiPreviewCard::setEmojifyChangedCallback(Fn<void(bool)> callback) {
	_emojifyChanged = std::move(callback);
}

void ComposeAiPreviewCard::setOriginalTitle(const QString &title) {
	_originalTitle->setText(title);
	refreshGeometry();
}

void ComposeAiPreviewCard::setOriginalVisible(bool visible) {
	if (_originalVisible == visible) {
		return;
	}
	_originalVisible = visible;
	_originalTitle->setVisible(visible);
	_originalBody->setVisible(visible);
	_originalToggle->setVisible(false);
	refreshGeometry();
}

void ComposeAiPreviewCard::setResultTitle(const TextWithEntities &title) {
	_resultTitle->setMarkedText(title);
	refreshGeometry();
}

void ComposeAiPreviewCard::setEmojifyVisible(bool visible) {
	_emojifyVisible = visible;
	_emojify->setVisible(visible);
	refreshGeometry();
}

void ComposeAiPreviewCard::setEmojifyChecked(bool checked) {
	_emojify->setChecked(checked, Ui::Checkbox::NotifyAboutChange::DontNotify);
	refreshGeometry();
}

void ComposeAiPreviewCard::setState(CardState state) {
	if (_state == state) {
		return;
	}
	const auto wasLoading = (_state == CardState::Loading);
	_state = state;
	switch (_state) {
	case CardState::Waiting:
	case CardState::Failed:
		_resultBody->setMarkedText(_original, _context);
		_copy->setVisible(false);
		if (wasLoading) {
			_skeleton.stop();
		}
		break;
	case CardState::Loading:
		_resultBody->setMarkedText(_original, _context);
		_copy->setVisible(false);
		_skeleton.start();
		break;
	case CardState::Ready:
		_copy->setVisible(true);
		if (wasLoading) {
			_skeleton.stop();
		}
		break;
	}
	refreshGeometry();
}

void ComposeAiPreviewCard::setResultText(TextWithEntities text) {
	_resultBody->setMarkedText(std::move(text), _context);
	refreshGeometry();
}

void ComposeAiPreviewCard::setShow(std::shared_ptr<Ui::Show> show) {
	const auto setupFilter = [&](not_null<Ui::FlatLabel*> label) {
		label->setClickHandlerFilter([=](
				const ClickHandlerPtr &handler,
				Qt::MouseButton button) {
			if (dynamic_cast<Ui::Text::PreClickHandler*>(handler.get())) {
				ActivateClickHandler(label, handler, ClickContext{
					.button = button,
					.other = QVariant::fromValue(ClickHandlerContext{
						.show = show,
					})
				});
				return false;
			}
			return true;
		});
	};
	setupFilter(_originalBody);
	setupFilter(_resultBody);
}

int ComposeAiPreviewCard::resizeGetHeight(int newWidth) {
	const auto padding = st::aiComposeCardPadding;
	const auto contentWidth = newWidth - padding.left() - padding.right();
	auto y = padding.top();

	_dividerVisible = false;
	if (_originalVisible) {
		_originalTitle->show();
		_originalBody->show();
		_originalTitle->resizeToWidth(contentWidth);
		_originalToggle->setVisible(false);

		const auto toggleTop = y
			+ (_originalTitle->height() - _originalToggle->height()) / 2;
		_originalToggle->moveToRight(padding.right(), toggleTop, newWidth);
		const auto originalTitleWidth = contentWidth
			- _originalToggle->width()
			- st::aiComposeCardControlSkip;
		_originalTitle->setGeometryToLeft(
			padding.left(),
			y,
			std::max(originalTitleWidth, 0),
			_originalTitle->height(),
			newWidth);
		y = std::max(
			y + _originalTitle->height(),
			toggleTop + _originalToggle->height());

		_ignoreResizedCallback = true;
		const auto wasOriginalSize = _originalBody->size();
		_originalBody->resizeToWidth(contentWidth);
		const auto fullOriginalHeight = _originalBody->height();
		_originalBody->resize(wasOriginalSize);
		_ignoreResizedCallback = false;

		const auto lineHeight = _originalBody->st().style.lineHeight;
		const auto originalHeight = _originalExpanded
			? fullOriginalHeight
			: std::min(fullOriginalHeight, lineHeight);
		_originalBody->setGeometryToLeft(
			padding.left(),
			y,
			contentWidth,
			originalHeight,
			newWidth);
		const auto expandable = fullOriginalHeight > lineHeight;
		_originalToggle->setVisible(expandable);
		y += originalHeight + st::aiComposeCardSectionSkip;
		_dividerTop = y;
		_dividerVisible = true;
		y += st::lineWidth + st::aiComposeCardSectionSkip;
	} else {
		_originalTitle->hide();
		_originalBody->hide();
		_originalToggle->hide();
	}

	_resultTitle->show();
	auto controlsWidth = 0;
	if (_emojifyVisible) {
		_emojify->show();
		_emojify->resizeToNaturalWidth(contentWidth);
		controlsWidth += _emojify->width()
			+ st::aiComposeCardControlSkip;
	} else {
		_emojify->hide();
	}
	const auto resultTitleWidth = std::max(
		contentWidth - controlsWidth,
		0);
	_resultTitle->resizeToWidth(resultTitleWidth);
	auto right = padding.right();
	if (_emojifyVisible) {
		_emojify->moveToRight(right, y, newWidth);
		right += _emojify->width() + st::aiComposeCardControlSkip;
	}
	_resultTitle->setGeometryToLeft(
		padding.left(),
		y,
		resultTitleWidth,
		_resultTitle->height(),
		newWidth);
	y = std::max(
		y + _resultTitle->height(),
		(_emojifyVisible
			? (y - _emojify->getMargins().top() + _emojify->height())
			: 0));

	const auto lineHeight = _resultBody->st().style.lineHeight
		? _resultBody->st().style.lineHeight
		: _resultBody->st().style.font->height;
	if (!_copy->isHidden()) {
		_resultBody->setSkipBlock(_copy->width(), lineHeight);
	} else {
		_resultBody->setSkipBlock(0, 0);
	}
	_resultBody->resizeToWidth(contentWidth);
	_resultBody->setGeometryToLeft(
		padding.left(),
		y,
		contentWidth,
		_resultBody->height(),
		newWidth);
	if (!_copy->isHidden()) {
		_copy->moveToRight(
			padding.right(),
			y + _resultBody->height() - lineHeight,
			newWidth);
	}
	y += _resultBody->height();

	return y + padding.bottom();
}

void ComposeAiPreviewCard::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::aiComposeCardBg);
	p.drawRoundedRect(
		rect(),
		st::aiComposeCardRadius,
		st::aiComposeCardRadius);
	if (_dividerVisible) {
		p.setBrush(Qt::NoBrush);
		auto color = st::windowSubTextFg->c;
		color.setAlphaF(st::aiComposeShadowOpacity);
		p.setPen(color);
		p.drawLine(
			st::aiComposeCardPadding.left(),
			_dividerTop,
			width() - st::aiComposeCardPadding.right(),
			_dividerTop);
	}
}

void ComposeAiPreviewCard::refreshGeometry() {
	if (width() > 0) {
		resizeToWidth(width());
	}
	if (_resized) {
		_resized();
	}
}

void ComposeAiPreviewCard::updateOriginalToggleIcon() {
	_originalToggle->setIconOverride(
		_originalExpanded ? &st::aiComposeCollapseIcon : nullptr,
		_originalExpanded ? &st::aiComposeCollapseIcon : nullptr);
	_originalToggle->setAccessibleName(_originalExpanded
		? tr::lng_sr_ai_compose_collapse_original(tr::now)
		: tr::lng_sr_ai_compose_expand_original(tr::now));
}

// ComposeAiContent

ComposeAiContent::ComposeAiContent(
	QWidget *parent,
	not_null<Ui::GenericBox*> box,
	ComposeAiBoxArgs args)
: RpWidget(parent)
, _box(box)
, _session(args.session)
, _original(std::move(args.text))
, _detectedFrom(Platform::Language::Recognize(_original.text))
, _to(DefaultAiTranslateTo(_detectedFrom))
, _tones(_session->data().aiComposeTones().list())
, _stylesData(ResolveStyleDescriptors(_tones))
, _translateStylesData(ResolveTranslateStyleDescriptors(_session, _stylesData))
, _preview(
	Ui::CreateChild<ComposeAiPreviewCard>(
		this,
		_session,
		_original,
		args.chatStyle))
, _authorLabel(Ui::CreateChild<Ui::FlatLabel>(
	this,
	st::aiComposeAuthorLabel)) {
	_preview->setResizeCallback([=] { refreshLayout(); });
	_preview->setChooseCallback([=] { chooseLanguage(); });
	_preview->setCopyCallback([=] { copyResult(); });
	_preview->setEmojifyChangedCallback([=](bool checked) {
		_emojify = checked;
		if (_mode != ComposeAiMode::Fix) {
			request();
		}
	});
	_preview->setShow(_box->uiShow());
	_authorLabel->setVisible(false);
	_authorLabel->heightValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		refreshLayout();
	}, lifetime());
	const auto show = _box->uiShow();
	_authorLabel->setClickHandlerFilter([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton button) {
		if (dynamic_cast<Ui::Text::PreClickHandler*>(handler.get())) {
			ActivateClickHandler(_authorLabel, handler, ClickContext{
				.button = button,
				.other = QVariant::fromValue(ClickHandlerContext{
					.show = show,
				})
			});
			return false;
		}
		return true;
	});
}

ComposeAiContent::~ComposeAiContent() {
	cancelRequest();
}

bool ComposeAiContent::hasResult() const {
	return _state == CardState::Ready;
}

const TextWithEntities &ComposeAiContent::result() const {
	return _result;
}

const std::vector<Ui::LabeledEmojiTab> &ComposeAiContent::stylesData() const {
	return _stylesData;
}

const std::vector<Data::AiComposeTone> &ComposeAiContent::tones() const {
	return _tones;
}

void ComposeAiContent::setReadyChangedCallback(Fn<void(bool)> callback) {
	_readyChanged = std::move(callback);
}

void ComposeAiContent::setLoadingChangedCallback(Fn<void(bool)> callback) {
	_loadingChanged = std::move(callback);
	notifyLoadingChanged();
}

void ComposeAiContent::setModeTabs(not_null<ComposeAiModeTabs*> tabs) {
	_tabs = tabs;
	_tabs->setChangedCallback([=](ComposeAiMode mode) {
		setMode(mode);
	});
	_tabs->setActive(_mode);
}

void ComposeAiContent::setStyleTabs(
		not_null<Ui::SlideWrap<Ui::LabeledEmojiScrollTabs>*> stylesWrap) {
	_stylesWrap = stylesWrap;
	_stylesWrap->setDuration(0);
	_styles = stylesWrap->entity();
	_styles->setChangedCallback([=](int index) {
		if (index >= 0 && index < int(_tones.size())) {
			const auto wasNoSelection = (_styleIndex < 0);
			_styleIndex = index;
			updateTitles();
			if (_mode == ComposeAiMode::Style) {
				request();
				if (wasNoSelection && _styleSelected) {
					_styleSelected();
				}
			}
		} else if (index == int(_tones.size())) {
			_styles->setActive(_styleIndex);
			_box->uiShow()->show(Box(
				CreateAiToneBox,
				_session,
				crl::guard(this, [=](Data::AiComposeTone tone) {
					selectToneById(tone.id);
				})));
		}
	});
	_styles->setActive(_styleIndex);
	_stylesWrap->toggle(_mode == ComposeAiMode::Style, anim::type::instant);
}

void ComposeAiContent::refreshTones() {
	auto previousKey = QString();
	auto hadSelection = false;
	if (_styleIndex >= 0 && _styleIndex < int(_tones.size())) {
		const auto &prev = _tones[_styleIndex];
		previousKey = prev.isDefault
			? prev.defaultType
			: QString::number(prev.id);
		hadSelection = true;
	}
	_tones = _session->data().aiComposeTones().list();
	_stylesData = ResolveStyleDescriptors(_tones);
	_translateStylesData = ResolveTranslateStyleDescriptors(
		_session,
		_stylesData);
	auto remapped = -1;
	if (hadSelection) {
		for (auto i = 0; i != int(_tones.size()); ++i) {
			const auto &tone = _tones[i];
			const auto key = tone.isDefault
				? tone.defaultType
				: QString::number(tone.id);
			if (key == previousKey) {
				remapped = i;
				break;
			}
		}
	}
	_styleIndex = remapped;
	if (_mode == ComposeAiMode::Style && hadSelection && _styleIndex < 0) {
		request();
	}
}

void ComposeAiContent::selectToneById(uint64 id) {
	for (auto i = 0; i != int(_tones.size()); ++i) {
		const auto &tone = _tones[i];
		if (!tone.isDefault && tone.id == id) {
			const auto wasNoSelection = (_styleIndex < 0);
			_styleIndex = i;
			updateTitles();
			if (_styles) {
				_styles->setActive(_styleIndex);
				_styles->scrollToActive();
			}
			if (_mode == ComposeAiMode::Style) {
				request();
				if (wasNoSelection && _styleSelected) {
					_styleSelected();
				}
			}
			return;
		}
	}
}

void ComposeAiContent::start() {
	updatePinnedTabs(anim::type::instant);
	updateTitles();
	request();
}

int ComposeAiContent::resizeGetHeight(int newWidth) {
	_preview->resizeToWidth(newWidth);
	_preview->moveToLeft(0, 0, newWidth);
	auto y = _preview->height();
	if (!_authorLabel->isHidden()) {
		_authorLabel->resizeToWidth(newWidth);
		_authorLabel->moveToLeft(
			0,
			y + st::aiComposeAuthorLabelTop,
			newWidth);
		y += st::aiComposeAuthorLabelTop + _authorLabel->height();
	}
	return y;
}

void ComposeAiContent::refreshLayout() {
	if (width() > 0) {
		resizeToWidth(width());
	}
}

void ComposeAiContent::chooseLanguage() {
	if (_mode != ComposeAiMode::Translate) {
		return;
	}
	const auto weak = QPointer<ComposeAiContent>(this);
	const auto session = _session;
	const auto styles = _translateStylesData;
	const auto selectedStyle = std::make_shared<int>(_translateStyleIndex);
	_box->uiShow()->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		const auto apply = [=](LanguageId id, int styleIndex) {
			if (!weak) {
				return;
			}
			weak->_to = id;
			if (const auto count = int(weak->_translateStylesData.size())) {
				weak->_translateStyleIndex = std::clamp(styleIndex, 0, count - 1);
			}
			weak->updateTitles();
			weak->request();
		};
		Ui::ChooseLanguageBox(
			box,
			tr::lng_languages(),
			[=](std::vector<LanguageId> ids) {
				if (ids.empty()) {
					return;
				}
				apply(ids.front(), *selectedStyle);
			},
			{ _to },
			false,
			nullptr);
		const auto bottom = box->setPinnedToBottomContent(
			object_ptr<Ui::VerticalLayout>(box));
		const auto skip = st::defaultSubsectionTitlePadding.left();
		const auto tabs = bottom->add(
			object_ptr<Ui::LabeledEmojiScrollTabs>(
				bottom,
				styles,
				session->data().customEmojiManager().factory(
					Data::CustomEmojiSizeTag::Large)),
			QMargins(
				(skip - st::aiComposeStyleTabsPadding.left()),
				0,
				(skip - st::aiComposeStyleTabsPadding.right()),
				0));
		tabs->setPaintOuterCorners(false);
		tabs->setChangedCallback([=](int index) {
			if (index >= 0 && index < int(styles.size())) {
				*selectedStyle = index;
				apply(_to, index);
				box->closeBox();
			}
		});
		tabs->setActive(std::clamp(*selectedStyle, 0, int(styles.size()) - 1));
		tabs->scrollToActive();
	}));
}

void ComposeAiContent::copyResult() {
	if (_state != CardState::Ready) {
		return;
	}
	TextUtilities::SetClipboardText(
		TextForMimeData::WithExpandedLinks(_result));
}

void ComposeAiContent::setMode(ComposeAiMode mode) {
	if (_mode == mode) {
		return;
	}
	if (mode != ComposeAiMode::Style) {
		_styleIndex = -1;
	}
	_mode = mode;
	_state = CardState::Waiting;
	_preview->setState(CardState::Waiting);
	setAuthorId(UserId(0));
	notifyLoadingChanged();
	if (_modeChanged) {
		_modeChanged(_mode);
	}
	updatePinnedTabs(anim::type::normal);
	updateTitles();
	refreshLayout();
	request();
}

void ComposeAiContent::updateTitles() {
	const auto hasResult = (_state == CardState::Loading)
		|| (_state == CardState::Ready);
	_preview->setOriginalVisible(hasResult);
	_preview->setOriginalTitle(
		(_mode == ComposeAiMode::Translate)
			? FromTitle(_detectedFrom)
			: tr::lng_ai_compose_original(tr::now));
	_preview->setResultTitle(
		hasResult
			? ((_mode == ComposeAiMode::Translate)
				? ToTitle(_to, currentTranslateStyleLabel())
				: tr::lng_ai_compose_result(tr::now, tr::marked))
			: tr::lng_ai_compose_original(tr::now, tr::marked));
	const auto emojifyOnlyMode = !hasResult
		&& (_mode == ComposeAiMode::Style)
		&& (_styleIndex < 0);
	_preview->setEmojifyVisible(
		(hasResult && (_mode != ComposeAiMode::Fix))
		|| emojifyOnlyMode);
	_preview->setEmojifyChecked(_emojify);
}

void ComposeAiContent::updatePinnedTabs(anim::type animated) {
	if (_tabs) {
		_tabs->setActive(_mode);
	}
	if (_styles) {
		_styles->setActive(_styleIndex);
	}
	if (_stylesWrap) {
		_stylesWrap->toggle(_mode == ComposeAiMode::Style, animated);
	}
}

void ComposeAiContent::cancelRequest() {
	++_requestToken;
	if (_requestId) {
		_session->api().composeWithAi().cancel(_requestId);
		_requestId = 0;
	}
}

void ComposeAiContent::request() {
	cancelRequest();
	if (_mode == ComposeAiMode::Style && _styleIndex < 0 && !_emojify) {
		if (_state != CardState::Waiting) {
			resetState(CardState::Waiting);
		}
		return;
	}
	resetState(CardState::Loading);

	auto request = Api::ComposeWithAi::Request{
		.text = _original,
		.emojify = (_mode != ComposeAiMode::Fix) && _emojify,
	};
	switch (_mode) {
	case ComposeAiMode::Translate: {
		request.translateToLang = _to.twoLetterCode();
		const auto style = currentTranslateStyle();
		if (!style.isEmpty()) {
			request.setDefaultTone(style);
		}
	} break;
	case ComposeAiMode::Style:
		if (_styleIndex >= 0 && _styleIndex < int(_tones.size())) {
			const auto &tone = _tones[_styleIndex];
			if (tone.isDefault) {
				request.setDefaultTone(tone.defaultType);
			} else {
				request.setCustomTone(tone.id, tone.accessHash);
			}
		}
		break;
	case ComposeAiMode::Fix:
		request.proofread = true;
		break;
	}

	const auto token = ++_requestToken;
	const auto weak = QPointer<ComposeAiContent>(this);
	_requestId = _session->api().composeWithAi().request(
		std::move(request),
		[=](Api::ComposeWithAi::Result &&result) {
			if (!weak || weak->_requestToken != token) {
				return;
			}
			weak->_requestId = 0;
			weak->applyResult(std::move(result));
		},
		[=](const MTP::Error &error) {
			if (!weak || weak->_requestToken != token) {
				return;
			}
			weak->_requestId = 0;
			if (MTP::IgnoreError(error)) {
				weak->resetState(CardState::Waiting);
				return;
			}
			weak->showError(error.type());
		});
}

void ComposeAiContent::setAuthorId(UserId authorId) {
	if (_authorId == authorId) {
		return;
	}
	_authorId = authorId;
	if (const auto user = _session->data().userLoaded(authorId)) {
		const auto name = user->shortName();
		auto mention = tr::marked(name);
		mention.entities.push_back(EntityInText(
			EntityType::MentionName,
			0,
			name.size(),
			TextUtilities::MentionNameDataFromFields({
				.selfId = _session->userId().bare,
				.userId = authorId.bare,
				.accessHash = user->accessHash(),
			})));
		_authorLabel->setMarkedText(
			tr::lng_ai_compose_author(
				tr::now,
				lt_user,
				std::move(mention),
				tr::marked),
			Core::TextContext({ .session = _session }));
		_authorLabel->setVisible(true);
	} else {
		_authorLabel->setMarkedText({});
		_authorLabel->setVisible(false);
		_authorId = UserId(0);
	}
	refreshLayout();
}

void ComposeAiContent::resetState(CardState state) {
	_state = state;
	_result = {};
	setAuthorId(UserId(0));
	_preview->setState(state);
	notifyLoadingChanged();
	updateTitles();
	notifyReadyChanged();
}

void ComposeAiContent::applyResult(Api::ComposeWithAi::Result &&result) {
	_result = std::move(result.resultText);
	if (_result.text.isEmpty()) {
		showError({});
		return;
	}
	auto display = (_mode == ComposeAiMode::Fix && result.diffText)
		? BuildDiffDisplay(*result.diffText)
		: _result;
	_state = _result.text.isEmpty() ? CardState::Failed : CardState::Ready;
	_preview->setState(_state);
	notifyLoadingChanged();
	if (_state == CardState::Ready) {
		_preview->setResultText(std::move(display));
		if (_mode == ComposeAiMode::Style
			&& _styleIndex >= 0
			&& _styleIndex < int(_tones.size())) {
			setAuthorId(_tones[_styleIndex].authorId);
		} else {
			setAuthorId(UserId(0));
		}
	}
	updateTitles();
	notifyReadyChanged();
	refreshLayout();
}

void ComposeAiContent::showError(const QString &error) {
	_state = CardState::Failed;
	setAuthorId(UserId(0));
	_preview->setState(CardState::Failed);
	notifyLoadingChanged();
	updateTitles();
	notifyReadyChanged();
	refreshLayout();
	if (error == u"AICOMPOSE_FLOOD_PREMIUM"_q) {
		const auto show = Main::MakeSessionShow(
			_box->uiShow(),
			_session);
		Settings::ShowPremiumPromoToast(
			show,
			ChatHelpers::ResolveWindowDefault(),
			tr::lng_ai_compose_flood_text(
				tr::now,
				lt_link,
				tr::link(tr::lng_ai_compose_flood_link(tr::now, tr::bold)),
				tr::rich),
			u"ai_compose"_q);
		if (_premiumFlood) {
			_premiumFlood();
		}
		return;
	}
	_box->showToast(error.isEmpty()
		? tr::lng_ai_compose_error(tr::now)
		: error);
}

void ComposeAiContent::notifyLoadingChanged() {
	if (_loadingChanged) {
		_loadingChanged(_state == CardState::Loading);
	}
}

void ComposeAiContent::notifyReadyChanged() {
	if (_readyChanged) {
		_readyChanged(_state == CardState::Ready);
	}
}

void ComposeAiContent::setPremiumFloodCallback(Fn<void()> callback) {
	_premiumFlood = std::move(callback);
}

void ComposeAiContent::setModeChangedCallback(
		Fn<void(ComposeAiMode)> callback) {
	_modeChanged = std::move(callback);
}

void ComposeAiContent::setStyleSelectedCallback(Fn<void()> callback) {
	_styleSelected = std::move(callback);
}

QString ComposeAiContent::currentTranslateStyle() const {
	return (_translateStyleIndex >= 0
		&& _translateStyleIndex < int(_translateStylesData.size()))
		? _translateStylesData[_translateStyleIndex].id
		: QString();
}

QString ComposeAiContent::currentTranslateStyleLabel() const {
	if (const auto style = currentTranslateStyle(); !style.isEmpty()) {
		return (_translateStyleIndex >= 0
			&& _translateStyleIndex < int(_translateStylesData.size()))
			? _translateStylesData[_translateStyleIndex].label
			: QString();
	}
	return QString();
}

ComposeAiMode ComposeAiContent::mode() const {
	return _mode;
}

bool ComposeAiContent::hasStyleSelection() const {
	return _styleIndex >= 0;
}

struct StyleTooltipHandle {
	QPointer<Ui::ImportantTooltip> tooltip;
	Fn<void(bool)> updateVisibility;
};

[[nodiscard]] StyleTooltipHandle SetupStyleTooltip(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::RpWidget*> pinnedToTop,
		not_null<Ui::RpWidget*> stylesWrap,
		Fn<ComposeAiMode()> currentMode) {
	const auto tooltip = Ui::CreateChild<Ui::ImportantTooltip>(
		box,
		object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			box,
			Ui::MakeNiceTooltipLabel(
				box,
				tr::lng_ai_compose_style_tooltip(tr::rich),
				st::historyMessagesTTLLabel.minWidth,
				st::ttlMediaImportantTooltipLabel),
			st::historyRecordTooltip.padding),
		st::historyRecordTooltip);
	tooltip->toggleFast(false);

	struct State {
		bool shown = false;
		bool shownOnce = false;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto updateGeometry = [=] {
		const auto local = stylesWrap->geometry();
		if (local.isEmpty()) {
			return;
		}
		const auto geometry = Ui::MapFrom(box, pinnedToTop, local);
		const auto countPosition = [=](QSize size) {
			const auto left = geometry.x()
				+ (geometry.width() - size.width()) / 2;
			return QPoint(
				std::max(std::min(left, box->width() - size.width()), 0),
				(geometry.y()
					+ geometry.height()
					- st::historyRecordTooltip.arrow
					- (st::aiComposeBoxStyleTabsSkip / 2)));
		};
		tooltip->pointAt(geometry, RectPart::Bottom, countPosition);
	};

	const auto updateVisibility = [=](bool visible) {
		const auto show = visible
			&& !Core::App().settings().readPref<bool>(
				kAiComposeStyleTooltipHiddenPref);
		if (state->shown != show) {
			state->shown = show;
			if (show) {
				updateGeometry();
				tooltip->raise();
			}
			if (show && !state->shownOnce) {
				state->shownOnce = true;
				tooltip->toggleFast(true);
			} else {
				tooltip->toggleAnimated(show);
			}
		}
	};

	stylesWrap->geometryValue(
	) | rpl::on_next([=](const QRect &geometry) {
		if (!geometry.isEmpty()) {
			if (state->shown) {
				updateGeometry();
			} else {
				updateVisibility(currentMode() == ComposeAiMode::Style);
			}
		}
	}, tooltip->lifetime());

	return { tooltip, updateVisibility };
}

} // namespace

void ComposeAiBox(not_null<Ui::GenericBox*> box, ComposeAiBoxArgs &&args) {
	const auto sendButtonHeight = st::aiComposeSendButton.inner.height;
	const auto buttonHeight = st::aiComposeSendButton.inner.icon.height()
		+ 2 * st::aiComposeSendButton.sendIconFillPadding;
	const auto boxStyle = [&](const style::Box &base) {
		const auto result = box->lifetime().make_state<style::Box>(base);
		result->button.height = buttonHeight;
		result->buttonHeight = buttonHeight;
		result->button.textTop = base.button.textTop
			- (base.button.height - buttonHeight) / 2;
		return result;
	};
	const auto boxStyleNoSend = boxStyle(st::aiComposeBox);
	const auto boxStyleWithSend = boxStyle(st::aiComposeBoxWithSend);
	box->setStyle(*boxStyleNoSend);
	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	const auto session = args.session;
	box->addTopButton(st::aiComposeBoxClose, [=] {
		box->closeBox();
	})->setAccessibleName(tr::lng_close(tr::now));
	box->addTopButton(st::aiComposeBoxInfoButton, [=] {
		box->uiShow()->show(Box(Ui::AboutCocoonBox));
	})->setAccessibleName(tr::lng_sr_ai_compose_info(tr::now));

	const auto body = box->verticalLayout();
	const auto tabsSkip = QMargins(0, 0, 0, st::aiComposeBoxStyleTabsSkip);
	const auto pinnedToTop = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));
	const auto tabs = pinnedToTop->add(
		object_ptr<ComposeAiModeTabs>(pinnedToTop),
		st::aiComposeContentMargin + tabsSkip);
	const auto content = body->add(
		object_ptr<ComposeAiContent>(box, box, args),
		st::aiComposeContentMargin);
	const auto contextMenu = box->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	const auto stylesWrapHolder = box->lifetime().make_state<
		QPointer<Ui::SlideWrap<Ui::LabeledEmojiScrollTabs>>>();
	const auto styleTooltipHolder = box->lifetime().make_state<
		QPointer<Ui::ImportantTooltip>>();
	const auto styleTooltipUpdater = box->lifetime().make_state<
		Fn<void(bool)>>();

	content->setModeTabs(tabs);

	const auto rebuildStylesWrap = [=] {
		auto savedScroll = -1;
		if (const auto old = stylesWrapHolder->data()) {
			savedScroll = old->entity()->scrollLeft();
			delete old;
		}
		if (const auto old = styleTooltipHolder->data()) {
			delete old;
		}
		auto emojiFactory = session->data().customEmojiManager().factory(
			Data::CustomEmojiSizeTag::Large);
		auto wrap = object_ptr<Ui::SlideWrap<Ui::LabeledEmojiScrollTabs>>(
			pinnedToTop,
			object_ptr<Ui::LabeledEmojiScrollTabs>(
				pinnedToTop,
				WithAddStyleTab(content->stylesData()),
				std::move(emojiFactory)),
			tabsSkip);
		const auto ptr = wrap.data();
		pinnedToTop->add(std::move(wrap), st::aiComposeContentMargin);
		*stylesWrapHolder = ptr;
		ptr->entity()->setContextMenuCallback([=](int index, QPoint globalPos) {
			const auto &tones = content->tones();
			if (index < 0 || index >= int(tones.size())) {
				return;
			}
			const auto &tone = tones[index];
			if (tone.isDefault) {
				return;
			}
			*contextMenu = base::make_unique_q<Ui::PopupMenu>(
				ptr->entity(),
				st::popupMenuWithIcons);
			const auto toneCopy = tone;
			if (toneCopy.creator) {
				(*contextMenu)->addAction(
					tr::lng_ai_compose_tone_edit(tr::now),
					[=] {
						box->uiShow()->show(Box(
							EditAiToneBox,
							session,
							toneCopy,
							crl::guard(content, [=](Data::AiComposeTone tone) {
								content->selectToneById(tone.id);
							})));
					},
					&st::menuIconEdit);
			}
			(*contextMenu)->addAction(
				tr::lng_ai_compose_tone_share(tr::now),
				[=] {
					const auto url = session->createInternalLinkFull(
						"addstyle/" + toneCopy.slug);
					FastShareLink(
						Main::MakeSessionShow(box->uiShow(), session),
						url);
				},
				&st::menuIconShare);
			(*contextMenu)->addAction(base::make_unique_q<Ui::Menu::Action>(
				(*contextMenu)->menu(),
				st::menuWithIconsAttention,
				Ui::Menu::CreateAction(
					(*contextMenu)->menu().get(),
					toneCopy.creator
						? tr::lng_ai_compose_tone_delete(tr::now)
						: tr::lng_ai_compose_tone_remove(tr::now),
					[=] {
						ConfirmDeleteAiTone(
							box->uiShow(),
							session,
							toneCopy);
					}),
				&st::menuIconDeleteAttention,
				&st::menuIconDeleteAttention));
			(*contextMenu)->popup(globalPos);
		});
		content->setStyleTabs(ptr);
		if (savedScroll >= 0) {
			ptr->entity()->setScrollLeft(savedScroll);
		}
		auto handle = SetupStyleTooltip(
			box,
			pinnedToTop,
			ptr,
			[=] { return content->mode(); });
		*styleTooltipHolder = handle.tooltip;
		*styleTooltipUpdater = std::move(handle.updateVisibility);
	};
	rebuildStylesWrap();

	session->data().aiComposeTones().updated(
	) | rpl::on_next([=] {
		content->refreshTones();
		rebuildStylesWrap();
	}, box->lifetime());

	const auto sparkle = LoadingTitleSparkle(session);
	const auto loading = box->lifetime().make_state<
		rpl::variable<bool>>();

	content->setLoadingChangedCallback([=](bool value) {
		*loading = value;
	});

	box->setTitle(rpl::combine(
		loading->value(),
		tr::lng_ai_compose_title(tr::marked)
	) | rpl::map([=](bool loading, TextWithEntities title) {
		return loading ? title.append(sparkle) : title;
	}), Core::TextContext({ .session = session }));

	auto premiumFlooded = std::make_shared<bool>(false);
	auto sendButton = std::make_shared<QPointer<Ui::SendButton>>();

	const auto applyAndClose = [=] {
		if (!content->hasResult()) {
			return;
		}
		args.apply(TextWithEntities(content->result()));
		box->closeBox();
	};
	const auto sendResult = [=](Api::SendOptions options) {
		if (!args.send || !content->hasResult()) {
			return;
		}
		args.send(
			TextWithEntities(content->result()),
			options,
			crl::guard(box, [=] {
				box->closeBox();
			}));
	};
	const auto addApplyButton = [=](
			const style::Box &style,
			rpl::producer<QString> text,
			Fn<void()> callback) {
		box->setStyle(style);
		const auto result = box->addButton(std::move(text), std::move(callback));
		result->setFullRadius(true);
		return result;
	};
	const auto disableButton = [=](not_null<Ui::RoundButton*> button) {
		button->clearState();
		button->setDisabled(true);
		button->setAttribute(Qt::WA_TransparentForMouseEvents);
		button->setTextFgOverride(
			anim::color(st::activeButtonBg, st::activeButtonFg, 0.5));
		button->setClickedCallback([] {
		});
	};

	const auto rebuildButtons = [=] {
		if (*sendButton) {
			delete sendButton->data();
		}
		*sendButton = nullptr;
		box->clearButtons();
		box->addTopButton(st::aiComposeBoxClose, [=] {
			box->closeBox();
		})->setAccessibleName(tr::lng_close(tr::now));
		box->addTopButton(st::aiComposeBoxInfoButton, [=] {
			box->uiShow()->show(Box(Ui::AboutCocoonBox));
		})->setAccessibleName(tr::lng_sr_ai_compose_info(tr::now));

		if (*premiumFlooded) {
			auto helper = Ui::Text::CustomEmojiHelper();
			const auto badge = helper.paletteDependent(
				Ui::Text::CustomEmojiTextBadge(
					u"x50"_q,
					st::aiComposeBadge,
					st::aiComposeBadgeMargin));
			const auto btn = addApplyButton(
				*boxStyleNoSend,
				tr::lng_ai_compose_increase_limit(), nullptr);
			btn->setContext(helper.context());
			btn->setText(rpl::single(
				tr::lng_ai_compose_increase_limit(tr::now, tr::marked)
					.append(' ')
					.append(badge)));
			const auto resolve = ChatHelpers::ResolveWindowDefault();
			const auto close = crl::guard(box, [=] {
				box->closeBox();
			});
			btn->setClickedCallback([=] {
				if (const auto controller = resolve(session)) {
					ShowPremiumPreviewBox(
						controller,
						PremiumFeature::AiCompose);
				}
				close();
			});
		} else if (content->mode() == ComposeAiMode::Style
				&& !content->hasStyleSelection()
				&& !content->hasResult()) {
			const auto btn = addApplyButton(
				*boxStyleNoSend,
				tr::lng_ai_compose_select_style(), nullptr);
			disableButton(btn);
		} else if (content->hasResult()) {
			const auto isStyle =
				(content->mode() == ComposeAiMode::Style);
			const auto btn = addApplyButton(
				args.send ? *boxStyleWithSend : *boxStyleNoSend,
				isStyle
					? tr::lng_ai_compose_apply_style()
					: tr::lng_ai_compose_apply(),
				applyAndClose);
			if (args.send) {
				const auto send = Ui::CreateChild<Ui::SendButton>(
					btn->parentWidget(),
					st::aiComposeSendButton);
				send->setState({ .type = Ui::SendButton::Type::Send });
				send->setAccessibleName(tr::lng_send_button(tr::now));
				send->show();
				btn->geometryValue(
				) | rpl::on_next([=](QRect geometry) {
					const auto size = sendButtonHeight;
					send->resize(size, size);
					send->moveToLeft(
						geometry.x() + geometry.width()
							+ st::aiComposeSendButtonSkip,
						geometry.y() + (geometry.height() - size) / 2);
				}, send->lifetime());
				send->setClickedCallback([=] {
					sendResult({});
				});
				if (args.setupMenu) {
					args.setupMenu(
						send,
						[=](Api::SendOptions options) {
							sendResult(options);
						});
				}
				*sendButton = send;
			}
		} else {
			const auto isStyle =
				(content->mode() == ComposeAiMode::Style);
			const auto btn = addApplyButton(
				*boxStyleNoSend,
				isStyle
					? tr::lng_ai_compose_apply_style()
					: tr::lng_ai_compose_apply(),
				nullptr);
			disableButton(btn);
		}
	};

	content->setReadyChangedCallback([=](bool) {
		rebuildButtons();
	});
	content->setPremiumFloodCallback([=] {
		*premiumFlooded = true;
		rebuildButtons();
	});
	content->setModeChangedCallback([=](ComposeAiMode mode) {
		rebuildButtons();
		(*styleTooltipUpdater)(mode == ComposeAiMode::Style);
	});
	content->setStyleSelectedCallback([=] {
		rebuildButtons();
		if (!Core::App().settings().readPref<bool>(kAiComposeStyleTooltipHiddenPref)) {
			Core::App().settings().writePref<bool>(kAiComposeStyleTooltipHiddenPref, true);
		}
		(*styleTooltipUpdater)(false);
	});

	rebuildButtons();
	content->start();
}

void ShowComposeAiBox(
		std::shared_ptr<Ui::Show> show,
		ComposeAiBoxArgs &&args) {
	show->show(Box(ComposeAiBox, std::move(args)));
}

} // namespace HistoryView::Controls
