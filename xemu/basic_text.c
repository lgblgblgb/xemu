/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifdef BASIC_TEXT_SUPPORT

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
