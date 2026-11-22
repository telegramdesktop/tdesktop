void setup() {
	receive(
		panel->emojiChosen() | rpl::map_to(
			ShowRequest::HideAnimated),
		other());
}
