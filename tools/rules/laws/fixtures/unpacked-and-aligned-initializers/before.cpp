auto source() {
	const auto top = std::max({
		skip,
		padding.top(),
		cover,
	});
	return chat->isUser()
		? VoiceSearchSource{ .sender = sender }
		: VoiceSearchSource{
			.sender = sender->asUser(),
			.chat = chat,
		};
}
