document.querySelectorAll('tg-slideshow').forEach(function (root) {
	var track = root.querySelector('.slideshow-track');
	if (!track || !track.children.length) {
		return;
	}
	var slides = track.children.length;
	var dots = Array.prototype.slice.call(
		root.querySelectorAll('.slideshow-dot'));
	var index = 0;
	var show = function (next) {
		index = ((next % slides) + slides) % slides;
		track.style.transform = 'translateX(' + (-index * 100) + '%)';
		dots.forEach(function (dot, i) {
			dot.classList.toggle('active', i === index);
		});
		Array.prototype.forEach.call(track.children, function (slide, i) {
			slide.querySelectorAll('video').forEach(function (video) {
				if (i === index) {
					if (video.autoplay && video.paused) {
						video.play().catch(function () {});
					}
				} else if (!video.paused) {
					video.pause();
				}
			});
		});
	};
	var prev = root.querySelector('.slideshow-prev');
	var next = root.querySelector('.slideshow-next');
	if (prev) {
		prev.addEventListener('click', function () { show(index - 1); });
	}
	if (next) {
		next.addEventListener('click', function () { show(index + 1); });
	}
	dots.forEach(function (dot, i) {
		dot.addEventListener('click', function () { show(i); });
	});
	show(0);
});
