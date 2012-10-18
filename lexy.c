#include "lexy.h"

#include <stdlib.h>

#include <tre/tre.h>

#include "global.h"
#include "treint.h"
#include "interp.h"

/*
Documentation of TCL interface

The main command is:

 lexydef <lexy name> <lexy state> <transition list> <lexy state> <transition list>...

where <lexy name> and every <lexy state> are arbitrary strings, that must not contain ':' or '/', and:

 <transition list> ::= <transition>...
 <transition> ::= <pattern> <lexy state and token type>
 <pattern> ::= any regular expression
 <lexy state and token type> ::= <token type> | <lexy state>:<token type>
 <token type> ::= nothing | keyword | id | comment | string

this expands into:

 {
  lexydef-append <lexy name>/<lexy name> <lexy state> <pattern> <lexy name>/<next lexy state> <token type>
  lexydef-append <lexy name>/<lexy state> <pattern> <lexy name>/<next lexy state> <token type>
  lexydef-append <lexy name>/<lexy state> <pattern> <lexy name>/<next lexy state> <token type>
  ...
 }

In essence lexy is a tokenizer defined as a single big state machine and lexydef-append adds new state transitions to it.
The transition consists of an initial state "<lexy name>/<lexy state>", a <pattern>, a state to move to "<lexy name>/<next lexy state>" and a <token type> to mark the token with. Whenever the state machine is in "<lexy name>/<lexy state>" state and <pattern> is matched, the text matched will be marked with <token type> and the new state for the state machine will go to "<lexy name>/<next lexy state>".
The initial state of the state machine for a new buffer is determined by looking up associations created with lexyassoc:

lexyassoc <lexy name>/<lexy state> <extension>

*/

int lexy_colors[0xff];

#define LEXY_ASSOCIATION_NUMBER 1024
#define LEXY_STATUS_BLOCK_SIZE 16
#define LEXY_STATUS_NUMBER LEXY_ROWS/LEXY_STATUS_BLOCK_SIZE
#define LEXY_LINE_LENGTH_LIMIT 512

#define LEXY_LOAD_HOOK_MAX_COUNT 512

const char *CONTINUATION_STATUS = "continuation-state";

struct lexy_row {
	bool enabled;
	bool jump;
	regex_t pattern;
	bool check;
	uint8_t token_type;
	uint16_t next_status;
	uint8_t file_group, lineno_group, colno_group;
};

struct lexy_association {
	char *extension;
	int start_status_index;
	const char *link_fn;
};

struct lexy_status_pointer {
	char *name;
	int start_status_index;
};

struct lexy_row lexy_rows[LEXY_ROWS];
struct lexy_association lexy_associations[LEXY_ASSOCIATION_NUMBER];
struct lexy_status_pointer lexy_status_pointers[LEXY_STATUS_NUMBER];

void lexy_init(void) {
	for (int i = 0; i < LEXY_ROWS; ++i) {
		lexy_rows[i].enabled = false;
		lexy_rows[i].jump = false;
	}

	for (int i = 0; i < LEXY_ASSOCIATION_NUMBER; ++i) {
		lexy_associations[i].extension = NULL;
		lexy_associations[i].link_fn = "teddy_intl::link_open";
	}

	for (int i = 0; i < 0xff; ++i) {
		lexy_colors[i] = 0;
	}
}

static int find_status(const char *name) {
	for (int i = 0; i < LEXY_STATUS_NUMBER; ++i) {
		if (lexy_status_pointers[i].name == NULL) continue;
		if (strcmp(lexy_status_pointers[i].name, name) == 0) return lexy_status_pointers[i].start_status_index;
	}
	return -1;
}

static int create_new_status(const char *name) {
	int next_row_block = 0;
	for (int i = 0; i < LEXY_STATUS_NUMBER; ++i) {
		if (lexy_status_pointers[i].name == NULL) {
			if (next_row_block + LEXY_STATUS_BLOCK_SIZE > LEXY_ROWS) {
				return -1;
			}
			lexy_status_pointers[i].name = strdup(name);
			alloc_assert(lexy_status_pointers[i].name);
			lexy_status_pointers[i].start_status_index = next_row_block;
			//printf("Status %s is %d\n", name, next_row_block);
			return next_row_block;
		} else {
			next_row_block = lexy_status_pointers[i].start_status_index + LEXY_STATUS_BLOCK_SIZE;
		}
	}
	return -1;
}

int lexy_assoc_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc < 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'lexyassoc': usage 'lexyassoc <lexy-name> <extension> [<link function>]");
		return TCL_ERROR;
	}

	const char *status_name = argv[1];
	const char *extension = argv[2];

	int start_status_index = find_status(status_name);
	if (start_status_index < 0) {
		Tcl_AddErrorInfo(interp, "Cannot find lexy status");
		return TCL_ERROR;
	}

	for (int i = 0; i < LEXY_ASSOCIATION_NUMBER; ++i) {
		if (lexy_associations[i].extension == NULL) {
			lexy_associations[i].extension = strdup(extension);
			lexy_associations[i].start_status_index = start_status_index;
			if (argc == 4) {
				lexy_associations[i].link_fn = strdup(argv[3]);
			}
			return TCL_OK;
		}
	}

	Tcl_AddErrorInfo(interp, "Out of association space");
	return TCL_ERROR;
}

static const char *deparse_token_type_name(int r) {
	int x = r + CFG_LEXY_NOTHING;
	switch(x) {
	case CFG_LEXY_KEYWORD: return "keyword";
	case CFG_LEXY_ID: return "id";
	case CFG_LEXY_COMMENT: return "comment";
	case CFG_LEXY_STRING: return "string";
	case CFG_LEXY_LITERAL: return "literal";

	case CFG_LEXY_NOTHING:
	default:
		return "nothing";
	}
}

static void parse_file_or_line_token(const char *token_type_name, int *file_group, int *lineno_group, int *colno_group) {
	char *type_copy = strdup(token_type_name);
	char *saveptr;
	char *tok = strtok_r(type_copy, ",", &saveptr);
	tok = strtok_r(NULL, ",", &saveptr);
	if (tok != NULL) {
		*file_group = atoi(tok);
		tok = strtok_r(NULL, ",", &saveptr);
		if (tok != NULL) {
			*lineno_group = atoi(tok);
			tok = strtok_r(NULL, ",", &saveptr);
			if (tok != NULL) {
				*colno_group = atoi(tok);
			}
		}
	}
	free(type_copy);
}

static int parse_token_type_name(const char *token_type_name, bool *check, int *file_group, int *lineno_group, int *colno_group) {
	*check = false;
	*file_group = 0; *lineno_group = 0; *colno_group = 0;

	if (strcmp(token_type_name, "nothing") == 0) return 0;
	if (strcmp(token_type_name, "keyword") == 0) return CFG_LEXY_KEYWORD - CFG_LEXY_NOTHING;
	if (strcmp(token_type_name,"id") == 0) return CFG_LEXY_ID - CFG_LEXY_NOTHING;
	if (strcmp(token_type_name, "identifier") == 0) return CFG_LEXY_ID - CFG_LEXY_NOTHING;
	if (strcmp(token_type_name, "comment") == 0) return CFG_LEXY_COMMENT - CFG_LEXY_NOTHING;
	if (strcmp(token_type_name, "string") == 0) return CFG_LEXY_STRING - CFG_LEXY_NOTHING;
	if (strcmp(token_type_name, "literal") == 0) return CFG_LEXY_LITERAL - CFG_LEXY_NOTHING;

	if (strncmp(token_type_name, "file", strlen("file")) == 0) {
		*check = true;
		parse_file_or_line_token(token_type_name, file_group, lineno_group, colno_group);
		return CFG_LEXY_FILE - CFG_LEXY_NOTHING;
	} else if (strncmp(token_type_name, "link", strlen("link")) == 0) {
		*check = false;
		parse_file_or_line_token(token_type_name, file_group, lineno_group, colno_group);
		return CFG_LEXY_FILE - CFG_LEXY_NOTHING;
	}
	return -1;
}

static struct lexy_row *new_row_for_state(int state) {
	//printf("New row for %d\n", state);
	int base = state;
	for (int offset = 0; offset < LEXY_STATUS_BLOCK_SIZE; ++offset) {
		struct lexy_row *row = lexy_rows + base + offset;
		if (!(row->enabled)) {
			//we found an empty row, we will use this unless it's right at the end of the block
			// in that case a new block needs to be allocated (the very last row of a bloc is used to store a pointer)
			if (offset == LEXY_STATUS_BLOCK_SIZE-1) {
				int continuation_status = create_new_status(CONTINUATION_STATUS);
				if (continuation_status < 0) {
					return NULL;
				}
				row->enabled = true;
				row->jump = true;
				row->next_status = continuation_status;
				printf("%d Added jump\n", offset);
			} else {
				//printf("%d Found\n", offset);
				return row;
			}
		}
		if (row->jump) {
			//printf("%d Following jump to %d\n", offset, row->next_status);
			base = row->next_status;
			offset = -1;
		}
	}
	return NULL;
}

int lexy_append_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 5) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'lexydef-append', usage: 'lexydef-append <lexy name> <pattern> <next lexy state> <token type>'");
		return TCL_ERROR;
	}

	const char *status_name = argv[1];
	const char *pattern = argv[2];
	const char *next_status_name = argv[3];
	const char *token_type_name = argv[4];

	//printf("Lexy append: %s %s %s %s\n", status_name, pattern, next_status_name, token_type_name);

	int status_index = find_status(status_name);
	if (status_index < 0) status_index = create_new_status(status_name);
	if (status_index < 0) {
		Tcl_AddErrorInfo(interp, "Out of status space");
		return TCL_ERROR;
	}

	int next_status_index = find_status(next_status_name);
	if (next_status_index < 0) next_status_index = create_new_status(next_status_name);
	if (next_status_index < 0) {
		Tcl_AddErrorInfo(interp, "Out of status space");
		return TCL_ERROR;
	}

	int file_group, lineno_group, colno_group;
	bool check;
	int token_type = parse_token_type_name(token_type_name, &check, &file_group, &lineno_group, &colno_group);
	if (token_type < 0) {
		Tcl_AddErrorInfo(interp, "Unknown token type");
		return TCL_ERROR;
	}

	char *fixed_pattern;
	asprintf(&fixed_pattern, "^(?:%s)", pattern);
	alloc_assert(fixed_pattern);

	regex_t compiled_pattern;
	int r = tre_regcomp(&compiled_pattern, fixed_pattern, REG_EXTENDED);
	if (r != REG_OK) {
#define REGERROR_BUF_SIZE 512
		char buf[REGERROR_BUF_SIZE];
		tre_regerror(r, &compiled_pattern, buf, REGERROR_BUF_SIZE);
		char *msg;
		asprintf(&msg, "Syntax error in regular expression [%s]: %s\n", fixed_pattern, buf);
		alloc_assert(msg);
		Tcl_AddErrorInfo(interp, msg);
		free(msg);
		return TCL_ERROR;
	}
	tre_regfree(&compiled_pattern);

	struct lexy_row *new_row = new_row_for_state(status_index);
	if (new_row == NULL) {
		Tcl_AddErrorInfo(interp, "Out of row space");
		return TCL_ERROR;
	}

	new_row->next_status = next_status_index;
	new_row->token_type = token_type;
	tre_regcomp(&(new_row->pattern), fixed_pattern, REG_EXTENDED);
	new_row->enabled = true;
	new_row->check = check;
	new_row->file_group = file_group;
	new_row->lineno_group = lineno_group;
	new_row->colno_group = colno_group;

	free(fixed_pattern);

	return TCL_OK;
}

static bool check_file_match(struct lexy_row *row, buffer_t *buffer, real_line_t *line, int glyph, int nmatch, regmatch_t *pmatch) {
	//printf("file_group %d nmatch %d (%d)\n", row->file_group, nmatch, tokenizer->verify_file);
	if (!row->check) return true;
	if (row->file_group >= nmatch) return false;

	lpoint_t start, end;
	start.line = end.line = line;
	start.glyph = glyph + pmatch[row->file_group].rm_so;
	end.glyph = glyph + pmatch[row->file_group].rm_eo;

	char *text = buffer_lines_to_text(buffer, &start, &end);
	bool r = (access(text, F_OK) == 0);
	//printf("Check <%s> %d (file_group: %d)\n", text, r, row->file_group);
	free(text);

	return r;
}

static void lexy_update_one_token(buffer_t *buffer, real_line_t *line, int *glyph, int *status) {
	int base = *status;
	if (line->cap > LEXY_LINE_LENGTH_LIMIT) {
		return;
		*glyph = line->cap;
	}

	for (int offset = 0; offset < LEXY_STATUS_BLOCK_SIZE; ++offset) {
		struct lexy_row *row = lexy_rows + base + offset;
		if (!(row->enabled)) {
			line->glyph_info[*glyph].color = 0;
			++(*glyph);
			return;
		}
		if (row->jump) {
			base = row->next_status;
			offset = -1;
			continue;
		}

		struct augmented_lpoint_t matchpoint;
		matchpoint.line = line;
		matchpoint.start_glyph = *glyph;
		matchpoint.offset = 0;

		tre_str_source tss;
		tre_bridge_init(&matchpoint, &tss);

#define NMATCH 10
		regmatch_t pmatch[NMATCH];

		int r = tre_reguexec(&(row->pattern), &tss, NMATCH, pmatch, 0);

		if (r == REG_OK) {
			//printf("\t\tMatched: %d (%d) - %d as %d [", *glyph+pmatch[0].rm_so, pmatch[0].rm_so, *glyph+pmatch[0].rm_eo, row->token_type);

			uint8_t token_type = row->token_type;

			if (row->token_type == CFG_LEXY_FILE - CFG_LEXY_NOTHING) {
				if (!check_file_match(row, buffer, line, *glyph, NMATCH, pmatch)) {
					token_type = CFG_LEXY_NOTHING - CFG_LEXY_NOTHING;
				}
			}

			for (int j = 0; j < pmatch[0].rm_eo; ++j) {
				//uint32_t code = line->glyph_info[*glyph + j].code;
				//printf("%c", (code >= 0x20) && (code <= 0x7f) ? (char)code : '?');
				line->glyph_info[*glyph + j].color = token_type;
			}
			//printf("]\n");
			*glyph += pmatch[0].rm_eo;
			*status = row->next_status;
			return;
		}
	}
}

static void lexy_update_line(buffer_t *buffer, real_line_t *line, int *state) {
	line->lexy_state_start = *state;
	for (int glyph = 0; glyph < line->cap; ) {
		lexy_update_one_token(buffer, line, &glyph, state);
	}
	line->lexy_state_end = *state;
}

static struct lexy_association *association_for_buffer(buffer_t *buffer) {
	if (buffer == NULL) return NULL;
	for (int i = 0; i < LEXY_ASSOCIATION_NUMBER; ++i) {
		struct lexy_association *a = lexy_associations + i;
		if (a->extension == NULL) return NULL;

		regex_t extre;
		if (tre_regcomp(&extre, a->extension, REG_EXTENDED) != REG_OK) {
			tre_regfree(&extre);
			continue;
		}
#define REGEXEC_NMATCH 5
		regmatch_t pmatch[REGEXEC_NMATCH];

		if (tre_regexec(&extre, buffer->path, REGEXEC_NMATCH, pmatch, 0) != REG_OK) {
			tre_regfree(&extre);
		} else {
			tre_regfree(&extre);
			return a;
		}
	}
	return NULL;
}

static int start_status_for_buffer(buffer_t *buffer) {
	struct lexy_association *a = association_for_buffer(buffer);
	if (a == NULL) return -1;
	return a->start_status_index;
}

const char *lexy_get_link_fn(buffer_t *buffer) {
	struct lexy_association *a = association_for_buffer(buffer);
	if (a == NULL) return "";
	return a->link_fn;
}

void lexy_update_starting_at(buffer_t *buffer, real_line_t *start_line, bool quick_exit) {
	if (start_line == NULL) return;
	int start_status_index = start_status_for_buffer(buffer);
	if (start_status_index < 0) return;

	int count = 0;
	int status = LEXY_ROWS;

	real_line_t *line;
	for (line = start_line; line != NULL; line = line->next) {
		if ((line->lexy_state_start != status) || (line->lexy_state_start == LEXY_ROWS) || (count < 2)) {
			if (status == LEXY_ROWS) {
				if (line->lexy_state_start == LEXY_ROWS) line->lexy_state_start = start_status_index;
				status = line->lexy_state_start;
			}

			lexy_update_line(buffer, line, &status);
		} else {
			if (quick_exit) return;
		}

		status = line->lexy_state_end;

		if (count >= LEXY_LOAD_HOOK_MAX_COUNT) break;
		++count;
	}

	buffer->lexy_last_update_line = line;
}

void lexy_update_for_move(buffer_t *buffer, real_line_t *start_line) {
	if (buffer->lexy_last_update_line == NULL) return;

	if (start_line->lineno > buffer->lexy_last_update_line->lineno) {
		start_line = buffer->lexy_last_update_line;
	} else {
		return;
	}

	for (int count = 0; count < 10; ++count) { // count is here just to prevent this loop from running forever on pathological situations
		lexy_update_starting_at(buffer, start_line, true);
		start_line = buffer->lexy_last_update_line;
		if (start_line == NULL) break;
		if (start_line->lineno >= buffer->cursor.line->lineno) break;
	}
}

int lexy_create_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	Tcl_AddErrorInfo(interp, "Not implemented anymore");
	return TCL_ERROR;
}

int lexy_dump_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	Tcl_AddErrorInfo(interp, "Not implemented");
	return TCL_ERROR;
}

static int lexy_parse_token( int state, const char *text, char **file, char **line, char **col) {
	//printf("Starting match\n");

	int base = state;
	for (int offset = 0; offset < LEXY_STATUS_BLOCK_SIZE; ++offset) {
		struct lexy_row *row = lexy_rows + base + offset;

		if (!(row->enabled)) return CFG_LEXY_NOTHING;

		if (row->jump)	{
			base = row->next_status;
			offset = -1;
			continue;
		}

		//printf("Checking offset %d\n", offset);

#define NMATCH 10
		regmatch_t pmatch[NMATCH];

		int r = tre_regexec(&(row->pattern), text, NMATCH, pmatch, 0);

		if (r == REG_OK) {
			*file = strndup(text + pmatch[row->file_group].rm_so, pmatch[row->file_group].rm_eo - pmatch[row->file_group].rm_so);
			if (row->lineno_group > 0) {
				*line = strndup(text + pmatch[row->lineno_group].rm_so, pmatch[row->lineno_group].rm_eo - pmatch[row->lineno_group].rm_so);
			}

			if (row->colno_group > 0) {
				*col = strndup(text + pmatch[row->colno_group].rm_so, pmatch[row->colno_group].rm_eo - pmatch[row->colno_group].rm_so);
			}

			return row->token_type;
		}
	}

	return CFG_LEXY_NOTHING;
}

int lexy_token_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	// lexy-token <starting state> <text>
	if (interp_context_buffer() == NULL) {
		Tcl_AddErrorInfo(interp, "Can not execute lexy-token command without an active buffer");
		return TCL_ERROR;
	}

	if (argc != 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to lexy-token command");
		return TCL_ERROR;
	}

	int r = 0;
	char *file = NULL, *line = NULL, *col = NULL;

	int status = (strcmp(argv[1], ".") == 0) ? start_status_for_buffer(interp_context_buffer()) : find_status(argv[1]);
	if (status >= 0) {
		r = lexy_parse_token(status, argv[2], &file, &line, &col);
	} else {
		Tcl_AddErrorInfo(interp, "Can not find status");
		return TCL_ERROR;
	}

	const char *ret[] = { deparse_token_type_name(r),
		(file != NULL) ? file : "", (line != NULL) ? line : "", (col != NULL) ? col : ""  };
	char *retstr = Tcl_Merge(4, ret);
	Tcl_SetResult(interp, retstr, TCL_VOLATILE);
	Tcl_Free(retstr);

	if (file != NULL) free(file);
	if (line != NULL) free(line);
	if (col != NULL) free(col);

	return TCL_OK;
}
