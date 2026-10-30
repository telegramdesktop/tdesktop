/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_caption_bar.h"

#include "api/api_stories.h"
#include "boxes/premium_preview_box.h"
#include "boxes/story_audio_box.h"
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
#include "editor/editor_audio_timeline.h"
#include "editor/photo_editor_common.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_checked_action.h"
#include "ui/controls/emoji_button.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_multiline_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_editor.h"
#include "styles/style_media_player.h"
#include "styles/style_media_view.h"

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

} // namespace

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
, _musicButton(base::make_unique_q<Ui::IconButton>(
	this,
	st::storyCaptionMusic))
, _period(options.period)
, _musicChoice(options.musicButton) {
	setupField();
	setupEmoji();
	setupPeriod();
	_musicButton->setClickedCallback([=] {
		chooseAudio();
	});
	_periodButton->setVisible(options.periodChoice);
	_musicButton->setVisible(_musicChoice);
}

CaptionBar::~CaptionBar() = default;

TextWithTags CaptionBar::caption() const {
	return _field->getTextWithAppliedMarkdown();
}

int CaptionBar::period() const {
	return _period;
}

StoryAudio CaptionBar::audio() const {
	auto result = _audio;
	if (_audioTimeline) {
		result.from = _audioTimeline->from();
		result.till = _audioTimeline->till();
	}
	return result;
}

void CaptionBar::chooseAudio() {
	ShowStoryAudioBox(_show, crl::guard(this, [=](StoryAudio audio) {
		if (!audio.empty() && audio.till <= audio.from) {
			audio.from = 0;
			audio.till = 0;
		}
		_audio = std::move(audio);
		refreshAudioTimeline();
		if (width() > 0) {
			resizeToWidth(width());
		}
		_audioChanges.fire({});
	}));
}

rpl::producer<> CaptionBar::audioChanges() const {
	return _audioChanges.events();
}

void CaptionBar::refreshAudioTimeline() {
	if (_audio.empty()) {
		_audioTimeline = nullptr;
		return;
	}
	auto track = std::make_shared<AudioTrack>(AudioTrack{
		.path = _audio.path,
		.content = _audio.content,
		.title = _audio.title,
		.performer = _audio.performer,
		.duration = _audio.duration,
		.from = _audio.from,
		.till = _audio.till,
		.volume = _audio.volume,
	});
	_audioTimeline = base::make_unique_q<AudioTimeline>(
		this,
		std::move(track));
	const auto timeline = _audioTimeline.get();
	timeline->show();
	timeline->trimChanges() | rpl::on_next([=] {
		_audioChanges.fire({});
	}, timeline->lifetime());
	timeline->removeRequests() | rpl::on_next([=] {
		crl::on_main(this, [=] {
			_audio = {};
			refreshAudioTimeline();
			if (width() > 0) {
				resizeToWidth(width());
			}
			_audioChanges.fire({});
		});
	}, timeline->lifetime());
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

	_emojiToggle->installEventFilter(panel);
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
		st::photoEditorCropRatioMenu);
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
			locked ? &st::storyCaptionMenuLock : nullptr,
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
	auto top = 0;
	if (_audioTimeline) {
		_audioTimeline->resizeToWidth(newWidth);
		_audioTimeline->moveToLeft(0, 0);
		top = _audioTimeline->height() + st::storyCaptionAudioSkip;
	}
	const auto skip = st::storyCaptionFieldSkip;
	const auto fieldLeft = st::storyCaptionEmojiLeft
		+ _emojiToggle->width()
		+ skip;
	const auto periodWidth = _periodButton->isHidden()
		? 0
		: _periodButton->width();
	const auto musicWidth = _musicButton->isHidden()
		? 0
		: _musicButton->width();
	const auto fieldWidth = std::max(
		newWidth - fieldLeft - periodWidth - musicWidth - skip,
		1);
	_field->resize(fieldWidth, _field->height());
	const auto minimal = int(st::storyCaptionHeightMin);
	const auto oneLine = st::storyCaptionField.style.font->height;
	const auto pad = std::max((minimal - oneLine) / 2, 0);
	const auto height = std::max(_field->height() + 2 * pad, minimal);

	_field->moveToLeft(fieldLeft, top + pad);
	const auto buttonsBottom = (minimal - _emojiToggle->height()) / 2;
	_emojiToggle->moveToLeft(
		st::storyCaptionEmojiLeft,
		top + height - buttonsBottom - _emojiToggle->height());
	_musicButton->moveToRight(
		0,
		top + height - buttonsBottom - _musicButton->height(),
		newWidth);
	_periodButton->moveToRight(
		musicWidth,
		top + height - buttonsBottom - _periodButton->height(),
		newWidth);
	return height;
}

void CaptionBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = std::min(width(), height()) / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(st::roundedBg);
	p.drawRoundedRect(rect(), radius, radius);
}

void CaptionBar::hideEvent(QHideEvent *e) {
	if (_emojiPanel) {
		_emojiPanel->hideFast();
	}
	_periodMenu = nullptr;
}

} // namespace Editor
