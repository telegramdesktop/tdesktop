/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tests/test_export_formats.hpp"

#include <QtCore/QJsonArray>

#include <set>

namespace Export::Tests {

using namespace Export::Data;

namespace {

using Kind = RichBlock::Kind;

constexpr auto kSharedId = 4101;
constexpr auto kWriterId = 4102;
constexpr auto kUnsupportedId = 4103;
constexpr auto kRichDate = TimeId(1704164400);
constexpr auto kHeadingJson = R"({
	"type": "heading",
	"level": 3,
	"text": { "type": "plain", "text": "Fixture <Article>" }
})";

struct KindRow {
	Kind kind = Kind::Unknown;
	const char *name = nullptr;
	int32 messageId = 0;
	int index = 0;
	const char *json = nullptr;
	QString (*html)(const Outputs &outputs) = nullptr;
};

[[nodiscard]] MTPRichText WirePlain(const char *text) {
	return MTP_textPlain(MTP_string(text));
}

[[nodiscard]] MTPPageCaption WireCaption(
		const char *text,
		const char *credit) {
	return MTP_pageCaption(
		WirePlain(text),
		credit ? WirePlain(credit) : MTPRichText(MTP_textEmpty()));
}

[[nodiscard]] MTPPageBlock HeadingBlock() {
	return MTP_pageBlockHeader(WirePlain("Fixture <Article>"));
}

[[nodiscard]] MTPPageBlock OrderedListBlock() {
	using ItemFlag = MTPDpageListItemText::Flag;
	const auto nested = MTP_pageBlockList(MTP_vector<MTPPageListItem>({
		MTP_pageListItemText(MTP_flags(0), WirePlain("Nested beta")),
		MTP_pageListItemText(
			MTP_flags(ItemFlag::f_checkbox | ItemFlag::f_checked),
			MTP_textBold(WirePlain("Done"))),
	}));
	return MTP_pageBlockOrderedList(
		MTP_flags(MTPDpageBlockOrderedList::Flag::f_start),
		MTP_vector<MTPPageListOrderedItem>({
			MTP_pageListOrderedItemText(
				MTP_flags(0),
				MTPstring(),
				WirePlain("Alpha & <one>"),
				MTPint(),
				MTPstring()),
			MTP_pageListOrderedItemBlocks(
				MTP_flags(0),
				MTPstring(),
				MTP_vector<MTPPageBlock>(1, nested),
				MTPint(),
				MTPstring()),
		}),
		MTP_int(3),
		MTPstring());
}

[[nodiscard]] MTPPageBlock QuoteBlock() {
	return MTP_pageBlockBlockquoteBlocks(
		MTP_vector<MTPPageBlock>({
			MTP_pageBlockParagraph(WirePlain("Quote opens.")),
			MTP_pageBlockParagraph(
				MTP_textItalic(WirePlain("Quote closes."))),
		}),
		WirePlain("Cited Source"));
}

[[nodiscard]] MTPPageTableCell WireCell(
		MTPDpageTableCell::Flags flags,
		const char *text,
		int colspan,
		int rowspan) {
	using Flag = MTPDpageTableCell::Flag;
	return MTP_pageTableCell(
		MTP_flags(flags
			| Flag::f_text
			| (colspan ? Flag::f_colspan : Flag())
			| (rowspan ? Flag::f_rowspan : Flag())),
		WirePlain(text),
		colspan ? MTP_int(colspan) : MTPint(),
		rowspan ? MTP_int(rowspan) : MTPint());
}

[[nodiscard]] MTPPageBlock TableBlock() {
	using Flag = MTPDpageTableCell::Flag;
	using TableFlag = MTPDpageBlockTable::Flag;
	const auto row = [](QVector<MTPPageTableCell> cells) {
		return MTP_pageTableRow(
			MTP_vector<MTPPageTableCell>(std::move(cells)));
	};
	return MTP_pageBlockTable(
		MTP_flags(TableFlag::f_bordered | TableFlag::f_striped),
		WirePlain("Scores & Ranks"),
		MTP_vector<MTPPageTableRow>({
			row({
				WireCell(
					Flag::f_header | Flag::f_align_center | Flag::f_valign_middle,
					"Team <A>",
					2,
					0),
			}),
			row({
				WireCell(
					Flag::f_align_right | Flag::f_valign_bottom,
					"Red",
					0,
					2),
				WireCell(MTPDpageTableCell::Flags(), "7", 0, 0),
			}),
			row({ WireCell(Flag::f_align_center, "9", 0, 0) }),
		}));
}

[[nodiscard]] MTPPageBlock CodeBlock() {
	return MTP_pageBlockPreformatted(
		WirePlain("if (a < b && c) {\n\tsay(\"hi\");\n}"),
		MTP_string("cpp"));
}

[[nodiscard]] MTPPageBlock ButtonRowBlock() {
	using Flag = MTPDpageButton::Flag;
	return MTP_pageBlockButtonRow(
		MTP_flags(MTPDpageBlockButtonRow::Flag::f_align_center),
		MTP_vector<MTPPageButton>({
			MTP_pageButton(
				MTP_flags(0),
				WirePlain("Open site"),
				MTP_inlineButtonTypeUrl(
					MTP_string("https://example.com/r?a=1&b=2")),
				MTPRichButtonStyle()),
			MTP_pageButton(
				MTP_flags(Flag::f_style),
				WirePlain("Ping"),
				MTP_inlineButtonTypeCallback(
					MTP_flags(0),
					MTP_bytes(QByteArray("ping-7"))),
				MTP_richButtonStyle(
					MTP_flags(MTPDrichButtonStyle::Flag::f_bg_primary))),
		}));
}

[[nodiscard]] MTPRichMessage SharedRich() {
	return MTP_richMessage(
		MTP_flags(0),
		MTP_vector<MTPPageBlock>({
			HeadingBlock(),
			OrderedListBlock(),
			QuoteBlock(),
			TableBlock(),
			CodeBlock(),
			MTP_pageBlockPhoto(
				MTP_flags(0),
				MTP_long(4501),
				WireCaption("Harbor at dawn", "Photo: Fixture Desk"),
				MTPstring(),
				MTPlong()),
			MTP_pageBlockDocument(
				MTP_long(4502),
				WireCaption("Spec <draft>", nullptr)),
			ButtonRowBlock(),
			MTP_pageBlockUnsupported(),
		}),
		MTP_vector<MTPPhoto>(1, PhotoFixture(4501, 800, 600, 54321)),
		MTP_vector<MTPDocument>(
			1,
			DocumentFixture(4502, "notes.txt", "text/plain", 4096)));
}

[[nodiscard]] MTPMessage SharedRichMessage() {
	return TextMessage({
		.id = kSharedId,
		.date = kRichDate,
		.fromId = peerFromUser(UserId(kAliceId)),
		.peerId = peerFromChat(ChatId(kGroupId)),
		.text = "Rich fallback text.",
		.rich = SharedRich(),
	});
}

[[nodiscard]] MTPMessage UnsupportedRichMessage() {
	using Flag = MTPDrichMessage::Flag;
	return TextMessage({
		.id = kUnsupportedId,
		.date = kRichDate + 7200,
		.fromId = peerFromUser(UserId(kAliceId)),
		.peerId = peerFromChat(ChatId(kGroupId)),
		.text = "Unsupported rich fallback.",
		.media = MTP_messageMediaUnsupported(),
		.rich = MTP_richMessage(
			MTP_flags(Flag::f_rtl | Flag::f_part),
			MTP_vector<MTPPageBlock>(1, HeadingBlock()),
			MTP_vector<MTPPhoto>(),
			MTP_vector<MTPDocument>()),
	});
}

[[nodiscard]] RichText PlainText(const char *text) {
	auto result = RichText();
	result.type = RichText::Type::Plain;
	result.text = text;
	return result;
}

[[nodiscard]] RichCaption Caption(const char *text, const char *credit) {
	auto result = RichCaption();
	result.text = PlainText(text);
	if (credit) {
		result.credit = PlainText(credit);
	}
	return result;
}

[[nodiscard]] RichBlock KindBlock(Kind kind) {
	auto result = RichBlock();
	result.kind = kind;
	return result;
}

[[nodiscard]] RichBlock TextBlock(Kind kind, const char *text) {
	auto result = KindBlock(kind);
	result.text = PlainText(text);
	return result;
}

[[nodiscard]] RichBlock PhotoBlock(uint64 id) {
	auto result = KindBlock(Kind::Photo);
	result.photoId = id;
	return result;
}

[[nodiscard]] std::vector<RichBlock> WriterTextBlocks() {
	auto author = TextBlock(Kind::AuthorDate, "Fixture Author");
	author.date = 1704153600;
	auto anchor = KindBlock(Kind::Anchor);
	anchor.name = "part-two";
	auto result = std::vector<RichBlock>();
	result.push_back(TextBlock(Kind::Paragraph, "Writer paragraph."));
	result.push_back(TextBlock(Kind::Footer, "Footer note."));
	result.push_back(TextBlock(Kind::Thinking, "Thinking aloud."));
	result.push_back(std::move(author));
	result.push_back(KindBlock(Kind::Divider));
	result.push_back(std::move(anchor));
	return result;
}

[[nodiscard]] std::vector<RichBlock> WriterMediaBlocks() {
	auto video = KindBlock(Kind::Video);
	video.documentId = 4602;
	video.autoplay = true;
	video.spoiler = true;
	video.caption = Caption("Clip", nullptr);
	auto cover = KindBlock(Kind::Cover);
	cover.blocks.push_back(PhotoBlock(4603));
	auto embed = KindBlock(Kind::Embed);
	embed.optionalUrl = "https://example.com/embed?v=1";
	embed.html = "<iframe src=\"https://example.com/e\"></iframe>";
	embed.width = 640;
	embed.height = 360;
	embed.fullWidth = true;
	auto post = KindBlock(Kind::EmbedPost);
	post.url = "https://example.com/post/9";
	post.webpageId = 4609;
	post.author = "Post Author";
	post.date = 1704157200;
	post.blocks.push_back(TextBlock(Kind::Paragraph, "Embedded text."));
	post.caption = Caption("Post caption", nullptr);
	auto collage = KindBlock(Kind::Collage);
	collage.blocks.push_back(PhotoBlock(4604));
	collage.blocks.push_back(PhotoBlock(4605));
	collage.caption = Caption("Collage caption", "Credit A");
	auto slide = PhotoBlock(4607);
	slide.spoiler = true;
	slide.caption = Caption("Slide one", nullptr);
	auto slideshow = KindBlock(Kind::Slideshow);
	slideshow.blocks.push_back(std::move(slide));
	slideshow.caption = Caption("Slides", nullptr);
	auto result = std::vector<RichBlock>();
	result.push_back(std::move(video));
	result.push_back(std::move(cover));
	result.push_back(std::move(embed));
	result.push_back(std::move(post));
	result.push_back(std::move(collage));
	result.push_back(std::move(slideshow));
	return result;
}

[[nodiscard]] std::vector<RichBlock> WriterReferenceBlocks() {
	auto channel = KindBlock(Kind::Channel);
	channel.channel.title = "Fixture Channel";
	channel.channel.username = "fixturechan";
	channel.channel.accessHash = 99;
	channel.channel.id = 3001;
	channel.channel.source = RichChannel::Source::Channel;
	channel.channel.broadcast = true;
	auto audio = KindBlock(Kind::Audio);
	audio.documentId = 4606;
	auto math = KindBlock(Kind::Math);
	math.formula = "a^2 + b^2 = c^2";
	auto details = TextBlock(Kind::Details, "More details");
	details.open = true;
	details.blocks.push_back(TextBlock(Kind::Paragraph, "Hidden detail."));
	auto related = TextBlock(Kind::RelatedArticles, "Related reading");
	related.relatedArticles.push_back({
		.url = "https://example.com/a",
		.title = "Article A",
		.description = "About A",
		.author = "Writer",
		.publishedDate = 1704160800,
		.webpageId = 4610,
	});
	auto map = KindBlock(Kind::Map);
	map.mapPoint = {
		.accessHash = 77,
		.accuracyRadius = 30,
		.latitude = 55.75,
		.longitude = 37.625,
		.source = RichMapPoint::Source::GeoPoint,
	};
	map.zoom = 15;
	map.mapWidth = 600;
	map.mapHeight = 300;
	map.caption = Caption("Square", nullptr);
	auto input = KindBlock(Kind::InputMap);
	input.mapPoint = {
		.latitude = -33.5,
		.longitude = 151.25,
		.source = RichMapPoint::Source::InputGeoPoint,
	};
	input.zoom = 12;
	input.mapWidth = 320;
	input.mapHeight = 240;
	auto result = std::vector<RichBlock>();
	result.push_back(std::move(channel));
	result.push_back(std::move(audio));
	result.push_back(std::move(math));
	result.push_back(std::move(details));
	result.push_back(std::move(related));
	result.push_back(std::move(map));
	result.push_back(std::move(input));
	result.push_back(RichBlock());
	return result;
}

[[nodiscard]] Photo CollagePhoto() {
	auto result = Photo();
	result.id = 4604;
	result.image.width = 320;
	result.image.height = 240;
	result.image.file.size = 1111;
	result.image.file.relativePath = u"chats/chat_1/photos/collage.jpg"_q;
	return result;
}

[[nodiscard]] Document VideoDocument() {
	auto result = Document();
	result.id = 4602;
	result.mime = "video/mp4";
	result.width = 1920;
	result.height = 1080;
	result.duration = 12;
	result.isVideoFile = true;
	result.file.size = 777;
	result.file.relativePath = u"chats/chat_1/video_files/clip.mp4"_q;
	return result;
}

[[nodiscard]] Document AudioDocument() {
	auto result = Document();
	result.id = 4606;
	result.name = "song.mp3";
	result.mime = "audio/mpeg";
	result.songPerformer = "Fixture Band";
	result.songTitle = "Fixture Song";
	result.duration = 185;
	result.isAudioFile = true;
	result.file.size = 3000;
	result.file.skipReason = File::SkipReason::FileType;
	return result;
}

[[nodiscard]] Message WriterMessage() {
	auto result = AliceGroupMessage(kWriterId, kRichDate + 3600);
	auto &rich = result.richMessage.emplace();
	const auto photo = CollagePhoto();
	rich.photos.emplace(photo.id, photo);
	for (const auto &document : { VideoDocument(), AudioDocument() }) {
		rich.documents.emplace(document.id, document);
	}
	const auto families = {
		&WriterTextBlocks,
		&WriterMediaBlocks,
		&WriterReferenceBlocks,
	};
	for (const auto family : families) {
		for (auto &block : family()) {
			rich.blocks.push_back(std::move(block));
		}
	}
	return result;
}

[[nodiscard]] QString CheckListHtml(const Outputs &outputs) {
	return ExpectGroupHtml(outputs, kSharedId, {
		"<ol class=\"rich_block rich_list\" "
		"data-rich-kind=\"ordered-list\" start=\"3\">",
		"<li class=\"rich_list_item\" data-rich-content=\"text\">",
		"<span class=\"rich_list_content\">Alpha &amp; &lt;one&gt;</span>",
		"<li class=\"rich_list_item\" data-rich-content=\"blocks\">",
		"<div class=\"rich_list_content\">",
		"<ul class=\"rich_block rich_list\" data-rich-kind=\"list\">",
		"<span class=\"rich_list_content\">Nested beta</span>",
		"<li class=\"rich_list_item rich_task_item\" "
		"data-rich-content=\"text\">",
		"<span aria-checked=\"true\" class=\"rich_task_marker\" "
		"role=\"checkbox\">\xE2\x98\x91</span>",
		"<span class=\"rich_list_content\"><strong>Done</strong></span>",
		"</ul>",
		"</ol>",
	});
}

[[nodiscard]] QString CheckQuoteHtml(const Outputs &outputs) {
	return ExpectGroupHtml(outputs, kSharedId, {
		"<blockquote class=\"rich_block rich_quote\" "
		"data-rich-kind=\"quote-blocks\">",
		"<div class=\"rich_quote_body\">",
		"<p class=\"rich_block rich_paragraph\" "
		"data-rich-kind=\"paragraph\" dir=\"auto\">\nQuote opens.\n",
		"<p class=\"rich_block rich_paragraph\" "
		"data-rich-kind=\"paragraph\" dir=\"auto\">\n"
		"<em>Quote closes.</em>\n",
		"<cite class=\"rich_quote_caption\">\nCited Source\n",
		"</blockquote>",
	});
}

[[nodiscard]] QString CheckTableHtml(const Outputs &outputs) {
	return ExpectGroupHtml(outputs, kSharedId, {
		"<div class=\"rich_block rich_table_wrap\" "
		"data-rich-kind=\"table\" tabindex=\"0\">",
		"<table class=\"rich_table bordered striped\">",
		"<caption>\nScores &amp; Ranks\n",
		"<tbody>",
		"<tr>",
		"<th class=\"rich_align_center rich_valign_middle\" colspan=\"2\">\n"
		"Team &lt;A&gt;\n",
		"<tr>",
		"<td class=\"rich_align_right rich_valign_bottom\" rowspan=\"2\">\n"
		"Red\n",
		"<td class=\"rich_align_left rich_valign_top\">\n7\n",
		"<tr>",
		"<td class=\"rich_align_center rich_valign_top\">\n9\n",
		"</table>",
	});
}

[[nodiscard]] QString CheckCodeOutput(const Outputs &outputs) {
	const auto raw = QByteArray(
		R"json("text": "if (a < b && c) {\n\tsay(\"hi\");\n}")json");
	return FirstFailure({
		ExpectNumber(
			u"raw escaped code in result.json"_q,
			OutputFile(outputs, "result.json").count(raw),
			1),
		ExpectGroupHtml(outputs, kSharedId, {
			"<pre class=\"rich_block rich_code\" data-language=\"cpp\" "
			"data-rich-kind=\"code\" dir=\"auto\">"
			"<code class=\"rich_code_content\">"
			"if (a &lt; b &amp;&amp; c) {<br>&#x09;say(&quot;hi&quot;);<br>}"
			"</code></pre>",
		}),
	});
}

[[nodiscard]] QString CheckPhotoHtml(const Outputs &outputs) {
	return ExpectGroupHtml(outputs, kSharedId, {
		"<figure class=\"rich_block rich_media_item\" data-photo-id=\"4501\" "
		"data-rich-kind=\"photo\" data-spoiler=\"false\">",
		"<a class=\"media clearfix pull_left block_link media_photo\" "
		"href=\"../../chats/chat_1/photos/photo_4@02-01-2024_06-00-00.jpg\">",
		"<div class=\"title bold\">\nPhoto\n",
		"<div class=\"status details\">\n800\xC3\x97" "600\n",
		"<figcaption class=\"rich_media_caption\" dir=\"auto\">",
		"<div class=\"rich_caption_text\">\nHarbor at dawn\n",
		"<cite class=\"rich_caption_credit\">\nPhoto: Fixture Desk\n",
		"</figure>",
	});
}

[[nodiscard]] QString CheckFileHtml(const Outputs &outputs) {
	return ExpectGroupHtml(outputs, kSharedId, {
		"<figure class=\"rich_block rich_media_item\" "
		"data-document-id=\"4502\" data-rich-kind=\"file\">",
		"<div class=\"media clearfix pull_left media_file\">",
		"<div class=\"title bold\">\nnotes.txt\n",
		"<div class=\"description\">\nExceeds maximum size, "
		"change data exporting settings to download.\n",
		"<div class=\"status details\">\n4.0 KB\n",
		"<div class=\"rich_caption_text\">\nSpec &lt;draft&gt;\n",
		"</figure>",
	});
}

[[nodiscard]] QString CheckButtonsHtml(const Outputs &outputs) {
	return ExpectGroupHtml(outputs, kSharedId, {
		"<div class=\"rich_block rich_button_row rich_button_align_center\" "
		"data-button-alignment=\"center\" data-rich-kind=\"button-row\">",
		"<div class=\"rich_button_group\">",
		"<a class=\"rich_button rich_button_block rich_button_style_default "
		"rich_button_action_url\" data-button-type=\"url\" "
		"data-button-url=\"https%3A%2F%2Fexample.com%2Fr%3Fa%3D1%26b%3D2\" "
		"href=\"https://example.com/r?a=1&amp;b=2\">Open site</a>",
		"<span class=\"rich_button rich_button_block "
		"rich_button_style_primary rich_button_action_callback\" "
		"data-button-requires-password=\"false\" "
		"data-button-style=\"primary\" data-button-type=\"callback\" "
		"data-callback-data-base64=\"cGluZy03\">Ping</span>",
	});
}

[[nodiscard]] QString CheckUnsupportedHtml(const Outputs &outputs) {
	return ExpectGroupHtml(outputs, kSharedId, {
		"<div class=\"rich_block rich_fallback\" "
		"data-rich-kind=\"unsupported\">\nUnsupported rich content\n",
	});
}

[[nodiscard]] QString CheckUnknownHtml(const Outputs &outputs) {
	return ExpectGroupHtml(outputs, kWriterId, {
		"<div class=\"rich_block rich_fallback\" "
		"data-rich-kind=\"unknown\">\nUnknown rich content\n",
	});
}

[[nodiscard]] std::vector<KindRow> TextKindRows() {
	return {
		{
			.kind = Kind::Unsupported,
			.name = "fallback.unsupported",
			.messageId = kSharedId,
			.index = 8,
			.json = R"({
				"type": "unsupported",
				"source_type": "page_block_unsupported",
				"unsupported": true
			})",
			.html = CheckUnsupportedHtml,
		},
		{
			.kind = Kind::Heading,
			.name = "kind.heading",
			.messageId = kSharedId,
			.index = 0,
			.json = kHeadingJson,
		},
		{
			.kind = Kind::Paragraph,
			.name = "kind.paragraph",
			.messageId = kWriterId,
			.index = 0,
			.json = R"({
				"type": "paragraph",
				"text": { "type": "plain", "text": "Writer paragraph." }
			})",
		},
		{
			.kind = Kind::Footer,
			.name = "kind.footer",
			.messageId = kWriterId,
			.index = 1,
			.json = R"({
				"type": "footer",
				"text": { "type": "plain", "text": "Footer note." }
			})",
		},
		{
			.kind = Kind::Thinking,
			.name = "kind.thinking",
			.messageId = kWriterId,
			.index = 2,
			.json = R"({
				"type": "thinking",
				"text": { "type": "plain", "text": "Thinking aloud." }
			})",
		},
		{
			.kind = Kind::AuthorDate,
			.name = "kind.author-date",
			.messageId = kWriterId,
			.index = 3,
			.json = R"({
				"type": "author_date",
				"author": { "type": "plain", "text": "Fixture Author" },
				"date": "2024-01-02T03:00:00",
				"date_unixtime": "1704153600"
			})",
		},
		{
			.kind = Kind::Code,
			.name = "nested.code",
			.messageId = kSharedId,
			.index = 4,
			.json = R"json({
				"type": "code",
				"text": {
					"type": "plain",
					"text": "if (a < b && c) {\n\tsay(\"hi\");\n}"
				},
				"language": "cpp"
			})json",
			.html = CheckCodeOutput,
		},
		{
			.kind = Kind::Divider,
			.name = "kind.divider",
			.messageId = kWriterId,
			.index = 4,
			.json = R"({ "type": "divider" })",
		},
		{
			.kind = Kind::Anchor,
			.name = "kind.anchor",
			.messageId = kWriterId,
			.index = 5,
			.json = R"({ "type": "anchor", "name": "part-two" })",
		},
	};
}

[[nodiscard]] std::vector<KindRow> ContainerKindRows() {
	return {
		{
			.kind = Kind::List,
			.name = "nested.list",
			.messageId = kSharedId,
			.index = 1,
			.json = R"({
				"type": "list",
				"kind": "ordered",
				"reversed": false,
				"start": 3,
				"items": [
					{
						"task_state": "none",
						"content": "text",
						"text": { "type": "plain", "text": "Alpha & <one>" }
					},
					{
						"task_state": "none",
						"content": "blocks",
						"blocks": [{
							"type": "list",
							"kind": "bullet",
							"items": [
								{
									"task_state": "none",
									"content": "text",
									"text": {
										"type": "plain",
										"text": "Nested beta"
									}
								},
								{
									"task_state": "checked",
									"content": "text",
									"text": {
										"type": "bold",
										"text": { "type": "plain", "text": "Done" }
									}
								}
							]
						}]
					}
				]
			})",
			.html = CheckListHtml,
		},
		{
			.kind = Kind::Quote,
			.name = "nested.quote",
			.messageId = kSharedId,
			.index = 2,
			.json = R"({
				"type": "quote",
				"content": "blocks",
				"pullquote": false,
				"blocks": [
					{
						"type": "paragraph",
						"text": { "type": "plain", "text": "Quote opens." }
					},
					{
						"type": "paragraph",
						"text": {
							"type": "italic",
							"text": { "type": "plain", "text": "Quote closes." }
						}
					}
				],
				"caption": { "type": "plain", "text": "Cited Source" }
			})",
			.html = CheckQuoteHtml,
		},
		{
			.kind = Kind::Photo,
			.name = "nested.photo",
			.messageId = kSharedId,
			.index = 5,
			.json = R"({
				"type": "photo",
				"photo_id": "4501",
				"photo": "chats/chat_1/photos/photo_4@02-01-2024_06-00-00.jpg",
				"photo_file_size": 54321,
				"width": 800,
				"height": 600,
				"spoiler": false,
				"caption": {
					"text": { "type": "plain", "text": "Harbor at dawn" },
					"credit": { "type": "plain", "text": "Photo: Fixture Desk" }
				}
			})",
			.html = CheckPhotoHtml,
		},
	};
}

[[nodiscard]] std::vector<KindRow> MediaKindRows() {
	return {
		{
			.kind = Kind::Video,
			.name = "kind.video",
			.messageId = kWriterId,
			.index = 6,
			.json = R"({
				"type": "video",
				"document_id": "4602",
				"file": "chats/chat_1/video_files/clip.mp4",
				"file_size": 777,
				"media_type": "video_file",
				"mime_type": "video/mp4",
				"duration_seconds": 12,
				"width": 1920,
				"height": 1080,
				"autoplay": true,
				"loop": false,
				"spoiler": true,
				"caption": {
					"text": { "type": "plain", "text": "Clip" },
					"credit": { "type": "empty" }
				}
			})",
		},
		{
			.kind = Kind::Cover,
			.name = "kind.cover",
			.messageId = kWriterId,
			.index = 7,
			.json = R"({
				"type": "cover",
				"block": {
					"type": "photo",
					"photo_id": "4603",
					"photo_skip_reason": "unavailable",
					"spoiler": false,
					"caption": {
						"text": { "type": "empty" },
						"credit": { "type": "empty" }
					}
				}
			})",
		},
		{
			.kind = Kind::Embed,
			.name = "kind.embed",
			.messageId = kWriterId,
			.index = 8,
			.json = R"({
				"type": "embed",
				"url": "https://example.com/embed?v=1",
				"html": "<iframe src=\"https://example.com/e\"></iframe>",
				"width": 640,
				"height": 360,
				"full_width": true,
				"allow_scrolling": false,
				"caption": {
					"text": { "type": "empty" },
					"credit": { "type": "empty" }
				}
			})",
		},
		{
			.kind = Kind::EmbedPost,
			.name = "kind.embed-post",
			.messageId = kWriterId,
			.index = 9,
			.json = R"({
				"type": "embed_post",
				"url": "https://example.com/post/9",
				"webpage_id": "4609",
				"author_photo_id": "0",
				"author_photo_skip_reason": "unavailable",
				"author": "Post Author",
				"date": "2024-01-02T04:00:00",
				"date_unixtime": "1704157200",
				"blocks": [{
					"type": "paragraph",
					"text": { "type": "plain", "text": "Embedded text." }
				}],
				"caption": {
					"text": { "type": "plain", "text": "Post caption" },
					"credit": { "type": "empty" }
				}
			})",
		},
	};
}

[[nodiscard]] std::vector<KindRow> GroupKindRows() {
	return {
		{
			.kind = Kind::Collage,
			.name = "kind.collage",
			.messageId = kWriterId,
			.index = 10,
			.json = R"({
				"type": "collage",
				"items": [
					{
						"type": "photo",
						"photo_id": "4604",
						"photo": "chats/chat_1/photos/collage.jpg",
						"photo_file_size": 1111,
						"width": 320,
						"height": 240,
						"spoiler": false,
						"caption": {
							"text": { "type": "empty" },
							"credit": { "type": "empty" }
						}
					},
					{
						"type": "photo",
						"photo_id": "4605",
						"photo_skip_reason": "unavailable",
						"spoiler": false,
						"caption": {
							"text": { "type": "empty" },
							"credit": { "type": "empty" }
						}
					}
				],
				"caption": {
					"text": { "type": "plain", "text": "Collage caption" },
					"credit": { "type": "plain", "text": "Credit A" }
				}
			})",
		},
		{
			.kind = Kind::Slideshow,
			.name = "kind.slideshow",
			.messageId = kWriterId,
			.index = 11,
			.json = R"({
				"type": "slideshow",
				"items": [{
					"type": "photo",
					"photo_id": "4607",
					"photo_skip_reason": "unavailable",
					"spoiler": true,
					"caption": {
						"text": { "type": "plain", "text": "Slide one" },
						"credit": { "type": "empty" }
					}
				}],
				"caption": {
					"text": { "type": "plain", "text": "Slides" },
					"credit": { "type": "empty" }
				}
			})",
		},
		{
			.kind = Kind::Channel,
			.name = "kind.channel",
			.messageId = kWriterId,
			.index = 12,
			.json = R"({
				"type": "channel",
				"channel": {
					"source_type": "channel",
					"id": "3001",
					"access_hash": "99",
					"title": "Fixture Channel",
					"username": "fixturechan",
					"broadcast": true,
					"megagroup": false,
					"monoforum": false
				}
			})",
		},
		{
			.kind = Kind::Audio,
			.name = "kind.audio",
			.messageId = kWriterId,
			.index = 13,
			.json = R"({
				"type": "audio",
				"document_id": "4606",
				"file_skip_reason": "file_type",
				"file_name": "song.mp3",
				"file_size": 3000,
				"media_type": "audio_file",
				"performer": "Fixture Band",
				"title": "Fixture Song",
				"mime_type": "audio/mpeg",
				"duration_seconds": 185,
				"caption": {
					"text": { "type": "empty" },
					"credit": { "type": "empty" }
				}
			})",
		},
		{
			.kind = Kind::File,
			.name = "nested.file",
			.messageId = kSharedId,
			.index = 6,
			.json = R"({
				"type": "file",
				"document_id": "4502",
				"file_skip_reason": "file_size",
				"file_name": "notes.txt",
				"file_size": 4096,
				"mime_type": "text/plain",
				"caption": {
					"text": { "type": "plain", "text": "Spec <draft>" },
					"credit": { "type": "empty" }
				}
			})",
			.html = CheckFileHtml,
		},
	};
}

[[nodiscard]] std::vector<KindRow> StructureKindRows() {
	return {
		{
			.kind = Kind::Math,
			.name = "kind.math",
			.messageId = kWriterId,
			.index = 14,
			.json = R"({ "type": "math", "formula": "a^2 + b^2 = c^2" })",
		},
		{
			.kind = Kind::Table,
			.name = "nested.table",
			.messageId = kSharedId,
			.index = 3,
			.json = R"({
				"type": "table",
				"title": { "type": "plain", "text": "Scores & Ranks" },
				"bordered": true,
				"striped": true,
				"compact": false,
				"rows": [
					{ "cells": [{
						"text": { "type": "plain", "text": "Team <A>" },
						"colspan": 2,
						"header": true,
						"align": "center",
						"vertical_align": "middle"
					}] },
					{ "cells": [
						{
							"text": { "type": "plain", "text": "Red" },
							"rowspan": 2,
							"header": false,
							"align": "right",
							"vertical_align": "bottom"
						},
						{
							"text": { "type": "plain", "text": "7" },
							"header": false,
							"align": "left",
							"vertical_align": "top"
						}
					] },
					{ "cells": [{
						"text": { "type": "plain", "text": "9" },
						"header": false,
						"align": "center",
						"vertical_align": "top"
					}] }
				]
			})",
			.html = CheckTableHtml,
		},
		{
			.kind = Kind::Details,
			.name = "kind.details",
			.messageId = kWriterId,
			.index = 15,
			.json = R"({
				"type": "details",
				"title": { "type": "plain", "text": "More details" },
				"open": true,
				"blocks": [{
					"type": "paragraph",
					"text": { "type": "plain", "text": "Hidden detail." }
				}]
			})",
		},
	};
}

[[nodiscard]] std::vector<KindRow> ReferenceKindRows() {
	return {
		{
			.kind = Kind::RelatedArticles,
			.name = "kind.related-articles",
			.messageId = kWriterId,
			.index = 16,
			.json = R"({
				"type": "related_articles",
				"title": { "type": "plain", "text": "Related reading" },
				"articles": [{
					"url": "https://example.com/a",
					"webpage_id": "4610",
					"title": "Article A",
					"description": "About A",
					"author": "Writer",
					"published_date": "2024-01-02T05:00:00",
					"published_date_unixtime": "1704160800"
				}]
			})",
		},
		{
			.kind = Kind::Map,
			.name = "kind.map",
			.messageId = kWriterId,
			.index = 17,
			.json = R"({
				"type": "map",
				"geo": {
					"source_type": "geo_point",
					"latitude": 55.75,
					"longitude": 37.625,
					"access_hash": "77",
					"accuracy_radius": 30
				},
				"zoom": 15,
				"width": 600,
				"height": 300,
				"caption": {
					"text": { "type": "plain", "text": "Square" },
					"credit": { "type": "empty" }
				}
			})",
		},
		{
			.kind = Kind::InputMap,
			.name = "kind.input-map",
			.messageId = kWriterId,
			.index = 18,
			.json = R"({
				"type": "input_map",
				"geo": {
					"source_type": "input_geo_point",
					"latitude": -33.5,
					"longitude": 151.25
				},
				"zoom": 12,
				"width": 320,
				"height": 240,
				"caption": {
					"text": { "type": "empty" },
					"credit": { "type": "empty" }
				}
			})",
		},
		{
			.kind = Kind::ButtonRow,
			.name = "nested.buttons",
			.messageId = kSharedId,
			.index = 7,
			.json = R"({
				"type": "button_row",
				"alignment": "center",
				"buttons": [
					{
						"text": { "type": "plain", "text": "Open site" },
						"button": {
							"type": "url",
							"url": "https://example.com/r?a=1&b=2"
						}
					},
					{
						"text": { "type": "plain", "text": "Ping" },
						"button": {
							"type": "callback",
							"requires_password": false,
							"dataBase64": "cGluZy03",
							"data": ""
						},
						"style": "primary"
					}
				]
			})",
			.html = CheckButtonsHtml,
		},
		{
			.kind = Kind::Unknown,
			.name = "fallback.unknown",
			.messageId = kWriterId,
			.index = 19,
			.json = R"({
				"type": "unsupported",
				"source_type": "unknown",
				"unsupported": true
			})",
			.html = CheckUnknownHtml,
		},
	};
}

[[nodiscard]] std::vector<KindRow> KindRows() {
	const auto families = {
		&TextKindRows,
		&ContainerKindRows,
		&MediaKindRows,
		&GroupKindRows,
		&StructureKindRows,
		&ReferenceKindRows,
	};
	auto result = std::vector<KindRow>();
	for (const auto family : families) {
		auto rows = family();
		result.insert(end(result), begin(rows), end(rows));
	}
	return result;
}

[[nodiscard]] int ParsedBlockCount(const Outputs &outputs, int32 id) {
	const auto parsed = ParsedMessage(outputs, kGroupDialog, id);
	return (parsed && parsed->richMessage)
		? int(parsed->richMessage->blocks.size())
		: 0;
}

[[nodiscard]] QString CheckKindRow(
		const Outputs &outputs,
		const KindRow &row) {
	const auto count = ParsedBlockCount(outputs, row.messageId);
	if (row.index >= count) {
		return u"message %1 has no parsed rich block %2"_q
			.arg(row.messageId)
			.arg(row.index);
	}
	const auto parsed = ParsedMessage(outputs, kGroupDialog, row.messageId);
	const auto &block = parsed->richMessage->blocks[row.index];
	const auto blocks = JsonMessage(outputs, kGroupDialog, row.messageId)
		.value(u"rich_message"_q).toObject()
		.value(u"blocks"_q).toArray();
	const auto expected = Json(row.json);
	return FirstFailure({
		ExpectNumber(u"parsed kind"_q, int(block.kind), int(row.kind)),
		expected.isEmpty() ? u"fixture literal does not parse"_q : QString(),
		ExpectNumber(u"JSON rich blocks"_q, blocks.size(), count),
		ExpectObject(blocks.at(row.index).toObject(), expected),
		row.html ? row.html(outputs) : QString(),
	});
}

[[nodiscard]] QString CheckRichInventory(const Outputs &outputs) {
	const auto rows = KindRows();
	auto order = QString();
	auto positions = std::set<std::pair<int32, int>>();
	auto shared = 0;
	auto writer = 0;
	for (auto i = 0; i != int(rows.size()); ++i) {
		const auto &row = rows[i];
		if (int(row.kind) != i && order.isEmpty()) {
			order = u"row %1 (%2) holds kind %3"_q
				.arg(i)
				.arg(QString::fromLatin1(row.name))
				.arg(int(row.kind));
		}
		positions.emplace(row.messageId, row.index);
		shared += (row.messageId == kSharedId) ? 1 : 0;
		writer += (row.messageId == kWriterId) ? 1 : 0;
	}
	return FirstFailure({
		order,
		ExpectNumber(u"kind rows"_q, int(rows.size()), 29),
		ExpectNumber(u"last declared kind"_q, int(Kind::Unknown), 28),
		ExpectNumber(
			u"distinct fixture blocks"_q,
			int(positions.size()),
			int(rows.size())),
		ExpectNumber(
			u"rows for 4101"_q,
			shared,
			ParsedBlockCount(outputs, kSharedId)),
		ExpectNumber(
			u"rows for 4102"_q,
			writer,
			ParsedBlockCount(outputs, kWriterId)),
	});
}

[[nodiscard]] QString ExpectRichEnvelope(
		const Outputs &outputs,
		int32 id,
		const char *fields,
		int blocks) {
	auto message = JsonMessage(outputs, kGroupDialog, id);
	auto rich = message.take(u"rich_message"_q).toObject();
	const auto list = rich.take(u"blocks"_q).toArray();
	return FirstFailure({
		ExpectObject(message, Json(fields)),
		ExpectObject(rich, Json(R"({ "rtl": false, "part": false })")),
		ExpectNumber(u"JSON %1 rich blocks"_q.arg(id), list.size(), blocks),
	});
}

[[nodiscard]] QString CheckSharedMessage(const Outputs &outputs) {
	return FirstFailure({
		ExpectRichEnvelope(
			outputs,
			kSharedId,
			R"({
				"id": 4101,
				"type": "message",
				"date": "2024-01-02T06:00:00",
				"date_unixtime": "1704164400",
				"from": "Alice Example",
				"from_id": "user1001"
			})",
			9),
		ExpectGroupHtml(outputs, kSharedId, {
			RecordStart(kSharedId),
			"<div class=\"pull_left userpic_wrap\">",
			kAliceFromName,
			"<div class=\"text rich_message\" data-rich-message=\"4101\" "
			"data-rich-part=\"false\" dir=\"auto\">",
			"<h3 class=\"rich_block rich_heading\" data-level=\"3\" "
			"data-rich-kind=\"heading\">\nFixture &lt;Article&gt;\n",
		}),
		ExpectAbsent(
			u"HTML 4101"_q,
			PageRecord(outputs, kGroupPage, kSharedId),
			{ "Rich fallback text.", "<div class=\"text\">" }),
		ExpectAbsent(
			u"result.json"_q,
			OutputFile(outputs, "result.json"),
			{ "Rich fallback text." }),
	});
}

[[nodiscard]] QString CheckWriterMessage(const Outputs &outputs) {
	return FirstFailure({
		ExpectRichEnvelope(
			outputs,
			kWriterId,
			R"({
				"id": 4102,
				"type": "message",
				"date": "2024-01-02T07:00:00",
				"date_unixtime": "1704168000",
				"from": "Alice Example",
				"from_id": "user1001"
			})",
			20),
		ExpectGroupHtml(outputs, kWriterId, {
			RecordStart(kWriterId),
			"<div class=\"pull_left userpic_wrap\">",
			kAliceFromName,
			"<div class=\"text rich_message\" data-rich-message=\"4102\" "
			"data-rich-part=\"false\" dir=\"auto\">",
			"<p class=\"rich_block rich_paragraph\" "
			"data-rich-kind=\"paragraph\" dir=\"auto\">\nWriter paragraph.\n",
		}),
	});
}

[[nodiscard]] QString CheckUnsupportedMediaMessage(const Outputs &outputs) {
	auto rich = Json(R"({ "rtl": true, "part": true })");
	rich.insert(u"blocks"_q, QJsonArray{ Json(kHeadingJson) });
	auto expected = Json(R"({
		"id": 4103,
		"type": "message",
		"date": "2024-01-02T08:00:00",
		"date_unixtime": "1704171600",
		"from": "Alice Example",
		"from_id": "user1001"
	})");
	expected.insert(u"rich_message"_q, rich);
	return FirstFailure({
		ExpectObject(
			JsonMessage(outputs, kGroupDialog, kUnsupportedId),
			expected),
		ExpectServiceHtml(outputs, kUnsupportedId, kUnsupportedNotice),
		ExpectAbsent(
			u"HTML 4103"_q,
			PageRecord(outputs, kGroupPage, kUnsupportedId),
			{
				"rich_message",
				"Fixture &lt;Article&gt;",
				"Unsupported rich fallback.",
			}),
		ExpectAbsent(
			u"result.json"_q,
			OutputFile(outputs, "result.json"),
			{ "Unsupported rich fallback." }),
	});
}

} // namespace

SliceFixture RichSlice() {
	return {
		.wire = { SharedRichMessage(), UnsupportedRichMessage() },
		.direct = { WriterMessage },
		.richSkips = { { 4502, File::SkipReason::FileSize } },
	};
}

std::vector<std::pair<QString, QString>> RichChecks(
		const Outputs &outputs) {
	auto result = std::vector<std::pair<QString, QString>>{
		{ u"rich.message.shared"_q, CheckSharedMessage(outputs) },
		{ u"rich.message.writer"_q, CheckWriterMessage(outputs) },
		{
			u"rich.message.unsupported-media"_q,
			CheckUnsupportedMediaMessage(outputs),
		},
		{ u"rich.inventory"_q, CheckRichInventory(outputs) },
	};
	for (const auto &row : KindRows()) {
		result.emplace_back(
			u"rich."_q + QString::fromLatin1(row.name),
			CheckKindRow(outputs, row));
	}
	return result;
}

} // namespace Export::Tests
