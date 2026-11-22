void run() {
	const auto top = std::max({ skip, padding.top(),
		cover,
	});
	tabs.push_back(
		{ .id = code, .text = tr::marked(
			BotInfoLanguageName(code)) });
}
