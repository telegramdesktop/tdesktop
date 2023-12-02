var IV = {
	notify: function(message) {
		if (window.external && window.external.invoke) {
			window.external.invoke(JSON.stringify(message));
		}
	},
	frameClickHandler: function(e) {
		var target = e.target, href;
		do {
			if (target.tagName == 'SUMMARY') return;
			if (target.tagName == 'DETAILS') return;
			if (target.tagName == 'LABEL') return;
			if (target.tagName == 'AUDIO') return;
			if (target.tagName == 'A') break;
		} while (target = target.parentNode);
		if (target && target.hasAttribute('href')) {
			var base_loc = document.createElement('A');
			base_loc.href = window.currentUrl;
			if (base_loc.origin != target.origin ||
					base_loc.pathname != target.pathname ||
					base_loc.search != target.search) {
				IV.notify({ event: 'link_click', url: target.href });
			}
		}
		e.preventDefault();
	},
	frameKeyDown: function (e) {
		let keyW = (e.key === 'w')
			|| (e.code === 'KeyW')
			|| (e.keyCode === 87);
		let keyQ = (e.key === 'q')
			|| (e.code === 'KeyQ')
			|| (e.keyCode === 81);
		let keyM = (e.key === 'm')
		    || (e.code === 'KeyM')
            || (e.keyCode === 77);
		if ((e.metaKey || e.ctrlKey) && (keyW || keyQ || keyM)) {
			e.preventDefault();
			IV.notify({
				event: 'keydown',
				modifier: e.ctrlKey ? 'ctrl' : 'cmd',
				key: keyW ? 'w' : keyQ ? 'q' : 'm',
			});
		} else if (e.key === 'Escape' || e.keyCode === 27) {
			e.preventDefault();
			IV.notify({
				event: 'keydown',
				key: 'escape',
			});
		}
	},
	updateStyles: function (styles) {
		if (IV.styles !== styles) {
			console.log('Setting', styles);
			IV.styles = styles;
			document.getElementsByTagName('html')[0].style = styles;
		} else {
			console.log('Skipping', styles);
		}
	},
	slideshowSlide: function(el, next) {
		var dir = window.getComputedStyle(el, null).direction || 'ltr';
		var marginProp = dir == 'rtl' ? 'marginRight' : 'marginLeft';
		if (next) {
			var s = el.previousSibling.s;
			s.value = (+s.value + 1 == s.length) ? 0 : +s.value + 1;
			s.forEach(function(el){ el.checked && el.parentNode.scrollIntoView && el.parentNode.scrollIntoView({behavior: 'smooth', block: 'center', inline: 'center'}); });
			el.firstChild.style[marginProp] = (-100 * s.value) + '%';
		} else {
			el.form.nextSibling.firstChild.style[marginProp] = (-100 * el.value) + '%';
		}
		return false;
	},
	initPreBlocks: function() {
		if (!hljs) return;
		var pres = document.getElementsByTagName('pre');
		for (var i = 0; i < pres.length; i++) {
			if (pres[i].hasAttribute('data-language')) {
				hljs.highlightBlock(pres[i]);
			}
		}
	},
	initEmbedBlocks: function() {
		var iframes = document.getElementsByTagName('iframe');
		for (var i = 0; i < iframes.length; i++) {
			(function(iframe) {
				window.addEventListener('message', function(event) {
					if (event.source !== iframe.contentWindow ||
							event.origin != window.origin) {
						return;
					}
					try {
						var data = JSON.parse(event.data);
					} catch(e) {
						var data = {};
					}
					if (data.eventType == 'resize_frame') {
						if (data.eventData.height) {
							iframe.style.height = data.eventData.height + 'px';
						}
					}
				}, false);
			})(iframes[i]);
		}
	}
};

document.onclick = IV.frameClickHandler;
document.onkeydown = IV.frameKeyDown;
window.onmessage = IV.postMessageHandler;
