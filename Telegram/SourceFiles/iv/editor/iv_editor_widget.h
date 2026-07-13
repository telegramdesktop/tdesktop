/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "base/flat_map.h"
#include "iv/editor/iv_editor_state.h"
#include "iv/markdown/iv_markdown_article.h"
#include "ui/style/style_core_types.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/dragging_scroll_manager.h"
#include "ui/rp_widget.h"
#include "rpl/lifetime.h"

#include <rpl/event_stream.h>

#include <QtCore/QPointer>
#include <QtGui/QImage>

#include <array>
#include <memory>
#include <optional>
#include <vector>

class Painter;
class QEvent;
class QContextMenuEvent;
class QInputMethodEvent;
class QKeyEvent;
class QMimeData;
class QMenu;
class QObject;
class QTouchEvent;
class QWheelEvent;

namespace Ui {
class ChatStyle;
class ChatTheme;
class InputField;
class PopupMenu;
class ElasticScroll;
struct PreparedList;
} // namespace Ui

namespace Ui::Emoji {
class SuggestionsController;
} // namespace Ui::Emoji

namespace style {
struct InputField;
struct Markdown;
} // namespace style

class PeerData;
class DocumentData;

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Iv {
class SearchController;
} // namespace Iv

namespace Iv::Editor {

class Widget;

const auto kEditorHeading1Sequence = QKeySequence("ctrl+t");
const auto kEditorHeading2Sequence = QKeySequence("ctrl+shift+h");
const auto kEditorTableSequence = QKeySequence("ctrl+shift+t");
const auto kEditorBodyTextSequence = QKeySequence("ctrl+shift+b");

struct PreparedMediaPasteTarget {
	std::optional<State::LeafPath> leaf;
	std::optional<State::InsertionAnchor> anchor;
	std::optional<State::ActiveTextInsertContext> context;
	std::optional<Markdown::PreparedEditBlockDropTarget> blockDrop;
};

struct WidgetServices {
	not_null<Main::Session*> session;
	std::shared_ptr<Main::SessionShow> show;
	not_null<QWidget*> outer;
	Fn<bool()> customEmojiPaused;
	Fn<void(
		not_null<Widget*>,
		QPointer<QWidget>,
		std::optional<State::ReplaceTarget>,
		RequestMediaType)> requestMedia;
	Fn<void(not_null<Widget*>, Ui::PreparedList, PreparedMediaPasteTarget)>
		applyPreparedMedia;
	Fn<void(uint64 /*photoId*/, Fn<void(QImage)>)> requestPhotoEditSource;
	Fn<void(not_null<Widget*>, Ui::PreparedList, State::ReplaceTarget)>
		replacePhotoWithList;
	Fn<MediaUploadState(uint64 /*mediaId*/)> mediaUploadState;
	Fn<void(not_null<Widget*>, uint64 /*mediaId*/)> cancelMediaUpload;
	Fn<void(not_null<Widget*>, State::BlockPath, QPointer<QWidget>)>
		addMediaAndGroupWithBlock;
	rpl::producer<> imeCompositionStarts;
};

class Widget final
	: public Ui::RpWidget
	, public Markdown::MediaBlockHost {
public:
	Widget(
		QWidget *parent,
		WidgetServices services,
		not_null<PeerData*> peer,
		std::shared_ptr<State> state,
		Fn<void(RichMessageLimitError)> showLimitToast = {});
	~Widget() override;

	void activateInitialNode();
	void activateSegment(int segmentIndex, int cursorOffset);
	[[nodiscard]] State::ApplyResult commitInlineField();
	[[nodiscard]] State::ApplyResult commitInlineFieldForClose();
	[[nodiscard]] bool closeSearch();
	void refreshPreparedContent();
	void refreshPreparedLeafAtActiveSource();
	void applyExternalRichPageMutation(Fn<bool(RichPage&)> mutation);
	void syncInlineFieldGeometry();
	void insertBlock(State::InsertAction action);
	void requestMedia(
		std::optional<State::ReplaceTarget> replaceTarget,
		RequestMediaType type = RequestMediaType::PhotoVideoAudio);
	void insertPreparedBlock(RichPage::Block block);
	void replacePreparedBlock(State::ReplaceTarget target, RichPage::Block block);
	void insertPreparedBlocks(std::vector<RichPage::Block> blocks);
	[[nodiscard]] bool hasActiveSelection() const;
	[[nodiscard]] std::shared_ptr<const RichPage>
		richPageForCurrentSelection() const;
	void replaceCurrentSelectionWithRichPage(
		std::shared_ptr<const RichPage> page);
	[[nodiscard]] TextWithEntities textSpanForCurrentSelection();
	void replaceCurrentSelectionWithText(TextWithEntities text);
	void pastePreparedBlock(
		RichPage::Block block,
		PreparedMediaPasteTarget target);
	void pastePreparedBlocks(
		std::vector<RichPage::Block> blocks,
		PreparedMediaPasteTarget target);
	void groupBlocksIntoGroup(State::BlockPath anchor, int insertedCount);
	void insertHeading1();
	void insertBlockquote();
	void insertEmoji(EmojiPtr emoji);
	void insertCustomEmoji(not_null<DocumentData*> document);
	enum class ToolbarFormatAction {
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
		Link,
		Math,
		Count,
	};
	enum class ToolbarLinkMode {
		Create,
		Edit,
	};
	struct ToolbarActionState {
		bool shown = false;
		bool enabled = false;
		bool active = false;
	};
	struct ToolbarState {
		[[nodiscard]] const ToolbarActionState &operator[](
				ToolbarFormatAction action) const {
			return actions[int(action)];
		}
		[[nodiscard]] ToolbarActionState &operator[](
				ToolbarFormatAction action) {
			return actions[int(action)];
		}

		ToolbarLinkMode linkMode = ToolbarLinkMode::Create;
		std::array<ToolbarActionState, int(ToolbarFormatAction::Count)>
			actions = {};
	};
	enum class AutosaveEventType {
		TextIdle,
		StructuralMutation,
	};
	struct AutosaveEvent {
		AutosaveEventType type = AutosaveEventType::TextIdle;
	};
	[[nodiscard]] ToolbarState toolbarStateValue() const;
	[[nodiscard]] rpl::producer<ToolbarState> toolbarStateChanges() const;
	[[nodiscard]] rpl::producer<AutosaveEvent> autosaveEvents() const;
	void performToolbarUndoRedo(bool redo);
	void applyToolbarFormatAction(ToolbarFormatAction action);
	void editLinkFromToolbar();
	void editMathFromToolbar();
	[[nodiscard]] bool inlineToolbarModeActive() const;
	struct ActiveBlockInfo {
		RichPage::BlockKind kind = RichPage::BlockKind::Unsupported;
		bool pullquote = false;
		int headingLevel = 0;
	};
	[[nodiscard]] std::optional<Markdown::PreparedEditBlockPath>
		selectedBlockPath() const;
	[[nodiscard]] ActiveBlockInfo activeBlockInfo() const;
	[[nodiscard]] std::optional<Markdown::PreparedEditListItemRange>
		currentListRangeAtCaret() const;
	[[nodiscard]] std::optional<Markdown::PreparedEditTableCellRange>
		currentTableRangeAtCaret() const;
	[[nodiscard]] std::optional<Markdown::PreparedEditListItemRange>
		currentListItemRangeAtCaret();
	[[nodiscard]] State::ListSelectionInfo listSelectionInfo(
		const Markdown::PreparedEditListItemRange &range) const;
	void fillListChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		const Markdown::PreparedEditListItemRange &range);
	void fillListItemChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		const Markdown::PreparedEditListItemRange &range);
	void fillTableChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		const Markdown::PreparedEditTableCellRange &range);
	void setInlineFieldExternalInteractionActive(bool active);
	void setTopContentPadding(int value);
	void setBottomContentPadding(int value);
	void setContentMaxWidth(int value);
	[[nodiscard]] rpl::producer<int> searchSlideHeightValue() const;

	struct ArticleColumn {
		int left = 0;
		int width = 0;
	};
	[[nodiscard]] ArticleColumn articleColumnForWidth(int outerWidth) const;

	int resizeGetHeight(int newWidth) override;

protected:
	bool eventFilter(QObject *object, QEvent *event) override;
	bool eventHook(QEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragMoveEvent(QDragMoveEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	bool focusNextPrevChild(bool next) override;
	void keyPressEvent(QKeyEvent *e) override;
	void inputMethodEvent(QInputMethodEvent *e) override;
	QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;
	void wheelEvent(QWheelEvent *e) override;
	void requestRepaint(QRect articleRect) override;
	void requestRelayout(QRect articleRect) override;

private:
	enum class MediaControl : uchar {
		None,
		ThreeDots,
		Plus,
		MediaPixels,
		UploadRadial,
		LayoutSwitch,
	};
	enum class MediaClickKind : uchar {
		Left,
		ContextMenu,
	};
	struct PressedMediaControl {
		MediaControl control = MediaControl::None;
		State::BlockPath path;
		int itemIndex = -1;

		[[nodiscard]] bool valid() const {
			return control != MediaControl::None;
		}
	};

	struct InlineFieldStyleData {
		const style::TextStyle *textStyle = nullptr;
		int lineHeight = 0;
		style::color textFg;
		QColor textMarkBg;
		style::align align = style::al_left;
		bool italic = false;
		bool quoteCaptionPlaceholder = false;
	};

	struct InlineFieldStyleKey {
		style::font font;
		int lineHeight = 0;
		style::color textFg;
		QColor textMarkBg;
		style::align align = style::al_left;
		bool quoteCaptionPlaceholder = false;

		friend inline bool operator==(
				const InlineFieldStyleKey &a,
				const InlineFieldStyleKey &b) {
			return (a.font == b.font)
				&& (a.lineHeight == b.lineHeight)
				&& (a.textFg == b.textFg)
				&& (a.textMarkBg == b.textMarkBg)
				&& (a.align == b.align)
				&& (a.quoteCaptionPlaceholder
					== b.quoteCaptionPlaceholder);
		}

		friend inline bool operator!=(
				const InlineFieldStyleKey &a,
				const InlineFieldStyleKey &b) {
			return !(a == b);
		}
	};

	struct CachedInlineFieldStyle {
		InlineFieldStyleKey key;
		std::shared_ptr<style::InputField> style;
		std::shared_ptr<style::owned_color> ownedTextFg;
		std::shared_ptr<style::owned_color> ownedTextMarkBg;
	};

	enum class DragSelectionMode {
		None,
		Text,
		Structural,
	};

	enum class ArticleSelectionOperation {
		None,
		GrowSelection,
		DragSelection,
	};

	struct ArticleSelectionDrag {
		bool active = false;
		bool fromField = false;
		bool startedBelow = false;
		bool codeHeader = false;
		bool dragStarted = false;
		QPoint pressPoint;
		QPoint globalPressPoint;
		Markdown::PreparedEditHit anchorHit;
		int textSegment = -1;
		int textOffset = 0;
		ArticleSelectionOperation operation
			= ArticleSelectionOperation::None;
		DragSelectionMode mode = DragSelectionMode::None;
		std::optional<Markdown::PreparedEditSelection> structuralSource;
		std::optional<State::TextNodeSpan> inlineSource;
		std::optional<Markdown::PreparedEditLeafSource> sourceLeaf;
		int sourceSegment = -1;
		int sourceFrom = 0;
		int sourceTo = 0;
		std::optional<Markdown::PreparedEditDropTarget> dropTarget;
		QRect indicatorRect;
	};

	struct ExternalMediaDrag {
		std::optional<Markdown::PreparedEditBlockDropTarget> dropTarget;
		QRect indicatorRect;
	};

	enum class HorizontalScrollDrag {
		None,
		Mouse,
		Touch,
	};

	struct HistoryLeafSelection {
		State::LeafPath leaf;
		int anchorOffset = 0;
		int cursorOffset = 0;

		friend inline bool operator==(
				const HistoryLeafSelection &a,
				const HistoryLeafSelection &b) {
			return (a.leaf == b.leaf)
				&& (a.anchorOffset == b.anchorOffset)
				&& (a.cursorOffset == b.cursorOffset);
		}
	};

	struct BoundarySelectionOrigin {
		HistoryLeafSelection leafSelection;
		Markdown::PreparedEditHit anchorHit;
		bool forward = false;

		friend inline bool operator==(
				const BoundarySelectionOrigin &a,
				const BoundarySelectionOrigin &b) {
			return (a.leafSelection == b.leafSelection)
				&& (a.anchorHit == b.anchorHit)
				&& (a.forward == b.forward);
		}
	};

	struct HistoryViewState {
		std::optional<HistoryLeafSelection> leafSelection;
		std::optional<Markdown::PreparedEditSelection> structuralSelection;
		std::optional<BoundarySelectionOrigin> boundarySelectionOrigin;

		friend inline bool operator==(
				const HistoryViewState &a,
				const HistoryViewState &b) {
			return (a.leafSelection == b.leafSelection)
				&& (a.structuralSelection == b.structuralSelection)
				&& (a.boundarySelectionOrigin
					== b.boundarySelectionOrigin);
		}
	};

	struct HistoryEntry {
		State::Snapshot snapshot;
		HistoryViewState viewState;
	};

	struct MutationTransactionResult {
		State::ApplyResult committed = State::ApplyResult::Unchanged;
		bool changed = false;
		bool failed = false;
	};

	struct RetainedLeafField {
		int historyIndex = -1;
		uint64 retainToken = 0;
		State::LeafPath leaf;
		State::FieldMode mode = State::FieldMode::Rich;
		std::optional<InlineFieldStyleKey> styleKey;
		base::unique_qptr<Ui::InputField> field;
		QPointer<Ui::Emoji::SuggestionsController> suggestions;
	};

	enum class ActivateReveal {
		Reveal,
		Skip,
	};

	void setDocument(const Markdown::MarkdownArticleContent &prepared);
	void activateTextOrdinal(
		int ordinal,
		int cursorOffset,
		ActivateReveal reveal = ActivateReveal::Reveal);
	void activateTextOrdinal(
		int ordinal,
		int selectionFrom,
		int selectionTo,
		ActivateReveal reveal = ActivateReveal::Reveal);
	[[nodiscard]] Markdown::MarkdownArticleTextLeafStyle
	inlineFieldStyleForSegment(int segmentIndex) const;
	[[nodiscard]] const CachedInlineFieldStyle &inlineFieldStyleFor(
		const Markdown::MarkdownArticleTextLeafStyle &style);
	[[nodiscard]] const CachedInlineFieldStyle &inlineFieldStyleFor(
		const InlineFieldStyleData &data);
	[[nodiscard]] InlineFieldStyleData normalizedInlineFieldStyle(
		const Markdown::MarkdownArticleTextLeafStyle &style) const;
	[[nodiscard]] InlineFieldStyleKey inlineFieldStyleKey(
		const InlineFieldStyleData &data) const;
	void refreshInlineFieldTextColorOverride();
	[[nodiscard]] std::optional<QColor> activeQuoteCaptionColor();
	[[nodiscard]] std::optional<QColor> activeQuotePlaceholderColor();
	void ensureInlineFieldForSegment(int segmentIndex);
	void setupInlineField();
	void ensureInlineFieldCreated();
	void recreateInlineField(const style::InputField &st);
	void refreshPalette();
	void refreshInlineFieldPlaceholder();
	void refreshInlineFieldPlaceholderColor();
	void refreshInlineFieldTextEmptyOverride();
	void refreshInlineFieldMaxLineWidthOverride();
	void activateTrailingParagraph();
	void setInlineFieldFromActiveState(int selectionFrom, int selectionTo);
	void revertInlineFieldToState();
	struct MathEditRequest {
		Ui::InputFieldTextRange range;
		QString source;
		int displayMathOrdinal = -1;
		bool editingExisting = false;
		bool allowSeparateLine = false;
		bool separateLine = false;
		bool insertNewDisplayBlock = false;
	};
	[[nodiscard]] std::optional<State::ActiveTextInsertContext>
	activeTextInsertContext() const;
	[[nodiscard]] bool hasFieldTextSpanSelection() const;
	[[nodiscard]] PreparedMediaPasteTarget preparedMediaPasteTarget() const;
	struct PreparedMediaPasteActivation {
		bool resolved = false;
		std::optional<State::ActiveTextInsertContext> context;
	};
	[[nodiscard]] PreparedMediaPasteActivation activatePreparedMediaPasteTarget(
		PreparedMediaPasteTarget target);
	void insertPreparedBlocks(
		std::vector<RichPage::Block> blocks,
		std::optional<State::ActiveTextInsertContext> context,
		bool useStructuralSelection = true);
	[[nodiscard]] std::optional<State::TextNodeSpan>
	visibleFullHeadingFieldTextSpan() const;
	[[nodiscard]] std::optional<MathEditRequest> activeMathEditRequest() const;
	[[nodiscard]] MathEditRequest newDisplayMathRequest() const;
	[[nodiscard]] int richOffsetForFieldOffset(
		const TextWithEntities &text,
		int offset) const;
	[[nodiscard]] int inlineFieldMaxVisualLineWidth() const;
	struct MathEditResult {
		QString source;
		bool separateLine = false;
	};
	[[nodiscard]] State::ApplyResult applyFieldTextToState();
	[[nodiscard]] State::ApplyResult applyMathEditResult(
		const MathEditRequest &request,
		MathEditResult result);
	bool showLastLimitToast();
	void hideInlineField();
	void acceptInlineField();
	void hideInlineFieldAndRefresh();
	void toggleSearch();
	void createSearchController();
	void scrollToSearchSegment(int segmentIndex);
	void updateSearchBarGeometry();
	[[nodiscard]] ArticleColumn searchBarColumn(int outerWidth) const;
	[[nodiscard]] int searchBarTop() const;
	[[nodiscard]] bool searchBlockedByLayer() const;
	void refreshPreparedLeafAtSource(
		const Markdown::PreparedEditLeafSource &source);
	void activateTextOrdinalAtEnd(int ordinal);
	[[nodiscard]] bool redirectKeyToField(QKeyEvent *e) const;
	[[nodiscard]] bool redirectImeToField() const;
	[[nodiscard]] bool prepareFieldForInput();
	[[nodiscard]] std::optional<int> removeCurrentStructuralSelection(
		bool forward);
	void removeStructuralSelectionAndReposition(bool forward);
	[[nodiscard]] bool replayKeyIntoField(QKeyEvent *e);
	[[nodiscard]] bool replayImeIntoField(QInputMethodEvent *e);
	[[nodiscard]] bool handleTabNavigation(QKeyEvent *e);
	[[nodiscard]] bool handleClipboardKey(QKeyEvent *e);
	[[nodiscard]] bool handleFieldBlockInsertShortcut(QKeyEvent *e);
	[[nodiscard]] bool handleStructuralBlockInsertShortcut(QKeyEvent *e);
	[[nodiscard]] bool handleHardcodedBlockShortcut(QKeyEvent *e);
	[[nodiscard]] bool fieldMonospaceShortcutUsesCodeBlock() const;
	[[nodiscard]] bool structuralMonospaceShortcutTargetsCodeBlock() const;
	void applyFieldMonospaceAction();
	void applyStructuralMonospaceAction();
	void insertCodeBlock();
	[[nodiscard]] bool handleFieldKey(QKeyEvent *e);
	struct VerticalNavigationTarget {
		int ordinal = -1;
		int offset = 0;
	};
	[[nodiscard]] bool commitAndActivateTextOrdinal(
		int ordinal,
		int selectionFrom,
		int selectionTo,
		ActivateReveal revealAfterRestore = ActivateReveal::Skip);
	void setActiveFieldCursorOffset(int offset);
	[[nodiscard]] std::optional<int> activeFieldPageCursorOffset(
		bool down) const;
	[[nodiscard]] std::optional<QPoint> activeFieldCursorArticlePoint() const;
	[[nodiscard]] bool fieldCursorLeavesVisibleRow(bool down) const;
	[[nodiscard]] int textEditableSegmentIndex(int ordinal) const;
	[[nodiscard]] std::optional<int> adjacentTextEditableOrdinal(
		bool down) const;
	[[nodiscard]] std::optional<int> textEditableOrdinalFromSegment(
		int segmentIndex,
		bool down) const;
	[[nodiscard]] std::optional<VerticalNavigationTarget> adjacentRowTarget(
		int ordinal,
		QPoint articlePoint,
		bool down);
	[[nodiscard]] std::optional<VerticalNavigationTarget> pageNavigationTarget(
		bool down);
	[[nodiscard]] std::optional<BoundarySelectionOrigin>
	currentBoundarySelectionOrigin(bool forward) const;
	[[nodiscard]] std::optional<Markdown::PreparedEditHit> boundaryHitFromTarget(
		const State::BoundaryTarget &target) const;
	[[nodiscard]] bool enterStructuralSelectionFromField(
		bool forward,
		bool page);
	[[nodiscard]] bool adjustStructuralSelectionFromKeyboard(
		bool forward,
		bool page);
	[[nodiscard]] bool restoreFieldFromBoundaryOrigin();
	void revealStructuralSelectionEdge(bool forward);
	[[nodiscard]] bool moveVerticalDownBoundary();
	void copyCurrentSelectionToClipboard();
	[[nodiscard]] TextForMimeData currentSelectionTextForClipboard() const;
	void pasteStructuredClipboardData(const ClipboardData &data);
	[[nodiscard]] bool handleIvClipboardMime(
		not_null<const QMimeData*> data,
		Ui::InputField::MimeAction action);
	[[nodiscard]] bool moveBoundary(bool forward, bool allowTrailing);

	// At the very first text node with no editable node above, inserts an
	// empty top-level paragraph before everything (so content can be added
	// above a non-trivial first block, like a table), unless the active node
	// already is a top-level paragraph or heading. Focuses the inserted
	// paragraph (Up) or keeps the currently edited node focused (Enter).
	[[nodiscard]] bool insertLeadingParagraphFromField(bool focusInserted);

	[[nodiscard]] bool moveBoundaryAfterCommit(
		State::ApplyResult committed,
		bool forward,
		bool allowTrailing,
		bool *mutated = nullptr);
	[[nodiscard]] bool moveTabBoundary(bool forward);
	[[nodiscard]] bool removeBoundaryOwner(bool forward);
	void ensurePendingActivation();
	void updateInlineFieldHeightOverride();
	void showMathEditBox(MathEditRequest request);
	void clearDisplayMathEditSession();
	void clearInlineFieldEditSession(
		bool keepRetainedFieldOnCurrentHistoryEntry = false);
	[[nodiscard]] HistoryViewState captureHistoryViewState() const;
	[[nodiscard]] HistoryEntry captureHistoryEntry() const;
	void restoreHistoryEntry(const HistoryEntry &entry);
	[[nodiscard]] static bool mutationTransactionChanged(bool changed);
	[[nodiscard]] static bool mutationTransactionChanged(
		State::ApplyResult result);
	[[nodiscard]] static bool mutationTransactionChanged(
		const MutationTransactionResult &result);
	void finishMutationTransaction(
		const HistoryEntry &before,
		bool changed,
		int beforeHistoryIndex,
		uint64 beforeRetainToken);
	template <typename Callback>
	auto recordMutationTransaction(Callback &&callback)
	-> decltype(callback()) {
		const auto before = captureHistoryEntry();
		const auto beforeHistoryIndex = _historyIndex;
		const auto beforeRetainToken = _retainedLeafFieldToken;
		auto result = callback();
		finishMutationTransaction(
			before,
			mutationTransactionChanged(result),
			beforeHistoryIndex,
			beforeRetainToken);
		return result;
	}
	void truncateHistoryRedo();
	[[nodiscard]] bool activeInlineFieldTextMatchesState() const;
	[[nodiscard]] bool canPerformFieldUndoRedo(bool redo) const;
	[[nodiscard]] bool canPerformHistoryUndoRedo(bool redo) const;
	[[nodiscard]] bool canPerformUndoRedo(bool redo) const;
	[[nodiscard]] bool handleUndoRedoShortcut(QKeyEvent *e);
	[[nodiscard]] bool handleUndoRedoShortcutOverride(QKeyEvent *e);
	[[nodiscard]] bool handleSelectAllShortcut(QKeyEvent *e);
	void selectWholeDocument();
	[[nodiscard]] bool performFieldUndoRedo(bool redo);
	void performUndoRedo(bool redo, bool allowFieldLocal = true);
	void notifyToolbarStateChanged();
	[[nodiscard]] ToolbarLinkMode toolbarLinkMode() const;
	[[nodiscard]] ToolbarActionState toolbarActionState(
		ToolbarFormatAction action) const;
	void clearFieldUndoRedoNoopState();
	[[nodiscard]] bool escapeActiveBlockBodyFromToolbar();
	[[nodiscard]] Fn<void()> captureScrollTopRestorer() const;
	template <typename Scroll>
	void scrollRangeToMakeVisible(Scroll *scroll, int top, int bottom) {
		const auto padding = effectiveBodyPadding();
		if (padding.top() + padding.bottom() < scroll->height()) {
			top -= padding.top();
			bottom += padding.bottom();
		}
		scroll->scrollToY(top, bottom);
	}
	void retainActiveLeafField(
		bool keepRetainedFieldOnCurrentHistoryEntry = false);
	[[nodiscard]] base::unique_qptr<Ui::InputField> reviveRetainedLeafField(
		int historyIndex,
		const State::LeafPath &leaf,
		State::FieldMode mode,
		const InlineFieldStyleKey &styleKey);
	void pruneRetainedLeafFields();
	void removeRetainedLeafFieldsAfter(int historyIndex);
	void moveRetainedLeafFields(
		int fromHistoryIndex,
		int toHistoryIndex,
		uint64 afterRetainToken);
	void beginArticleRelayoutDeferral();
	void endArticleRelayoutDeferral();
	[[nodiscard]] bool articleRelayoutDeferralActive() const;
	void requestDeferredArticleRelayout();
	void requestDeferredInlineFieldGeometry();
	void requestDeferredInlineFieldHeightOverride();
	void clearArticleEditableHeightOverride();
	void flushArticleRelayoutDeferral();
	void beginInlineFieldRevealSuppression();
	void endInlineFieldRevealSuppression();
	[[nodiscard]] bool inlineFieldRevealSuppressed() const;
	void resizeCurrentContentToWidth(int width);
	void relayoutCurrentContent();
	void refreshAfterInlineFieldCommit(State::ApplyResult committed);
	void refreshAfterInlineFieldCommit(
		State::ApplyResult committed,
		std::optional<Markdown::PreparedEditLeafSource> source);
	void ensureArticleLayoutForInlineField(int width);
	void syncArticleVisibleTopBottom();
	void syncInlineFieldGeometry(int width);
	[[nodiscard]] QRect activeInlineFieldRevealRect() const;
	[[nodiscard]] QRect mapFieldLocalRectToScrollContent(
		QWidget *inner,
		QRect rect) const;
	void revealActiveInlineField();
	void clearSelection();
	void clearTextSelection();
	void clearStructuralSelection();
	void setStructuralSelection(
		Markdown::PreparedEditSelection selection,
		std::optional<BoundarySelectionOrigin> origin = std::nullopt);
	[[nodiscard]] bool broaderSelectionHasSelectedText() const;
	[[nodiscard]] std::vector<State::TextNodeSpan>
	broaderSelectionTextSpans() const;
	[[nodiscard]] std::vector<State::BlockPath>
	broaderSelectionMediaBlocks() const;
	[[nodiscard]] bool hasStructuralSelection() const;
	void startArticleSelection(
		QPoint pressPoint,
		QPoint globalPressPoint,
		const Markdown::MarkdownArticleHitTestResult &hit,
		const Markdown::PreparedEditHit &editHit,
		bool fromField = false,
		bool startedBelow = false);
	[[nodiscard]] bool startSelectionDragFromExistingState(
		QPoint pressPoint,
		QPoint globalPressPoint,
		const Markdown::PreparedEditHit &editHit,
		bool fromField = false);
	void updateArticleSelection(
		QPoint articlePoint,
		const Markdown::MarkdownArticleHitTestResult &hit,
		const Markdown::PreparedEditHit &editHit);
	void updateArticleDropTarget(QPoint articlePoint);
	void clearArticleDropTarget();
	void updateExternalDropTarget(QPoint articlePoint);
	void clearExternalDropTarget();
	void pasteBlocksAtDropTarget(
		std::vector<RichPage::Block> blocks,
		const Markdown::PreparedEditBlockDropTarget &target);
	void finishArticleSelection();
	[[nodiscard]] Ui::ElasticScroll *selectionScrollArea() const;
	[[nodiscard]] bool articleSelectionAutoScrollActive() const;
	void updateArticleSelectionAutoScroll(QPoint widgetPoint);
	void updateArticleSelectionDragAtArticlePoint(
		QPoint articlePoint,
		const Markdown::MarkdownArticleHitTestResult &hit,
		const Markdown::PreparedEditHit &editHit);
	void updateArticleSelectionDragAtWidgetPoint(QPoint widgetPoint);
	void updateArticleSelectionDragFromCursor();
	[[nodiscard]] bool applyStructuralSelectionDrop();
	[[nodiscard]] bool applyInlineSelectionDrop();
	[[nodiscard]] bool handleStructuralSelectionKey(QKeyEvent *e);
	void addFieldBlockFormatActions(not_null<QMenu*> menu);
	void handleFieldContextMenuRequest(
		Ui::InputField::ContextMenuRequest request);
	[[nodiscard]] bool handleFieldMouseEvent(QEvent *event);
	[[nodiscard]] bool handleHorizontalScrollWheel(
		QWheelEvent *e,
		QPoint articlePoint);
	[[nodiscard]] std::optional<Markdown::PreparedEditTableCellSource>
	activeTableCellSourceAt(
		QObject *object,
		const QContextMenuEvent &e) const;
	[[nodiscard]] std::optional<Markdown::PreparedEditListItemRange>
	effectiveListRangeForSource(
		const std::optional<Markdown::PreparedEditListItemSource> &source,
		const std::optional<Markdown::PreparedEditBlockPath> &block);
	[[nodiscard]] std::optional<Markdown::PreparedEditListItemRange>
	fullListRangeForSource(
		const Markdown::PreparedEditListItemSource &source) const;
	[[nodiscard]] Markdown::PreparedEditTableCellRange
	effectiveTableRangeForCell(
		const Markdown::PreparedEditTableCellSource &source);
	void showListContextMenu(
		const Markdown::PreparedEditListItemRange &range,
		QPoint globalPos);
	void applyListChange(Fn<bool()> change);
	void showTableContextMenu(
		const Markdown::PreparedEditTableCellRange &range,
		QPoint globalPos);
	void applyTableChange(Fn<bool()> change);
	[[nodiscard]] std::optional<State::BlockPath> simpleMediaBlockPathFromHit(
		const Markdown::PreparedEditHit &hit) const;
	[[nodiscard]] std::optional<State::BlockPath> groupedMediaBlockPathFromHit(
		const Markdown::PreparedEditHit &hit) const;
	[[nodiscard]] bool structuralPhotoVideoSelectionAvailable() const;
	[[nodiscard]] bool clickHitsStructuralPhotoVideoSelection(
		const Markdown::PreparedEditHit &hit) const;
	void showSimpleMediaMenu(const State::BlockPath &path, QPoint globalPos);
	void showGroupedMediaMenu(
		const State::BlockPath &path,
		int itemIndex,
		QPoint globalPos);
	void showStructuralPhotoVideoMenu(QPoint globalPos);
	[[nodiscard]] bool showMediaMenuFromHit(
		const Markdown::PreparedEditHit &hit,
		const Markdown::MarkdownArticleHitTestResult &articleHit,
		QPoint globalPos,
		MediaClickKind clickKind);
	[[nodiscard]] bool activateGroupedMediaLinkFromHit(
		const Markdown::PreparedEditHit &hit,
		const Markdown::MarkdownArticleHitTestResult &articleHit,
		Qt::MouseButton button);
	[[nodiscard]] bool applyMediaBlockChange(Fn<bool()> change);
	[[nodiscard]] int groupedActiveIndexForPath(
		const State::BlockPath &path) const;
	void restoreGroupedActiveIndexForPath(
		const State::BlockPath &path,
		int activeIndex);
	bool applyGroupedMediaChangePreservingActiveIndex(
		const State::BlockPath &path,
		Fn<bool()> change);
	void requestReplaceMedia(State::BlockPath path);
	void editPhotoBlock(State::BlockPath path);
	void editGroupedItemPhoto(State::BlockPath path, int itemIndex);
	void openPhotoEditor(
		uint64 photoId,
		bool spoiler,
		State::ReplaceTarget target);
	void showPhotoEditor(
		QImage source,
		bool spoiler,
		State::ReplaceTarget target);
	void paintMediaControls(Painter &p, QPoint topLeft);
	struct MediaControlLayout {
		QRect threeDots;
		QRect plus;
		QRect radial;
		QRect layoutSwitch;
	};
	[[nodiscard]] MediaControlLayout mediaControlLayout(
		QRect mediaRect) const;
	[[nodiscard]] PressedMediaControl mediaControlHitTest(
		QPoint articlePoint) const;
	void cancelMediaUploadForBlock(const State::BlockPath &path);
	void cancelMediaUploadForGroupedItem(
		const State::BlockPath &path,
		int itemIndex);
	void addToCollageFromBlock(const State::BlockPath &path);
	void toggleGroupedMediaIntent(const State::BlockPath &path);
	[[nodiscard]] MediaUploadState mediaUploadStateForBlock(
		const State::BlockPath &path) const;
	[[nodiscard]] MediaUploadState mediaUploadStateForGroupedItem(
		const State::BlockPath &path,
		int itemIndex) const;
	[[nodiscard]] Markdown::PreparedEditSelection structuralSelectionFromHits(
		const Markdown::PreparedEditHit &anchor,
		const Markdown::PreparedEditHit &focus) const;
	[[nodiscard]] int editableOrdinalForSegment(int segmentIndex) const;
	[[nodiscard]] int segmentIndexForEditableOrdinal(int ordinal) const;
	[[nodiscard]] style::margins effectiveBodyPadding() const;
	[[nodiscard]] QPoint articleTopLeft() const;
	[[nodiscard]] int articleWidth(int outerWidth) const;
	void touchEvent(QTouchEvent *e);
	[[nodiscard]] QRect fieldOuterRectForSegment(int segmentIndex) const;
	[[nodiscard]] QRect outerEditableSegmentRect(int segmentIndex) const;
	[[nodiscard]] Markdown::MarkdownArticlePaintContext textPaintContext(
		QRect clip);

	const not_null<Main::Session*> _session;
	const std::shared_ptr<Main::SessionShow> _show;
	const not_null<QWidget*> _outer;
	const Fn<bool()> _customEmojiPaused;
	const Fn<void(
		not_null<Widget*>,
		QPointer<QWidget>,
		std::optional<State::ReplaceTarget>,
		RequestMediaType)> _requestMedia;
	const Fn<void(not_null<Widget*>, Ui::PreparedList, PreparedMediaPasteTarget)>
		_applyPreparedMedia;
	const Fn<void(uint64, Fn<void(QImage)>)> _requestPhotoEditSource;
	const Fn<void(not_null<Widget*>, Ui::PreparedList, State::ReplaceTarget)>
		_replacePhotoWithList;
	const Fn<MediaUploadState(uint64)> _mediaUploadState;
	const Fn<void(not_null<Widget*>, uint64)> _cancelMediaUpload;
	const Fn<void(not_null<Widget*>, State::BlockPath, QPointer<QWidget>)>
		_addMediaAndGroupWithBlock;
	const not_null<PeerData*> _peer;
	const std::shared_ptr<State> _state;
	const Fn<void(RichMessageLimitError)> _showLimitToast;
	std::shared_ptr<style::Markdown> _articleStyle;
	std::shared_ptr<Markdown::MarkdownArticle> _article;
	base::unique_qptr<Ui::InputField> _field;
	rpl::event_stream<AutosaveEvent> _autosaveEvents;
	rpl::event_stream<ToolbarState> _toolbarStateChanges;
	std::unique_ptr<Ui::ChatTheme> _theme;
	std::unique_ptr<Ui::ChatStyle> _style;
	std::vector<Ui::Text::SpecialColor> _highlightColors;
	rpl::lifetime _highlightReadyLifetime;
	std::vector<CachedInlineFieldStyle> _fieldStyles;
	std::optional<style::owned_color> _inlineFieldTextColorOverride;
	std::optional<style::owned_color> _inlineFieldPlaceholderColorOverride;
	std::optional<InlineFieldStyleKey> _activeFieldStyleKey;
	std::optional<State::LeafPath> _fieldLeaf;
	State::FieldMode _fieldMode = State::FieldMode::Rich;
	QPointer<Ui::Emoji::SuggestionsController> _fieldSuggestions;
	int _articleHeight = 0;
	int _topContentPadding = 0;
	int _bottomContentPadding = 0;
	int _contentMaxWidth = 0;
	int _activeOrdinal = -1;
	int _activeSegmentIndex = -1;
	bool _activeSegmentIsDisplayMath = false;
	int _activeDisplayMathBaselineHeight = 0;
	int _pendingOrdinal = -1;
	int _pendingCursorOffset = 0;
	std::vector<HistoryEntry> _history;
	int _historyIndex = -1;
	std::vector<RetainedLeafField> _retainedLeafFields;
	uint64 _retainedLeafFieldToken = 0;
	std::optional<int> _retainingFieldHistoryIndexOverride;
	std::optional<bool> _restoringHistoryRedo;
	bool _restoringHistory = false;
	bool _performingUndoRedo = false;
	bool _suppressHistoryRedoInvalidation = false;
	bool _revivedRetainedField = false;
	bool _fieldUndoAvailable = false;
	bool _fieldRedoAvailable = false;
	std::optional<TextWithTags> _fieldUndoNoopState;
	std::optional<TextWithTags> _fieldRedoNoopState;
	Markdown::MarkdownArticleSelection _selection;
	Markdown::MarkdownArticleSelectionEndpoints _selectionEndpoints;
	Markdown::PreparedEditSelection _structuralSelection;
	std::optional<BoundarySelectionOrigin> _boundarySelectionOrigin;
	Ui::VisibleRange _visibleRange;
	ArticleSelectionDrag _articleSelectionDrag;
	ExternalMediaDrag _externalMediaDrag;
	Ui::DraggingScrollManager _selectScroll;
	std::optional<Qt::Orientation> _horizontalScrollLock;
	bool _settingField = false;
	bool _trackingPointerPress = false;
	bool _inlineFieldExternalInteractionActive = false;
	bool _keyboardStructuralSelectionActive = false;
	Markdown::MarkdownArticleEditControlHit _pressedControl;
	std::optional<QPoint> _pressedControlPoint;
	PressedMediaControl _pressedMediaControl;
	std::optional<QPoint> _pressedMediaControlPoint;
	HorizontalScrollDrag _horizontalScrollDrag = HorizontalScrollDrag::None;
	std::optional<QPoint> _pendingTouchHorizontalScrollPoint;
	bool _syncingInlineFieldGeometry = false;
	bool _refreshingInlineFieldMaxLineWidthOverride = false;
	bool _pendingHeightOverrideUpdate = false;
	int _articleRelayoutDeferralDepth = 0;
	bool _articleRelayoutDeferred = false;
	bool _inlineFieldGeometryDeferred = false;
	bool _inlineFieldHeightOverrideDeferred = false;
	bool _articleEditableHeightOverrideClearDeferred = false;
	bool _searchRefreshDeferred = false;
	int _inlineFieldRevealSuppressionDepth = 0;
	std::unique_ptr<SearchController> _search;
	rpl::variable<int> _searchSlideHeight = 0;
};

} // namespace Iv::Editor
