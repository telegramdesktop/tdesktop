/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_box.h"

#include <QtCore/QDir>

#include "base/algorithm.h"
#include "base/event_filter.h"
#include "base/flat_map.h"
#include "base/options.h"
#include "base/unique_qptr.h"
#include "base/weak_qptr.h"
#include "boxes/create_ai_box.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_changes.h"
#include "data/data_file_origin.h"
#include "data/data_msg_id.h"
#include "data/data_types.h"
#include "data/data_emoji_statuses.h"
#include "dialogs/ui/dialogs_pill.h"
#include "history/history_item.h"
#include "history/view/controls/history_view_compose_ai_button.h"
#include "boxes/compose_ai_box.h"
#include "main/main_session.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "data/data_document.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "chat_helpers/tabbed_selector.h"
#include "iv/editor/iv_editor_state.h"
#include "iv/editor/iv_editor_toolbar_pill.h"
#include "iv/editor/iv_editor_widget.h"
#include "iv/editor/iv_editor_window.h"
#include "lang/lang_keys.h"
#include "menu/menu_checked_action.h"
#include "menu/menu_send_details.h"
#include "styles/style_settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "ui/controls/send_button.h"
#include "ui/delayed_activation.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/toast/toast.h"
#include "data/data_peer_values.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/tooltip.h"
#include "ui/rect_part.h"

#include <crl/crl_on_main.h>
#include <rpl/never.h>

#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtGui/QRegion>
#include <QtGui/QScreen>
#include <QtSvg/QSvgRenderer>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

#include "styles/style_chat_helpers.h"
#include "styles/style_basic.h"
#include "styles/style_editor.h"
#include "styles/style_iv.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Iv::Editor {
namespace {

enum class ToolbarActionId : uchar {
	Undo,
	Redo,
	Bold,
	Italic,
	Underline,
	StrikeOut,
	Spoiler,
	Subscript,
	Superscript,
	Marked,
	PlainText,
	Heading,
	Link,
	Math,
	Blockquote,
	Pullquote,
	CodeBlock,
	Details,
	OrderedList,
	BulletList,
	TaskList,
	Attach,
	Table,
	Location,
	Divider,
};

[[nodiscard]] QString WithParenShortcut(
		const QString &label,
		QKeySequence seq) {
	const auto shortcut = seq.toString(QKeySequence::NativeText);
	return shortcut.isEmpty()
		? label
		: (label + u" ("_q + shortcut + u")"_q);
}

[[nodiscard]] QString WithTabShortcut(
		const QString &label,
		QKeySequence seq) {
	const auto shortcut = seq.toString(QKeySequence::NativeText);
	return shortcut.isEmpty()
		? label
		: (label + QChar('\t') + shortcut);
}

[[nodiscard]] QString ToolbarActionLabel(
		ToolbarActionId action,
		Widget::ToolbarLinkMode linkMode
			= Widget::ToolbarLinkMode::Create) {
	switch (action) {
	case ToolbarActionId::Undo:
		return tr::lng_wnd_menu_undo(tr::now);
	case ToolbarActionId::Redo:
		return tr::lng_wnd_menu_redo(tr::now);
	case ToolbarActionId::Bold:
		return tr::lng_menu_formatting_bold(tr::now);
	case ToolbarActionId::Italic:
		return tr::lng_menu_formatting_italic(tr::now);
	case ToolbarActionId::Underline:
		return tr::lng_menu_formatting_underline(tr::now);
	case ToolbarActionId::StrikeOut:
		return tr::lng_menu_formatting_strike_out(tr::now);
	case ToolbarActionId::Spoiler:
		return tr::lng_menu_formatting_spoiler(tr::now);
	case ToolbarActionId::Subscript:
		return tr::lng_menu_formatting_subscript(tr::now);
	case ToolbarActionId::Superscript:
		return tr::lng_menu_formatting_superscript(tr::now);
	case ToolbarActionId::Marked:
		return tr::lng_menu_formatting_marked(tr::now);
	case ToolbarActionId::PlainText:
		return tr::lng_menu_formatting_plain_text(tr::now);
	case ToolbarActionId::Heading:
		return tr::lng_article_insert_heading(tr::now);
	case ToolbarActionId::Link:
		return (linkMode == Widget::ToolbarLinkMode::Edit)
			? tr::lng_menu_formatting_link_edit(tr::now)
			: tr::lng_menu_formatting_link_create(tr::now);
	case ToolbarActionId::Math:
		return tr::lng_article_insert_math(tr::now);
	case ToolbarActionId::Blockquote:
		return tr::lng_article_insert_blockquote(tr::now);
	case ToolbarActionId::Pullquote:
		return tr::lng_article_insert_pullquote(tr::now);
	case ToolbarActionId::CodeBlock:
		return tr::lng_article_insert_code(tr::now);
	case ToolbarActionId::Details:
		return tr::lng_article_insert_details(tr::now);
	case ToolbarActionId::OrderedList:
		return tr::lng_article_insert_ordered_list(tr::now);
	case ToolbarActionId::BulletList:
		return tr::lng_article_insert_bullet_list(tr::now);
	case ToolbarActionId::TaskList:
		return tr::lng_article_insert_task_list(tr::now);
	case ToolbarActionId::Attach:
		return tr::lng_article_insert_media(tr::now);
	case ToolbarActionId::Table:
		return tr::lng_article_insert_table(tr::now);
	case ToolbarActionId::Location:
		return tr::lng_maps_point(tr::now);
	case ToolbarActionId::Divider:
		return tr::lng_article_insert_divider(tr::now);
	}
	return QString();
}

[[nodiscard]] const style::icon *HeadingIcon(int level) {
	switch (level) {
	case 1: return &st::ivEditorToolbarHeading1Icon;
	case 2: return &st::ivEditorToolbarHeading2Icon;
	case 3: return &st::ivEditorToolbarHeading3Icon;
	case 4: return &st::ivEditorToolbarHeading4Icon;
	case 5: return &st::ivEditorToolbarHeading5Icon;
	case 6: return &st::ivEditorToolbarHeading6Icon;
	}
	return &st::ivEditorToolbarHeadingIcon;
}

[[nodiscard]] QImage PremiumStarImage() {
	const auto factor = style::DevicePixelRatio();
	const auto side = st::ivEditorToolbarPremiumStarSize;
	const auto size = QSize(side, side);
	auto image = QImage(
		size * factor,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(factor);
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		auto svg = QSvgRenderer(Ui::Premium::ColorizedSvg(
			Ui::Premium::ButtonGradientStops()));
		svg.render(&p, QRectF(QPointF(), QSizeF(size)));
	}
	return image;
}

template <typename Button>
void SetupToolbarButtonState(
		not_null<Button*> button,
		ToolbarButtonState state,
		anim::type animated = anim::type::normal) {
	const auto disabled = (state == ToolbarButtonState::Disabled);
	const auto active = (state == ToolbarButtonState::Active);
	button->setAttribute(Qt::WA_TransparentForMouseEvents, disabled);
	button->setPointerCursor(!disabled);
	if (active) {
		button->setRippleColorOverride(&st::lightButtonBgOver);
		button->setForceRippled(true, animated);
		button->setIconColorOverride(st::windowActiveTextFg->c);
	} else {
		button->setForceRippled(false, animated);
		button->setRippleColorOverride(nullptr);
		button->setIconColorOverride(disabled
			? std::optional<QColor>(st::windowSubTextFg->c)
			: std::nullopt);
	}
}

class ToolbarStarButton final : public Ui::RippleButton {
public:
	ToolbarStarButton(
		QWidget *parent,
		const style::IconButton &st,
		not_null<Main::Session*> session);

	void setIconOverride(const style::icon *icon);
	void setIconColorOverride(std::optional<QColor> color);
	void setRippleColorOverride(const style::color *color);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	[[nodiscard]] QImage prepareRippleMask() const override;
	[[nodiscard]] QPoint prepareRippleStartPosition() const override;

private:
	void validateFrame();

	const style::IconButton &_st;
	const style::icon *_iconOverride = nullptr;
	const style::color *_rippleColorOverride = nullptr;
	std::optional<QColor> _iconColorOverride;
	QImage _frame;
	bool _premium = false;

};

class Toolbar final : public Ui::RpWidget {
public:
	Toolbar(
		QWidget *parent,
		not_null<Widget*> editor,
		QPointer<QWidget> tooltipParent,
		bool hasRequestMedia,
		Fn<void(not_null<Widget*>, QPointer<QWidget>, rpl::producer<>)> requestMap,
		Fn<void()> toggleEmoji,
		not_null<Main::Session*> session);

	int resizeGetHeight(int width) override;
	bool eventFilter(QObject *object, QEvent *event) override;
	void hideShownTooltip();
	void setEmojiColumnOpen(bool open);
	[[nodiscard]] int minimalWidth() const;
	[[nodiscard]] int contentMaxWidth() const;

private:
	struct PillButton {
		Ui::IconButton *button = nullptr;
		Widget::ToolbarFormatAction format = {};
	};

	not_null<Ui::IconButton*> addPillButton(
		not_null<ToolbarPill*> pill,
		ToolbarActionId action,
		const style::icon *icon,
		Fn<void()> callback,
		std::optional<Widget::ToolbarFormatAction> format = std::nullopt,
		Fn<QString()> tooltip = nullptr);
	not_null<ToolbarStarButton*> addStarPillButton(
		not_null<ToolbarPill*> pill,
		ToolbarActionId action,
		const style::icon *icon,
		Fn<void()> callback,
		Fn<QString()> tooltip);
	void buildPills();
	void scheduleTooltip(not_null<Ui::RippleButton*> button);
	void showTooltip(not_null<Ui::RippleButton*> button);
	void hideTooltip();
	void updateTooltipGeometry();
	void fillHeadingMenu(not_null<Ui::PopupMenu*> menu);
	void fillBlockStyleMenu(not_null<Ui::PopupMenu*> menu);
	void showBlockStyleMenu(not_null<Ui::IconButton*> button);
	void fillTextStyleMenu(not_null<Ui::PopupMenu*> menu);
	void showTextStyleMenu(not_null<Ui::IconButton*> button);
	void fillListStyleMenu(not_null<Ui::PopupMenu*> menu);
	void showListStyleMenu(not_null<Ui::RippleButton*> button);
	void fillTableStyleMenu(not_null<Ui::PopupMenu*> menu);
	void showTableStyleMenu(not_null<Ui::RippleButton*> button);
	void fillAttachMenu(not_null<Ui::PopupMenu*> menu);
	void showAttachMenu(not_null<Ui::RippleButton*> button);
	void applyBlockText();
	void updateFromEditorState();
	void updateInputMask();

	const QPointer<Widget> _editor;
	const not_null<Main::Session*> _session;
	const QPointer<QWidget> _tooltipParent;
	const bool _hasRequestMedia = false;
	const Fn<void(not_null<Widget*>, QPointer<QWidget>, rpl::producer<>)> _requestMap;
	const Fn<void()> _toggleEmoji;
	Widget::ToolbarState _toolbarState = {};
	object_ptr<ToolbarPill> _undoRedoPill = { nullptr };
	object_ptr<ToolbarPill> _controlsPill = { nullptr };
	object_ptr<ToolbarPill> _emojiPill = { nullptr };
	std::vector<PillButton> _stateButtons;
	Ui::IconButton *_linkButton = nullptr;
	Ui::IconButton *_emojiButton = nullptr;
	ToolbarStarButton *_listButton = nullptr;
	ToolbarStarButton *_tableButton = nullptr;
	base::flat_map<Ui::RippleButton*, Fn<QString()>> _tooltipFactories;
	base::unique_qptr<Ui::ImportantTooltip> _tooltip;
	Ui::RippleButton *_hovered = nullptr;
	Ui::RippleButton *_scheduledTooltip = nullptr;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

[[nodiscard]] QRect DefaultWindowGeometry() {
	const auto padding = st::ivEditorBodyPadding;
	const auto size = QSize(
		std::max(
			st::ivEditorWindowMinSize.width(),
			st::messageMarkdown.pageMaxWidth
				+ padding.left()
				+ padding.right()),
		st::ivEditorWindowDefaultSize.height());
	auto result = QRect(QPoint(), size);
	if (const auto screen = QGuiApplication::primaryScreen()) {
		result.moveCenter(screen->availableGeometry().center());
	}
	return result;
}

[[nodiscard]] int MaximalExtendBy(not_null<Window*> window) {
	const auto screen = window->screen()
		? window->screen()
		: QGuiApplication::primaryScreen();
	if (!screen) {
		return 0;
	}
	const auto desktop = screen->availableGeometry();
	return std::max(desktop.width() - window->body()->width(), 0);
}

[[nodiscard]] bool CanExtendNoMove(
		not_null<Window*> window,
		int extendBy) {
	const auto screen = window->screen()
		? window->screen()
		: QGuiApplication::primaryScreen();
	if (!screen) {
		return false;
	}
	const auto desktop = screen->availableGeometry();
	const auto inner = window->body()->mapToGlobal(window->body()->rect());
	const auto innerRight = inner.x() + inner.width() + extendBy;
	const auto desktopRight = desktop.x() + desktop.width();
	return innerRight <= desktopRight;
}

int TryToExtendWidthBy(not_null<Window*> window, int addToWidth) {
	const auto screen = window->screen()
		? window->screen()
		: QGuiApplication::primaryScreen();
	if (!screen) {
		return 0;
	}
	const auto desktop = screen->availableGeometry();
	const auto inner = window->body()->mapToGlobal(window->body()->rect());
	accumulate_min(addToWidth, MaximalExtendBy(window));
	const auto newWidth = inner.width() + addToWidth;
	const auto newLeft = std::min(
		inner.x(),
		desktop.x() + desktop.width() - newWidth);
	if (inner.x() != newLeft || inner.width() != newWidth) {
		window->setGeometry(QRect(newLeft, inner.y(), newWidth, inner.height()));
	}
	return addToWidth;
}

[[nodiscard]] QString SubmitText(const ShowWindowDescriptor &descriptor) {
	if (!descriptor.submitLabel.isEmpty()) {
		return descriptor.submitLabel;
	}
	switch (descriptor.submitType) {
	case ShowWindowDescriptor::SubmitType::Send:
		return tr::lng_send_button(tr::now);
	case ShowWindowDescriptor::SubmitType::Save:
		return tr::lng_settings_save(tr::now);
	}
	return tr::lng_send_button(tr::now);
}

[[nodiscard]] bool IsEmojiDocument(not_null<DocumentData*> document) {
	const auto info = document->sticker();
	return info && (info->setType == Data::StickersType::Emoji);
}

class WindowContext final : public ChatHelpers::Show {
public:
	WindowContext(
		not_null<Window*> window,
		not_null<Main::Session*> session)
	: _window(window.get())
	, _session(session) {
	}

	void activate() override {
		if (const auto window = _window.data()) {
			Ui::ActivateWindow(window);
		}
	}

	void showOrHideBoxOrLayer(
			std::variant<
				v::null_t,
				object_ptr<Ui::BoxContent>,
				std::unique_ptr<Ui::LayerWidget>> &&layer,
			Ui::LayerOptions options,
			anim::type animated) const override {
		using ObjectBox = object_ptr<Ui::BoxContent>;
		using UniqueLayer = std::unique_ptr<Ui::LayerWidget>;
		const auto window = _window.data();
		if (!window) {
			return;
		} else if (auto layerWidget = std::get_if<UniqueLayer>(&layer)) {
			window->showLayer(std::move(*layerWidget), options, animated);
		} else if (auto box = std::get_if<ObjectBox>(&layer)) {
			window->showBox(std::move(*box), options, animated);
		} else {
			window->hideLayer(animated);
		}
	}

	not_null<QWidget*> toastParent() const override {
		const auto window = _window.data();
		Assert(window != nullptr);
		return window->body();
	}

	bool valid() const override {
		return !_window.isNull();
	}

	operator bool() const override {
		return valid();
	}

	Main::Session &session() const override {
		return *_session;
	}

	bool paused(ChatHelpers::PauseReason reason) const override {
		const auto window = _window.data();
		if (!window
			|| window->isHidden()
			|| !window->isActiveWindow()) {
			return true;
		} else if (reason < ChatHelpers::PauseReason::RoundPlaying
			&& window->isLayerShown()) {
			return true;
		}
		return false;
	}

	rpl::producer<> pauseChanged() const override {
		return rpl::never<>();
	}

	rpl::producer<bool> adjustShadowLeft() const override {
		return rpl::single(false);
	}

	SendMenu::Details sendMenuDetails() const override {
		return { SendMenu::Type::Disabled };
	}

	bool showMediaPreview(
			Data::FileOrigin,
			not_null<DocumentData*>) const override {
		return false;
	}

	bool showMediaPreview(
			Data::FileOrigin,
			not_null<PhotoData*>) const override {
		return false;
	}

	void processChosenSticker(
			ChatHelpers::FileChosen &&) const override {
	}

private:
	const QPointer<Window> _window;
	const not_null<Main::Session*> _session;

};

ToolbarStarButton::ToolbarStarButton(
	QWidget *parent,
	const style::IconButton &st,
	not_null<Main::Session*> session)
: RippleButton(parent, st.ripple)
, _st(st) {
	resize(_st.width, _st.height);
	Data::AmPremiumValue(session) | rpl::on_next([=](bool premium) {
		_premium = premium;
		_frame = QImage();
		update();
	}, lifetime());
	style::PaletteChanged() | rpl::on_next([=] {
		_frame = QImage();
		update();
	}, lifetime());
}

void ToolbarStarButton::setIconOverride(const style::icon *icon) {
	if (_iconOverride == icon) {
		return;
	}
	_iconOverride = icon;
	_frame = QImage();
	update();
}

void ToolbarStarButton::setIconColorOverride(std::optional<QColor> color) {
	if (_iconColorOverride == color) {
		return;
	}
	_iconColorOverride = color;
	_frame = QImage();
	update();
}

void ToolbarStarButton::setRippleColorOverride(const style::color *color) {
	_rippleColorOverride = color;
	update();
}

void ToolbarStarButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	paintRipple(
		p,
		_st.rippleAreaPosition,
		_rippleColorOverride ? &(*_rippleColorOverride)->c : nullptr);
	validateFrame();
	p.drawImage(0, 0, _frame);
}

void ToolbarStarButton::validateFrame() {
	const auto ratio = style::DevicePixelRatio();
	if (!_frame.isNull() && _frame.size() == size() * ratio) {
		return;
	}
	_frame = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	_frame.setDevicePixelRatio(ratio);
	_frame.fill(Qt::transparent);
	auto p = QPainter(&_frame);
	const auto icon = _iconOverride ? _iconOverride : &_st.icon;
	auto position = _st.iconPosition;
	if (position.x() < 0) {
		position.setX((width() - icon->width()) / 2);
	}
	if (position.y() < 0) {
		position.setY((height() - icon->height()) / 2);
	}
	if (_iconColorOverride) {
		icon->paint(p, position, width(), *_iconColorOverride);
	} else {
		icon->paint(p, position, width());
	}
	if (!_premium) {
		const auto star = PremiumStarImage();
		const auto side = st::ivEditorToolbarPremiumStarSize;
		const auto skip = st::ivEditorToolbarPremiumStarSkip;
		const auto outline = st::ivEditorToolbarPremiumStarOutline;
		const auto at = QPoint(
			width() - side - skip.x(),
			height() - side - skip.y());
		p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		p.drawImage(at - QPoint(outline, 0), star);
		p.drawImage(at + QPoint(outline, 0), star);
		p.drawImage(at - QPoint(0, outline), star);
		p.drawImage(at + QPoint(0, outline), star);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		p.drawImage(at, star);
	}
}

void ToolbarStarButton::onStateChanged(
		State was,
		StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

QImage ToolbarStarButton::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QPoint ToolbarStarButton::prepareRippleStartPosition() const {
	const auto result = mapFromGlobal(QCursor::pos())
		- _st.rippleAreaPosition;
	const auto rect = QRect(0, 0, _st.rippleAreaSize, _st.rippleAreaSize);
	return rect.contains(result)
		? result
		: DisabledRippleStartPosition();
}

Toolbar::Toolbar(
	QWidget *parent,
	not_null<Widget*> editor,
	QPointer<QWidget> tooltipParent,
	bool hasRequestMedia,
	Fn<void(not_null<Widget*>, QPointer<QWidget>, rpl::producer<>)> requestMap,
	Fn<void()> toggleEmoji,
	not_null<Main::Session*> session)
: Ui::RpWidget(parent)
, _editor(editor.get())
, _session(session)
, _tooltipParent(std::move(tooltipParent))
, _hasRequestMedia(hasRequestMedia)
, _requestMap(std::move(requestMap))
, _toggleEmoji(std::move(toggleEmoji))
, _undoRedoPill(this, st::ivEditorPillShadow)
, _controlsPill(this, st::ivEditorPillShadow)
, _emojiPill(this, st::ivEditorPillShadow) {
	buildPills();
	_undoRedoPill->show();
	_controlsPill->show();
	_emojiPill->show();
	_toolbarState = _editor ? _editor->toolbarStateValue() : Widget::ToolbarState();
	if (_editor) {
		_editor->toolbarStateChanges() | rpl::on_next([=](const Widget::ToolbarState &state) {
			_toolbarState = state;
			updateFromEditorState();
		}, lifetime());
	}
	updateFromEditorState();
}

not_null<Ui::IconButton*> Toolbar::addPillButton(
		not_null<ToolbarPill*> pill,
		ToolbarActionId action,
		const style::icon *icon,
		Fn<void()> callback,
		std::optional<Widget::ToolbarFormatAction> format,
		Fn<QString()> tooltip) {
	const auto raw = pill->addButton(
		st::ivEditorToolbarButton,
		icon,
		icon,
		ToolbarButtonState::Inactive);
	raw->setAccessibleName(ToolbarActionLabel(action, _toolbarState.linkMode));
	raw->setClickedCallback([=] {
		if (callback) {
			callback();
		}
	});
	if (format) {
		_stateButtons.push_back({ raw.get(), *format });
	}
	if (tooltip) {
		_tooltipFactories.emplace(raw.get(), std::move(tooltip));
		raw->installEventFilter(this);
	}
	return raw;
}

not_null<ToolbarStarButton*> Toolbar::addStarPillButton(
		not_null<ToolbarPill*> pill,
		ToolbarActionId action,
		const style::icon *icon,
		Fn<void()> callback,
		Fn<QString()> tooltip) {
	auto owned = object_ptr<ToolbarStarButton>(
		pill.get(),
		st::ivEditorToolbarButton,
		_session);
	const auto raw = owned.data();
	raw->setIconOverride(icon);
	SetupToolbarButtonState(
		not_null<ToolbarStarButton*>(raw),
		ToolbarButtonState::Inactive,
		anim::type::instant);
	pill->addButton(std::move(owned), st::ivEditorToolbarButton);
	raw->setAccessibleName(
		ToolbarActionLabel(action, _toolbarState.linkMode));
	raw->setClickedCallback([=] {
		if (callback) {
			callback();
		}
	});
	if (tooltip) {
		_tooltipFactories.emplace(raw, std::move(tooltip));
		raw->installEventFilter(this);
	}
	return raw;
}

void Toolbar::buildPills() {
	const auto insertType = [=](State::InsertBlockType type) {
		if (_editor) {
			_editor->insertBlock({ .type = type });
		}
	};
	addPillButton(
		not_null<ToolbarPill*>(_undoRedoPill.data()),
		ToolbarActionId::Undo,
		&st::ivEditorToolbarUndoIcon,
		[=] {
			if (_editor) {
				_editor->performToolbarUndoRedo(false);
			}
		},
		Widget::ToolbarFormatAction::Undo,
		[] {
			return WithParenShortcut(
				tr::lng_wnd_menu_undo(tr::now),
				QKeySequence(QKeySequence::Undo));
		});
	addPillButton(
		not_null<ToolbarPill*>(_undoRedoPill.data()),
		ToolbarActionId::Redo,
		&st::ivEditorToolbarRedoIcon,
		[=] {
			if (_editor) {
				_editor->performToolbarUndoRedo(true);
			}
		},
		Widget::ToolbarFormatAction::Redo,
		[] {
			return WithParenShortcut(
				tr::lng_wnd_menu_redo(tr::now),
				QKeySequence(QKeySequence::Redo));
		});

	const auto controls = not_null<ToolbarPill*>(_controlsPill.data());
	const auto heading = addPillButton(
		controls,
		ToolbarActionId::Heading,
		&st::ivEditorToolbarTextStyleIcon,
		nullptr,
		std::nullopt,
		[] { return tr::lng_menu_formatting(tr::now); });
	heading->setIsMenuButton(true);
	heading->setClickedCallback([=] {
		showBlockStyleMenu(heading);
	});
	const auto textStyle = addPillButton(
		controls,
		ToolbarActionId::Bold,
		&st::ivEditorToolbarBoldIcon,
		nullptr,
		std::nullopt,
		[] { return tr::lng_article_tooltip_text_style(tr::now); });
	textStyle->setIsMenuButton(true);
	textStyle->setClickedCallback([=] {
		showTextStyleMenu(textStyle);
	});
	const auto listStyle = addStarPillButton(
		controls,
		ToolbarActionId::BulletList,
		&st::ivEditorToolbarBulletListIcon,
		nullptr,
		[=] {
			const auto active = _editor
				&& _editor->currentListRangeAtCaret().has_value();
			return active
				? tr::lng_article_tooltip_list_style(tr::now)
				: tr::lng_article_list_insert(tr::now);
		});
	listStyle->setIsMenuButton(true);
	listStyle->setClickedCallback([=] {
		showListStyleMenu(listStyle);
	});
	_listButton = listStyle;
	const auto tableStyle = addStarPillButton(
		controls,
		ToolbarActionId::Table,
		&st::ivEditorToolbarTableIcon,
		nullptr,
		[=] {
			const auto active = _editor
				&& _editor->currentTableRangeAtCaret().has_value();
			return active
				? tr::lng_article_tooltip_table_style(tr::now)
				: WithParenShortcut(
					tr::lng_article_tooltip_table_insert(tr::now),
					kEditorTableSequence);
		});
	tableStyle->setIsMenuButton(true);
	tableStyle->setClickedCallback([=] {
		if (_editor && _editor->currentTableRangeAtCaret()) {
			showTableStyleMenu(tableStyle);
		} else {
			insertType(State::InsertBlockType::Table);
		}
	});
	_tableButton = tableStyle;
	_linkButton = addPillButton(
		controls,
		ToolbarActionId::Link,
		&st::ivEditorToolbarLinkIcon,
		[=] {
			if (_editor) {
				_editor->editLinkFromToolbar();
			}
		},
		Widget::ToolbarFormatAction::Link,
		[] {
			return WithParenShortcut(
				tr::lng_article_tooltip_link(tr::now),
				QKeySequence(Ui::kEditLinkSequence));
		});
	if (_hasRequestMedia) {
		const auto attach = addStarPillButton(
			controls,
			ToolbarActionId::Attach,
			&st::ivEditorToolbarAttachIcon,
			nullptr,
			[] { return tr::lng_article_tooltip_media(tr::now); });
		attach->setIsMenuButton(true);
		attach->setClickedCallback([=] {
			showAttachMenu(attach);
		});
	}
	addStarPillButton(
		controls,
		ToolbarActionId::Math,
		&st::ivEditorToolbarMathIcon,
		[=] {
			if (_editor) {
				_editor->editMathFromToolbar();
			}
		},
		[] { return tr::lng_article_tooltip_formula(tr::now); });

	_emojiButton = addPillButton(
		not_null<ToolbarPill*>(_emojiPill.data()),
		ToolbarActionId::Attach,
		&st::ivEditorToolbarEmojiIcon,
		[=] {
			if (_toggleEmoji) {
				_toggleEmoji();
			}
		},
		std::nullopt,
		[] { return tr::lng_article_tooltip_emoji(tr::now); });
	_emojiButton->setAccessibleName(tr::lng_article_insert_emoji(tr::now));
}

void Toolbar::fillHeadingMenu(not_null<Ui::PopupMenu*> menu) {
	const auto starSize = _session->premium()
		? 0
		: st::ivEditorStyleMenuPremiumStarSize;
	for (const auto level : std::array{ 1, 2, 3, 4, 5, 6 }) {
		const auto icon = HeadingIcon(level);
		const auto shortcut = (level == 1)
			? kEditorHeading1Sequence
			: (level == 2)
			? kEditorHeading2Sequence
			: QKeySequence();
		Menu::AddActiveColorAction(
			menu,
			WithTabShortcut(Markdown::HeadingLevelLabel(level), shortcut),
			[=] {
				if (_editor) {
					_editor->insertBlock({
						.type = State::InsertBlockType::Heading,
						.headingLevel = level,
					});
				}
			},
			icon,
			false,
			starSize);
	}
}

void Toolbar::fillBlockStyleMenu(not_null<Ui::PopupMenu*> menu) {
	const auto info = _editor
		? _editor->activeBlockInfo()
		: Widget::ActiveBlockInfo();
	const auto kind = info.kind;
	using Kind = RichPage::BlockKind;
	const auto insertType = [=](State::InsertBlockType type) {
		if (_editor) {
			_editor->insertBlock({ .type = type });
		}
	};
	const auto premium = _session->premium();
	const auto starSize = premium
		? 0
		: st::ivEditorStyleMenuPremiumStarSize;
	const auto withShortcut = [&](const QString &label, QKeySequence seq) {
		if (!premium) {
			return label;
		}
		const auto shortcut = seq.toString(QKeySequence::NativeText);
		return shortcut.isEmpty()
			? label
			: (label + QChar('\t') + shortcut);
	};

	auto sub = std::make_unique<Ui::PopupMenu>(menu, st::popupMenuWithIcons);
	fillHeadingMenu(not_null<Ui::PopupMenu*>(sub.get()));
	menu->addAction(
		tr::lng_article_insert_heading(tr::now),
		std::move(sub),
		&st::ivEditorToolbarHeadingIcon,
		&st::ivEditorToolbarHeadingIcon);

	Menu::AddActiveColorAction(
		menu,
		WithTabShortcut(
			tr::lng_article_insert_text(tr::now),
			kEditorBodyTextSequence),
		[=] { applyBlockText(); },
		&st::ivEditorToolbarPlainTextIcon,
		(kind == Kind::Paragraph));
	Menu::AddActiveColorAction(
		menu,
		withShortcut(
			tr::lng_article_insert_blockquote(tr::now),
			Ui::kBlockquoteSequence),
		[=] { insertType(State::InsertBlockType::Blockquote); },
		&st::ivEditorToolbarBlockquoteIcon,
		(kind == Kind::Quote && !info.pullquote));
	Menu::AddActiveColorAction(
		menu,
		tr::lng_article_insert_pullquote(tr::now),
		[=] { insertType(State::InsertBlockType::Pullquote); },
		&st::ivEditorToolbarPullquoteIcon,
		(kind == Kind::Quote && info.pullquote),
		starSize);
	Menu::AddActiveColorAction(
		menu,
		withShortcut(
			tr::lng_article_insert_code(tr::now),
			Ui::kMonospaceSequence),
		[=] { insertType(State::InsertBlockType::Code); },
		&st::ivEditorToolbarCodeIcon,
		(kind == Kind::Code));
	Menu::AddActiveColorAction(
		menu,
		tr::lng_article_insert_divider(tr::now),
		[=] { insertType(State::InsertBlockType::Divider); },
		&st::ivEditorToolbarDividerIcon,
		false,
		starSize);
}

void Toolbar::applyBlockText() {
	if (!_editor) {
		return;
	}
	const auto info = _editor->activeBlockInfo();
	using Kind = RichPage::BlockKind;
	switch (info.kind) {
	case Kind::Quote:
		_editor->insertBlock({
			.type = info.pullquote
				? State::InsertBlockType::Pullquote
				: State::InsertBlockType::Blockquote,
		});
		break;
	case Kind::Code:
		_editor->insertBlock({ .type = State::InsertBlockType::Code });
		break;
	case Kind::Heading:
		_editor->applyToolbarFormatAction(
			Widget::ToolbarFormatAction::PlainText);
		break;
	default:
		break;
	}
}

void Toolbar::showBlockStyleMenu(not_null<Ui::IconButton*> button) {
	if (_menu) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	fillBlockStyleMenu(not_null<Ui::PopupMenu*>(_menu.get()));
	_menu->popup(button->mapToGlobal(QPoint(0, button->height())));
}

void Toolbar::fillTextStyleMenu(not_null<Ui::PopupMenu*> menu) {
	using Action = Widget::ToolbarFormatAction;
	const auto premium = _session->premium();
	const auto starSize = premium
		? 0
		: st::ivEditorStyleMenuPremiumStarSize;
	const auto add = [&](
			Action action,
			const QString &label,
			const style::icon *icon,
			QKeySequence shortcut = QKeySequence(),
			bool premiumOnly = false) {
		const auto &state = _toolbarState[action];
		if (!state.shown) {
			return;
		}
		Menu::AddActiveColorAction(
			menu,
			WithTabShortcut(label, shortcut),
			[=] {
				if (_editor) {
					_editor->applyToolbarFormatAction(action);
				}
			},
			icon,
			state.active,
			premiumOnly ? starSize : 0);
	};
	// Bold..Spoiler exist in regular messages too, so they are not marked
	// as premium - only the rich-message-only entities below are.
	add(Action::Bold,
		tr::lng_menu_formatting_bold(tr::now),
		&st::ivEditorToolbarBoldIcon,
		QKeySequence::Bold);
	add(Action::Italic,
		tr::lng_menu_formatting_italic(tr::now),
		&st::ivEditorToolbarItalicIcon,
		QKeySequence::Italic);
	add(Action::Underline,
		tr::lng_menu_formatting_underline(tr::now),
		&st::ivEditorToolbarUnderlineIcon,
		QKeySequence::Underline);
	add(Action::StrikeOut,
		tr::lng_menu_formatting_strike_out(tr::now),
		&st::ivEditorToolbarStrikeOutIcon,
		Ui::kStrikeOutSequence);
	add(Action::Spoiler,
		tr::lng_menu_formatting_spoiler(tr::now),
		&st::ivEditorToolbarSpoilerIcon,
		Ui::kSpoilerSequence);
	add(Action::Subscript,
		tr::lng_menu_formatting_subscript(tr::now),
		&st::ivEditorToolbarSubscriptIcon,
		QKeySequence(),
		true);
	add(Action::Superscript,
		tr::lng_menu_formatting_superscript(tr::now),
		&st::ivEditorToolbarSuperscriptIcon,
		QKeySequence(),
		true);
	add(Action::Marked,
		tr::lng_menu_formatting_marked(tr::now),
		&st::ivEditorToolbarMarkedIcon,
		QKeySequence(),
		true);
}

void Toolbar::showTextStyleMenu(not_null<Ui::IconButton*> button) {
	if (_menu) {
		return;
	}
	auto menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	fillTextStyleMenu(not_null<Ui::PopupMenu*>(menu.get()));
	if (menu->empty()) {
		return;
	}
	_menu = std::move(menu);
	_menu->popup(button->mapToGlobal(QPoint(0, button->height())));
}

void Toolbar::fillAttachMenu(not_null<Ui::PopupMenu*> menu) {
	const auto starSize = _session->premium()
		? 0
		: st::ivEditorStyleMenuPremiumStarSize;
	Menu::AddActiveColorAction(
		menu,
		tr::lng_attach_photo_or_video(tr::now),
		[=] {
			if (_editor) {
				_editor->requestMedia(
					std::nullopt,
					RequestMediaType::PhotoVideo);
			}
		},
		&st::ivEditorToolbarAttachIcon,
		false,
		starSize);
	Menu::AddActiveColorAction(
		menu,
		tr::lng_in_dlg_audio_file(tr::now),
		[=] {
			if (_editor) {
				_editor->requestMedia(
					std::nullopt,
					RequestMediaType::Audio);
			}
		},
		&st::ivEditorToolbarAudioIcon,
		false,
		starSize);
	if (_requestMap) {
		Menu::AddActiveColorAction(
			menu,
			tr::lng_maps_point(tr::now),
			[=] {
				if (_editor) {
					const auto parent = _tooltipParent;
					auto closeRequests = parent
						? static_cast<Ui::RpWidget*>(parent.data())->death()
						: rpl::never<>();
					Ui::PreventDelayedActivation();
					_requestMap(
						not_null<Widget*>(_editor.data()),
						parent,
						std::move(closeRequests));
				}
			},
			&st::ivEditorToolbarLocationIcon,
			false,
			starSize);
	}
}

void Toolbar::showAttachMenu(not_null<Ui::RippleButton*> button) {
	if (_menu) {
		return;
	}
	auto menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	fillAttachMenu(not_null<Ui::PopupMenu*>(menu.get()));
	_menu = std::move(menu);
	_menu->popup(button->mapToGlobal(QPoint(0, button->height())));
}

void Toolbar::fillListStyleMenu(not_null<Ui::PopupMenu*> menu) {
	const auto insertType = [=](State::InsertBlockType type) {
		if (_editor) {
			_editor->insertBlock({ .type = type });
		}
	};
	const auto starSize = _session->premium()
		? 0
		: st::ivEditorStyleMenuPremiumStarSize;
	const auto addInserts = [=](not_null<Ui::PopupMenu*> target) {
		Menu::AddActiveColorAction(
			target,
			tr::lng_article_insert_ordered_list(tr::now),
			[=] { insertType(State::InsertBlockType::OrderedList); },
			&st::ivEditorToolbarOrderedListIcon,
			false,
			starSize);
		Menu::AddActiveColorAction(
			target,
			tr::lng_article_insert_bullet_list(tr::now),
			[=] { insertType(State::InsertBlockType::BulletList); },
			&st::ivEditorToolbarBulletListIcon,
			false,
			starSize);
		Menu::AddActiveColorAction(
			target,
			tr::lng_article_insert_task_list(tr::now),
			[=] { insertType(State::InsertBlockType::TaskList); },
			&st::ivEditorToolbarTaskListIcon,
			false,
			starSize);
		Menu::AddActiveColorAction(
			target,
			tr::lng_article_insert_details(tr::now),
			[=] { insertType(State::InsertBlockType::Details); },
			&st::ivEditorToolbarDetailsIcon,
			false,
			starSize);
	};

	const auto range = _editor
		? _editor->currentListRangeAtCaret()
		: std::optional<Markdown::PreparedEditListItemRange>();
	if (!range) {
		addInserts(menu);
		return;
	}
	const auto info = _editor->listSelectionInfo(*range);
	const auto hasItemMenu = info.valid
		&& (info.listKind == RichPage::ListKind::Ordered)
		&& !info.taskList;

	auto changeSub = std::make_unique<Ui::PopupMenu>(
		menu,
		st::popupMenuWithIcons);
	_editor->fillListChangeMenu(
		not_null<Ui::PopupMenu*>(changeSub.get()),
		*range);
	menu->addAction(
		tr::lng_article_list_change(tr::now),
		std::move(changeSub),
		&st::ivEditorToolbarBulletListIcon,
		&st::ivEditorToolbarBulletListIcon);

	if (hasItemMenu) {
		const auto itemRange = _editor->currentListItemRangeAtCaret();
		if (itemRange) {
			auto itemSub = std::make_unique<Ui::PopupMenu>(
				menu,
				st::popupMenuWithIcons);
			_editor->fillListItemChangeMenu(
				not_null<Ui::PopupMenu*>(itemSub.get()),
				*itemRange);
			menu->addAction(
				tr::lng_article_list_item_change(tr::now),
				std::move(itemSub),
				&st::ivEditorToolbarOrderedListIcon,
				&st::ivEditorToolbarOrderedListIcon);
		}
	}

	auto insertSub = std::make_unique<Ui::PopupMenu>(
		menu,
		st::popupMenuWithIcons);
	addInserts(not_null<Ui::PopupMenu*>(insertSub.get()));
	menu->addAction(
		tr::lng_article_list_insert(tr::now),
		std::move(insertSub),
		&st::ivEditorToolbarBulletListIcon,
		&st::ivEditorToolbarBulletListIcon);
}

void Toolbar::showListStyleMenu(not_null<Ui::RippleButton*> button) {
	if (_menu) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	fillListStyleMenu(not_null<Ui::PopupMenu*>(_menu.get()));
	_menu->popup(button->mapToGlobal(QPoint(0, button->height())));
}

void Toolbar::fillTableStyleMenu(not_null<Ui::PopupMenu*> menu) {
	if (!_editor) {
		return;
	}
	const auto range = _editor->currentTableRangeAtCaret();
	if (!range) {
		return;
	}
	_editor->fillTableChangeMenu(menu, *range);
}

void Toolbar::showTableStyleMenu(not_null<Ui::RippleButton*> button) {
	if (_menu) {
		return;
	}
	auto menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	fillTableStyleMenu(not_null<Ui::PopupMenu*>(menu.get()));
	if (menu->empty()) {
		return;
	}
	_menu = std::move(menu);
	_menu->popup(button->mapToGlobal(QPoint(0, button->height())));
}

void Toolbar::updateFromEditorState() {
	for (const auto &pb : _stateButtons) {
		const auto &state = _toolbarState[pb.format];
		const auto value = state.active
			? ToolbarButtonState::Active
			: state.enabled
			? ToolbarButtonState::Inactive
			: ToolbarButtonState::Disabled;
		SetupToolbarButton(not_null<Ui::IconButton*>(pb.button), value);
	}
	if (_linkButton) {
		_linkButton->setAccessibleName(
			ToolbarActionLabel(ToolbarActionId::Link, _toolbarState.linkMode));
	}
	if (_listButton) {
		const auto inList = _editor
			&& _editor->currentListRangeAtCaret().has_value();
		SetupToolbarButtonState(
			not_null<ToolbarStarButton*>(_listButton),
			inList
				? ToolbarButtonState::Active
				: ToolbarButtonState::Inactive);
	}
	if (_tableButton) {
		const auto inTable = _editor
			&& _editor->currentTableRangeAtCaret().has_value();
		SetupToolbarButtonState(
			not_null<ToolbarStarButton*>(_tableButton),
			inTable
				? ToolbarButtonState::Active
				: ToolbarButtonState::Inactive);
	}
}

void Toolbar::setEmojiColumnOpen(bool open) {
	if (!_emojiButton) {
		return;
	}
	SetupToolbarButton(
		not_null<Ui::IconButton*>(_emojiButton),
		open ? ToolbarButtonState::Active : ToolbarButtonState::Inactive);
}

int Toolbar::minimalWidth() const {
	return st::ivEditorToolbarPadding.left()
		+ st::ivEditorToolbarPadding.right()
		+ 2 * st::ivEditorToolbarGroupsSkip
		+ _undoRedoPill->naturalSize().width()
		+ _controlsPill->naturalSize().width()
		+ _emojiPill->naturalSize().width();
}

int Toolbar::contentMaxWidth() const {
	const auto padding = st::ivEditorToolbarPadding;
	return minimalWidth() - padding.left() - padding.right();
}

int Toolbar::resizeGetHeight(int width) {
	const auto padding = st::ivEditorToolbarPadding;
	const auto top = padding.top();
	const auto column = _editor
		? _editor->articleColumnForWidth(width)
		: Widget::ArticleColumn{ 0, width };
	const auto fitsArticle = (column.width >= contentMaxWidth());
	const auto left = fitsArticle ? column.left : 0;
	const auto right = fitsArticle ? (column.left + column.width) : width;
	const auto undoRedoLeft = left;
	_undoRedoPill->moveToLeft(undoRedoLeft, top, width);
	const auto controlsLeft = undoRedoLeft
		+ _undoRedoPill->naturalSize().width()
		+ st::ivEditorToolbarGroupsSkip;
	_controlsPill->moveToLeft(controlsLeft, top, width);
	const auto emojiLeft = right - _emojiPill->naturalSize().width();
	_emojiPill->moveToLeft(emojiLeft, top, width);
	updateInputMask();
	if (_hovered && _hovered->isHidden()) {
		hideTooltip();
	}
	updateTooltipGeometry();
	return top + _controlsPill->naturalSize().height() + padding.bottom();
}

void Toolbar::updateInputMask() {
	auto region = QRegion();
	const auto add = [&](not_null<const ToolbarPill*> pill) {
		if (!pill->isHidden()) {
			region += pill->geometry();
		}
	};
	add(not_null<const ToolbarPill*>(_undoRedoPill.data()));
	add(not_null<const ToolbarPill*>(_controlsPill.data()));
	add(not_null<const ToolbarPill*>(_emojiPill.data()));
	if (region.isEmpty()) {
		clearMask();
	} else {
		setMask(region);
	}
}

void Toolbar::hideShownTooltip() {
	hideTooltip();
}

bool Toolbar::eventFilter(QObject *object, QEvent *event) {
	const auto button = static_cast<Ui::RippleButton*>(object);
	if (_tooltipFactories.contains(button)) {
		if (event->type() == QEvent::Enter) {
			scheduleTooltip(not_null<Ui::RippleButton*>(button));
		} else if (event->type() == QEvent::Leave) {
			if (_scheduledTooltip == button) {
				_scheduledTooltip = nullptr;
			}
			if (_hovered == button) {
				hideTooltip();
			}
		}
	}
	return Ui::RpWidget::eventFilter(object, event);
}

void Toolbar::scheduleTooltip(not_null<Ui::RippleButton*> button) {
	// Showing the tooltip synchronously grabs the widget to build an
	// animation cache, which crashes if we're inside a widget-tree
	// destructor (an Enter event synthesized while a layer is destroyed
	// and the mouse ends up over one of the toolbar buttons). Offload the
	// actual show to the next event loop iteration, guarded by the button
	// still being hovered (cleared on Leave) and by not queuing twice.
	const auto already = (_scheduledTooltip != nullptr);
	_scheduledTooltip = button;
	if (already) {
		return;
	}
	crl::on_main(this, [=] {
		if (const auto button = base::take(_scheduledTooltip)) {
			showTooltip(not_null<Ui::RippleButton*>(button));
		}
	});
}

void Toolbar::showTooltip(not_null<Ui::RippleButton*> button) {
	hideTooltip();
	const auto i = _tooltipFactories.find(button.get());
	if (i == end(_tooltipFactories) || !i->second) {
		return;
	}
	_hovered = button;
	const auto parent = _tooltipParent
		? _tooltipParent.data()
		: (parentWidget() ? parentWidget() : this);
	_tooltip.reset(Ui::CreateChild<Ui::ImportantTooltip>(
		parent,
		Ui::MakeNiceTooltipLabel(
			parent,
			rpl::single(TextWithEntities::Simple(i->second())),
			st::boxWideWidth,
			st::defaultImportantTooltipLabel),
		st::defaultImportantTooltip));
	_tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	_tooltip->toggleFast(false);
	updateTooltipGeometry();
	_tooltip->raise();
	_tooltip->toggleAnimated(true);
}

void Toolbar::hideTooltip() {
	_hovered = nullptr;
	_scheduledTooltip = nullptr;
	if (_tooltip) {
		_tooltip->toggleFast(false);
		_tooltip = nullptr;
	}
}

void Toolbar::updateTooltipGeometry() {
	if (!_tooltip || !_hovered) {
		return;
	}
	const auto parent = _tooltip->parentWidget();
	const auto geometry = Ui::MapFrom(parent, _hovered, _hovered->rect());
	_tooltip->pointAt(geometry, RectPart::Top | RectPart::Center);
}

} // namespace

struct WindowHost::Impl final {
public:
	explicit Impl(ShowWindowDescriptor descriptor);
	~Impl();
	void close();
	void activateClose();

private:
	void setupWindow(ShowWindowDescriptor &&descriptor);
	void setupEmojiColumn(const ShowWindowDescriptor &descriptor);
	void setupBottomAiStar(
		not_null<HistoryView::Controls::ComposeAiButton*> button,
		not_null<Main::Session*> session);
	void layout();
	void updateBottomMask();
	void toggleEmojiColumn();
	void showEmojiColumn();
	void hideEmojiColumn(bool skipResize = false);
	void updateEditorVisibleTopBottom();
	void setEmojiColumnInteractionActive(bool active);
	[[nodiscard]] int emojiColumnWidth() const;
	[[nodiscard]] int minimalWindowWidth() const;
	[[nodiscard]] int minimalWindowWidthWithEmojiColumn() const;
	[[nodiscard]] bool handleCloseRequest();
	void finishCloseFromAcceptedEvent();
	void finishClose();
	[[nodiscard]] bool articleChanged();
	[[nodiscard]] bool articleEmptyForDiscard();
	[[nodiscard]] bool showCloseConfirmation();
	[[nodiscard]] bool showDiscardConfirmation();
	[[nodiscard]] bool confirmCancel();
	void discard();
	void submit();

	std::unique_ptr<Window> _window;
	std::shared_ptr<ChatHelpers::Show> _show;
	std::shared_ptr<State> _state;
	RichPage _initialPage;
	object_ptr<Ui::RpWidget> _top = { nullptr };
	object_ptr<Ui::RpWidget> _bottomFade = { nullptr };
	object_ptr<Ui::RpWidget> _bottom = { nullptr };
	object_ptr<Ui::ElasticScroll> _scroll = { nullptr };
	QPointer<Widget> _editor;
	object_ptr<Toolbar> _toolbar = { nullptr };
	object_ptr<ToolbarPill> _discard = { nullptr };
	object_ptr<ToolbarPill> _cancel = { nullptr };
	object_ptr<ToolbarPill> _aiPill = { nullptr };
	object_ptr<Ui::SendButton> _send = { nullptr };
	Ui::RpWidget *_sendLock = nullptr;
	object_ptr<ChatHelpers::TabbedSelector> _emojiColumn = { nullptr };
	object_ptr<Ui::PlainShadow> _emojiColumnShadow = { nullptr };
	object_ptr<ToolbarPill> _emojiColumnClose = { nullptr };
	Fn<bool()> _discarded;
	Fn<bool()> _cancelled;
	Fn<bool()> _changedCancelled;
	Fn<bool()> _confirmed;
	Fn<void()> _closed;
	base::weak_qptr<Ui::GenericBox> _closeConfirmation;
	base::weak_qptr<Ui::GenericBox> _discardConfirmation;
	rpl::lifetime _lifetime;
	int _emojiColumnExtendedBy = 0;
	int _searchTopSlide = 0;
	bool _emojiColumnShown = false;
	bool _emojiColumnInteractionActive = false;
	bool _closingApproved = false;
	bool _closedNotified = false;

};

WindowHost::Impl::Impl(ShowWindowDescriptor descriptor) {
	setupWindow(std::move(descriptor));
}

WindowHost::Impl::~Impl() {
	hideEmojiColumn(true);
}

void WindowHost::Impl::close() {
	finishClose();
}

void WindowHost::Impl::activateClose() {
	if (confirmCancel()) {
		finishClose();
	}
}

void WindowHost::Impl::setupWindow(ShowWindowDescriptor &&descriptor) {
	const auto title = tr::lng_article_editor_title(tr::now);

	if (!descriptor.state) {
		descriptor.state = std::make_shared<State>();
	}
	_state = descriptor.state;
	_initialPage = _state->richPage();

	_window = std::make_unique<Window>();
	const auto window = _window.get();
	window->setCloseRequestHandler([=] {
		return handleCloseRequest();
	});
	_show = std::make_shared<WindowContext>(window, descriptor.session);
	if (descriptor.showCreated) {
		descriptor.showCreated(_show);
	}
	window->setTitle(title);
	window->setWindowTitle(title);
	window->setMinimumSize(st::ivEditorWindowMinSize);
	window->setGeometry(DefaultWindowGeometry());

	window->body()->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(window->body().get()).fillRect(clip, st::windowBg);
	}, window->body()->lifetime());

	_top = object_ptr<Ui::RpWidget>(window->body().get());
	_top->setAttribute(Qt::WA_TransparentForMouseEvents);
	_top->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_top.data());
		Dialogs::PaintTopFade(
			p,
			_top->width(),
			_top->height(),
			st::windowBg->c);
	}, _top->lifetime());
	_bottomFade = object_ptr<Ui::RpWidget>(window->body().get());
	_bottomFade->setAttribute(Qt::WA_TransparentForMouseEvents);
	_bottomFade->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_bottomFade.data());
		Dialogs::PaintBottomFade(
			p,
			_bottomFade->width(),
			_bottomFade->height(),
			st::windowBg->c);
	}, _bottomFade->lifetime());
	_bottom = object_ptr<Ui::RpWidget>(window->body().get());

	const auto hasRequestMedia = static_cast<bool>(descriptor.requestMedia);
	_scroll = object_ptr<Ui::ElasticScroll>(window->body().get(), st::boxScroll);
	using OverscrollType = Ui::ElasticScroll::OverscrollType;
	_scroll->setOverscrollTypes(OverscrollType::Real, OverscrollType::Real);
	const auto scroll = _scroll.data();
	scroll->setOverscrollBg(st::windowBg->c);
	style::PaletteChanged(
	) | rpl::on_next([=] {
		scroll->setOverscrollBg(st::windowBg->c);
	}, scroll->lifetime());
	_editor = _scroll->setOwnedWidget(object_ptr<Widget>(
		_scroll.data(),
		WidgetServices{
			.session = descriptor.session,
			.show = _show,
			.outer = window->body(),
			.customEmojiPaused = [show = _show] {
				return show->paused(ChatHelpers::PauseReason::Layer);
			},
			.requestMedia = std::move(descriptor.requestMedia),
			.applyPreparedMedia = std::move(descriptor.applyPreparedMedia),
			.requestPhotoEditSource
				= std::move(descriptor.requestPhotoEditSource),
			.replacePhotoWithList
				= std::move(descriptor.replacePhotoWithList),
			.mediaUploadState = std::move(descriptor.mediaUploadState),
			.cancelMediaUpload = std::move(descriptor.cancelMediaUpload),
			.addMediaAndGroupWithBlock
				= std::move(descriptor.addMediaAndGroupWithBlock),
			.imeCompositionStarts = window->imeCompositionStarts(),
		},
		descriptor.peer,
		descriptor.state,
		std::move(descriptor.showLimitToast)));
	const auto editor = not_null<Widget*>(_editor.data());
	if (descriptor.editorCreated) {
		descriptor.editorCreated(editor);
	}
	const auto body = QPointer<QWidget>(window->body().get());
	setupEmojiColumn(descriptor);

	_toolbar = object_ptr<Toolbar>(
		window->body().get(),
		editor,
		body,
		hasRequestMedia,
		std::move(descriptor.requestMap),
		[=] {
			_toolbar->hideShownTooltip();
			toggleEmojiColumn();
		},
		descriptor.session);
	window->setMinimumWidth(minimalWindowWidth());
	if (descriptor.discarded) {
		_discard = object_ptr<ToolbarPill>(
			_bottom.data(),
			st::ivEditorPillShadow);
		const auto button = _discard->addButton(
			st::ivEditorBottomDiscardButton,
			&st::ivEditorBottomDiscardIcon,
			&st::ivEditorBottomDiscardIcon,
			ToolbarButtonState::Inactive);
		button->setAccessibleName(tr::lng_record_lock_discard(tr::now));
		button->setClickedCallback([=] {
			discard();
		});
	}
	if (descriptor.submitType == ShowWindowDescriptor::SubmitType::Save) {
		_cancel = object_ptr<ToolbarPill>(
			_bottom.data(),
			st::ivEditorPillShadow);
		const auto button = _cancel->addButton(
			st::ivEditorBottomCancelButton,
			&st::ivEditorBottomCancelIcon,
			&st::ivEditorBottomCancelIcon,
			ToolbarButtonState::Inactive);
		button->setAccessibleName(tr::lng_cancel(tr::now));
		button->setClickedCallback([=] {
			if (confirmCancel()) {
				finishClose();
			}
		});
	}
	if (!base::options::value<bool>(Ui::kOptionHideAiButton)) {
		const auto session = descriptor.session;
		_aiPill = object_ptr<ToolbarPill>(
			_bottom.data(),
			st::ivEditorPillShadow);
		auto owned = object_ptr<HistoryView::Controls::ComposeAiButton>(
			_aiPill.data(),
			st::ivEditorToolbarButton,
			st::ivEditorBottomAiIcon,
			st::ivEditorBottomAiStar1,
			st::ivEditorBottomAiStar2);
		const auto button = owned.data();
		_aiPill->addButton(std::move(owned), st::ivEditorToolbarButton);
		button->setAccessibleName(tr::lng_ai_compose_title(tr::now));
		button->setClickedCallback([=] {
			if (!session->premium()) {
				const auto show = _show;
				show->showToast({
					.text = tr::lng_article_premium_required(
						tr::now,
						lt_link,
						tr::link(tr::bold(
							tr::lng_article_premium_required_link(
								tr::now))),
						tr::marked),
					.filter = [=](
							const ClickHandlerPtr &handler,
							Qt::MouseButton button) {
						if (button != Qt::LeftButton) {
							return false;
						}
						if (show && show->valid()) {
							ShowPremiumPreviewToBuy(
								show,
								PremiumFeature::RichFormatting);
						} else if (const auto window
								= session->tryResolveWindow(nullptr)) {
							ShowPremiumPreviewToBuy(
								window,
								PremiumFeature::RichFormatting);
						}
						return true;
					},
					.icon = &st::settingsToastStarIcon,
					.adaptive = true,
					.duration = Ui::Toast::kDefaultDuration * 2,
				});
				return;
			}
			const auto editor = _editor;
			if (editor && editor->hasActiveSelection()) {
				auto span = editor->textSpanForCurrentSelection();
				if (!span.text.isEmpty()) {
					HistoryView::Controls::ShowComposeAiBox(_show, {
						.session = session,
						.text = std::move(span),
						.apply = [editor](TextWithEntities result) {
							if (!editor || result.text.isEmpty()) {
								return;
							}
							editor->replaceCurrentSelectionWithText(
								std::move(result));
						},
					});
					return;
				}
				auto source = editor->richPageForCurrentSelection();
				if (source && !source->blocks.empty()) {
					HistoryView::Controls::ShowComposeAiBox(_show, {
						.session = session,
						.richSource = std::move(source),
						.applyRich = [editor](
								std::shared_ptr<const RichPage> page) {
							if (!editor || !page || page->blocks.empty()) {
								return;
							}
							editor->replaceCurrentSelectionWithRichPage(
								std::move(page));
						},
					});
					return;
				}
			}
			ShowCreateAiBox(_show, {
				.session = session,
				.applyToPage = [editor](std::shared_ptr<const RichPage> page) {
					if (!editor || !page || page->blocks.empty()) {
						return;
					}
					editor->insertPreparedBlocks(page->blocks);
				},
			});
		});
		setupBottomAiStar(button, session);
	}
	const auto save = (descriptor.submitType
		== ShowWindowDescriptor::SubmitType::Save);
	_send = object_ptr<Ui::SendButton>(
		_bottom.data(),
		save ? st::ivEditorBottomSaveSend : st::ivEditorBottomSend);
	const auto raw = _send.data();
	raw->setAccessibleName(SubmitText(descriptor));
	raw->setClickedCallback([=] { submit(); });
	raw->show();
	if (descriptor.setupSubmitButton) {
		descriptor.setupSubmitButton(
			not_null<Ui::RpWidget*>(raw));
	}
	if (!save) {
		const auto session = descriptor.session;
		const auto peer = descriptor.peer;
		session->changes().peerFlagsValue(
			peer,
			Data::PeerUpdate::Flag::StarsPerMessage
		) | rpl::on_next([=] {
			raw->setState({
				.starsToSend = peer->starsPerMessageChecked(),
			});
		}, raw->lifetime());
		raw->finishAnimating();
		raw->widthValue() | rpl::skip(1) | rpl::on_next([=] {
			layout();
		}, raw->lifetime());
	}
	{
		const auto lockIcon = &st::ivEditorSendLockIcon;
		const auto lockPadding = st::ivEditorSendLockBadgePadding;
		_sendLock = Ui::CreateChild<Ui::RpWidget>(_bottom.data());
		const auto lock = _sendLock;
		lock->setAttribute(Qt::WA_TransparentForMouseEvents);
		lock->resize(
			lockIcon->width() + 2 * lockPadding,
			lockIcon->height() + 2 * lockPadding);
		raw->geometryValue() | rpl::on_next([=](QRect geometry) {
			lock->move(geometry.topLeft() + st::ivEditorSendLockBadgeShift);
			lock->raise();
			updateBottomMask();
		}, lock->lifetime());
		lock->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(lock);
			auto hq = PainterHighQualityEnabler(p);
			const auto border = st::ivEditorSendLockBadgeStroke;
			auto pen = QPen(st::windowBg);
			pen.setWidth(border);
			p.setPen(pen);
			p.setBrush(st::windowBgActive);
			const auto half = border / 2.;
			p.drawEllipse(QRectF(lock->rect()).marginsRemoved(
				QMarginsF(half, half, half, half)));
			lockIcon->paint(p, lockPadding, lockPadding, lock->width());
		}, lock->lifetime());
		lock->hide();
		const auto session = descriptor.session;
		const auto state = _state;
		const auto editor = not_null<Widget*>(_editor.data());
		const auto premium = lock->lifetime().make_state<bool>(true);
		const auto refresh = [=] {
			const auto locked = !*premium
				&& !state->articleEmpty()
				&& !Iv::CanSerializeAsSimple(state->richPage(), session);
			lock->setVisible(locked);
			if (locked) {
				lock->raise();
			}
			updateBottomMask();
		};
		Data::AmPremiumValue(
			session
		) | rpl::on_next([=](bool value) {
			*premium = value;
			refresh();
		}, lock->lifetime());
		editor->toolbarStateChanges() | rpl::on_next([=] {
			refresh();
		}, lock->lifetime());
	}

	_discarded = std::move(descriptor.discarded);
	_cancelled = std::move(descriptor.cancelled);
	_changedCancelled = std::move(descriptor.changedCancelled);
	_confirmed = std::move(descriptor.confirmed);
	_closed = std::move(descriptor.closed);

	window->body()->sizeValue() | rpl::on_next([=](QSize) {
		layout();
	}, _lifetime);
	editor->searchSlideHeightValue() | rpl::on_next([=](int slide) {
		if (_searchTopSlide == slide) {
			return;
		}
		_searchTopSlide = slide;
		if (_top && _toolbar) {
			_top->setGeometry(
				0,
				0,
				_top->width(),
				_toolbar->height() + slide);
		}
	}, _lifetime);
	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue()
	) | rpl::on_next([=](int, int) {
		updateEditorVisibleTopBottom();
	}, _lifetime);
	window->events() | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress) {
			const auto event = static_cast<QKeyEvent*>(e.get());
			if (event->key() == Qt::Key_Escape) {
				event->accept();
				if (_editor && _editor->closeSearch()) {
					return;
				}
				if (confirmCancel()) {
					finishClose();
				}
			}
		}
	}, _lifetime);

	layout();
	_top->show();
	_bottomFade->show();
	_bottom->show();
	_scroll->show();
	_top->raise();
	_bottomFade->raise();
	_toolbar->show();
	_toolbar->raise();
	_bottom->raise();
	window->show();
	editor->activateInitialNode();
}

void WindowHost::Impl::setupBottomAiStar(
		not_null<HistoryView::Controls::ComposeAiButton*> button,
		not_null<Main::Session*> session) {
	const auto premium = button->lifetime().make_state<bool>(false);
	const auto refresh = [=] {
		if (*premium) {
			button->setPremiumStar(QImage(), QPoint(), 0);
		} else {
			const auto side = st::ivEditorToolbarPremiumStarSize;
			const auto skip = st::ivEditorToolbarPremiumStarSkip;
			button->setPremiumStar(
				PremiumStarImage(),
				QPoint(
					button->width() - side - skip.x(),
					button->height() - side - skip.y()),
				st::ivEditorToolbarPremiumStarOutline);
		}
	};
	Data::AmPremiumValue(session) | rpl::on_next([=](bool value) {
		*premium = value;
		refresh();
	}, button->lifetime());
	style::PaletteChanged() | rpl::on_next([=] {
		refresh();
	}, button->lifetime());
}

void WindowHost::Impl::setupEmojiColumn(const ShowWindowDescriptor &descriptor) {
	using Selector = ChatHelpers::TabbedSelector;
	_emojiColumnShadow = object_ptr<Ui::PlainShadow>(_window->body().get());
	_emojiColumnShadow->hide();
	_emojiColumnClose = object_ptr<ToolbarPill>(
		_window->body().get(),
		st::ivEditorPillShadow);
	_emojiColumnClose->addButton(
		st::ivEditorEmojiColumnClose,
		&st::ivEditorEmojiColumnCloseIcon,
		&st::ivEditorEmojiColumnCloseIconOver,
		ToolbarButtonState::Inactive
	)->setClickedCallback([=] {
		hideEmojiColumn();
	});
	_emojiColumnClose->hide();
	const auto closeNatural = _emojiColumnClose->naturalSize();
	const auto closeMargins = _emojiColumnClose->shadowMargins();
	const auto closeVisibleWidth = closeNatural.width()
		- closeMargins.left()
		- closeMargins.right();
	const auto searchRightReserved = st::ivEditorEmojiColumnCloseScrollSkip
		+ closeVisibleWidth
		+ st::emojiPanRadius
		+ st::defaultEmojiPan.searchMargin.left();
	_emojiColumn = object_ptr<Selector>(
		_window->body().get(),
		ChatHelpers::TabbedSelectorDescriptor{
			.show = _show,
			.st = st::defaultEmojiPan,
			.level = ChatHelpers::PauseReason::Layer,
			.mode = Selector::Mode::EmojiOnly,
			.features = {
				.stickersSettings = false,
				.openStickerSets = false,
			},
			.searchRightReserved = searchRightReserved,
		});
	_emojiColumn->hide();
	_emojiColumn->setCurrentPeer(descriptor.peer);
	_emojiColumn->emojiChosen(
	) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
		if (_editor) {
			_editor->insertEmoji(data.emoji);
		}
	}, _lifetime);
	_emojiColumn->customEmojiChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen data) {
		const auto document = data.document;
		if (!IsEmojiDocument(document)) {
			return;
		}
		if (document->isPremiumEmoji()
			&& !descriptor.session->premium()
			&& !Data::AllowEmojiWithoutPremium(descriptor.peer, document)) {
			ShowPremiumPreviewBox(
				_show,
				PremiumFeature::AnimatedEmoji);
		} else if (_editor) {
			_editor->insertCustomEmoji(document);
		}
	}, _lifetime);
}

void WindowHost::Impl::layout() {
	if (!_window || !_top || !_bottomFade || !_bottom || !_toolbar || !_editor
		|| !_emojiColumn || !_emojiColumnShadow || !_emojiColumnClose) {
		return;
	}
	const auto width = _window->body()->width();
	const auto height = _window->body()->height();
	const auto padding = st::ivEditorBottomControlsPadding;
	const auto emojiWidth = _emojiColumnShown ? emojiColumnWidth() : 0;
	const auto editorWidth = std::max(width - emojiWidth, 0);
	_editor->setContentMaxWidth(_toolbar->contentMaxWidth());
	const auto toolbarHeight = _toolbar->resizeGetHeight(editorWidth);
	auto buttonsHeight = _send->height();
	if (_cancel) {
		buttonsHeight = std::max(
			buttonsHeight,
			_cancel->naturalSize().height());
	}
	if (_discard) {
		buttonsHeight = std::max(
			buttonsHeight,
			_discard->naturalSize().height());
	}
	const auto bottomHeight = padding.top() + buttonsHeight + padding.bottom();
	const auto buttonsTop = padding.top();
	_top->setGeometry(0, 0, editorWidth, toolbarHeight + _searchTopSlide);
	_toolbar->setGeometry(0, 0, editorWidth, toolbarHeight);
	_toolbar->raise();
	_bottomFade->setGeometry(0, height - bottomHeight, editorWidth, bottomHeight);
	_bottom->setGeometry(0, height - bottomHeight, editorWidth, bottomHeight);
	const auto column = _editor->articleColumnForWidth(editorWidth);
	const auto fitsArticle = (column.width >= _toolbar->contentMaxWidth());
	const auto right = fitsArticle
		? (column.left + column.width)
		: editorWidth;
	const auto left = fitsArticle ? column.left : 0;
	const auto leftPill = _discard
		? _discard.data()
		: _cancel.data();
	const auto shadowSkipRight = leftPill
		? leftPill->shadowMargins().right()
		: 0;
	const auto shadowSkipTop = leftPill
		? leftPill->shadowMargins().top()
		: 0;
	_send->moveToLeft(
		right - shadowSkipRight - _send->width(),
		buttonsTop + shadowSkipTop,
		editorWidth);
	if (leftPill) {
		leftPill->moveToLeft(
			right
				- shadowSkipRight
				- _send->width()
				- st::ivEditorToolbarGroupsSkip
				- leftPill->naturalSize().width(),
			buttonsTop,
			editorWidth);
	}
	if (_aiPill) {
		_aiPill->moveToLeft(
			left - _aiPill->shadowMargins().left(),
			buttonsTop,
			editorWidth);
	}
	updateBottomMask();
	_scroll->setGeometry(0, 0, editorWidth, std::max(height, 1));
	_scroll->setBarTopInset(toolbarHeight);
	_scroll->setBarBottomInset(bottomHeight);
	if (_emojiColumnShown) {
		_emojiColumn->setGeometry(
			editorWidth,
			0,
			emojiWidth,
			height);
		_emojiColumnShadow->setGeometry(editorWidth, 0, st::lineWidth, height);
		_emojiColumnShadow->show();
		_emojiColumnShadow->raise();
		const auto closeNatural = _emojiColumnClose->naturalSize();
		const auto closeMargins = _emojiColumnClose->shadowMargins();
		const auto searchCenterY = st::defaultEmojiPan.searchMargin.top()
			+ st::defaultTabbedSearch.height / 2;
		const auto closeVisibleHeight = closeNatural.height()
			- closeMargins.top()
			- closeMargins.bottom();
		const auto closeX = width
			- st::emojiScroll.width
			- st::ivEditorEmojiColumnCloseScrollSkip
			- closeNatural.width()
			+ closeMargins.right();
		const auto closeY = searchCenterY
			- closeVisibleHeight / 2
			- closeMargins.top();
		_emojiColumnClose->moveToLeft(closeX, closeY, width);
		_emojiColumnClose->show();
		_emojiColumnClose->raise();
	} else {
		_emojiColumn->hide();
		_emojiColumnShadow->hide();
		_emojiColumnClose->hide();
	}
	_editor->setTopContentPadding(toolbarHeight);
	_editor->setBottomContentPadding(bottomHeight);
	_editor->resizeToWidth(std::max(_scroll->width(), 1));
	updateEditorVisibleTopBottom();
}

void WindowHost::Impl::updateBottomMask() {
	if (!_bottom) {
		return;
	}
	auto bottomMask = QRegion();
	const auto addMask = [&](Ui::RpWidget *widget) {
		if (widget && !widget->isHidden()) {
			bottomMask += widget->geometry();
		}
	};
	addMask(_discard.data());
	addMask(_cancel.data());
	addMask(_aiPill.data());
	addMask(_send.data());
	addMask(_sendLock);
	if (bottomMask.isEmpty()) {
		_bottom->clearMask();
	} else {
		_bottom->setMask(bottomMask);
	}
}

void WindowHost::Impl::toggleEmojiColumn() {
	if (_emojiColumnShown) {
		hideEmojiColumn();
	} else {
		showEmojiColumn();
	}
}

void WindowHost::Impl::showEmojiColumn() {
	if (_emojiColumnShown || !_window || !_emojiColumn) {
		return;
	}
	const auto window = not_null<Window*>(_window.get());
	const auto wanted = emojiColumnWidth();
	const auto minimal = std::max(
		minimalWindowWidthWithEmojiColumn() - window->width(),
		0);
	_emojiColumnExtendedBy = 0;
	if (!window->isMaximized() && !window->isFullScreen()) {
		if (CanExtendNoMove(window, wanted)) {
			_emojiColumnExtendedBy = TryToExtendWidthBy(window, wanted);
		} else if (minimal > 0 && CanExtendNoMove(window, minimal)) {
			_emojiColumnExtendedBy = TryToExtendWidthBy(window, minimal);
		} else if (window->width() < minimalWindowWidthWithEmojiColumn()) {
			_emojiColumnExtendedBy = TryToExtendWidthBy(window, minimal);
		}
	}
	window->setMinimumWidth(minimalWindowWidthWithEmojiColumn());
	_emojiColumnShown = true;
	if (_toolbar) {
		_toolbar->setEmojiColumnOpen(true);
	}
	_emojiColumn->setRoundRadius(0);
	setEmojiColumnInteractionActive(true);
	_emojiColumn->showStarted();
	_emojiColumn->show();
	layout();
	_emojiColumn->afterShown();
}

void WindowHost::Impl::hideEmojiColumn(bool skipResize) {
	if (!_emojiColumnShown || !_window || !_emojiColumn) {
		return;
	}
	const auto window = not_null<Window*>(_window.get());
	_emojiColumn->beforeHiding();
	_emojiColumn->hide();
	_emojiColumn->hideFinished();
	_emojiColumnShown = false;
	if (_toolbar) {
		_toolbar->setEmojiColumnOpen(false);
	}
	window->setMinimumWidth(minimalWindowWidth());
	setEmojiColumnInteractionActive(false);
	if (!skipResize
		&& _emojiColumnExtendedBy > 0
		&& !window->isMaximized()
		&& !window->isFullScreen()) {
		window->resize(
			window->width() - _emojiColumnExtendedBy,
			window->height());
	}
	_emojiColumnExtendedBy = 0;
	layout();
}

void WindowHost::Impl::updateEditorVisibleTopBottom() {
	if (!_scroll || !_editor) {
		return;
	}
	const auto scrollTop = _scroll->scrollTop();
	_editor->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
}

void WindowHost::Impl::setEmojiColumnInteractionActive(bool active) {
	if (_emojiColumnInteractionActive == active || !_editor) {
		_emojiColumnInteractionActive = active;
		return;
	}
	_emojiColumnInteractionActive = active;
	_editor->setInlineFieldExternalInteractionActive(active);
}

int WindowHost::Impl::emojiColumnWidth() const {
	const auto &pan = st::defaultEmojiPan;
	const auto columns = 8;
	const auto innerWidth = columns * pan.desiredSize;
	return innerWidth
		+ pan.padding.left()
		+ pan.padding.right()
		- pan.margin.left()
		- pan.margin.right()
		+ st::emojiPanRadius
		+ st::emojiScroll.width;
}

int WindowHost::Impl::minimalWindowWidth() const {
	const auto base = st::ivEditorWindowMinSize.width();
	return _toolbar ? std::max(base, _toolbar->minimalWidth()) : base;
}

int WindowHost::Impl::minimalWindowWidthWithEmojiColumn() const {
	return minimalWindowWidth() + emojiColumnWidth();
}

bool WindowHost::Impl::handleCloseRequest() {
	if (_closingApproved) {
		return true;
	}
	if (confirmCancel()) {
		finishCloseFromAcceptedEvent();
		return true;
	}
	return false;
}

void WindowHost::Impl::finishCloseFromAcceptedEvent() {
	_closingApproved = true;
	if (_closedNotified) {
		return;
	}
	_closedNotified = true;
	hideEmojiColumn(true);
	if (const auto closed = base::take(_closed)) {
		crl::on_main(closed);
	}
}

void WindowHost::Impl::finishClose() {
	finishCloseFromAcceptedEvent();
	if (_window) {
		_window->close();
	}
}

bool WindowHost::Impl::articleChanged() {
	if (!_state) {
		return false;
	}
	if (_editor
		&& (_editor->commitInlineFieldForClose()
			== State::ApplyResult::Failed)) {
		return true;
	}
	return !RichPagesEqual(_initialPage, _state->richPage());
}

bool WindowHost::Impl::articleEmptyForDiscard() {
	if (!_state) {
		return true;
	} else if (_editor
		&& (_editor->commitInlineFieldForClose()
			== State::ApplyResult::Failed)) {
		return false;
	}
	return _state->articleEmpty();
}

bool WindowHost::Impl::showCloseConfirmation() {
	if (_closeConfirmation) {
		return true;
	} else if (!_show || !_show->valid()) {
		return false;
	}
	const auto window = QPointer<Window>(_window.get());
	const auto close = [=](Fn<void()> closeBox) {
		closeBox();
		if (!window) {
			return;
		} else if (!_cancelled || _cancelled()) {
			finishClose();
		}
	};
	_closeConfirmation = _show->show(Ui::MakeConfirmBox({
		.text = tr::lng_theme_editor_sure_close(),
		.confirmed = close,
		.confirmText = tr::lng_close(),
	}));
	return true;
}

bool WindowHost::Impl::showDiscardConfirmation() {
	if (_discardConfirmation) {
		return true;
	} else if (!_show || !_show->valid()) {
		return false;
	}
	const auto window = QPointer<Window>(_window.get());
	const auto discard = [=](Fn<void()> closeBox) {
		closeBox();
		if (!window) {
			return;
		} else if (!_discarded || _discarded()) {
			finishClose();
		}
	};
	_discardConfirmation = _show->show(Ui::MakeConfirmBox({
		.text = tr::lng_iv_editor_discard_draft_sure(tr::now),
		.confirmed = discard,
		.confirmText = tr::lng_record_lock_discard(),
		.confirmStyle = &st::attentionBoxButton,
	}));
	return true;
}

bool WindowHost::Impl::confirmCancel() {
	if (!articleChanged()) {
		if (_changedCancelled && !articleEmptyForDiscard()) {
			return _changedCancelled();
		}
		return !_cancelled || _cancelled();
	} else if (_changedCancelled) {
		return _changedCancelled();
	}
	return showCloseConfirmation()
		? false
		: (!_cancelled || _cancelled());
}

void WindowHost::Impl::discard() {
	if (!_discard) {
		return;
	} else if (articleEmptyForDiscard()) {
		if (!_discarded || _discarded()) {
			finishClose();
		}
	} else {
		[[maybe_unused]] const auto shown = showDiscardConfirmation();
	}
}

void WindowHost::Impl::submit() {
	if (!_editor) {
		return;
	}
	if (_editor->commitInlineField() == State::ApplyResult::Failed) {
		return;
	}
	if (!_confirmed || _confirmed()) {
		finishClose();
	}
}

WindowHost::WindowHost(ShowWindowDescriptor descriptor)
: _impl(std::make_unique<Impl>(std::move(descriptor))) {
}

WindowHost::~WindowHost() = default;

void WindowHost::close() {
	_impl->close();
}

void WindowHost::activateClose() {
	_impl->activateClose();
}

std::unique_ptr<WindowHost> ShowWindow(ShowWindowDescriptor descriptor) {
	if (!descriptor.state) {
		descriptor.state = std::make_shared<State>();
	}
	return std::unique_ptr<WindowHost>(new WindowHost(std::move(descriptor)));
}

void SetupToolbarButton(
		not_null<Ui::IconButton*> button,
		ToolbarButtonState state,
		anim::type animated) {
	SetupToolbarButtonState(button, state, animated);
}

} // namespace Iv::Editor
