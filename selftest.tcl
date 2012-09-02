proc print_to_result {text} {
	global st_result
	buffer eval $st_result {
		m $:$
		c $text
	}
}

proc selftest_init {} {
	global st_stage st_result
	set st_stage [buffer make +selftest-stage+]
	set st_result [buffer make +selftest-result+]
	print_to_result "Selftest initialized\n"
}

proc cleanup_stage {reset_to_path} {
	global st_stage
	buffer eval $st_stage {
		m 1:1 $:$
		c [teddy::slurp $reset_to_path]
	}
}

proc assert_result {target_file} {
	global st_stage st_result
	set output ""
	set target [teddy::slurp $target_file]
	buffer eval $st_stage {
		m 1:1 $:$
		set output [c]
		m nil 1:1
	}
	if {$output eq $target} {
		print_to_result "OK\n"
	} else {
		print_to_result "FAILED\noutput: <$output>\ntarget: <$target>\n"
	}
}

proc double_spacing_test {} {
	global st_result st_stage
	print_to_result "Double space test... "
	cleanup_stage "test/input1.txt"
	buffer eval $st_stage {
		m 1:1 $:$
		s {^.*$} {
			m nil +0:+0
			c "\n"
		}
	}
	assert_result "test/output1_double.txt"

	print_to_result "Undoing double spacing test... "
	buffer eval $st_stage {
		m nil 1:1
		while {[m +1:1]} {
			kill_line
		}
		m -1:$ +0:+0
		c ""
	}

	assert_result "test/input1.txt"
}

proc blank_line_above_match_test {} {
	global st_result st_stage
	print_to_result "Blank line above match test... "
	cleanup_stage "test/input1.txt"
	buffer eval $st_stage {
		m 1:1 $:$
		s {erbetta|farfalletta} {
			m +0:1 +0:$
			c "\n[c]"
		}
	}

	assert_result "test/output1_line_above.txt"
}

proc blank_line_below_match_test {} {
	global st_result st_stage
	print_to_result "Blank line below match test... "
	cleanup_stage "test/input1.txt"
	buffer eval $st_stage {
		m 1:1 $:$
		s {erbetta|farfalletta} {
			m nil +0:$
			c "\n"
		}
	}

	assert_result "test/output1_line_below.txt"
}

proc line_numbering_test {} {
	global st_result st_stage
	print_to_result "Line numbering test... "
	cleanup_stage "test/input1.txt"
	buffer eval $st_stage {
		m 1:1 $:$
		s {^.*$} {
			set n [teddy::lineof [lindex [m] 1]]
			c "$n\t[c]"
		}
	}

	assert_result "test/output1_numbered.txt"
}

proc line_numbering_nonempty_test {} {
	global st_result st_stage
	print_to_result "Line numbering of non-empty lines test... "
	cleanup_stage "test/output1_double.txt"
	buffer eval $st_stage {
		m 1:1 $:$
		s {^.+$} {
			set n [teddy::lineof [lindex [m] 1]]
			c "$n\t[c]"
		}
	}

	assert_result "test/output1_double_numbered.txt"
}

proc todos_test {} {
	global st_result st_stage
	print_to_result "Conversion to DOS test... "
	cleanup_stage "test/input1.txt"
	buffer eval $st_stage {
		m 1:1 $:$
		s "$" c "\r"
		m $:1 $:$
		c ""
	}

	assert_result "test/output1_dos.txt"
}

proc tounix_test {} {
	global st_result st_stage
	print_to_result "Conversion to UNIX test... "
	cleanup_stage "test/output1_dos.txt"
	buffer eval $st_stage {
		m 1:1 $:$
		s "\r" c ""
	}

	assert_result "test/input1.txt"
}

proc reverse_test {} {
	global st_result st_stage
	print_to_result "File reverse test... "
	cleanup_stage "test/input1.txt"
	buffer eval $st_stage {
		m 1:1 $:$
		c [join [lreverse [split [c] "\n"]] "\n"]
	}

	assert_result "test/output1_reverse.txt"
}

proc chareverse_test {} {
	global st_result st_stage
	print_to_result "Line reverse test... "
	cleanup_stage "test/input1.txt"
	buffer eval $st_stage {
		m 1:1 $:$
		s {^.*$} {
			c [string reverse [c]]
		}
	}

	assert_result "test/output1_reverseline.txt"
}

proc pairs_test {} {
	global st_result st_stage
	print_to_result "Couple pairs of lines test... "
	cleanup_stage "test/input1.txt"
	buffer eval $st_stage {
		m 1:1 $:$
		s {^.*$} {
			m nil +0:+0
			m +0:+0 +0:+1
			c "\t"
		}
	}

	assert_result "test/output1_pairs.txt"
}

proc section_delete_test {} {
	global st_result st_stage
	print_to_result "Section delete test... "
	cleanup_stage "test/input1.txt"
	buffer eval $st_stage {
		m nil 1:1
		set start [s -get teresa]
		m {*}$start
		set end [s -get volo]
		m [lindex $start 0] [lindex $end 1]
		c ""
	}

	assert_result "test/output1_section_delete.txt"
}

# MAIN
selftest_init
double_spacing_test
blank_line_above_match_test
blank_line_below_match_test
line_numbering_test
line_numbering_nonempty_test
todos_test
tounix_test
reverse_test
chareverse_test
pairs_test
section_delete_test
