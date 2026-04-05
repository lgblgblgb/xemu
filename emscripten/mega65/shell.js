/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2026 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

var SAMPLE_PROGRAM = '10 FOR A=10 TO 0 STEP -1\n20 PRINT A\n30 NEXT A\n40 PRINT "LIFT OFF."';
var msg_gate_wrap = false;
function msg_gate ( id ) {
	emulator_output("[SHELL] Submitting emscripten gateway message " + id);
	return msg_gate_wrap ? msg_gate_wrap(id) : -1;
}
var canvas = document.getElementById("canvas");
var con = document.getElementById("console");
var container = document.getElementById("console-container");
var overlay = document.getElementById("loading-overlay");
var loadingText = document.getElementById("loading-text");
var versionInfo = document.getElementById("version-info");
var basicEditor = document.getElementById("basiceditor");
basicEditor.value = SAMPLE_PROGRAM;
function emulator_output ( text ) {
	if (text) {
		let node = document.createTextNode(text + "\n");
		con.appendChild(node);
		container.scrollTop = container.scrollHeight;
		if (con.childNodes.length > 256)
			con.removeChild(con.firstChild);
		if (versionInfo && text.startsWith("VERSION: ")) {
			let s = text.split(" ");
			emulator_output("[SHELL] Version string identified: " + s.slice(1).join(" "));
			versionInfo.innerHTML += " <i>" + s.at(-2) + "/" + s.at(-4) + "</i>";
			versionInfo = false;
		}
	}
}
function editor_get () {
	function editor_error ( offset, error ) {
		emulator_output("[BASIC EDITOR] PARSE ERROR: " + error);
		const basicError = document.getElementById("editorerror");
		basicError.innerText = error;
		basicError.style.display = "block";
		basicError.onclick = basicEditor.oninput = () => {
			basicError.style.display = "none";
			basicEditor.focus();
			basicError.onclick = basicEditor.oninput = null;
		};
		if (offset >= 0) {
			basicEditor.blur();
			basicEditor.focus();
			basicEditor.setSelectionRange(offset, offset);
		} else {
			basicEditor.focus();
		}
		return "";
	}
	if (basicEditor.value.length > 8192)
		return editor_error(-1, "Too long input");
	let output = "";
	let lastnum = -1;
	let offset = 0;
	for (const line of basicEditor.value.split("\n")) {
		if (line.trim() == "") {
			offset += line.length + 1;
			continue;
		}
		const prev_line_desc = lastnum >= 0 ? lastnum : "[START]";
		const num = parseInt(line, 10);
		if (isNaN(num) || num < 0 || num > 64000)
			return editor_error(offset, "Line without line number, or invalid line number after line " + prev_line_desc);
		if (num <= lastnum)
			return editor_error(offset, "Non-increasing line number " + num + " after line " + prev_line_desc);
		lastnum = num;
		let r = "";
		for (let i = 0; i < line.length; i++) {
			const c = line[i].toUpperCase();
			if (c < " ")		r += " ";
			else if (c > "z")	return editor_error(offset + i, "Invalid character in line " + num);
			else			r += c;
		}
		r = r.trim();
		if (r.length > 80)
			return editor_error(offset, "Too long line in " + num);
		output += r + "\n";
		offset += line.length + 1;
	}
	if (output.length < 16)
		return editor_error(-1, "Too short input");
	emulator_output("[BASIC EDITOR] Validated content with " + output.length + " bytes of text");
	return output;
}
function write_file ( fn, data ) {
	if (!msg_gate_wrap)
		return;
	const encoder = new TextEncoder();
	const encoded_data = encoder.encode(data);
	fn = "/files/hdos/" + fn.toUpperCase();
	emulator_output("[SHELL] Writing file (" + encoded_data.length + " bytes) to " + fn);
	Module.FS.writeFile(fn, encoded_data);
}
function run_this () {
	if (!msg_gate_wrap)
		return;
	const s = editor_get();
	if (s != "") {
		write_file("PRG.BAS", s);
		msg_gate(6);	// import basic program request
	}
}
emulator_output("[SHELL] Shell is online @ " + navigator.userAgent);
emulator_output("[SHELL] Origin: " + document.URL);
var Module = {
	arguments: "-fastboot -besure -sdlrenderquality 2 -gui none -hdosvirt -lockvideostd -videostd 0".split(" "),
	canvas: canvas,
	setStatus: function ( text ) {
		if (loadingText) {
			loadingText.textContent = text ? text : "Loading";
			emulator_output("[SHELL] Loading progress message: \"" + loadingText.textContent + "\"");
		}
	},
	print: function ( text ) {
		emulator_output(text);
	},
	printErr: function ( text ) {
		emulator_output("STDERR: " + text);
	},
	onRuntimeInitialized: function () {
		emulator_output("[SHELL] onRuntimeInitialized");
		msg_gate_wrap = Module.cwrap("msg_gate", "number", ["number"]);
		overlay.style.display = "none";
	},
	preRun: [function () {
		ENV.XEMU_EM_BROWSER = navigator.userAgent;
		ENV.XEMU_EM_ORIGIN = document.URL;
		ENV.XEMU_EM_OS = navigator.platform;
		ENV.SDL_EMSCRIPTEN_KEYBOARD_ELEMENT = "#canvas";
		emulator_output("[SHELL] PRERUN: Starting emulator with command line: " + Module.arguments.join(" "));
	},],
	postRun: [function () {
		emulator_output("[SHELL] POSTRUN: Focus to emulator initially");
		canvas.focus();
	},],
};
// A horrible mess to remedy focus problems ...
canvas.addEventListener("mousedown", () => { canvas.focus(); emulator_output("[SHELL] Focus to emulator on canvas click"); });
basicEditor.addEventListener("blur", () => { setTimeout(() => { canvas.focus(); emulator_output("[SHELL] Focus to emulator on editor blur"); }, 20); });
setInterval(() => {
	const inFocus = document.activeElement;
	if (inFocus !== basicEditor && inFocus !== canvas) {
        	canvas.focus();
		emulator_output("[SHELL] Focus to emulator on timeout");
	}
}, 200);
