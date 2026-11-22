auto source() {
	return chat->isUser()
		? VoiceSearchSource{ .sender = sender }
		: VoiceSearchSource{
			.sender = sender->asUser(),
			.chat = chat,
			};
}
