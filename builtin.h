#ifndef __BUILTIN_TCL__
#define __BUILTIN_TCL__

#define BUILTIN_TCL_CODE "#### UTILITIES ##################################################################\n\
# Utility commands the user could find interesting\n\
\n\
proc kill_line {} {\n\
   # this is the same as m +:1 +1:1 except for the last line of the buffer (where it will not select anything)\n\
   m +0:1 +:$\n\
   m +:+ +:+1\n\
   if {[undo tag] eq \"kill_line\"} {\n\
      undo fusenext\n\
   }\n\
   c \"\"\n\
   undo tag kill_line\n\
   cb put [undo get before]\n\
}\n\
\n\
proc wander {body} {\n\
	set saved_mark [m]\n\
	switch -exact [catch {uplevel 1 $body} out] {\n\
		1 {\n\
			m $saved_mark\n\
			error $out\n\
		}\n\
		default {\n\
			m $saved_mark\n\
			return $out\n\
		}\n\
	}\n\
}\n\
\n\
namespace eval bindent {\n\
	proc get_indentchar {} {\n\
		set indentchar [buffer propget [buffer current] indentchar]\n\
		if {$indentchar eq \"\" } {\n\
			return \"\\t\"\n\
		} else {\n\
			return $indentchar\n\
		}\n\
	}\n\
\n\
	# Adds an indentation level\n\
	namespace export incr\n\
	proc incr {} {\n\
		m line\n\
		set indentchar [get_indentchar]\n\
		set sm [m]\n\
		s {^.*$} { c \"$indentchar[c]\" }\n\
		m \"[teddy::lineof [lindex $sm 0]]:1\" \"[teddy::lineof [lindex $sm 1]]:$\"\n\
	}\n\
\n\
	# Removes an indentation level\n\
	namespace export descr\n\
	proc decr {} {\n\
		m line\n\
		set indentchar [get_indentchar]\n\
		set sm [m]\n\
		s \"^$indentchar\" {\n\
			c \"\"\n\
			m +:$\n\
		}\n\
		m \"[teddy::lineof [lindex $sm 0]]:1\" \"[teddy::lineof [lindex $sm 1]]:$\"\n\
	}\n\
\n\
	# Guesses indentation, saves the guess as the indentchar buffer property\n\
	namespace export guess\n\
	proc guess {} {\n\
		set count 0\n\
		set tabs 0\n\
		set spaces 0\n\
		forlines {\n\
			if {[::incr count] > 200} { break }\n\
			set text [c]\n\
			if {[string length $text] > 200} { continue }\n\
\n\
			if {[regexp {^(\\s+)} $text -> s]} {\n\
				if {[string first \"\\t\" $s] >= 0} {\n\
					::incr tabs\n\
				} elseif {[string length $s] > 1} {\n\
					::incr spaces\n\
				}\n\
			}\n\
		}\n\
\n\
		if {$spaces > $tabs} {\n\
			buffer propset [buffer current] indentchar \"  \"\n\
		} else {\n\
			buffer propset [buffer current] indentchar \"\\t\"\n\
		}\n\
	}\n\
\n\
	proc get_current_line_indent {} {\n\
		wander {\n\
			m [s -l1k {^(?: |\\t)+}]\n\
			return [c]\n\
		}\n\
	}\n\
\n\
	# Equalizes indentation for paste\n\
	namespace export pasteq\n\
	proc pasteq {text} {\n\
		if {[lindex [m] 0] ne \"nil\"} { c $text; return }\n\
\n\
		set cursor [m]\n\
		m +:1 +:$\n\
		if {![regexp {^(?: |\\t)*$} [c]]} {\n\
			m {*}$cursor\n\
			c $text\n\
			return\n\
		}\n\
\n\
		set dst_indent [get_current_line_indent]\n\
		buffer eval temp {\n\
			c $text\n\
			m 1:1\n\
			set src_indent [get_current_line_indent]\n\
			if {$src_indent ne \"\" || $dst_indent ne \"\"} {\n\
				s \"^$src_indent\" {\n\
					c $dst_indent\n\
					m +:$\n\
				}\n\
			}\n\
			m all\n\
			set r [c]\n\
		}\n\
		m +:1 +:$\n\
		c $r\n\
	}\n\
\n\
	namespace ensemble create -subcommands {incr decr guess pasteq}\n\
}\n\
\n\
proc man {args} {\n\
	shell [buffer make \"+man/$args+\"] man -a $args\n\
}\n\
\n\
proc clear {} {\n\
	m all; c \"\"\n\
}\n\
\n\
proc forlines {args} {\n\
	if {[llength $args] == 1} {\n\
		set pattern {^.*$}\n\
		set body [lindex $args 0]\n\
	} elseif {[llength $args] == 2} {\n\
		set pattern [lindex $args 0]\n\
		set body [lindex $args 1]\n\
	} else {\n\
		error \"Wrong number of arguments to forlines [llength $args] expected 1 or 2\"\n\
	}\n\
\n\
	wander {\n\
		m 1:1\n\
		s $pattern {\n\
			m line\n\
			uplevel 1 $body\n\
		}\n\
	}\n\
}\n\
\n\
proc ss {args} {\n\
	buffer eval temp {\n\
		c [lindex $args 0]\n\
		m 1:1\n\
		s {*}[lrange $args 1 end]\n\
		m all\n\
		set text [c]\n\
	}\n\
	return $text\n\
}\n\
\n\
proc O {args} {\n\
	teddy::open {*}$args\n\
}\n\
\n\
proc P {args} {\n\
	if {[string first \"+bg/\" [buffer name]] == 0} {\n\
		buffer rename \"+bg![string range [buffer name] 4 end]\"\n\
	} elseif {[string first \"+bg!\" [buffer name]] == 0} {\n\
		buffer rename \"+bg/[string range [buffer name] 4 end]\"\n\
	}\n\
}\n\
\n\
namespace eval teddy {\n\
	namespace export open\n\
	proc open {path} {\n\
		if {![file exists $path]} {\n\
			catch {shellsync \"\" touch $path}\n\
		}\n\
		buffer open $path\n\
	}\n\
\n\
	# options passed to ls to display a directory\n\
	namespace export ls_options\n\
	set ls_options {-F -1 --group-directories-first}\n\
\n\
	# name of command to execute when opening a url or a binary file\n\
	# suggested xdg-open/open/plumb/...\n\
	namespace export open_cmd\n\
	set open_cmd xdg-open\n\
\n\
	# returns current line number\n\
	namespace export lineof\n\
	proc lineof {x} {\n\
		return [lindex [split $x \":\"] 0]\n\
	}\n\
\n\
	namespace export colof\n\
	proc colof {x} {\n\
		return [lindex [split $x \":\"] 1]\n\
	}\n\
\n\
	# reads a file from disk\n\
	namespace export slurp\n\
	proc slurp {path} {\n\
		set thefile [::open $path]\n\
		fconfigure $thefile -translation binary\n\
		set r [read $thefile]\n\
		close $thefile\n\
		return $r\n\
	}\n\
\n\
	# deletes irrelevant spaces from the end of lines, and empty lines from the end of file\n\
	namespace export spaceman\n\
	proc spaceman {} {\n\
		set saved [m]\n\
		# delete empty spaces from the end of lines\n\
		m nil 1:1\n\
		s {(?: |\\t)+$} {\n\
			if {[teddy::lineof [lindex [m] 1]] ne [teddy::lineof [lindex $saved 1]]} {\n\
				c \"\"\n\
			}\n\
		}\n\
		m $saved\n\
	}\n\
\n\
	# on a +bg frame selects the user's input\n\
	namespace export select_input\n\
	proc select_input {} {\n\
		m +:1\n\
		m {*}[s -line \"\\05\"]\n\
		if {[lindex [m] 0] eq \"nil\"} { return }\n\
		m +:+1 +:$\n\
	}\n\
\n\
	# replaces the current line of input for a job with the last line of input that was sent to the job\n\
	# it is meant to be bound to ctrl-p, a better version could be written to cycle through the history of input sent to the job\n\
	proc previnput {} {\n\
		select_input\n\
		c [history input 1]\n\
	}\n\
}\n\
\n\
#### THEMES ##################################################################\n\
# Color themes for teddy\n\
\n\
proc antique_theme {} {\n\
	setcfg -global editor_bg_color [rgbcolor \"white\"]\n\
	setcfg -global border_color [rgbcolor black]\n\
\n\
	setcfg -global editor_fg_color [rgbcolor black]\n\
	setcfg -global posbox_border_color 0\n\
	setcfg -global posbox_bg_color 15654274\n\
	setcfg -global posbox_fg_color 0\n\
	setcfg -global scrollbar_bg_color [rgbcolor black]\n\
\n\
	setcfg -global lexy_nothing [rgbcolor black]\n\
	setcfg -global lexy_keyword [rgbcolor \"midnight blue\"]\n\
	setcfg -global lexy_comment [rgbcolor \"dark green\"]\n\
	setcfg -global lexy_string [rgbcolor \"saddle brown\"]\n\
	setcfg -global lexy_id [rgbcolor black]\n\
	setcfg -global lexy_literal [rgbcolor \"saddle brown\"]\n\
	setcfg -global lexy_file [rgbcolor \"midnight blue\"]\n\
\n\
	setcfg -global editor_sel_invert 1\n\
\n\
	buffer lexy\n\
}\n\
\n\
proc acme_theme {} {\n\
	setcfg -global editor_bg_color [rgbcolor 255 255 234]\n\
	setcfg -global border_color [rgbcolor black]\n\
	setcfg -global editor_bg_cursorline [rgbcolor 232 232 212]\n\
	setcfg -global scrollbar_bg_color [rgbcolor 153 153 76]\n\
\n\
	setcfg -global editor_fg_color [rgbcolor black]\n\
\n\
	setcfg -global tag_bg_color [rgbcolor 234 255 255]\n\
	setcfg -global tag_fg_color [rgbcolor black]\n\
\n\
	setcfg -global posbox_border_color 0\n\
	setcfg -global posbox_bg_color 15654274\n\
	setcfg -global posbox_fg_color 0\n\
\n\
	setcfg -global lexy_nothing [rgbcolor black]\n\
	setcfg -global lexy_keyword [rgbcolor black]\n\
	setcfg -global lexy_comment [rgbcolor \"dark green\"]\n\
	setcfg -global lexy_string [rgbcolor \"saddle brown\"]\n\
	setcfg -global lexy_id [rgbcolor black]\n\
	setcfg -global lexy_literal [rgbcolor black]\n\
	setcfg -global lexy_file [rgbcolor \"midnight blue\"]\n\
\n\
	setcfg -global editor_sel_color [rgbcolor 238 238 158]\n\
	setcfg -global editor_sel_invert 0\n\
\n\
	buffer lexy\n\
}\n\
\n\
proc zenburn_theme {simple} {\n\
	setcfg -global editor_bg_color [rgbcolor 12 12 12]\n\
	setcfg -global border_color [rgbcolor white]\n\
	setcfg -global editor_bg_cursorline [rgbcolor 31 31 31]\n\
	setcfg -global editor_sel_color [rgbcolor 47 47 47]\n\
\n\
	setcfg -global editor_fg_color [rgbcolor white]\n\
	setcfg -global scrollbar_bg_color [rgbcolor 46 51 48]\n\
\n\
	setcfg -global tag_bg_color [rgbcolor 46 51 48]\n\
	setcfg -global tag_fg_color [rgbcolor 133 172 141]\n\
\n\
	setcfg -global posbox_border_color 0\n\
	setcfg -global posbox_bg_color 15654274\n\
	setcfg -global posbox_fg_color 0\n\
\n\
	set idc [rgbcolor 197 197 183 ]\n\
\n\
	if {$simple} {\n\
		setcfg -global lexy_nothing [rgbcolor white]\n\
		setcfg -global lexy_keyword [rgbcolor white]\n\
		setcfg -global lexy_comment [rgbcolor 127 159 127]\n\
		setcfg -global lexy_string [rgbcolor 204 147 147]\n\
		setcfg -global lexy_id [rgbcolor white]\n\
		setcfg -global lexy_literal [rgbcolor white]\n\
		setcfg -global lexy_file [rgbcolor cyan]\n\
	} else {\n\
		setcfg -global lexy_nothing [rgbcolor white]\n\
		setcfg -global lexy_keyword [rgbcolor 240 223 175]\n\
		setcfg -global lexy_comment [rgbcolor 127 159 127]\n\
		setcfg -global lexy_string [rgbcolor 204 147 147]\n\
		setcfg -global lexy_id $idc\n\
		setcfg -global lexy_literal [rgbcolor 220 163 163]\n\
		setcfg -global lexy_file [rgbcolor cyan]\n\
	}\n\
\n\
	setcfg -global editor_sel_invert 0\n\
\n\
	buffer lexy\n\
}\n\
\n\
proc solarized_theme {} {\n\
	set base03    [rgbcolor 0  43  54]\n\
	set base02    [rgbcolor 7  54  66]\n\
	set base01    [rgbcolor 88 110 117]\n\
	set base00    [rgbcolor 101 123 131]\n\
	set base0     [rgbcolor 131 148 150]\n\
	set base1     [rgbcolor 147 161 161]\n\
	set base2     [rgbcolor 238 232 213]\n\
	set base3 [rgbcolor 253 246 227]\n\
	set yellow    [rgbcolor 181 137   0]\n\
	set orange    [rgbcolor 203  75  22]\n\
	set red       [rgbcolor 220  50  47]\n\
	set magenta   [rgbcolor 211  54 130]\n\
	set violet    [rgbcolor 108 113 196]\n\
	set blue      [rgbcolor 38 139 210]\n\
	set cyan      [rgbcolor 42 161 152]\n\
	set green [rgbcolor 133 153   0]\n\
\n\
	setcfg -global editor_bg_color $base03\n\
	setcfg -global border_color $base0\n\
	setcfg -global editor_bg_cursorline $base02\n\
	setcfg -global editor_sel_color $base01\n\
	setcfg -global scrollbar_bg_color $base1\n\
\n\
	setcfg -global editor_fg_color $base2\n\
\n\
	setcfg -global tag_bg_color $base01\n\
	setcfg -global tag_fg_color $base2\n\
\n\
	setcfg -global posbox_border_color 0\n\
	setcfg -global posbox_bg_color 15654274\n\
	setcfg -global posbox_fg_color 0\n\
\n\
	setcfg -global lexy_nothing $base2\n\
	setcfg -global lexy_keyword $green\n\
	setcfg -global lexy_comment $yellow\n\
	setcfg -global lexy_string $cyan\n\
	setcfg -global lexy_id $base2\n\
	setcfg -global lexy_literal $cyan\n\
	setcfg -global lexy_file [rgbcolor cyan]\n\
\n\
	setcfg -global editor_sel_invert 0\n\
\n\
	buffer lexy\n\
}\n\
\n\
proc solarized_simple_theme {} {\n\
	set base03    [rgbcolor 0  43  54]\n\
	set base02    [rgbcolor 7  54  66]\n\
	set base01    [rgbcolor 88 110 117]\n\
	set base00    [rgbcolor 101 123 131]\n\
	set base0     [rgbcolor 131 148 150]\n\
	set base1     [rgbcolor 147 161 161]\n\
	set base2     [rgbcolor 238 232 213]\n\
	set base3 [rgbcolor 253 246 227]\n\
	set yellow    [rgbcolor 181 137   0]\n\
	set orange    [rgbcolor 203  75  22]\n\
	set red       [rgbcolor 220  50  47]\n\
	set magenta   [rgbcolor 211  54 130]\n\
	set violet    [rgbcolor 108 113 196]\n\
	set blue      [rgbcolor 38 139 210]\n\
	set cyan      [rgbcolor 42 161 152]\n\
	set green [rgbcolor 133 153   0]\n\
\n\
	setcfg -global editor_bg_color $base03\n\
	setcfg -global border_color $base0\n\
	setcfg -global editor_bg_cursorline $base02\n\
	setcfg -global editor_sel_color $base01\n\
	setcfg -global scrollbar_bg_color $base1\n\
\n\
	setcfg -global editor_fg_color $base2\n\
\n\
	setcfg -global tag_bg_color $base01\n\
	setcfg -global tag_fg_color $base2\n\
\n\
	setcfg -global posbox_border_color 0\n\
	setcfg -global posbox_bg_color 15654274\n\
	setcfg -global posbox_fg_color 0\n\
\n\
	setcfg -global lexy_nothing $base2\n\
	setcfg -global lexy_keyword $base2\n\
	setcfg -global lexy_comment $green\n\
	setcfg -global lexy_string $cyan\n\
	setcfg -global lexy_id $base2\n\
	setcfg -global lexy_literal $base2\n\
	setcfg -global lexy_file $blue\n\
\n\
	setcfg -global editor_sel_invert 0\n\
\n\
	buffer lexy\n\
}\n\
\n\
### IMPLEMENTATION OF USER COMMANDS #######################################\n\
# Implementations of commands useful to the user\n\
\n\
proc lexydef {name args} {\n\
	for {set i 0} {$i < [llength $args]} {set i [expr $i + 2]} {\n\
		set start_state [lindex $args $i]\n\
		set transitions [lindex $args [expr $i + 1]]\n\
		for {set j 0} {$j < [llength $transitions]} {set j [expr $j + 3]} {\n\
			set match_kind [lindex $transitions $j]\n\
			set pattern [lindex $transitions [expr $j + 1]]\n\
			set state_and_token_type [split [lindex $transitions [expr $j + 2]] :]\n\
\n\
			if {[llength $state_and_token_type] > 1} {\n\
				set next_state [lindex $state_and_token_type 0]\n\
				if {[llength [split $next_state /]] <= 1} {\n\
					set next_state \"$name/$next_state\"\n\
				}\n\
				set type [lindex $state_and_token_type 1]\n\
			} else {\n\
				set next_state \"$name/$start_state\"\n\
				set type [lindex $state_and_token_type 0]\n\
			}\n\
\n\
			lexydef-append \"$name/$start_state\" $match_kind $pattern $next_state $type\n\
		}\n\
	}\n\
}\n\
\n\
proc unknown {args} {\n\
   if {[string index [lindex $args 0] 0] eq \"|\"} {\n\
      lset args 0 [string range [lindex $args 0] 1 end]\n\
      c [shellsync [c] {*}$args]\n\
   } else {\n\
      # normal unknown code\n\
\n\
      if {[teddy_intl::inpath [lindex $args 0]]} {\n\
          shell {*}$args\n\
	  } else {\n\
	      error \"Unknown command [lindex $args 0]\"\n\
	  }\n\
   }\n\
}\n\
\n\
#### INTERNAL COMMANDS #####################################################\n\
# Commands called internally by teddy (teddy_intl namespace)\n\
\n\
namespace eval teddy_intl {\n\
	namespace export iopen_search\n\
	proc iopen_search {z} {\n\
		if {$z eq \"\"} { return }\n\
		set k [s -literal -get $z]\n\
		#puts \"Searching <$z> -> <$k>\"\n\
		if {[lindex $k 0] ne \"nil\"} {\n\
			m nil [lindex $k 0]\n\
		}\n\
	}\n\
\n\
	# Returns the base directory of pwf or pwd for artificial buffers\n\
	namespace export file_directory\n\
	proc file_directory {} {\n\
		if {[string index [pwf] 0] eq \"+\"} {\n\
			return [pwd]\n\
		} else {\n\
			set r [pwf]\n\
			set last_slash [string last / $r]\n\
			if {$last_slash < 0} { return [pwd] }\n\
			return [string range $r 0 $last_slash]\n\
		}\n\
	}\n\
\n\
	namespace export link_open\n\
	proc link_open {islink text} {\n\
		if {$islink} {\n\
			set r [lexy-token . $text]\n\
		} else {\n\
			set r [lexy-token filesearch/0 $text]\n\
		}\n\
\n\
		set link_text [lindex $r 1]\n\
		if {[lindex $r 0] eq \"nothing\"} {\n\
			set link_text $text\n\
		}\n\
\n\
		# if it contains a colon assume it's a URL already and don't mess with it\n\
		if {![regexp \":\" $link_text]} {\n\
			if {[string index $link_text 0] ne \"/\"} {\n\
				set link_text \"[teddy_intl::file_directory]/$link_text\"\n\
			}\n\
		}\n\
\n\
		set b [buffer open $link_text]\n\
\n\
		if {$b eq \"\"} {\n\
			$teddy::open_cmd $link_text\n\
		} else {\n\
			set line [lindex $r 2]\n\
			set col [lindex $r 3]\n\
\n\
			if {$line ne \"\"} {\n\
				if {$col eq \"\"} { set col 1 }\n\
				if {[string index $line 0] eq \"/\"} {\n\
					# it's a regex\n\
					set line [string range $line 1 end-1]\n\
					buffer eval $b { m [s $line] }\n\
				} else {\n\
					# it's a line number otherwise\n\
					buffer eval $b { m nil $line:$col }\n\
				}\n\
			}\n\
			buffer focus $b\n\
		}\n\
	}\n\
\n\
	namespace export guide_open\n\
	proc guide_open {islink text} {\n\
		if {$text ne \"@\"} {\n\
			return [link_open $islink $text]\n\
		}\n\
\n\
		if {[teddy::colof [lindex [m] 1]] > 3} {\n\
			return [link_open $islink $text]\n\
		}\n\
\n\
		m +:3 +:$\n\
		eval [c]\n\
	}\n\
\n\
	namespace export man_link_open\n\
	proc man_link_open {islink text} {\n\
		if {!$islink} { return }\n\
\n\
		set r [lexy-token . $text]\n\
		if {[lindex $r 2] eq \"\"} { return }\n\
\n\
		man [lindex $r 2] [lindex $r 1]\n\
	}\n\
\n\
	namespace export tags_search_menu_command\n\
	proc tags_search_menu_command {text} {\n\
		set tagout [teddy::tags $text]\n\
		if {[llength $tagout] > 1} {\n\
			set b [buffer make +tags/$text]\n\
			buffer eval $b {\n\
				foreach x $tagout {\n\
					c \"$x\\n\"\n\
				}\n\
			}\n\
		} else {\n\
			tags_link_open 0 [lindex $tagout 0]\n\
		}\n\
	}\n\
\n\
	namespace export tags_link_open\n\
	proc tags_link_open {islink text} {\n\
		set r [lexy-token tagsearch/0 $text]\n\
\n\
		set b [buffer open [lindex $r 1]]\n\
\n\
		if {[lindex $r 2] ne \"\"} {\n\
			buffer eval $b {\n\
				m nil 1:1\n\
				m {*}[s -literal [lindex $r 2]]\n\
			}\n\
		}\n\
\n\
		buffer focus $b\n\
	}\n\
\n\
	namespace export dir\n\
	proc dir {directory} {\n\
	 	if {[string index $directory end] != \"/\"} {\n\
	 		set directory \"$directory/\"\n\
	 	}\n\
		set b [buffer make $directory]\n\
		buffer eval $b {\n\
			m all\n\
			c [shellsync \"\" ls {*}$teddy::ls_options $directory ]\n\
			m nil 1:1\n\
		}\n\
	}\n\
\n\
	namespace export loadhistory\n\
	proc loadhistory {} {\n\
		if [info exists ::env(XDG_CONFIG_HOME)] {\n\
			set histf $::env(XDG_CONFIG_HOME)/teddy/teddy_history\n\
		} else {\n\
			set histf $::env(HOME)/.config/teddy/teddy_history\n\
		}\n\
\n\
		if {[catch {set f [open $histf RDONLY]} err]} {\n\
			puts \"No history\"\n\
			return\n\
		}\n\
\n\
		while {[gets $f line] >= 0} {\n\
			set line [split $line \"\\t\"]\n\
			teddy::history cmd add [lindex $line 0] [lindex $line 1] [lindex $line 2]\n\
		}\n\
		close $f\n\
	}\n\
\n\
	namespace export loadsession\n\
	proc loadsession {sessionfile} {\n\
		eval [teddy::slurp $sessionfile]\n\
	}\n\
\n\
	namespace export savesession_mitem\n\
	proc savesession_mitem {} {\n\
		set b [buffer make +sessions+]\n\
		buffer eval $b {\n\
			c \"Saving the session will let you restore the state of this window after you close it.\\n\"\n\
			c \"To save this session set the session name in the following line and Eval it:\\n\\n\"\n\
			c \"teddy::session tie session-name\"\n\
		}\n\
	}\n\
\n\
	namespace export loadsession_mitem\n\
	proc loadsession_mitem {} {\n\
		set b [buffer make +sessions+]\n\
		buffer eval $b {\n\
			c \"Pick a session to load by evaluating one of this lines:\\n\\n\"\n\
			set count 0\n\
			set dir [teddy::session directory none]\n\
			foreach filename [glob -directory $dir  *] {\n\
				if {![regexp {\\.session$} $filename]} { continue }\n\
				set sessioname $filename\n\
\n\
				set sessioname [regsub {\\.session$} $sessioname \"\"]\n\
				set sessioname [ string range $sessioname [expr [string length $dir] + 1] end]\n\
\n\
				c \"teddy::session load $sessioname\\n\"\n\
\n\
				set sessioninfo [teddy::slurp $filename]\n\
\n\
				if {![regexp -lineanchor {^cd (.*?)$} $sessioninfo -> sessiondir]} {\n\
					set sessiondir \"???\"\n\
				}\n\
\n\
				if {![regexp {^# (\\d+)} $sessioninfo -> sessionts]} {\n\
					set sessionts \"0\"\n\
				}\n\
\n\
				set sessiondate [shellsync \"\" date -d \"@$sessionts\" +%Y-%m-%d]\n\
\n\
				c \"# in $sessiondir last access: $sessiondate\\n\"\n\
\n\
				incr count\n\
			}\n\
\n\
			if {$count == 0} { c \"No session found\\n\" }\n\
\n\
			m nil 1:1\n\
		}\n\
		buffer focus $b\n\
	}\n\
}\n\
\n\
#### DEFAULT HOOKS ###########################################################\n\
# Default buffer hooks (empty)\n\
\n\
proc buffer_setup_hook {buffer-name} { }\n\
proc buffer_loaded_hook {buffer-name} { }\n\
\n\
#### LEXY DEFINITIONS ##########################################################\n\
# Definitions of lexy state machines to syntax highlight source code\n\
\n\
\n\
lexydef c 0 {\n\
		space \"\" nothing\n\
		keywords {auto>|_Bool>|break>|case>|char>|_Complex>|const>|continue>|default>|do>|double>|else>|enum>|extern>|float>|for>|goto>|if>|_Imaginary>|inline>|int>|long>|register>|restrict>|return>|short>|signed>|sizeof>|static>|struct>|switch>|typedef>|union>|unsigned>|void>|volatile>|while>|int8_t>|uint8_t>|int16_t>|uint16_t>|int32_t>|uint32_t>|int64_t>|uint64_t>|size_t>|time_t>|bool>} keyword\n\
		keywords {NULL>|true>|false>} literal\n\
\n\
		matchspace \"#\\s*(?:include|ifdef|ifndef|if|else|endif|pragma|define)\\\\>\" keyword\n\
		match \"-?(?:0x)[0-9a-fA-F]*\" literal\n\
		match \"-?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?\" literal\n\
\n\
		match \"[a-zA-Z_][a-zA-Z0-9_]*\" id\n\
\n\
		region \"//,\\n,\" comment\n\
		region {/*,*/,} comment\n\
\n\
		region {',',\\\\} string\n\
		region {\",\",\\\\} string\n\
\n\
		any \".\" nothing\n\
	}\n\
\n\
lexyassoc c/0 {\\.c$}\n\
lexyassoc c/0 {\\.h$}\n\
\n\
# 		keywords {} keyword\n\
\n\
lexydef tcl 0 {\n\
 		space \"\" nothing\n\
 		keywords {after>|error>|lappend>|platform>|tcl_findLibrary>|append>|eval>|lassign>|platform::shell>|tcl_startOfNextWord>|apply>|exec>|lindex>|proc>|tcl_startOfPreviousWord>|array>|exit>|linsert>|puts>|tcl_wordBreakAfter>|auto_execok>|expr>|list>|pwd>|tcl_wordBreakBefore>|auto_import>|fblocked>|llength>|re_syntax>|tcltest>|auto_load>|fconfigure>|load>|read>|tclvars>|auto_mkindex>|fcopy>|lrange>|refchan>|tell>|auto_mkindex_old>|file>|lrepeat>|regexp>|time>|auto_qualify>|fileevent>|lreplace>|registry>|tm>|auto_reset>|filename>|lreverse>|regsub>|trace>|bgerror>|flush>|lsearch>|rename>|unknown>|binary>|for>|lset>|return>|unload>|break>|foreach>|lsort>|unset>|catch>|format>|mathfunc>|scan>|update>|cd>|gets>|mathop>|seek>|uplevel>|chan>|glob>|memory>|set>|upvar>|clock>|global>|msgcat>|socket>|variable>|close>|history>|namespace>|source>|vwait>|concat>|http>|open>|split>|while>|continue>|if>|else>|package>|string>|dde>|incr>|parray>|subst>|dict>|info>|pid>|switch>|encoding>|interp>|pkg::create>|eof>|join>|pkg_mkIndex>|tcl_endOfWord>|{*}} keyword\n\
		region \"#,\\n,\" comment\n\
\n\
		match \"\\$[a-zA-Z_][a-zA-Z0-9_]*\" id\n\
		region {\",\",\\\\} string\n\
\n\
		match {\\<[a-zA-Z0-9]*\\>} nothing\n\
\n\
		any \".\" nothing\n\
	}\n\
\n\
lexyassoc tcl/0 {\\.tcl$}\n\
\n\
lexydef python 0 {\n\
		space \"\" nothing\n\
		keywords {and>|del>|from>|not>|while>|as>|elif>|global>|or>|with>|assert>|else>|if>|pass>|yield>|break>|except>|import>|print>|class>|exec>|in>|raise>|continue>|finally>|is>|return>|def>|for>|lambda>|try>} keyword\n\
		keywords {None>|True>|False>} literal\n\
\n\
		match \"-?(?:0[bB])?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?[LljJ]?\" literal\n\
		match \"-?(?:0[xX])[0-9a-fA-F]*\" literal\n\
\n\
		match {[a-zA-Z_][a-zA-Z0-9_]*} id\n\
\n\
		region {\"\"\",\"\"\",\\\\} string\n\
		region {''',''',\\\\} string\n\
		region {\",\",\\\\} string\n\
		region {',',\\\\} string\n\
\n\
		region \"#,\\n,\" comment\n\
\n\
		any \".\" nothing\n\
	}\n\
\n\
lexyassoc python/0 {\\.py$}\n\
\n\
lexydef java 0 {\n\
		space \"\" nothing\n\
		keywords {abstract>|continue>|for>|new>|switch>|assert>|default>|goto>|package>|synchronized>|boolean>|do>|if>|private>|this>|break>|double>|implements>|protected>|throw>|byte>|else>|import>|public>|throws>|case>|enum>|instanceof>|return>|transient>|catch>|extends>|int>|short>|try>|char>|final>|interface>|static>|void>|class>|finally>|long>|strictfp>|volatile>|const>|float>|native>|super>|while>|null>} keyword\n\
		keywords {null>|true>|false>} literal\n\
\n\
		match \"-?(?:0x)[0-9a-fA-F]*\" literal\n\
		match \"-?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?\" literal\n\
\n\
		match \"[a-zA-Z_][a-zA-Z0-9_]*\" id\n\
\n\
		region \"//,\\n,\" comment\n\
		region {/*,*/,} comment\n\
		region {',',\\\\} string\n\
		region {\",\",\\\\} string\n\
\n\
		any \".\" nothing\n\
	}\n\
\n\
lexyassoc java/0 {\\.java$}\n\
\n\
lexydef go 0 {\n\
		space \"\" nothing\n\
		keywords {break>|default>|func>|interface>|select>|case>|defer>|go>|map>|struct>|chan>|else>|goto>|package>|switch>|const>|fallthrough>|if>|range>|type>|continue>|for>|import>|return>|var>|nil>|true>} keyword\n\
		keywords {nil>|true>|false>} literal\n\
\n\
		match \"-?(?:0x)[0-9a-fA-F]*\" literal\n\
		match \"-?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?\" literal\n\
\n\
		match {\\<[a-zA-Z_][a-zA-Z0-9_]*\\>} id\n\
\n\
		region \"//,\\n,\" comment\n\
		region {/*,*/,} comment\n\
		region {',',\\\\} string\n\
		region {\",\",\\\\} string\n\
		region {`,`,} string\n\
\n\
		any \".\" nothing\n\
	}\n\
\n\
lexyassoc go/0 {\\.go$}\n\
\n\
lexydef filesearch 0 {\n\
		space \"\" nothing\n\
		keywords \"@\" link\n\
		match {https?://\\S+} link\n\
		match {([^:[:space:]()]+):(\\d+)(?::(\\d+))?} file,1,2,3\n\
		matchspace {([^:[:space:]()]+):(/(?:[^/]|\\\\/)*/)} file,1,2\n\
		matchspace {File \"(.+?)\", line (\\d+)} file,1,2\n\
		matchspace {\\<at (\\S+) line (\\d+)} file,1,2\n\
		matchspace {\\<in (\\S+) on line (\\d+)} file,1,2\n\
		match {([^:[:space:]()]+):\\[(\\d+),(\\d+)\\]} file,1,2,3\n\
		match {\\<([^:[:space:]()]+\\.[^:[:space:]()]+)\\>} file\n\
		match {\\<\\S+/} file\n\
		any \".\" nothing\n\
	}\n\
\n\
lexyassoc filesearch/0 {^\\+bg}\n\
lexyassoc filesearch/0 {/$}\n\
lexyassoc filesearch/0 {^\\+guide} teddy_intl::guide_open\n\
lexyassoc filesearch/0 {/guide$} teddy_intl::guide_open\n\
\n\
lexydef mansearch 0 {\n\
		match {\\<(\\S+)\\((\\d+)\\)} link,1,2\n\
		any \".\" nothing\n\
	}\n\
\n\
lexyassoc mansearch/0 {^\\+man} teddy_intl::man_link_open\n\
\n\
lexydef tagsearch 0 {\n\
		matchspace {(\\S+)\\t/(.+)/} link,1,2\n\
		any \".\" nothing\n\
	}\n\
\n\
lexyassoc tagsearch/0 {^\\+tags} teddy_intl::tags_link_open\n\
\n\
lexydef js 0 {\n\
		space \"\" nothing\n\
		match {</script>} html/0:keyword\n\
		keywords {break>|const>|continue>|delete>|do>|while>|export>|for>|in>|function>|if>|else>|instanceOf>|label>|let>|new>|return>|switch>|this>|throw>|try>|catch>|typeof>|var>|void>|while>|with>|yield>} keyword\n\
		keywords {null>|true>|false>} literal\n\
		match \"-?(?:0x)[0-9a-fA-F]*\" literal\n\
		match \"-?[0-9][0-9]*(?:\\\\.[0-9]+)?(?:e-[0-9]+?)?\" literal\n\
\n\
		match \"[a-zA-Z_][a-zA-Z0-9_]*\" id\n\
\n\
		region \"//,\\n,\" comment\n\
		region {/*,*/,} comment\n\
		region {\",\",\\\\} string\n\
		region {',',\\\\} string\n\
		any \".\" nothing\n\
	}\n\
\n\
lexydef html 0 {\n\
		space \"\" nothing\n\
		region {<!--,-->,} comment\n\
		keywords {<} html-tag-start:keyword\n\
		match {>} keyword\n\
		match {&.*?;} literal\n\
		any \".\" nothing\n\
	} html-tag-start {\n\
		keywords {script} html-tag-waiting-for-script:keyword\n\
		match {[a-zA-Z]+} html-tag:keyword\n\
		any \".\" nothing\n\
	} html-tag {\n\
		region {',',\\\\} string\n\
		region {\",\",\\\\} string\n\
		keywords {>} 0:nothing\n\
		any \".\" nothing\n\
	} html-tag-waiting-for-script {\n\
		keywords {>} js/0:nothing\n\
		any \".\" nothing\n\
	}\n\
\n\
lexyassoc html/0 {\\.html$}\n\
"
#endif
