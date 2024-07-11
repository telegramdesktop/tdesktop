var LocationPicker = {
	startZoom: 14,
	flySpeed: 2.4,
	notify: function(message) {
		if (window.external && window.external.invoke) {
			window.external.invoke(JSON.stringify(message));
		}
	},
	frameKeyDown: function (e) {
		const keyW = (e.key === 'w')
			|| (e.code === 'KeyW')
			|| (e.keyCode === 87);
		const keyQ = (e.key === 'q')
			|| (e.code === 'KeyQ')
			|| (e.keyCode === 81);
		const keyM = (e.key === 'm')
			|| (e.code === 'KeyM')
			|| (e.keyCode === 77);
		if ((e.metaKey || e.ctrlKey) && (keyW || keyQ || keyM)) {
			e.preventDefault();
			LocationPicker.notify({
				event: 'keydown',
				modifier: e.ctrlKey ? 'ctrl' : 'cmd',
				key: keyW ? 'w' : keyQ ? 'q' : 'm',
			});
		} else if (e.key === 'Escape' || e.keyCode === 27) {
			e.preventDefault();
			LocationPicker.notify({
				event: 'keydown',
				key: 'escape',
			});
		}
	},
	isNight: function() {
		var html = document.getElementsByTagName('html')[0];
		return html.style.getPropertyValue('--td-night') == '1';
	},
	lightPreset: function() {
		return LocationPicker.isNight() ? 'night' : 'day';
	},
	updateStyles: function (styles) {
		if (LocationPicker.styles !== styles) {
			LocationPicker.styles = styles;
			document.getElementsByTagName('html')[0].style = styles;

			LocationPicker.map.setConfigProperty(
				'basemap',
				'lightPreset',
				LocationPicker.lightPreset());
		}
	},
	init: function (params) {
		mapboxgl.accessToken = params.token;
		if (params.protocol) {
			mapboxgl.config.API_URL = params.protocol + '://domain/api.mapbox.com';
		}

		var options = { container: 'map', config: {
			basemap: { lightPreset: LocationPicker.lightPreset() }
		} };
		var center = params.center;
		if (center) {
			center = [center[1], center[0]];
			options.center = center;
			options.zoom = LocationPicker.startZoom;
		} else if (params.bounds) {
			options.bounds = params.bounds;
			center = new mapboxgl.LngLatBounds(params.bounds).getCenter();
		} else {
			center = [0, 0];
		}
		LocationPicker.map = new mapboxgl.Map(options);
		LocationPicker.createMarker(center);
		LocationPicker.trackMovement();
		LocationPicker.initSearchVenueRipple();
	},
	marker: function() {
		return document.getElementById('marker_drop');
	},
	createMarker: function(center) {
		document.getElementById('marker').style.display = 'flex';
	},
	clearMovingTimer: function() {
		if (LocationPicker.clearMovingTimeoutId) {
			clearTimeout(LocationPicker.clearMovingTimeoutId);
			LocationPicker.clearMovingTimeoutId = 0;
		}
	},
	startMovingTimer: function(done) {
		LocationPicker.clearMovingTimer();
		LocationPicker.clearMovingTimeoutId = setTimeout(done, 500);
	},
	trackMovement: function() {
		LocationPicker.map.on('movestart', function() {
			LocationPicker.marker().classList.add('moving');
			LocationPicker.clearMovingTimer();
			LocationPicker.toggleSearchVenues(false);
			LocationPicker.notify({ event: 'move_start' });
		});
		LocationPicker.map.on('moveend', function() {
			LocationPicker.startMovingTimer(function() {
				LocationPicker.marker().classList.remove('moving');
				LocationPicker.notify({
					event: 'move_end',
					latitude: LocationPicker.map.getCenter().lat,
					longitude: LocationPicker.map.getCenter().lng
				});
			});
		});
	},
	narrowTo: function (point) {
		LocationPicker.map.flyTo({
			center: [point[1], point[0]],
			zoom: LocationPicker.startZoom,
			speed: LocationPicker.flySpeed,
		});
	},
	send: function () {
		LocationPicker.notify({
			event: 'send',
			latitude: LocationPicker.map.getCenter().lat,
			longitude: LocationPicker.map.getCenter().lng
		});
	},
	addRipple: function (button, x, y) {
		const ripple = document.createElement('span');
		ripple.classList.add('ripple');

		const inner = document.createElement('span');
		inner.classList.add('inner');

		var rect = button.getBoundingClientRect();
		x -= rect.x;
		y -= rect.y;

		const mx = button.clientWidth - x;
		const my = button.clientHeight - y;
		const sq1 = x * x + y * y;
		const sq2 = mx * mx + y * y;
		const sq3 = x * x + my * my;
		const sq4 = mx * mx + my * my;
		const radius = Math.sqrt(Math.max(sq1, sq2, sq3, sq4));

		inner.style.width = inner.style.height = `${2 * radius}px`;
		inner.style.left = `${x - radius}px`;
		inner.style.top = `${y - radius}px`;
		inner.classList.add('inner');

		ripple.addEventListener('animationend', function (e) {
			if (e.animationName === 'fadeOut') {
				ripple.remove();
			}
		});

		ripple.appendChild(inner);
		button.appendChild(ripple);
	},
	stopRipples: function (button) {
		const id = button.id ? button.id : button;
		button = document.getElementById(id);
		const ripples = button.getElementsByClassName('ripple');
		for (var i = 0; i < ripples.length; ++i) {
			const ripple = ripples[i];
			if (!ripple.classList.contains('hiding')) {
				ripple.classList.add('hiding');
			}
		}
	},
	initSearchVenueRipple: function() {
		var button = document.getElementById('search_venues_inner');
		button.addEventListener('mousedown', function (e) {
			LocationPicker.addRipple(e.currentTarget, e.clientX, e.clientY);
			LocationPicker.searchVenuesPressed = true;
		});
		button.addEventListener('mouseup', function (e) {
			const id = e.currentTarget.id;
			setTimeout(function () {
				LocationPicker.stopRipples(id);
			}, 0);
			if (LocationPicker.searchVenuesPressed) {
				LocationPicker.searchVenuesPressed = false;
				LocationPicker.toggleSearchVenues(false);
				LocationPicker.notify({
					event: 'search_venues',
					latitude: LocationPicker.map.getCenter().lat,
					longitude: LocationPicker.map.getCenter().lng
				});
			}
		});
		button.addEventListener('mouseleave', function (e) {
			LocationPicker.stopRipples(e.currentTarget);
			LocationPicker.searchVenuesPressed = false;
		});
	},
	toggleSearchVenues: function(shown) {
		var button = document.getElementById('search_venues');
		button.classList.toggle('shown', shown);
	},
};
