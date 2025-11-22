// Modified by Magstic, based on original work by Topik (GPLv3)

let count = 0;
let player;
let connector;
let YTMDesktopUrl = "http://127.0.0.1:9863";
let readyCheck = setInterval(function () {
	if (count >= 3) {
		clearInterval(readyCheck);
		player = new Player();
		connector = new Connector(player);
	}
}, 500);
document.addEventListener("DOMContentLoaded", function () {
	let require = [
		"class/player.js?v=2",
		"class/connector.js?v=2",
		"class/GDMPSettings.js?v=2"
	];
	for (i in require) {
		var s = document.createElement('script');
		s.src = require[i];
		(document.head || document.documentElement).appendChild(s);
		s.onload = function () {
			count++;
		};
	}
});
