/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_caption_bar.h"

#include "api/api_stories.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "editor/editor_round_record_button.h"
#include "editor/photo_editor_common.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_checked_action.h"
#include "ui/abstract_button.h"
#include "ui/controls/emoji_button.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_multiline_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_editor.h"
#include "styles/style_media_view.h"

#include <QtCore/QLocale>

namespace Editor {
namespace {

constexpr auto kStoryPeriods = std::array{
	6 * 3600,
	12 * 3600,
	Api::kStoryDefaultPeriod,
	2 * Api::kStoryDefaultPeriod,
};

constexpr auto kPeriodDashes = 5;
constexpr auto kPeriodDashWeight = 1.;
constexpr auto kPeriodGapWeight = 1.5;

constexpr auto kRecordDotPeriod = crl::time(600);

[[nodiscard]] QString RecordingTimeText(crl::time elapsed) {
	return Ui::FormatDurationText(elapsed / 1000)
		+ QLocale().decimalPoint()
		+ QString::number((elapsed % 1000) / 100);
}

} // namespace

class RecordCancelButton final : public Ui::AbstractButton {
public:
	explicit RecordCancelButton(QWidget *parent);

private:
	void paintEvent(QPaintEvent *e) override;

	const QString _text;

};

RecordCancelButton::RecordCancelButton(QWidget *parent)
: AbstractButton(parent)
, _text(tr::lng_cancel(tr::now)) {
	resize(
		st::storyRecordFont->width(_text) + 2 * st::storyRecordCancelPadding,
		st::storyCaptionHeightMin);
	setPointerCursor(true);
}

void RecordCancelButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setFont(st::storyRecordFont);
	p.setPen(st::storyRecordCancelFg);
	p.drawText(rect(), Qt::AlignCenter, _text);
}

class PeriodButton final : public Ui::RippleButton {
public:
	PeriodButton(QWidget *parent, int hours);

	void setHours(int hours);

private:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;
	[[nodiscard]] QPoint rippleOrigin() const;

	int _hours = 0;

};

PeriodButton::PeriodButton(QWidget *parent, int hours)
: RippleButton(parent, st::storiesComposeRippleLight)
, _hours(hours) {
	resize(st::storyCaptionPeriodSize, st::storyCaptionPeriodSize);
}

void PeriodButton::setHours(int hours) {
	_hours = hours;
	update();
}

QPoint PeriodButton::rippleOrigin() const {
	const auto ripple = st::storyCaptionPeriodRipple;
	return QPoint((width() - ripple) / 2, (height() - ripple) / 2);
}

void PeriodButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto origin = rippleOrigin();
	paintRipple(p, origin.x(), origin.y());

	auto hq = PainterHighQualityEnabler(p);
	const auto diameter = float64(st::storyCaptionPeriodDiameter);
	const auto rect = QRectF(
		(width() - diameter) / 2.,
		(height() - diameter) / 2.,
		diameter,
		diameter);
	auto pen = QPen(st::storyCaptionPeriodFg->c);
	pen.setWidthF(float64(st::storyCaptionPeriodStroke));
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);

	p.drawArc(rect, 90 * 16, 180 * 16);

	const auto total = kPeriodDashes * kPeriodDashWeight
		+ (kPeriodDashes + 1) * kPeriodGapWeight;
	const auto dash = kPeriodDashWeight / total * 180.;
	const auto gap = kPeriodGapWeight / total * 180.;
	auto angle = gap;
	for (auto i = 0; i != kPeriodDashes; ++i) {
		p.drawArc(
			rect,
			int(base::SafeRound((90. - angle - dash) * 16.)),
			int(base::SafeRound(dash * 16.)));
		angle += dash + gap;
	}

	p.setFont(st::storyCaptionPeriodFont);
	p.setPen(st::storyCaptionPeriodFg);
	p.drawText(rect, Qt::AlignCenter, QString::number(_hours));
}

QImage PeriodButton::prepareRippleMask() const {
	const auto ripple = st::storyCaptionPeriodRipple;
	return Ui::RippleAnimation::EllipseMask(QSize(ripple, ripple));
}

QPoint PeriodButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - rippleOrigin();
}

CaptionBar::CaptionBar(
	not_null<QWidget*> parent,
	not_null<Ui::RpWidget*> panelParent,
	const StoryOptions &options)
: RpWidget(parent)
, _show(options.show)
, _panelParent(panelParent)
, _field(base::make_unique_q<Ui::InputField>(
	this,
	st::storyCaptionField,
	Ui::InputField::Mode::MultiLine,
	tr::lng_story_caption_ph(),
	options.caption))
, _emojiToggle(base::make_unique_q<Ui::EmojiButton>(
	this,
	st::storyCaptionEmoji))
, _periodButton(base::make_unique_q<PeriodButton>(
	this,
	options.period / 3600))
, _roundButton(base::make_unique_q<Ui::IconButton>(
	this,
	st::storyCaptionRound))
, _period(options.period)
, _periodChoice(options.periodChoice) {
	setupField();
	setupEmoji();
	setupPeriod();
	_periodButton->setVisible(_periodChoice);
	_roundButton->setVisible(options.roundVideo);
	_recordAnimation.init([=](crl::time now) {
		update();
		return true;
	});
}

CaptionBar::~CaptionBar() = default;

TextWithTags CaptionBar::caption() const {
	return _field->getTextWithAppliedMarkdown();
}

int CaptionBar::period() const {
	return _period;
}

rpl::producer<> CaptionBar::roundRequests() const {
	return _roundButton->clicks() | rpl::to_empty;
}

void CaptionBar::setRoundRecording(bool recording) {
	if (_recording == recording) {
		return;
	}
	_recording = recording;
	_field->setVisible(!recording);
	_emojiToggle->setVisible(!recording);
	_periodButton->setVisible(!recording && _periodChoice);
	_roundButton->setVisible(!recording);
	if (recording) {
		_emojiPanel->hideAnimated();
		if (!_recordCancel) {
			_recordCancel = base::make_unique_q<RecordCancelButton>(this);
			_recordCancel->setClickedCallback([=] {
				_roundCancels.fire({});
			});
		}
		if (!_recordButton) {
			_recordButton = base::make_unique_q<RoundRecordButton>(
				_panelParent);
			_recordButton->setClickedCallback([=] {
				_roundStops.fire({});
			});
		}
		_recordStarted = crl::now();
		_recordCancel->show();
		_recordButton->showAnimated();
		_recordAnimation.start();
	} else {
		_recordAnimation.stop();
		_recordCancel->hide();
		_recordButton->hideAnimated();
	}
	resizeToWidth(width());
	updateRecordButtonGeometry();
	update();
}

void CaptionBar::setRoundRecordingAmplitude(float64 amplitude) {
	if (_recordButton) {
		_recordButton->setAmplitude(amplitude);
	}
}

void CaptionBar::updateRecordButtonGeometry() {
	if (!_recordButton || !_recording) {
		return;
	}
	const auto center = mapTo(
		_panelParent.get(),
		_roundButton->geometry().center());
	_recordButton->move(center - _recordButton->center());
}

rpl::producer<> CaptionBar::roundStopRequests() const {
	return _roundStops.events();
}

rpl::producer<> CaptionBar::roundCancelRequests() const {
	return _roundCancels.events();
}

rpl::producer<> CaptionBar::submits() const {
	return _submits.events();
}

rpl::producer<> CaptionBar::cancels() const {
	return _cancels.events();
}

void CaptionBar::setInnerFocus() {
	_field->setFocus();
}

void CaptionBar::setupField() {
	const auto session = &_show->session();
	const auto field = _field.get();
	field->setMaxLength(
		Data::PremiumLimits(session).storyCaptionLengthCurrent());
	field->setSubmitSettings(Core::App().settings().sendSubmitWay());

	const auto allow = [](not_null<DocumentData*>) { return true; };
	InitMessageFieldHandlers({
		.session = session,
		.show = _show,
		.field = field,
		.customEmojiPaused = [=] {
			return _show->paused(ChatHelpers::PauseReason::Layer);
		},
		.allowPremiumEmoji = allow,
		.fieldStyle = &st::storyCaptionField,
	});
	Ui::Emoji::SuggestionsController::Init(
		_panelParent,
		field,
		session,
		Ui::Emoji::SuggestionsController::Options{
			.suggestCustomEmoji = true,
			.allowCustomWithoutPremium = allow,
			.st = &st::storiesComposeControls.suggestions,
		});

	field->submits() | rpl::on_next([=] {
		_submits.fire({});
	}, field->lifetime());
	field->cancelled() | rpl::on_next([=] {
		_cancels.fire({});
	}, field->lifetime());
	field->heightChanges() | rpl::on_next([=] {
		resizeToWidth(width());
	}, field->lifetime());
}

void CaptionBar::setupEmoji() {
	using Selector = ChatHelpers::TabbedSelector;
	_emojiPanel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		_panelParent,
		ChatHelpers::TabbedPanelDescriptor{
			.ownedSelector = object_ptr<Selector>(
				nullptr,
				ChatHelpers::TabbedSelectorDescriptor{
					.show = _show,
					.st = st::storiesEmojiPan,
					.level = ChatHelpers::PauseReason::Layer,
					.mode = Selector::Mode::EmojiOnly,
					.features = {
						.megagroupSet = false,
						.stickersSettings = false,
						.openStickerSets = false,
					},
				}),
		});
	const auto panel = _emojiPanel.get();
	panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	panel->hide();
	panel->selector()->setCurrentPeer(_show->session().user());

	const auto field = _field.get();
	panel->selector()->emojiChosen(
	) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(field->textCursor(), data.emoji);
	}, field->lifetime());
	panel->selector()->customEmojiChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen data) {
		const auto info = data.document->sticker();
		const auto premiumOnly = info
			&& (info->setType == Data::StickersType::Emoji)
			&& !_show->session().premium();
		if (premiumOnly) {
			ShowPremiumPreviewBox(_show, PremiumFeature::AnimatedEmoji);
		} else {
			Data::InsertCustomEmoji(field, data.document);
		}
	}, field->lifetime());

	_emojiToggle->setClickedCallback([=] {
		updatePanelGeometry();
		panel->toggleAnimated();
		panel->raise();
	});
}

void CaptionBar::updatePanelGeometry() {
	const auto global = mapToGlobal(QPoint(0, 0));
	const auto local = _panelParent->mapFromGlobal(global);
	_emojiPanel->setDropDown(false);
	_emojiPanel->moveBottomRight(
		local.y(),
		local.x() + _emojiPanel->width());
}

void CaptionBar::setupPeriod() {
	_periodButton->setClickedCallback([=] {
		showPeriodMenu();
	});
}

void CaptionBar::showPeriodMenu() {
	_periodMenu = base::make_unique_q<Ui::PopupMenu>(
		_periodButton.get(),
		st::storiesPopupMenu);
	_periodMenu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomRight);
	auto about = base::make_unique_q<Ui::Menu::MultilineAction>(
		_periodMenu->menu(),
		_periodMenu->menu()->st(),
		st::photoEditorCornersMenuAboutLabel,
		st::photoEditorCornersMenuAboutPosition,
		TextWithEntities{ tr::lng_story_period_about(tr::now) });
	_periodMenu->addAction(std::move(about));
	_periodMenu->addSeparator();

	const auto premium = _show->session().premium();
	for (const auto period : kStoryPeriods) {
		const auto locked = !premium && (period != Api::kStoryDefaultPeriod);
		Menu::AddCheckedAction(
			_periodMenu.get(),
			tr::lng_hours(tr::now, lt_count, period / 3600),
			[=] {
				if (locked) {
					showPremiumHint(period);
				} else {
					setPeriod(period);
				}
			},
			nullptr,
			(_period == period));
	}
	_periodMenu->popup(_periodButton->mapToGlobal(
		QPoint(_periodButton->width(), 0)));
}

void CaptionBar::showPremiumHint(int period) {
	const auto show = _show;
	_show->showToast(Ui::Toast::Config{
		.text = tr::lng_story_period_premium(
			tr::now,
			lt_count,
			period / 3600,
			lt_link,
			Ui::Text::Link(
				tr::lng_story_period_premium_link(tr::now),
				u"internal:premium"_q),
			Ui::Text::WithEntities),
		.filter = [=](const ClickHandlerPtr &, Qt::MouseButton) {
			ShowPremiumPreviewToBuy(show, PremiumFeature::Stories);
			return true;
		},
	});
}

void CaptionBar::setPeriod(int period) {
	_period = period;
	_periodButton->setHours(period / 3600);
}

int CaptionBar::resizeGetHeight(int newWidth) {
	const auto skip = st::storyCaptionFieldSkip;
	const auto fieldLeft = st::storyCaptionEmojiLeft
		+ _emojiToggle->width()
		+ skip;
	const auto periodWidth = _periodButton->isHidden()
		? 0
		: _periodButton->width();
	const auto roundWidth = _roundButton->isHidden()
		? 0
		: _roundButton->width();
	const auto fieldWidth = std::max(
		newWidth - fieldLeft - periodWidth - roundWidth - skip,
		1);
	_field->resize(fieldWidth, _field->height());
	const auto minimal = int(st::storyCaptionHeightMin);
	const auto oneLine = st::storyCaptionField.style.font->height;
	const auto pad = std::max((minimal - oneLine) / 2, 0);
	const auto singleLine
		= _field->height() <= st::storyCaptionField.heightMin;
	const auto height = _recording
		? minimal
		: singleLine
		? minimal
		: std::max(_field->height() + 2 * pad, minimal);

	_field->moveToLeft(fieldLeft, pad);
	if (_recordCancel) {
		_recordCancel->move(
			(newWidth - _recordCancel->width()) / 2,
			(height - _recordCancel->height()) / 2);
	}
	const auto buttonsBottom = (minimal - _emojiToggle->height()) / 2;
	_emojiToggle->moveToLeft(
		st::storyCaptionEmojiLeft,
		height - buttonsBottom - _emojiToggle->height());
	_roundButton->moveToRight(
		0,
		height - buttonsBottom - _roundButton->height(),
		newWidth);
	_periodButton->moveToRight(
		roundWidth,
		height - buttonsBottom - _periodButton->height(),
		newWidth);
	return height;
}

void CaptionBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto pill = QRect(0, 0, width(), height());
	const auto radius = std::min(pill.width(), pill.height()) / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(st::roundedBg);
	p.drawRoundedRect(pill, radius, radius);
	if (_recording) {
		paintRecording(p, pill);
	}
}

void CaptionBar::paintRecording(QPainter &p, QRect pill) {
	const auto elapsed = crl::now() - _recordStarted;
	const auto phase = std::fmod(elapsed / float64(kRecordDotPeriod), 2.);
	const auto alpha = (phase < 1.) ? (1. - phase) : (phase - 1.);
	const auto radius = float64(st::storyRecordDotRadius);
	p.setPen(Qt::NoPen);
	p.setBrush(st::storyRecordDotFg);
	p.setOpacity(alpha);
	p.drawEllipse(
		QPointF(pill.x() + st::storyRecordDotLeft, pill.center().y()),
		radius,
		radius);
	p.setOpacity(1.);

	p.setFont(st::storyRecordFont);
	p.setPen(st::storyRecordTimerFg);
	p.drawText(
		pill.translated(st::storyRecordTimerLeft, 0),
		style::al_left,
		RecordingTimeText(elapsed));
}

void CaptionBar::hideEvent(QHideEvent *e) {
	if (_emojiPanel) {
		_emojiPanel->hideFast();
	}
	_periodMenu = nullptr;
}

} // namespace Editor
