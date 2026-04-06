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


const EXAMPLE_PROGRAMS = [
	'0 REM *** STUPID CONTDOWN ***\n10 FOR A=10 TO 1 STEP -1\n20 PRINT A\n30 NEXT A\n40 PRINT "LIFT OFF"',
	'0 REM *** USELESS PRINTOUT ***\n10 PRINT "HELLO WORLD!"',
	'0 REM *** CLASSIC SIMPLE MAZE ***\n20 PRINT CHR$(205.5+RND(1));\n30 FOR A=0 TO 10 : NEXT A : REM MEGA65 IS TOO FAST, WAIT\n40 GOTO 20',
];
var msg_gate_wrap = false;
var canvas = document.getElementById("canvas");
var con = document.getElementById("console");
var container = document.getElementById("console-container");
var overlay = document.getElementById("loading-overlay");
var loadingText = document.getElementById("loading-text");
var versionInfo = document.getElementById("version-info");
var statusInfo = document.getElementById("status-info");
var basicEditor = document.getElementById("basiceditor");
var basicError = document.getElementById("editorerror");
var editorContainer = document.getElementById("editor-container");
var exampleNumber = 0;
var ready = false;


function is_ready() {
	return ready && msg_gate_wrap;
}


function mark_ready ( val ) {
	statusInfo.innerHTML = val ? "READY" : "BOOTING";
	const valName = val ? "TRUE" : "FALSE";
	emulator_output("[SHELL] Marking ready request detected for " + valName);
	if (!msg_gate_wrap || ready == val)
		return;
	ready = val;
	emulator_output("[SHELL] Marking ready as " + valName);
	if (val && editorContainer && editorContainer.style.display == "none")
		editorContainer.style.display = "block";
}


function msg_gate ( id ) {
	if (!is_ready()) {
		emulator_output("[SHELL] Cannot submit emscripten gateway message " + id + " because emulator is not in ready state yet");
		return -1;
	}
	emulator_output("[SHELL] Submitting emscripten gateway message " + id);
	if (id == 1) {
		ready = false;
		emulator_output("[SHELL] Setting ready state to false because of hard reset request message");
	}
	return msg_gate_wrap(id);
}


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
		if (text.startsWith("MSG: hypervisor first enter"))
			mark_ready(false);
		if (text.startsWith("MSG: hypervisor first leave"))
			mark_ready(true);
	}
}


function next_example () {
	if (basicEditor.value == EXAMPLE_PROGRAMS[exampleNumber])
		emulator_output("[SHELL] OK, umodified program");
	else
		emulator_output("[SHELL] WOW, modified program");
	exampleNumber = (exampleNumber + 1) % EXAMPLE_PROGRAMS.length;
	basicEditor.value = EXAMPLE_PROGRAMS[exampleNumber];
}


function editor_get () {
	function editor_error ( offset, error ) {
		emulator_output("[BASIC EDITOR] PARSE ERROR: " + error);
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
	let lastNum = -1;
	let offset = 0;
	for (const line of basicEditor.value.split("\n")) {
		if (line.trim() == "") {
			offset += line.length + 1;
			continue;
		}
		const prevLineDesc = lastNum >= 0 ? lastNum : "[START]";
		const num = parseInt(line, 10);
		if (isNaN(num) || num < 0 || num > 65535)
			return editor_error(offset, "Line without line number, or invalid line number after line " + prevLineDesc);
		if (num <= lastNum)
			return editor_error(offset, "Non-increasing line number " + num + " after line " + prevLineDesc);
		lastNum = num;
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
	if (!is_ready())
		return;
	const encoder = new TextEncoder();
	const encodedData = encoder.encode(data);
	fn = "/files/hdos/" + fn.toUpperCase();
	emulator_output("[SHELL] Writing file (" + encodedData.length + " bytes) to " + fn);
	Module.FS.writeFile(fn, encodedData);
}


function run_this () {
	if (!is_ready()) {
		emulator_output("[SHELL] Emulator is not in ready state yet");
		return;
	}
	const s = editor_get();
	if (s != "") {
		write_file("PRG.BAS", s);
		msg_gate(6);	// import basic program request
	}
}


emulator_output("[SHELL] Shell is online @ " + navigator.userAgent);
emulator_output("[SHELL] Origin: " + document.URL);
basicEditor.value = EXAMPLE_PROGRAMS[exampleNumber];
statusInfo.innerHTML = "LOADING";
if (editorContainer)
	editorContainer.style.display = "none";


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
		// Solution found: https://stackoverflow.com/questions/45936800/emscripten-canvas-jquery-toggle-focus
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
