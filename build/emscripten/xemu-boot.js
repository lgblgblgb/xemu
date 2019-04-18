/* Xemu/Xep128 Emscripten "booter" and option/FS parser + file XHR downloader.
 * Note: it's pre-alpha UGLY code, with globals, without modularization, etc etc ...
 * Some code based on (or the original) the Emscripten generated code. Other parts:
  
Copyright (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
http://xep128.lgb.hu/

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


if (typeof window.Module !== "undefined")
	alert("You can't have multiple Xemu emulator and/or Emscripten Module object within one web page! Your page will probably not work ...");
else
	window.Module = (function () {




function CONFIG_ERROR ( text ) {
	if (arguments.length > 1)
		text = Array.prototype.slice.call(arguments).join(' ');
	text = "XEMU CONFIG/CALL ERROR: " + text;
	console.error(text);
	window.alert(text);
	return null;
}

var Xemu = {
	"memoryInitializer":	"<XEMU>.html.mem",
	"emulatorJS":		"<XEMU>.js",
	"fileFetchList":	[],
	"URLpostfix":		"",
	"URLprefix":		"",
	"fileObjects":		{},		// internal!
	"filesFetched":		1,		// internal!
	"fileFetchError":	false,		// internal!
	"fileWaiters":		0,		// internal!
	"started":		false,		// internal!
	"mainLoop":		false,		// internal!
	"arguments":		[],
	"getFromMonitor":	function (msg) { console.log("Answer from monitor: " + msg); }
};
var Module = { Xemu: Xemu };



Module.preRun = [
	(function () {
		var a;
		/* Populate fetched objects to the MEMFS */
		FS.mkdir("/files");
		for (a in Xemu.fileObjects) {
			console.log("JS-Laucher/preRun: populating file: " + a);
			FS.writeFile("/files/" + a, Xemu.fileObjects[a], { encoding: "binary" });
			Xemu.fileObjects[a] = undefined;
		}
		Xemu.fileObjects = undefined;
		/* Set some env variables, used by the emulator to query browser/browsing information */
		ENV["XEMU_EN_BROWSER"] = navigator.userAgent;
		ENV["XEMU_EN_ORIGIN"] = String(window.location);
		//for (a in ENV)
		//	console.log("ENV[%s]=%s", a, ENV[a]);
		if (Module.cwrap)
			Xemu.sendToMonitor = Module.cwrap(
				"monitor_queue_command",	// name of C function
				"number",			// return type
				["string"]			// argument types
			);
	}),
	(function () {
		console.log("*** End of PRE-RUN phase ***");
	})
];


Module.postRun = [
	(function() {
		console.log("*** End of POST-RUN phase ***");
		Xemu.mainLoop = true;
	})
];


Module.print = function ( text ) {
	if (arguments.length > 1) text = Array.prototype.slice.call(arguments).join(' ');
	// These replacements are necessary if you render to raw HTML
	//text = text.replace(/&/g, "&amp;");
	//text = text.replace(/</g, "&lt;");
	//text = text.replace(/>/g, "&gt;");
	//text = text.replace('\n', '<br>', 'g');
	console.log(text);
	if (Xemu.outputElement) {
		Xemu.outputElement.value += text + "\n";
		Xemu.outputElement.scrollTop = Xemu.outputElement.scrollHeight; // focus on bottom
	}
};


Module.printErr = function ( text ) {
	if (arguments.length > 1)
		text = Array.prototype.slice.call(arguments).join(' ');
	if (text == "Looks like you are rendering without using requestAnimationFrame for the main loop. You should use 0 for the frame rate in emscripten_set_main_loop in order to use requestAnimationFrame, as that can greatly improve your frame rates!")
		text = "EMSCRIPTEN-MADNESS-POINTLESS-ERROR";
	if (0) { // XXX disabled for safety typeof dump == 'function') {
		dump(text + '\n'); // fast, straight to the real console
	} else {
		if (text != "EMSCRIPTEN-MADNESS-POINTLESS-ERROR")
			console.error(text);
	}
};


Module.setStatus = function ( text ) {
	if (!Module.setStatus.last)
		Module.setStatus.last = { time: Date.now(), text: '' };
	if (text === Module.setStatus.text)
		return;
	var m = text.match(/([^(]+)\((\d+(\.\d+)?)\/(\d+)\)/);
	var now = Date.now();
	if (m && now - Date.now() < 30)
		return; // if this is a progress update, skip it if too soon
	if (m) {
		text = m[1];
		Xemu.progressElement.value = parseInt(m[2])*100;
		Xemu.progressElement.max = parseInt(m[4])*100;
		Xemu.progressElement.hidden = false;
		Xemu.spinnerElement.hidden = false;
	} else {
		Xemu.progressElement.value = null;
		Xemu.progressElement.max = null;
		Xemu.progressElement.hidden = true;
		if (!text)
			Xemu.spinnerElement.style.display = 'none';
	}
	Xemu.statusElement.innerHTML = text;
};


Module.totalDependencies = 0;


Module.monitorRunDependencies = function ( left ) {
	this.totalDependencies = Math.max(this.totalDependencies, left);
	Module.setStatus(left ? 'Preparing... (' + (this.totalDependencies-left) + '/' + this.totalDependencies + ')' : 'All downloads complete.');
};



//Xemu.requestFullScreen = Module.requestFullScreen;


// Optional functionality provided to the user to help to "upload" things to the browser.
// Must be used as an event handler for file type input field.
// The actual work then should be done by the passResult callback
Xemu.fileUploadHandler = function ( file, minsize, maxsize, passResult, passResultParams ) {
	"use strict";
	function upload_msg ( msg ) {
		console.log("FILE-UPLOAD: " + msg);
	}
	function upload_error ( msg ) {
		upload_msg("ERROR: " + msg);
		alert(msg);
	}
	if (!window.FileReader) {
		upload_error("Your browser is too old, it does not seem to support FileReader API.");
		return;
	}
	var reader = new FileReader();
	if (!reader.readAsArrayBuffer) {
		upload_error("Your browser is too old, does not support readAsArrayBuffer() method in FileReader instance");
		return ;
	}
	if (file.length != 1 || !file[0] || !file[0].name || !file[0].size) {
		upload_error("Select exactly one (non-zero length) file!");
		return;
	}
	file = file[0];
	if (minsize <= 0)
		minsize = 1;
	if (maxsize <= 0 || maxsize > 1000000)
		maxsize = 1000000;
	if (file.size < minsize || file.size > maxsize) {
		upload_error("Selected file has size of " + file.size + ", but must be " + (minsize == maxsize ? "exactly " + minsize : "between " + minsize + " and " + maxsize) + " bytes.");
		return;
	}
	reader.onerror = function (e) {
		this.onloadend = undefined;
		upload_error("File upload to your browser has caught an error (your browser may not have the privilege to read that file, or such).");
	};
	reader.onabort = function (e) {
		this.onloadend = undefined;
		upload_error("File upload to your browser has been aborted.");
	};
	reader.onloadend = function (e) {
		if (this.result) {
			if (this.result.byteLength == file.size) {
				upload_msg("file read is done :-) Calling the specified handler with the result ArrayBuffer.");
				passResult(this.result, passResultParams);
			} else
				upload_error("uploaded file has different size (" + this.result.byteLength + ") than it was requested (" + file.size + ")");
		} else
			upload_msg("?? uploaded ended with no result (ie: error?). See previous message(s).");
	};
	console.log("FILE-UPLOAD: received file upload request for " + file.name + " with length of " + file.size + " bytes. Starting FileReader ...");
	reader.readAsArrayBuffer(file);
};


Xemu.fileEmscriptenWriter = function ( data, params ) {
	// Module.Xemu.dir("/files/");
	data = new Uint8Array(data);
	console.log("About to writing " + data.length + " bytes to the file " + params.fileName + " with open mode " + params.openMode);
	var stream = FS.open(params.fileName, params.openMode);
	console.log("FS.open = " + stream);
	// console.log(data);
	console.log("FS.write[len=" + data.length + "] = " + FS.write(stream, data, 0, data.length, 0));
	console.log("FS.close = " + FS.close(stream));
	FS.syncfs(true, function (e) {
		console.log("FS.syncfs: " + e);
		// Module.Xemu.dir("/files/");
	});
};


// for debug purposes, it shows the contents of the "emscripten VFS" into an object
function __dir__ ( dirName, result ) {
	"use strict";
	if (dirName == undefined)
		dirName = "/";
	if (result == undefined)
		result = [];
	var list = FS.readdir(dirName);
	var a;
	var dirs = [];
	for (a in list) {
		var name = dirName + list[a];
		if (list[a] != "." && list[a] != "..") {
			var stat = FS.stat(name);
			if (FS.isDir(stat.mode)) {
				name += "/";
				dirs.push(name);
			}
			result.push({"name": name, "size": stat.size, "mode": stat.mode, "mtime": stat.mtime});
		}
	}
	for (a in dirs) {
		try {
			result = __dir__(dirs[a], result);
		} catch { }
	}
	return result;
}


Xemu.dir = function ( dirName )
{
	console.table(__dir__(dirName));
};


Xemu.start = function (user_settings) {
	"use strict";
	var item;
	function createXemuElement (kind, root_e, content, attribs, styles) {
		var a;
		var e = document.createElement(kind);
		e.innerHTML = content;
		if (attribs !== null && typeof attribs == "object")
			for (a in attribs) {
				if (e[a] === undefined)
					console.log("Warning: tag " + kind + " doesn't seem to support tag attribute " + a);
				e[a] = attribs[a];
			}
		if (styles !== null && typeof styles == "object")
			for (a in styles) {
				if (e.style[a] === undefined)
					console.log("Warning: tag " + kind + " doesn't seem to support CSS attribute " + a);
				e.style[a] = styles[a];
			}
		root_e.appendChild(e);
		return e;
	}
	if (Xemu.started)
		return CONFIG_ERROR("Emulator has been already started!");
	Xemu.started = true;
	if (user_settings === null || typeof user_settings !== "object")
		return CONFIG_ERROR("An object parameter is needed for Xemu.start()");
	for (item in user_settings)
		Xemu[item] = user_settings[item];
	user_settings = undefined;
	if (Xemu.emulatorId === undefined)
		return CONFIG_ERROR("emulatorId was not defined for Xemu.start()");
	if (Xemu.divId === undefined)
		return CONFIG_ERROR("divId was not defined for Xemu.start()");
	Xemu.div = document.getElementById(Xemu.divId);
	if (Xemu.div === null)
		return CONFIG_ERROR("Cannot get DIV element by id name '" + Xemu.divId + "'");
	if (Xemu.div.tagName.toLowerCase() !== "div")
		return CONFIG_ERROR("Given DIV element by ID is actually not a DIV element.");
	//Xemu.div = document.createElement("div");
	Xemu.div.innerHTML = "";
	Xemu.div.innerText = "";

	//createXemuElement (kind, root_e, content, attribs, styles)
	Xemu.statusElement = createXemuElement("div", Xemu.div, "Downloading ...", null, null);


	//Xemu.statusElement = document.createElement("div");
        //Xemu.statusElement.innerHTML = "Downloading ...";
	Xemu.progressElement = document.createElement("progress");

	Xemu.progressElement = createXemuElement("progress", Xemu.div, "", {
		value: "0", max: "100", hidden: true, className: "xemu"
	}, null);


	Xemu.progressElement.value = "0";
	Xemu.progressElement.max = "100";
	Xemu.progressElement.hidden = true;
	Xemu.progressElement.className = "xemu";

	Xemu.spinnerElement = document.createElement("div");
	Xemu.outputElement = document.createElement("textarea");
	Module.canvas = Xemu.canvasElement = document.createElement("canvas");
	Xemu.canvasElement.innerText = "Screen of software emulation of '" + Xemu.emulatorId + "'";
	Xemu.div.appendChild(Xemu.statusElement);
	Xemu.div.appendChild(Xemu.progressElement);
	Xemu.div.appendChild(Xemu.spinnerElement);
	Xemu.div.appendChild(Xemu.statusElement);
	Xemu.div.appendChild(Xemu.outputElement);
	Xemu.div.appendChild(Xemu.canvasElement);

	Xemu.outputElement.value = "";	// clear browser cache

	Module.canvas.oncontextmenu = function (e) { e.preventDefault(); };
	// As a default initial behavior, pop up an alert when webgl context is lost. To make your
	// application robust, you may want to override this behavior before shipping!
	// See http://www.khronos.org/registry/webgl/specs/latest/1.0/#5.15.2
	Module.canvas.addEventListener("webglcontextlost", function(e) {
		if (window.confirm("Your browser lost WebGL context. Can I try to reload with the hope to fix the problem (resets/reload the emulation!)?"))
			window.location.reload();
		e.preventDefault();
	}, false);

	Module.setStatus('Downloading...');
	window.onerror = function(event) {
		// TODO: do not warn on ok events like simulating an infinite loop or exitStatus
		Module.setStatus('Exception thrown, see JavaScript console');
		spinnerElement.style.display = 'none';
		Module.setStatus = function(text) {
			if (text)
				Module.printErr('[post-exception status] ' + text);
		};
	};


	//item = document.createElement("style");
	//item.innerText = "";
	//document.head.appendChild(item);

	Module.arguments = Xemu.arguments;
	Xemu.memoryInitializer = Xemu.memoryInitializer.replace("<XEMU>", Xemu.emulatorId);
	Xemu.emulatorJS = Xemu.emulatorJS.replace("<XEMU>", Xemu.emulatorId);
	Xemu.fileFetchList.push("!!" + Xemu.memoryInitializer);
	function URLize ( name ) {
	}
	function bootEmulator ( reason ) {
		// TODO: URLize here too ....
		console.log("*** Booting emulator using " + Xemu.emulatorJS + ": " + reason + " ***");
		var script = document.createElement("script");
		script.src = Xemu.emulatorJS;
		document.body.appendChild(script);
	}
	function downloadFile ( name ) {
		var url, xhr = new XMLHttpRequest();
		function downloadFileHandler ( resp ) {
			if (Xemu.fileFetchError)
				return;
			console.log("Download (" + (Xemu.filesFetched + 1) + "/" + Xemu.fileFetchList.length + ") " + name + " as " + url + " [" + xhr.status + " " + xhr.statusText + "]");
			if (xhr.status != 200) {
				Xemu.fileFetchError = true;
				CONFIG_ERROR("Xemu loader: Cannot download file " + name + " as " + url + ", error: " + xhr.status + " " + xhr.statusText);
				return;
			}
			console.log(resp);
			console.log(resp.currentTarget);
			console.log(xhr);
			console.log(this);
			Xemu.fileObjects[name] = new Uint8Array(xhr.response);
			xhr = undefined;
			console.log("Download status: itemsDone = " + Xemu.filesFetched + ", allitems = " + Xemu.fileFetchList.length);
			Xemu.filesFetched++;
			if (Xemu.filesFetched == Xemu.fileFetchList.length)
				bootEmulator("all file-only downloads are done.");
		}
		name = name.replace("<XEMU>", Xemu.emulatorId);
		if (name == "!!" + Xemu.memoryInitializer) {
			name = name.substr(2);
			Module['memoryInitializerRequest'] = xhr;
			console.log("Download (1/" + Xemu.fileFetchList.length + ") " + name + " is not monitored by the loader, special file.");
		} else {
			Xemu.fileWaiters++;
			xhr.addEventListener("load",  downloadFileHandler);
			xhr.addEventListener("error", downloadFileHandler);
			xhr.addEventListener("abort", downloadFileHandler);
		}
		if (name.search("://") != -1) {
			url = name;
		} else if (Xemu.URLprefix != "") {
			url = Xemu.URLprefix + name + Xemu.URLpostfix;
		} else if (typeof Module['locateFile'] === 'function') {	// not user ...
			url = Module['locateFile'](name) + Xemu.URLpostfix;
		} else if (Module['memoryInitializerPrefixURL']) {	// not used ...
			url = Module['memoryInitializerPrefixURL'] + name + Xemu.URLpostfix;
		} else {
			url = name + Xemu.URLpostfix;
		}
		name = name.replace(/\?.*$/, "").replace(/^.*\/(.*)$/, "$1").trim();
		url = url.replace("<XEMU>", Xemu.emulatorId);
		if (name == "") {
			CONFIG_ERROR("Bad file name");
			Xemu.fileFetchError = true;
			return;
		}
		//url = "file:///home/lgb/prog_here/xep128/" + name;
		console.log("Fetching " + name + " using URL " + url);
		//alert("Fetching " + name + " using URL " + url);
		xhr.open('GET', url, true);
		xhr.responseType = 'arraybuffer';
		xhr.send(null);
	}
	for (item in Xemu.fileFetchList)
		if (!Xemu.fileFetchError)
			downloadFile(Xemu.fileFetchList[item]);
	console.log("Files for XHR fetching are submitted ...");
	if (Xemu.fileWaiters == 0)
		bootEmulator("no file-only downloads.");
};


/* --- the end --- */
return Module;
})();
