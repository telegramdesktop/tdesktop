void setup() {
	bind(
		[=] { notify(); },
		state);
}
