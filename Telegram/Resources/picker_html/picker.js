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
			LocationPicker.notify({ event: 'movestart' });
		});
		LocationPicker.map.on('moveend', function() {
			LocationPicker.startMovingTimer(function() {
				LocationPicker.marker().classList.remove('moving');
				LocationPicker.notify({
					event: 'moveend',
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
	}
};
