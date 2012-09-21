#ifndef __AUTOCONF_TEDDY__
#define __AUTOCONF_TEDDY__

#define AUTOCONF_TEDDY "setcfg main_font \"Arial-11\"\n\
set monospaced_font \"Monospace-11\"\n\
setcfg posbox_font Arial-8\n\
\n\
antique_theme\n\
\n\
setcfg focus_follows_mouse 1\n\
\n\
bindkey Ctrl-c {cb put [c]; m nil +0:+0}\n\
bindkey Ctrl-v {c [cb get]}\n\
bindkey Ctrl-x {cb put [c]; c \"\"}\n\
bindkey Ctrl-y {c [cb pget]}\n\
\n\
bindkey Ctrl-u undo\n\
\n\
bindkey Ctrl-s {buffer save}\n\
\n\
bindkey Ctrl-f search\n\
bindkey Ctrl-g {search [teddyhistory search 1]}\n\
\n\
bindkey Ctrl-a {m +0:^1}\n\
bindkey Ctrl-e {m +0:$ }\n\
bindkey Ctrl-Left {m +0:-1w}\n\
bindkey Ctrl-Right {m +0:+1w}\n\
bindkey Ctrl-Home {m 1:0}\n\
bindkey Ctrl-End {m $:0}\n\
\n\
bindkey Ctrl-l iopen\n\
\n\
bindkey Ctrl-Backspace {m +0:-1w +0:+0; c \"\"}\n\
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
	global monospaced_font\n\
	if { [string range $name 0 2] == \"+bg\" } {\n\
		setcfg autowrap 0\n\
		setcfg tab_width 8\n\
		setcfg main_font $monospaced_font\n\
	} elseif { [string range $name 0 3] == \"+man\" } {\n\
		setcfg autowrap 0\n\
		setcfg main_font $monospaced_font\n\
	} else {\n\
		return { }\n\
	}\n\
}\n\
"
#endif
