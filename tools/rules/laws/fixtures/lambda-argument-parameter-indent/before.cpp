template <typename F>
void consume(int, F) {
}
void run() {
	consume(
		1,
		[=](
				int first,
				int second) {
			return first + second;
		});
}
