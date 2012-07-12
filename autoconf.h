#ifndef __AUTOCONF_TEDDY__
#define __AUTOCONF_TEDDY__

#define AUTOCONF_TEDDY "setcfg main_font \"Arial-11\"\n\
setcfg posbox_font Arial-8\n\
\n\
setcfg focus_follows_mouse 1\n\
\n\
setcfg editor_bg_color [rgbcolor \"antique white\"]\n\
setcfg border_color [rgbcolor black]\n\
\n\
setcfg editor_fg_color [rgbcolor black]\n\
\n\
setcfg posbox_border_color 0\n\
setcfg posbox_bg_color 15654274\n\
setcfg posbox_fg_color 0\n\
\n\
lexycfg nothing [rgbcolor black]\n\
lexycfg keyword [rgbcolor \"midnight blue\"]\n\
lexycfg comment [rgbcolor \"dark green\"]\n\
lexycfg string [rgbcolor \"dark brown\"]\n\
lexycfg id [rgbcolor black]\n\
lexycfg literal [rgbcolor \"dark brown\"]\n\
\n\
bindkey Ctrl-Space mark\n\
bindkey Ctrl-Shift-Space {mark lines}\n\
\n\
bindkey Ctrl-c {cb put [c]; mark stop}\n\
bindkey Ctrl-v {c [cb get]}\n\
bindkey Ctrl-x {cb put [c]; c \"\"}\n\
bindkey Ctrl-y {c [cb pget]}\n\
\n\
bindkey Ctrl-u undo\n\
\n\
bindkey Ctrl-s save\n\
bindkey Ctrl-b bufman\n\
\n\
bindkey Ctrl-f search\n\
bindkey Ctrl-g {search [teddyhistory search 1]}\n\
\n\
bindkey Ctrl-a gohome\n\
bindkey Ctrl-e {go $}\n\
bindkey Ctrl-Left {move prev wnwa}\n\
bindkey Ctrl-Right {move next wnwa}\n\
bindkey Ctrl-Home {go 1}\n\
bindkey Ctrl-End {go -1}\n\
\n\
bindkey Ctrl-l {interactarg go}\n\
\n\
bindkey Ctrl-Backspace {mark start; move prev wnwa; cb pput [c]; c \"\"}\n\
bindkey Ctrl-k kill_line\n\
\n\
bindkey Ctrl-, {bindent decr \"\\t\"}\n\
bindkey Ctrl-. {bindent incr \"\\t\"}\n\
\n\
proc grep {args} {\n\
	unknown grep -n {*}$args\n\
}\n\
\n\
proc buffer_setup_hook {name} {\n\
	if { [string range $name 0 2] == \"+bg\" } {\n\
		return { hscroll monospaced tabwidth:8 }\n\
	} elseif { [string range $name 0 3] == \"+man\" } {\n\
		return { hscroll monospaced }\n\
	} elseif { [string range $name 0 6] == \"+dirrec\"} {\n\
		return { hscroll monospaced tabwidth:8 }\n\
	} else {\n\
		return { }\n\
	}\n\
}\n\
"
#endif
