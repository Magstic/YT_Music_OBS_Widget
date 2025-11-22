// Modified by Magstic, based on original work by Topik (GPLv3)

class Player {
	lastTrackTime = null;

	constructor() {
		this.lastUpdateData = {};
		this.isPlaying = false;
		this._marqueeBound = false;
		this._waveformSocket = null;
		this._waveformReconnectTimer = null;
		this._waveformUrl = "ws://127.0.0.1:9450";
		this._coverCache = {};
		this._lastCoverKey = null;
		this._lastCoverUrl = null;
		this._coverLoadToken = 0;
		this._blurBase = 4;
		this._blurMax = 24;
		this._blurThresholdMs = 8000;
		this._blurHold = false;
		this._currentBlur = null;
		this._initWaveformSocket();
		this._initBlurDefaults();
	}

	updateSongInfo(playerInfo) {
		const prev = (this.lastUpdateData && this.lastUpdateData.track) ? this.lastUpdateData.track : null;
		const curr = (playerInfo && playerInfo.track) ? playerInfo.track : null;
		const prevTime = (this.lastUpdateData && this.lastUpdateData.time) ? this.lastUpdateData.time : null;
		const currTime = (playerInfo && playerInfo.time) ? playerInfo.time : null;
		let trackChanged = false;
		if (curr) {
			const prevId = prev ? (prev.videoId || prev.id || prev.entityId || prev.url || '') : '';
			const currId = curr.videoId || curr.id || curr.entityId || curr.url || '';
			const prevTitle = prev ? (prev.title || '') : '';
			const currTitle = curr.title || '';
			const normalizeAuthor = (t) => (t && (t.author || (t.artists ? t.artists.map(a => a.name).join(', ') : (t.channel || '')))) || '';
			const prevAuthor = normalizeAuthor(prev);
			const currAuthor = normalizeAuthor(curr);
			trackChanged = (!prev) || (prevId !== currId) || (prevTitle !== currTitle) || (prevAuthor !== currAuthor);
			if (!trackChanged && prevTime && currTime) {
				const durationChanged = (prevTime.total !== currTime.total);
				const timeRewound = (typeof prevTime.current === 'number' && typeof currTime.current === 'number') && (currTime.current + 2000 < prevTime.current);
				if (durationChanged || timeRewound) {
					trackChanged = true;
				}
			}
		}

		if (playerInfo.playState === true) {
			this.isPlaying = true;
			$("body").removeClass("isOffline");
			$("body").removeClass("paused");
		} else {
			this.isPlaying = false;
			$("body").addClass("isOffline");
			$("body").addClass("paused");
		}

		if (curr) {
			// 如果是第一次有 track（例如暫停狀態下刷新後的第一筆資料），或實際換曲，就完整刷新 track info
			if (trackChanged || !this.lastUpdateData || !this.lastUpdateData.track) {
				this.lastTrackTime = null;
				this.updateTrackInfo(playerInfo);
			} else {
				this.updateTrackTime(playerInfo);
			}
		}

		this.lastUpdateData = playerInfo;
	}

	periodicallyUpdateTrackTime(setTime) {
		let newTimeSet = false;
		if (this.lastTrackTime === null) {
			this.lastTrackTime = new Date(setTime);
		} else {
			let newTime = new Date(setTime);
			if (Math.abs(this.lastTrackTime - newTime) > 100) {
				this.lastTrackTime = new Date(setTime);
			}
		}

		if (this.lastTrackTime === null) {
			return;
		}
		let progressEl = $(".online .song-info__time progress");
		if(progressEl.length === 0) {
			return;
		}
		let total = parseInt(progressEl.attr("max"));
		let from = 14;
		let length = 5;
		if (total >= 3600000) {
			from = 11;
			length = 8;
		}
		let dateTotal = new Date(total);
		if (this.lastTrackTime > dateTotal) {
			return;
		}
		$(".online .song-info__time-current").text(this.lastTrackTime.toISOString().substr(from, length));
		$(".online .song-info__time-max").text(dateTotal.toISOString().substr(from, length));
	}

	updateTrackTime(playerInfo) {
		if (playerInfo.hasOwnProperty("time")) {
			this.ensureTimeRowStructure();
			let el = $(".online .song-info__time");
			el.find("progress").attr("max", playerInfo.time.total);
			el.find("progress").attr("value", playerInfo.time.current);
			this.periodicallyUpdateTrackTime(playerInfo.time.current);
		}
		this._updateBlurByTime(playerInfo);
	}

	updateTrackInfo(playerInfo) {
		let el = $(".online");
		if (playerInfo.hasOwnProperty("track")) {
			el.find(".song-info__title").html('<span class="song-info__title-inner"></span>');
			el.find(".song-info__title-inner").text(playerInfo.track.title || '');
			{
				const t = playerInfo.track;
				const author = t.author || (t.artists ? t.artists.map(a => a.name).join(', ') : (t.channel || '')) || '';
				el.find(".song-info__artist-name").html('<span class="song-info__artist-name-inner"></span>');
				el.find(".song-info__artist-name-inner").text(author);
			}
			{
				const t = playerInfo.track;
				const coverKey = this._getCoverKey(t);
				let coverUrl = coverKey && this._coverCache[coverKey] ? this._coverCache[coverKey] : null;
				if (!coverUrl) {
					if (t.hasOwnProperty("thumbnails") && t.thumbnails.length > 0) {
						coverUrl = this.getAlbumArt(t.thumbnails[0].url, 420, 420);
					} else if (t.cover) {
						coverUrl = this.getAlbumArt(t.cover, 420, 420);
					} else if (t.videoId) {
						coverUrl = 'https://i.ytimg.com/vi/' + t.videoId + '/hqdefault.jpg';
					}
					if (coverKey && coverUrl) {
						this._coverCache[coverKey] = coverUrl;
					}
				}
				if (coverUrl) {
					if (this._lastCoverKey !== coverKey || this._lastCoverUrl !== coverUrl) {
						this._blurHold = true;
						const token = ++this._coverLoadToken;
						const img = new Image();
						img.onload = () => {
							if (this._coverLoadToken !== token) return;
							const art = el.find(".song-info__album-art");
							const current = art.find(".song-info__album-art-image.is-visible");
							const next = $('<div class="song-info__album-art-image"></div>');
							next.css("background-image", "url('" + coverUrl + "')");
							art.append(next);
							const dom = next[0];
							if (dom) { void dom.offsetWidth; }
							next.addClass("is-visible");
							if (current.length) {
								current.removeClass("is-visible");
								setTimeout(() => { current.remove(); }, 260);
							}
							el.find(".song-info").css("background-image", "url('" + coverUrl + "')");
							this._lastCoverKey = coverKey;
							this._lastCoverUrl = coverUrl;
							this._blurHold = false;
							this._setBlur(this._blurBase);
						};
						img.onerror = () => {
							if (this._coverLoadToken !== token) return;
							this._blurHold = false;
							if (this._currentBlur !== this._blurBase) {
								this._setBlur(this._blurBase);
							}
						};
						img.src = coverUrl;
					} else {
						this._blurHold = false;
						if (this._currentBlur !== this._blurBase) {
							this._setBlur(this._blurBase);
						}
					}
				}
			}
			this.ensureTimeRowStructure();
			this.updateTrackTime(playerInfo);
			this.applyTitleMarquee();
			this.applyArtistMarquee();
		}
	}

	ensureTimeRowStructure() {
		const container = $(".online .song-info__time .song-info__time-container");
		if (!container.length) return;
		if (container.children('.song-info__time-left').length === 0) {
			const currentEl = container.find('.song-info__time-current');
			const maxEl = container.find('.song-info__time-max');
			const sepEl = container.find('.song-info__time-separator');
			const left = $('<div class="song-info__time-left"></div>');
			const mid = $('<div class="song-info__time-waveform"></div>');
			const right = $('<div class="song-info__time-right"></div>');
			if (currentEl.length) {
				left.append(currentEl);
			}
			if (maxEl.length) {
				right.append(maxEl);
			}
			sepEl.remove();
			container.empty().append(left, mid, right);
		}
		const midNow = container.children('.song-info__time-waveform');
		if (midNow.length && midNow.children().length === 0) {
			for (let i = 0; i < 12; i++) {
				midNow.append('<div class="bar"></div>');
			}
		}
	}

	_initWaveformSocket() {
		if (!("WebSocket" in window)) {
			return;
		}
		const connect = () => {
			try {
				const ws = new WebSocket(this._waveformUrl);
				this._waveformSocket = ws;
				ws.onopen = () => {
					$("body").addClass("has-external-waveform");
				};
				ws.onclose = () => {
					$("body").removeClass("has-external-waveform");
					this._scheduleWaveformReconnect();
				};
				ws.onerror = () => {
					try { ws.close(); } catch (e) {}
				};
				ws.onmessage = (event) => {
					let payload;
					try {
						payload = JSON.parse(event.data);
					} catch (e) {
						return;
					}
					if (!payload || !Array.isArray(payload.bars)) {
						return;
					}
					this.applyExternalWaveform(payload.bars);
				};
			} catch (e) {
				this._scheduleWaveformReconnect();
			}
		};
		connect();
	}

	_scheduleWaveformReconnect() {
		if (this._waveformReconnectTimer) {
			clearTimeout(this._waveformReconnectTimer);
		}
		this._waveformReconnectTimer = setTimeout(() => {
			this._initWaveformSocket();
		}, 3000);
	}

	applyExternalWaveform(bars) {
		const container = $(".online .song-info__time .song-info__time-container .song-info__time-waveform");
		if (!container.length) return;
		const barsEls = container.children(".bar");
		if (!barsEls.length) return;
		const count = Math.min(barsEls.length, bars.length);
		for (let i = 0; i < count; i++) {
			let v = Number(bars[i]);
			if (!isFinite(v)) v = 0;
			if (v < 0) v = 0;
			if (v > 1) v = 1;
			const level = 0.3 + v * 0.7;
			barsEls[i].style.setProperty("--external-level", level.toString());
		}
	}

	applyTitleMarquee() {
		const titleEl = $(".online .song-info__title");
		if (titleEl.length === 0) return;
		const inner = titleEl.find('.song-info__title-inner');
		if (inner.length === 0) return;

		titleEl.removeClass('is-scrolling');
		titleEl.get(0).style.removeProperty('--marquee-distance');
		titleEl.get(0).style.removeProperty('--marquee-duration');

		const containerWidth = Math.floor(titleEl.width());
		const textWidth = Math.ceil(inner[0].scrollWidth);
		const overflow = textWidth - containerWidth;
		const minOverflow = Math.max(12, Math.floor(containerWidth * 0.06));
		if (overflow > minOverflow) {
			const distance = overflow;
			const speed = 30;
			const travel = distance / speed;
			let total = travel / 0.30;
			if (total < 12) total = 12;
			if (total > 40) total = 40;
			titleEl.get(0).style.setProperty('--marquee-distance', distance + 'px');
			titleEl.get(0).style.setProperty('--marquee-duration', total + 's');
			titleEl.addClass('is-scrolling');
		}

		if (!this._marqueeBound) {
			this._marqueeBound = true;
			let resizeTimer;
			$(window).on('resize', () => {
				clearTimeout(resizeTimer);
				resizeTimer = setTimeout(() => { this.applyTitleMarquee(); this.applyArtistMarquee(); }, 200);
			});
		}
	}

	applyArtistMarquee() {
		const artistEl = $(".online .song-info__artist-name");
		if (artistEl.length === 0) return;
		artistEl.removeClass('is-scrolling');
		artistEl.get(0).style.removeProperty('--marquee-distance');
		artistEl.get(0).style.removeProperty('--marquee-duration');
	}

	_initBlurDefaults() {
		const el = $(".online .song-info");
		if (!el.length) return;
		const style = getComputedStyle(el[0]);
		const raw = style.getPropertyValue("--card-blur");
		if (raw) {
			const n = parseFloat(raw);
			if (!isNaN(n)) {
				this._blurBase = n;
			}
		}
		if (!(this._blurMax > this._blurBase)) {
			this._blurMax = this._blurBase * 3;
		}
		this._setBlur(this._blurBase);
	}

	_setBlur(value) {
		const el = $(".online .song-info");
		if (!el.length) return;
		let v = Number(value);
		if (!isFinite(v) || v < 0) v = 0;
		el[0].style.setProperty("--card-blur", v + "px");
		this._currentBlur = v;
	}

	_updateBlurByTime(playerInfo) {
		if (this._blurHold) return;
		if (!playerInfo || !playerInfo.time) {
			if (this._currentBlur !== this._blurBase) {
				this._setBlur(this._blurBase);
			}
			return;
		}
		const total = Number(playerInfo.time.total);
		const current = Number(playerInfo.time.current);
		if (!isFinite(total) || total <= 0 || !isFinite(current) || current < 0) {
			if (this._currentBlur !== this._blurBase) {
				this._setBlur(this._blurBase);
			}
			return;
		}
		const remain = total - current;
		const threshold = this._blurThresholdMs;
		if (!isFinite(threshold) || threshold <= 0) {
			if (this._currentBlur !== this._blurBase) {
				this._setBlur(this._blurBase);
			}
			return;
		}
		if (remain >= threshold) {
			if (this._currentBlur !== this._blurBase) {
				this._setBlur(this._blurBase);
			}
			return;
		}
		let t = 1 - remain / threshold;
		if (t < 0) t = 0;
		if (t > 1) t = 1;
		const value = this._blurBase + t * (this._blurMax - this._blurBase);
		this._setBlur(value);
	}

	_getCoverKey(track) {
		if (!track) return null;
		if (track.album && track.album.id) return 'album:' + track.album.id;
		if (Array.isArray(track.thumbnails) && track.thumbnails.length > 0 && track.thumbnails[0].url) {
			return 'thumb:' + track.thumbnails[0].url;
		}
		if (track.cover) return 'cover:' + track.cover;
		if (track.videoId) return 'video:' + track.videoId;
		if (track.id) return 'id:' + track.id;
		return null;
	}

	getAlbumArt(url, width, height) {
		return url.replace("w60-h60", "w" + width + "-h" + height);
	}
}
