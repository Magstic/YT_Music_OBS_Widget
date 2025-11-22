// Modified by Magstic, based on original work by Topik (GPLv3)

class Connector {
	player;
	#_settings;
	authCode;
	authToken;

	constructor(player) {
		this.player = player;
		this.#_settings = new GDMPSettings();
		this.authToken = localStorage.getItem('authToken');
		if (this.authToken === null) {
			this.authenticate();
		} else {
			this.connectToSocketIo();
		}
	}


	requestToken() {
		let that = this;
		$.ajax({
			url: YTMDesktopUrl + "/api/v1/auth/request",
			type: "POST",
			data: JSON.stringify({
				"appId": "ytmd-obs-widget",
				"code": that.authCode
			}),
			contentType: "application/json; charset=utf-8",
			dataType: "json",
			success: function (data, textStatus, jqXHR) {
				if (data.token) {
					that.authToken = data.token;
					localStorage.setItem('authToken', that.authToken);
					that.connectToSocketIo();
				}
			},
			error: function (jqXHR, textStatus, errorThrown) {
				alert("Open YTMD, autorization windows should pop up. Click 'Allow' to authorize the widget.");
				that.authenticate();
			}
		});
	}

	authenticate() {
		let that = this;
		$.ajax({
			url: YTMDesktopUrl + "/api/v1/auth/requestcode",
			type: "POST",
			data: JSON.stringify({
				"appId": "ytmd-obs-widget",
				"appName": "YT Music OBS Widget",
				"appVersion": "1.0.0"
			}),
			contentType: "application/json; charset=utf-8",
			dataType: "json",
			success: function (data, textStatus, jqXHR) {
				if (data.statusCode === 403) {
					alert("Open YTMD and enable Companion server in Settings > Integrations then refresh this page or OBS source.");
				} else if (data.code) {
					that.authCode = data.code;
					that.requestToken();
				}
			},
			error: function (jqXHR, textStatus, errorThrown) {
				alert("Open YTMD, go to Settings > Integrations > Companion sever, then Enable Allow browser communication, then Enable companion authorization. You might want to refresh this page or OBS source after. If you are using old version of YTMD, update it to at least version 2.");
				that.authenticate();
			}
		});
	}

	connectToSocketIo() {
		let that = this;
		const serverUrl = YTMDesktopUrl + '/api/v1/realtime';
		let attemptCount = 0;
		const options = {
			transports: ['websocket'],
			auth: {
				token: this.authToken
			},
			reconnectionAttempts: 100,
			reconnectionDelayMax: 1000, // 毫秒
		};
		const socket = io(serverUrl, options);

		socket.on('state-update', (data) => {
			that.setSettings(data);
			that.player.updateSongInfo(that.export());
		});
		socket.on("disconnect", () => {
			this.connectToSocketIo();
		});

		socket.on('connect_error', (err) => {
			if (err.message.includes('websocket error')) {
				alert("Open YTMD, go to Settings > Integrations, Enable Companion server, then Enable Allow browser communication, then Enable companion authorization. You might want to refresh this page or OBS source after. If you are using old version of YTMD, update it to at least version 2.");
				console.log('WebSocket connection failed. The server might be down or unreachable.');
				return;
			}
			if (err.message.includes('Authentication')) {
				console.warn("Authentication error. Attempting to re-authenticate...");
				that.authenticate();
				return;
			}

			console.error('Connection error:', err);
			attemptCount++;
			if (attemptCount <= options.reconnectionAttempts) {
				console.log(`Attempt ${attemptCount} to reconnect...`);
				setTimeout(connectSocket, Math.min(attemptCount * 1000, options.reconnectionDelayMax)); // 遞增等待時間以退避重連
			} else {
				alert("Maximum reconnection attempts reached. Please restart the widget.");
			}
		});
	}

	export() {
		return JSON.parse(JSON.stringify(this.#_settings));
	}

	setSettings(data) {
		this.#_settings.playState = data.player.trackState === 1 || data.player.trackState === 2;
		this.#_settings.volume = data.player.volume;
		this.#_settings.track = data.video;
		this.#_settings.time = {total: data.video.durationSeconds * 1000, current: data.player.videoProgress * 1000};
	}
}
