/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


/* Note: currently this is for CBM BASIC 10 (or maybe 2, since the 'base' token set is the same)
 * TODO: extend this to more CBM BASIC versions and also to other BASIC dialects used in Xemu!!! */

#include "xemu/emutools.h"
#include "xemu/basic_text.h"
#include <stdlib.h>
#include <strings.h>



#ifdef CBM_BASIC_TEXT_SUPPORT

// This table is from VICE (utility petcat), though with a heavy edited form, current
// no multiple BASIC versions etc, just straight BASIC 10 ...
// Later, this source should be extended to all CBM BASIC dialects switchable, also
// probably with other non-commodore BASICs should be considered as well, available
// in Xemu ...

static const struct {
	const Uint16 token;
	const char *str;
} basic_tokens[] = {
   {  0x80,"end"      },
   {  0x81,"for"      },
   {  0x82,"next"     },
   {  0x83,"data"     },
   {  0x84,"input#"   },
   {  0x85,"input"    },
   {  0x86,"dim"      },
   {  0x87,"read"     },
   {  0x88,"let"      },
   {  0x89,"goto"     },
   {  0x8a,"run"      },
   {  0x8b,"if"       },
   {  0x8c,"restore"  },
   {  0x8d,"gosub"    },
   {  0x8e,"return"   },
   {  0x8f,"rem"      },

   {  0x90,"stop"     },
   {  0x91,"on"       },
   {  0x92,"wait"     },
   {  0x93,"load"     },
   {  0x94,"save"     },
   {  0x95,"verify"   },
   {  0x96,"def"      },
   {  0x97,"poke"     },
   {  0x98,"print#"   },
   {  0x99,"print"    },
   {  0x9a,"cont"     },
   {  0x9b,"list"     },
   {  0x9c,"clr"      },
   {  0x9d,"cmd"      },
   {  0x9e,"sys"      },
   {  0x9f,"open"     },

   {  0xa0,"close"    },
   {  0xa1,"get"      },
   {  0xa2,"new"      },
   {  0xa3,"tab("     },
   {  0xa4,"to"       },
   {  0xa5,"fn"       },
   {  0xa6,"spc("     },
   {  0xa7,"then"     },
   {  0xa8,"not"      },
   {  0xa9,"step"     },
   {  0xaa,"+"        },
   {  0xab,"-"        },
   {  0xac,"*"        },
   {  0xad,"/"        },
   {  0xae,"^"        },
   {  0xaf,"and"      },

   {  0xb0,"or"       },
   {  0xb1,">"        },
   {  0xb2,"="        },
   {  0xb3,"<"        },
   {  0xb4,"sgn"      },
   {  0xb5,"int"      },
   {  0xb6,"abs"      },
   {  0xb7,"usr"      },
   {  0xb8,"fre"      },
   {  0xb9,"pos"      },
   {  0xba,"sqr"      },
   {  0xbb,"rnd"      },
   {  0xbc,"log"      },
   {  0xbd,"exp"      },
   {  0xbe,"cos"      },
   {  0xbf,"sin"      },

   {  0xc0,"tan"      },
   {  0xc1,"atn"      },
   {  0xc2,"peek"     },
   {  0xc3,"len"      },
   {  0xc4,"str$"     },
   {  0xc5,"val"      },
   {  0xc6,"asc"      },
   {  0xc7,"chr$"     },
   {  0xc8,"left$"    },
   {  0xc9,"right$"   },
   {  0xca,"mid$"     },
   {  0xcb,"go"       },
   {  0xcc,"rgr"      },
   {  0xcd,"rclr"     },
   {  0xcf,"joy"      },

   {  0xd0,"rdot"     },
   {  0xd1,"dec"      },
   {  0xd2,"hex$"     },
   {  0xd3,"err$"     },
   {  0xd4,"instr"    },
   {  0xd5,"else"     },
   {  0xd6,"resume"   },
   {  0xd7,"trap"     },
   {  0xd8,"tron"     },
   {  0xd9,"troff"    },
   {  0xda,"sound"    },
   {  0xdb,"vol"      },
   {  0xdc,"auto"     },
   {  0xdd,"pudef"    },
   {  0xde,"graphic"  },
   {  0xdf,"paint"    },

   {  0xe0,"char"     },
   {  0xe1,"box"      },
   {  0xe2,"circle"   },
   {  0xe3,"gshape"   },
   {  0xe4,"sshape"   },
   {  0xe5,"draw"     },
   {  0xe6,"locate"   },
   {  0xe7,"color"    },
   {  0xe8,"scnclr"   },
   {  0xe9,"scale"    },
   {  0xea,"help"     },
   {  0xeb,"do"       },
   {  0xec,"loop"     },
   {  0xed,"exit"     },
   {  0xee,"directory"},
   {  0xef,"dsave"    },

   {  0xf0,"dload"    },
   {  0xf1,"header"   },
   {  0xf2,"scratch"  },
   {  0xf3,"collect"  },
   {  0xf4,"copy"     },
   {  0xf5,"rename"   },
   {  0xf6,"backup"   },
   {  0xf7,"delete"   },
   {  0xf8,"renumber" },
   {  0xf9,"key"      },
   {  0xfa,"monitor"  },
   {  0xfb,"using"    },
   {  0xfc,"until"    },
   {  0xfd,"while"    },

   {0xce02,"pot"      },
   {0xce03,"bump"     },
   {0xce04,"pen"      },
   {0xce05,"rsppos"   },
   {0xce06,"rsprite"  },
   {0xce07,"rspcolor" },
   {0xce08,"xor"      },
   {0xce09,"rwindow"  },
   {0xce0a,"pointer"  },

   {0xfe02,"bank"     },
   {0xfe03,"filter"   },
   {0xfe04,"play"     },
   {0xfe05,"tempo"    },
   {0xfe06,"movspr"   },
   {0xfe07,"sprite"   },
   {0xfe08,"sprcolor" },
   {0xfe09,"rreg"     },
   {0xfe0a,"envelope" },
   {0xfe0b,"sleep"    },
   {0xfe0c,"catalog"  },
   {0xfe0d,"dopen"    },
   {0xfe0e,"append"   },
   {0xfe0f,"dclose"   },

   {0xfe10,"bsave"    },
   {0xfe11,"bload"    },
   {0xfe12,"record"   },
   {0xfe13,"concat"   },
   {0xfe14,"dverify"  },
   {0xfe15,"dclear"   },
   {0xfe16,"sprsav"   },
   {0xfe17,"collision"},
   {0xfe18,"begin"    },
   {0xfe19,"bend"     },
   {0xfe1a,"window"   },
   {0xfe1b,"boot"     },
   {0xfe1c,"width"    },
   {0xfe1d,"sprdef"   },
   {0xfe1e,"quit"     },
   {0xfe1f,"stash"    },

   {0xfe21,"fetch"    },
   {0xfe23,"swap"     },
   {0xfe24,"off"      },
   {0xfe25,"fast"     },
   {0xfe26,"slow"     },
   {0, NULL }

};


//char xemu_basic_decoder_error[1024];


static const char *ERROR_HEAD = "BASIC program exporting error:\n";

static const char *end_of_line = NL;	// native OS line ending is used ...
static const char *tex_head = "\\verbatimfont{\\codefont}\n\\begin{verbatim}\n";
static const char *tex_end = "\\end{verbatim}\n";


#define CONTEXT_POINTER0	0
#define CONTEXT_POINTER1	1
#define CONTEXT_LINENO0		2
#define CONTEXT_LINENO1		3
#define CONTEXT_NORMAL		4
#define CONTEXT_QUOTED		5
#define CONTEXT_REM		6
#define CONTEXT_BEGIN		7
#define CONTEXT_END		8


int xemu_basic_to_text_malloc ( Uint8 **buffer, int output_super_limit, const Uint8 *prg, int real_addr, const Uint8 *prg_limit, int basic_dialect, int flags )
{
	int size = xemu_basic_to_text(NULL, output_super_limit, prg, real_addr, prg_limit, basic_dialect, flags);
	if (size < 0) {
		*buffer = NULL;
		return size;
	}
	*buffer = malloc(size + 1);
	if (!*buffer) {
		ERROR_WINDOW("%sCannot allocate memory", ERROR_HEAD);
		return -1;
	}
	size = xemu_basic_to_text(*buffer, size, prg, real_addr, prg_limit, basic_dialect, flags);
	if (size < 0) {
		free(*buffer);
		return size;
	}
	buffer[size] = 0;
	return size;
}


int xemu_basic_to_text ( Uint8 *output, int output_size, const Uint8 *prg, int real_addr, const Uint8 *prg_limit, int basic_dialect, int flags )
{
	int output_used = 0;
	char outbuf[256];	// the longest thing here is a single one-time decoded entity. This is waaay too much, but let's play safe ...
	const char *o = NULL;
	int num = -1, old_num = -1;
	int ptr = 0;
	int context = CONTEXT_BEGIN;
	int token = 0;
	for (;;) {
		if (XEMU_UNLIKELY(prg > prg_limit)) {
			ERROR_WINDOW("%sProgram flows outside of the allowed memory area after line %d", ERROR_HEAD, old_num);
			return -1;
		}
		Uint8 c = *prg++;
		real_addr++;
		if (context == CONTEXT_BEGIN) {
			ptr = c;
			context = CONTEXT_POINTER1;	// CONTEXT_BEGIN is same as CONTEXT_POINTER0, just occures once, so at the first line, we simply skip CONTEXT_POINTER0 since they're the same in a way ;-P Hard to explain
			if ((flags & BASIC_TO_TEXT_FLAG_TEX))
				o = tex_head;
			else
				continue;
		} else if (context == CONTEXT_POINTER0) {
			printf("ptr_to_here=%d real_addr=%d, delta=%d\n", ptr, real_addr, real_addr-ptr);
			if (ptr != real_addr - 1) {
				ERROR_WINDOW("%sBad BASIC chaining near line %d", ERROR_HEAD, num);
				return -1;
			}
			ptr = c;
			context = CONTEXT_POINTER1;
			continue;
		} else if (context == CONTEXT_POINTER1) {
			ptr += c << 8;
			if (XEMU_UNLIKELY(!ptr)) {
				if ((flags & BASIC_TO_TEXT_FLAG_TEX)) {
					context = CONTEXT_END;
					o = tex_end;
				} else
					break;	// END OF BASIC PROGRAM ;)
			} else {
				context = CONTEXT_LINENO0;
				continue;
			}
		} else if (context == CONTEXT_LINENO0) {
			context = CONTEXT_LINENO1;
			old_num = num;
			num = c;
			continue;
		} else if (context == CONTEXT_LINENO1) {
			num += c << 8;
			context = CONTEXT_NORMAL;
			sprintf(outbuf, "%5d ", num);
			o = outbuf;
		} else if (context == CONTEXT_NORMAL && (c >= 0x80 || token != 0)) {
			if ((c == 0xCE || c == 0xFE) && token == 0) {
				token = c << 8;
			} else {
				token += c;
				for (int a = 0 ;; a++) {
					if (basic_tokens[a].token == token) {
						o = basic_tokens[a].str;
						break;		// token found, "o" holds what it must be output
					} else if (basic_tokens[a].token == 0) {
						ERROR_WINDOW("%sUnknown token $%04X in line %d", ERROR_HEAD, token, num);
						return -1;
					}
				}
				if (token == 0x8F)	// REM
					context = CONTEXT_REM;
				token = 0;
			}
		} else if (c == 0) {	// end of basic line
			if (XEMU_UNLIKELY(token)) {	// TODO: maybe it's useless to check? see the condition above ...
				ERROR_WINDOW("%sUnfinished extended token sequence in line %d", ERROR_HEAD, num);
				return -1;
			}
			context = CONTEXT_POINTER0;
			o = end_of_line;
		} else {
			if (c == '"') {
				if (context == CONTEXT_NORMAL)
					context = CONTEXT_QUOTED;
				else if (context == CONTEXT_QUOTED)
					context = CONTEXT_NORMAL;
			}
			if (c >= 'A' && c <= 'Z')
				c +=  'a' - 'A';
			else if (c >= ('A' | 0x80) && c <= ('Z' | 0x80))
				c &= 0x7F;
			outbuf[0] = c;
			outbuf[1] = 0;
			o = outbuf;
		}
		// flush output in a safe way
		if (XEMU_UNLIKELY(!o)) {	// just for code errors, should not happen!
			ERROR_WINDOW("%sInternal error, uninitialized output buffer in line %d", ERROR_HEAD, num);
			return -1;
		}
		while (*o) {
			if (XEMU_UNLIKELY(output_used >= output_size)) {
				ERROR_WINDOW("%sOutput buffer is too small for this program in line %d", ERROR_HEAD, num);
				return -1;
			}
			output_used++;
			if (output)
				*output++ = *o++;
			else
				o++;	// this mode (when output is NULL) is just for calculating the size of output
		}
		if (XEMU_UNLIKELY(context == CONTEXT_END))
			break;	// END OF BASIC PROGRAM :)
		o = NULL;	// just for code errors, should not happen!
	}
	return output_used;		// END OF BASIC PROGRAM :) Give back the size of output buffer used
}

#endif


static const struct {
	const Uint8	screen_code;
	const char	*text;
} conv_tab_screen[] = {
	{ 0x00, "@"  },
	{ 0x1B, "["  },
	{ 0x1C, "\\" },		// pound symbol, mapped to backslash
	{ 0x1C, "{pound}" },	// pound symbol, alternative representation
	{ 0x1C, "£" },		// pound symbol (UTF8!)
	{ 0x1D, "]" },
	{ 0x1E, "^" },		// up-arrow symbol
	{ 0x1F, "_" },		// left-arrow symbol, mapped to underscore
	{ 0x40, "{dash}" },
	{ 0x5E, "{pi}"   },
	{ 0x93, "{home}" },
	// $80-$9F
	                                        { 0x82, "{uloff}"}, { 0x83, "{stop}" },                     { 0x85, "{wht}"  },                     { 0x87, "{bell}" },
	                    { 0x89, "{ht}"   }, { 0x8A, "{lf}"   }, { 0x8B, "{shen}" }, { 0x8C, "{shdi}" }, { 0x8D, "{ret}"  }, { 0x8E, "{text}" }, { 0x8F, "{flon}" },
	{ 0x90, "{f9}"   }, { 0x91, "{down}" }, { 0x92, "{rvon}" }, { 0x93, "{home}" }, { 0x94, "{del}"  }, { 0x95, "{f10}"  }, { 0x96, "{f11}"  }, { 0x97, "{f12}"  },
	{ 0x98, "{tab}"  }, { 0x99, "{f13}"  }, { 0x9A, "{f14}"  }, { 0x9B, "{esc}"  }, { 0x9C, "{red}"  }, { 0x9D, "{right}"}, { 0x9E, "{grn}"  }, { 0x9F, "{blu}"  },
	// $E0-$DF
	                    { 0xE1, "{orng}" }, { 0xE2, "{ulon}" }, { 0xE3, "{run}"  }, { 0xE4, "{help}" }, { 0xE5, "{f1}"   }, { 0xE6, "{f3}"   }, { 0xE7, "{f5}"   },
	{ 0xE8, "{f7}"   }, { 0xE9, "{f2}"   }, { 0xEA, "{f4}"   }, { 0xEB, "{f6}"   }, { 0xEC, "{f8}"   }, { 0xED, "{sret}" }, { 0xEE, "{gfx}"  }, { 0xEF, "{floff}"},
	{ 0xD0, "{blk}"  }, { 0xD1, "{up}"   }, { 0xD2, "{rvoff}"}, { 0xD3, "{clr}"  }, { 0xD4, "{inst}" }, { 0xD5, "{brn}"  }, { 0xD6, "{lred}" }, { 0xD7, "{gry1}" },
	{ 0xD8, "{gry2}" }, { 0xD9, "{lgrn}" }, { 0xDA, "{lblu}" }, { 0xDB, "{gry3}" }, { 0xDC, "{pur}"  }, { 0xDD, "{left}" }, { 0xDE, "{yel}"  }, { 0xDF, "{cyn}"  },
	{ 0x00, NULL }
};



char *xemu_cbm_screen_to_text ( char *buffer, const int buffer_size, const Uint8 *v, const int cols, const int rows, const int lowercase )
{
	char *t = buffer;
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			if (XEMU_UNLIKELY(t - buffer > buffer_size - 16)) {
				ERROR_WINDOW("Sorry, ASCII converted screen does not fit into the output buffer");
				return NULL;
			}
			Uint8 c = *v++;
			// first, check out translation table for special cases
			for (int a = 0; conv_tab_screen[a].text; a++) {
				if (conv_tab_screen[a].screen_code == c) {
					t += sprintf(t, "%s", conv_tab_screen[a].text);
					goto next_char;
				}
			}
			//const Uint8 inv = c & 0x80;
			//c &= 0x7F;
			if (c >= 0x01 && c <= 0x1A) {	// Capital A-Z (in uppercase mode), a-z (in lower case mode)
				*t++ = c - 1 + (lowercase ? 'a' : 'A');
				continue;
			}
			if (c >= 0x20 && c <= 0x3F) {	// space, various common marks, numbers: at the same place as in ASCII!
				*t++ = c;
				continue;
			}
			if (c >= 0x41 && c <= 0x5A) {	// Gfx chars (in uppercase mode), A-Z (in lower case mode)
				if (lowercase)
					*t++ = c;
				else
					t += sprintf(t, "{%c}", c);
				continue;
			}
			// Missing policy for remaining characters, let's dump with its screen code
			t += sprintf(t, "{$%02X}", c);
			// well yeah, do not say anything ... C does not leave too much choice to continue from a nested loop ...
		next_char:
			continue;
		}
		while (t > buffer && t[-1] == ' ')	// remove trailing spaces
			t--;
		t += sprintf(t, "%s", NL);	// put a newline
	}
	// remove empty lines from the end of our capture
	while (t > buffer && (t[-1] == '\r' || t[-1] == '\n'))
		t--;
	strcpy(t, NL);	// still, a final newline. THIS ALSO CLOSES OUR STRING with '\0'!!!!!
	// remove empty lines from the beginning of our capture
	while (*buffer == '\r' || *buffer == '\n')
		buffer++;
	// return our result!
	return buffer;
}


int xemu_cbm_text_to_screen ( Uint8 *v, const int cols, const int rows, const char *buffer, const int lowercase )
{
	const Uint8 *start = v;
	const Uint8 *end   = v + (cols * rows);
	char ch_prev, ch = 0;
	v += cols;	// do not use the first line, we expect user to have the cursor there, to be safe
	while (*buffer && v < end) {
		ch_prev = ch;
		// first, check out translation table for special cases
		for (int a = 0; conv_tab_screen[a].text; a++) {
			const int l = strlen(conv_tab_screen[a].text);
			if (!strncmp(conv_tab_screen[a].text, buffer, l)) {
				*v++ = conv_tab_screen[a].screen_code;
				buffer += l;
				ch = 0x40;	// something, which is not newline ;)
				goto next_char;
			}
		}
		// fetch next to-be-pasted ASCII character
		ch = *buffer++;
		// Special sequences which are NOT handled by the "conv_tab_screen" loop above
		if (ch == '{') {
			const char *p = strchr(buffer, '}');
			if (!p) {
				*v++ = 0x1B;	// just fake a '[' because ...
				continue;	// ... closing pair of '{' is not found, ignore this character
			}
			const int l = (int)(p - buffer);
			if (l == 1) {		// single char {X} case
				*v++ = *buffer;
			} else if (l == 3 && *buffer == '$') {
				char *e;
				const Uint8 h = (Uint8)strtol(buffer + 1, &e, 16);
				if (e == p)
					*v++ = h;
			}
			buffer = p + 1;		// move to the next ASCII to-be-pasted character after '}'
			continue;
		}
		if (ch == '}') {		// if there is a '}' for whatever reason, translate into ']'
			*v++ = 0x1D;
			continue;
		}
		if (ch == '\n' || ch == '\r') {
			if ((ch_prev == '\n' || ch_prev == '\r') && ch_prev != ch) {
				ch_prev = 0;
				continue;	// \r\n or other multi-ctrl-char sequence for line break
			}
			// new line
			while ((v - start) % cols)
				*v++ = 32;
			continue;
		}
		if (ch == '\t')	{		// space (also TAB is rendered as space for now ...)
			*v++ = 32;
			continue;
		}
		if ((signed char)ch < 32)	// ignore invalid characters (as a signed byte value this is also true for >=128 unsigned ones)
			continue;
		if (ch >= 0x61 && ch <= 0x7A) {	// ASCII small letters
			*v++ = ch - 0x61 + 1;
			continue;
		}
		if (ch >= 0x41 && ch <= 0x5A) {	// ASCII capital letters
			*v++ = lowercase ? ch : ch - 0x41 + 1;
			continue;
		}
		if (ch >= 0x20 && ch <= 0x3F) {	// space, various common marks, numbers: at the same place as in ASCII!
			*v++ = ch;
			continue;
		}
		// The unknown stuff ... Now mark with '@'
		DEBUGPRINT("PASTE: unknown ASCII character: %c (%d)" NL, ch, (unsigned int)ch);
		*v++ = 0;
	next_char:
		continue;
	}
	while (v < end && ((v - start) % cols))
		*v++ = 32;
	return 0;
}
