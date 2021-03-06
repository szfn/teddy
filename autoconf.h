#ifndef __AUTOCONF_TEDDY__
#define __AUTOCONF_TEDDY__

#define AUTOCONF_TEDDY "### FONT CONFIGURATION ###\n\
\n\
# Set the font used by default in editors\n\
setcfg main_font \"Arial-11\"\n\
\n\
# Create a monospace font to be used on some editor (see buffer_setup_hook)\n\
set monospaced_font \"Monospace-11\"\n\
\n\
# Set the font used on the box displaying cursor position, at the bottom right corner of the editor window\n\
setcfg posbox_font Arial-8\n\
\n\
### COLOR THEME ###\n\
\n\
# Run the antique theme (see builtin.tcl for how themes are defined)\n\
antique_theme\n\
\n\
# Set to zero if you don't want to underline links\n\
setcfg underline_links 1\n\
\n\
### MISC OPTIONS ###\n\
\n\
# When focus_follows_mouse is set to 1 the focus will be moved to the editor under the mouse pointer\n\
setcfg focus_follows_mouse 1\n\
\n\
# When autoindent is set to 1 pressing return will copy the indentation of the current line on the new line\n\
setcfg autoindent 1\n\
\n\
# warp_mouse can be:\n\
# - no-warp: mouse position is not warped when focus is moved to a different editor\n\
# - warp-to-top: mouse is warped to the top of the editor getting focus\n\
# - warp-to-cursor: mouse is warped to the current line of the editor getting focus\n\
# no-warp is not a recommended option if focus_follows_mouse is set to 1\n\
setcfg warp_mouse warp-to-cursor\n\
\n\
# interactive_search_case_sensitive controls case sensitivity for the interactive search function (activated by 'search' command):\n\
# 0 makes interactive search case insensitive, 1 makes it case sensitive, 2 (default) makes it case sensitive only when the search text contains upcase letters\n\
setcfg interactive_search_case_sensitive 2\n\
\n\
# When autowrap is set to 1 lines that don't fit in the editor will be wrapped around (this is a soft-wrap, it is not saved)\n\
# When autowrap is set to 0 an horizontal scrollbar is shown when necessary\n\
setcfg autowrap 1\n\
\n\
# Set the width of a tabulation as number of characters (on variable width fonts the width of a M character will be used)\n\
setcfg tab_width 4\n\
\n\
# Set the width of indent spaces to be bigger so that $tab_width spaces at the beginning of the line align with a tab, even when using a proportional font\n\
setcfg largeindent 1\n\
\n\
# When oldarrow is set to 1 the arrow cursor is used for editors\n\
setcfg oldarrow 0\n\
\n\
# When tags_discard_lineno is set to 1 lines of the TAGS file pointing to a specific line are discarded\n\
setcfg tags_discard_lineno 1\n\
\n\
# When autoreload is set to 1 unchanged buffer associated to a file that changed on disk are autoreloaded from disk\n\
setcfg autoreload 1\n\
\n\
# When autocompl_popup is set to 1 the autocompletion window appears automatically\n\
setcfg autocompl_popup 1\n\
\n\
### KEYBINDINGS ###\n\
\n\
# Ctrl-c copy to clipboard\n\
bindkey Ctrl-c {cb put [c]}\n\
\n\
# Ctrl-v paste from clipboard (bindent pasteq equalizes indentation levels)\n\
bindkey Ctrl-v {pasteq [cb get]}\n\
\n\
# Ctrl-x cuts from clipboard\n\
bindkey Ctrl-x {cb put [c]; c \"\"}\n\
\n\
# Ctrl-y pastes from primary selection (this is like the middle mouse button)\n\
bindkey Ctrl-y {pasteq [cb pget]}\n\
\n\
# Ctrl-z undoes last action\n\
bindkey Ctrl-z undo\n\
bindkey Ctrl-Z {undo redo}\n\
\n\
# Ctrl-s saves (calls teddy::spaceman to delete extra spaces at the end of lines)\n\
bindkey Ctrl-s {teddy::spaceman; buffer save}\n\
\n\
# Ctrl-f starts interactive search (also known as \"search as you type\" or \"incremental search\")\n\
bindkey Ctrl-f search\n\
\n\
# Ctrl-g starts interactive search with the last thing searched\n\
bindkey Ctrl-g {search [teddy::history search 1]}\n\
\n\
# Ctrl-a goes to the first non-whitespace character of the line\n\
bindkey Ctrl-a {m +0:^1}\n\
\n\
# Ctrl-e goes to the last character of the line\n\
bindkey Ctrl-e {m +0:$ }\n\
\n\
# Ctrl-p in a +bg buffer replaces the current user input with the last sent user input\n\
bindkey Ctrl-p teddy::previnput\n\
\n\
# Ctrl-left / Ctrl-right moves one word forward/backward\n\
bindkey Ctrl-Left {m +0:-1w}\n\
bindkey Ctrl-Right {m +0:+1w}\n\
\n\
# Ctrl-backspace deletes word preceding cursor\n\
bindkey Ctrl-Backspace {m +0:-1w +0:+0; c \"\"}\n\
\n\
# Ctrl-home/Ctrl-end moves to the first/last line\n\
bindkey Ctrl-Home {m 1:0}\n\
bindkey Ctrl-End {m $:$}\n\
\n\
# Ctrl-l starts interactive file search\n\
bindkey Ctrl-l iopen\n\
\n\
# Ctrl-k removes current line and saves it to the clipboard, multiple consecutive executions append removed lines to the clipboard\n\
bindkey Ctrl-k kill_line\n\
\n\
# Ctrl-. / Ctrl-, add/remove one tab at the beginning of every line of the selected text\n\
bindkey Ctrl-, {| Bindent -}\n\
bindkey Ctrl-. {| Bindent +}\n\
\n\
# Ctrl-Delete kills attached process (if any)\n\
bindkey Ctrl-Delete kill\n\
\n\
# Ctrl-b jumps to the previous cursor positions, Ctrl-B undoes one Ctrl-b\n\
bindkey Ctrl-b { buffer jumpring prev }\n\
bindkey Ctrl-B { buffer jumpring next }\n\
\n\
### OPEN COMMANDS ###\n\
\n\
teddy::plumb addrule {\n\
	type is link\n\
	bufname match ^\\+man\n\
} do {\n\
	man $line $linkfile\n\
}\n\
\n\
teddy::plumb addrule {\n\
	text match ^@\n\
} do {\n\
	if {[teddy::colof [lindex [m] 1]] > 3} {\n\
		error \"Wrong position\"\n\
	}\n\
\n\
	m +:3 +:$\n\
	shelloreval [c]\n\
}\n\
\n\
teddy::plumb addrule { } do {\n\
	set b [buffer open [teddy_intl::file_directory] $linkfile]\n\
	if {$line ne \"\"} {\n\
		if {$col eq \"\"} { set col 1 }\n\
		if {[string index $line 0] eq \"/\"} {\n\
			# it's a regex\n\
			set line [string range $line 1 end-1]\n\
			buffer eval $b { m [s -k $line] }\n\
		} else {\n\
			# it's a line number otherwise\n\
			buffer eval $b { m nil $line:$col }\n\
		}\n\
	}\n\
	buffer focus $b\n\
}\n\
\n\
teddy::plumb addrule { } do { shell $teddy::open_cmd $linkfile }\n\
\n\
\n\
### BUFFER HOOKS ###\n\
\n\
# This procedure gets called every time a new buffer is created, it allows you to do some customization of how buffers are displayed\n\
proc buffer_setup_hook {name} {\n\
	global monospaced_font\n\
	if { [string range $name 0 2] == \"+bg\" } {\n\
		# For buffers containing the output of external commands we disable autowrapping, …\n\
		setcfg autowrap 0\n\
		# …set a tab width of 8, ...\n\
		setcfg tab_width 8\n\
		# …and set a monospaced font.\n\
		setcfg main_font $monospaced_font\n\
		buffer lexy\n\
	} elseif { [string range $name 0 3] == \"+man\" } {\n\
		# For buffers containing manpages we disable autowrapping and set a monospaced font\n\
		setcfg autowrap 0\n\
		setcfg main_font $monospaced_font\n\
		buffer lexy\n\
	} else {\n\
		# Otherwise we accept default\n\
	}\n\
}\n\
\n\
# This procedure gets called after a new buffer is loaded\n\
proc buffer_loaded_hook {name} { shell @b0 Bindent guess }\n\
\n\
# This command is executed before a buffer is saved\n\
proc buffer_save_hook {} {\n\
}\n\
\n\
### OTHER ###\n\
\n\
# Selects recently typed text\n\
bindkey Pause {m [undo region after]}\n\
\n\
"
#endif
