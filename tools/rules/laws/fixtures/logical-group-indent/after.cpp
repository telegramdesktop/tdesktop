bool enabled(bool show, bool photo, bool video, bool gif) {
	return show
		&& (photo
			|| video
			|| gif);
}
