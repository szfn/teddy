#include "lexy.h"

#include <stdlib.h>

#include <tre/tre.h>

#include <unicode/uchar.h>

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

pthread_attr_t lexy_thread_attrs;

int lexy_colors[0xff];

#define LEXY_ASSOCIATION_NUMBER 1024
#define LEXY_STATUS_BLOCK_SIZE 16
#define LEXY_STATUS_NUMBER LEXY_ROWS/LEXY_STATUS_BLOCK_SIZE
#define LEXY_LINE_LENGTH_LIMIT 512

#define LEXY_LOAD_HOOK_MAX_COUNT 4096
#define LEXY_QUICK_EXIT_MAX_COUNT 10

#define LEXY_DEFAULT_LINK_OPEN_FN "teddy_intl::link_open"

const char *CONTINUATION_STATUS = "continuation-state";

enum match_kind {
	LM_KEYWORDS = 0,
	LM_REGION,
	LM_REGEXP,
	LM_REGEXP_SPACE,
	LM_ANY,
	LM_SPACE,
	LM_UNKNOWN,
};

struct lexy_row {
	bool enabled;
	bool jump;

	enum match_kind match_kind;

	/* LM_KEYWORDS */
	size_t kwlen;
	char *kws;
	bool kwsp; /* enable special character match */

	/* LM_REGION */
	char *region_end;
	char escape;

	/* LM_REGEXP match */
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
		lexy_associations[i].link_fn = LEXY_DEFAULT_LINK_OPEN_FN;
	}

	for (int i = 0; i < 0xff; ++i) {
		lexy_colors[i] = 0;
	}

	pthread_attr_init(&lexy_thread_attrs);
	pthread_attr_setdetachstate(&lexy_thread_attrs, PTHREAD_CREATE_DETACHED);
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

static enum match_kind parse_match_kind(const char *match_kind) {
	if (strcmp(match_kind, "keywords") == 0) {
		return LM_KEYWORDS;
	} else if (strcmp(match_kind, "region") == 0) {
		return LM_REGION;
	} else if (strcmp(match_kind, "match") == 0) {
		return LM_REGEXP;
	} else if (strcmp(match_kind, "matchspace") == 0) {
		return LM_REGEXP_SPACE;
	} else if (strcmp(match_kind, "any") == 0) {
		return LM_ANY;
	} else if (strcmp(match_kind, "space") == 0) {
		return LM_SPACE;
	} else {
		return LM_UNKNOWN;
	}
}

int lexy_append_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 6) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'lexydef-append', usage: 'lexydef-append <lexy name> <match kind> <pattern> <next lexy state> <token type>'");
		return TCL_ERROR;
	}

	const char *status_name = argv[1];
	const char *match_kind_str = argv[2];
	const char *pattern = argv[3];
	const char *next_status_name = argv[4];
	const char *token_type_name = argv[5];

	enum match_kind match_kind = parse_match_kind(match_kind_str);

	if (match_kind == LM_UNKNOWN) {
		Tcl_AddErrorInfo(interp, "Unknown match kind");
		return TCL_ERROR;
	}

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

	struct lexy_row *new_row = new_row_for_state(status_index);
	if (new_row == NULL) {
		Tcl_AddErrorInfo(interp, "Out of row space");
		return TCL_ERROR;
	}

	new_row->next_status = next_status_index;
	new_row->token_type = token_type;
	new_row->match_kind = match_kind;
	new_row->check = check;
	new_row->file_group = file_group;
	new_row->lineno_group = lineno_group;
	new_row->colno_group = colno_group;

	switch (match_kind) {
	case LM_REGEXP_SPACE:
	case LM_REGEXP: {
		char *fixed_pattern;
		asprintf(&fixed_pattern, "^(?:%s)", pattern);
		alloc_assert(fixed_pattern);
		regex_t compiled_pattern;
		int r = tre_regcomp(&(new_row->pattern), fixed_pattern, REG_EXTENDED);
		if (r != REG_OK) {
#define REGERROR_BUF_SIZE 512
			char buf[REGERROR_BUF_SIZE];
			tre_regerror(r, &compiled_pattern, buf, REGERROR_BUF_SIZE);
			char *msg;
			asprintf(&msg, "Syntax error in regular expression [%s]: %s\n", fixed_pattern, buf);
			alloc_assert(msg);
			Tcl_AddErrorInfo(interp, msg);
			free(msg);
			free(fixed_pattern);
			return TCL_ERROR;
		}
		new_row->enabled = true;
		free(fixed_pattern);
		break;
	}

	case LM_SPACE:
		new_row->enabled = true;
		break;

	case LM_KEYWORDS:
		new_row->kwlen = strlen(pattern);
		new_row->kws = strdup(pattern);
		new_row->kwsp = true;
		alloc_assert(new_row->kws);
		for (int i = 0; i < new_row->kwlen; ++i) {
			if (new_row->kws[i] == '|') new_row->kws[i] = '\0';
		}
		new_row->enabled = true;
		break;

	case LM_ANY:
		new_row->enabled = true;
		break;

	case LM_REGION: {
		char *copy_pattern = strdup(pattern);
		alloc_assert(copy_pattern);

		char *saveptr;
		char *start = strtok_r(copy_pattern, ",", &saveptr);
		char *end = strtok_r(NULL, ",", &saveptr);
		char *escape = strtok_r(NULL, ",", &saveptr);

		if (escape == NULL) escape = "\0";

		if ((start == NULL) || (end == NULL) || (escape == NULL)) {
			Tcl_AddErrorInfo(interp, "Wrong pattern for 'region' match kind");
			return TCL_ERROR;
		}

		new_row->match_kind = LM_KEYWORDS;
		new_row->kwlen = strlen(start);
		new_row->kws = strdup(start);
		new_row->kwsp = false;
		alloc_assert(new_row->kws);
		new_row->check = false;
		new_row->enabled = true;

		int region_status = create_new_status("synthetic");
		if (region_status < 0) {
			Tcl_AddErrorInfo(interp, "Out of row space\n");
			return TCL_ERROR;
		}

		new_row->next_status = region_status;

		struct lexy_row *region_row = new_row_for_state(region_status);
		if (region_row == NULL) {
			Tcl_AddErrorInfo(interp, "Out of row space\n");
			return TCL_ERROR;
		}

		region_row->next_status = next_status_index;
		region_row->token_type = token_type;
		region_row->match_kind = LM_REGION;
		region_row->region_end = strdup(end);
		region_row->escape = escape[0];
		region_row->check = false;
		region_row->enabled = true;

		free(copy_pattern);
		break;
	}

	case LM_UNKNOWN:
		Tcl_AddErrorInfo(interp, "Unknown match kind");
		return TCL_ERROR;
	}

	return TCL_OK;
}

static bool check_file_match(struct lexy_row *row, buffer_t *buffer, int glyph, int nmatch, regmatch_t *pmatch) {
	//printf("file_group %d nmatch %d (%d)\n", row->file_group, nmatch, tokenizer->verify_file);
	if (!row->check) return true;
	if (row->file_group >= nmatch) return false;

	int start = glyph + pmatch[row->file_group].rm_so, end = glyph + pmatch[row->file_group].rm_eo;
	char *text = buffer_lines_to_text(buffer, start, end);
	bool r = (access(text, F_OK) == 0);
	//printf("Check <%s> %d (file_group: %d)\n", text, r, row->file_group);
	free(text);

	return r;
}

static int bufmatch(buffer_t *buffer, int start, const char *needle, bool special) {
	//printf("Checking %d %s\n", start, needle);
	int j = 0;
	uint32_t cur_code;
	for (int i = 0; i < strlen(needle); ++j) {
		bool valid;
		cur_code = utf8_to_utf32(needle, &i, strlen(needle), &valid);
		if (!valid) return -1;

		my_glyph_info_t *cur_glyph = bat(buffer, start + j);
		if (cur_glyph == NULL) return -1;

		//printf("cur_code %d (%c) cur_glyph %d (%c)\n", cur_code, (char)cur_code, cur_glyph->code, (char)cur_glyph->code);

		if (special)
			if (cur_code == '>') break;
		if (cur_code != cur_glyph->code) return -1;
	}

	if (special && (cur_code == '>')) {
		my_glyph_info_t *g = bat(buffer, start + j);
		if (g != NULL) {
			//printf("Extra check for <%s> is %d (%c)\n", needle, g->code, (char)g->code);
			if (u_isalnum(g->code) || (g->code == '_')) return -1;
		}
		return strlen(needle) - 1;
	} else {
		return strlen(needle);
	}
}

static void lexy_update_one_token(buffer_t *buffer, int *i, int *status) {
	int base = *status;

	//printf("Coloring one token at %p:%d (status %d)\n", buffer, *i, *status);

	for (int offset = 0; offset < LEXY_STATUS_BLOCK_SIZE; ++offset) {
		struct lexy_row *row = lexy_rows + base + offset;
		if (!(row->enabled)) {
			bat(buffer, *i)->color = 0;
			bat(buffer, *i)->status = *status;
			++(*i);
			return;
		}
		if (row->jump) {
			base = row->next_status;
			offset = -1;
			continue;
		}



		int match_len = -1;

		//printf("%d %d Testing match_kind: %d\n", base, offset, row->match_kind);

		uint8_t token_type = row->token_type;

		switch (row->match_kind) {
		case LM_REGEXP_SPACE:
		case LM_REGEXP: {
			struct augmented_lpoint_t matchpoint;
			matchpoint.buffer = buffer;
			matchpoint.start_glyph = *i;
			matchpoint.offset = 0;
			matchpoint.endatnewline = true;
			matchpoint.endatspace = !(row->match_kind == LM_REGEXP_SPACE);

			tre_str_source tss;
			tre_bridge_init(&matchpoint, &tss);
#define NMATCH 10
			regmatch_t pmatch[NMATCH];
			//printf("\tPattern matching:\n");
			int r = tre_reguexec(&(row->pattern), &tss, NMATCH, pmatch, 0);
			//printf("\tdone %d %d\n", r, REG_OK);

			if (r == REG_OK) {
				//printf("\t\tMatched: %d (%d) - %d as %d [", *i+pmatch[0].rm_so, pmatch[0].rm_so, *i+pmatch[0].rm_eo, row->token_type);

				if (row->token_type == CFG_LEXY_FILE - CFG_LEXY_NOTHING) {
					//printf("Checking match <%s>\n", buffer_lines_to_text(buffer, *i, (*i)+pmatch[0].rm_eo)); // leaky
					match_len = pmatch[0].rm_eo;
					if (!check_file_match(row, buffer, *i, NMATCH, pmatch)) {
						token_type = 0;
					}
				} else {
					match_len = pmatch[0].rm_eo;
				}
			}
			break;
		}

		case LM_SPACE:
			if ((bat(buffer, *i)->code == 0x20) || (bat(buffer, *i)->code == 0x09)) {
				match_len = 1;
			}
			break;

		case LM_KEYWORDS:
			for (int start = 0; start < row->kwlen; start += strlen(row->kws + start)+1) {
				//printf("Matching %d <%s>\n", row->kwlen, row->kws + start);
				int m = bufmatch(buffer, *i, row->kws + start, row->kwsp);
				if (m >= 0) {
					match_len = m;
					break;
				}
			}
			break;

		case LM_ANY:
			match_len = 1;
			break;

		case LM_REGION: {
			int j;
			for (j = *i; j < BSIZE(buffer); ++j) {
				my_glyph_info_t *g = bat(buffer, j);
				if (g == NULL) break;
				//printf("\tChecking %d %c\n", g->code, (char)g->code);
				int m = bufmatch(buffer, j, row->region_end, false);
				if (m >= 0) {
					j += m;
					break;
				}
			}
			match_len = j - *i;
			break;
		}

		case LM_UNKNOWN:
			// do nothing, this is an error
			break;
		}

		if (match_len >= 0) {
			for (int j = 0; j < match_len; ++j) {
				my_glyph_info_t *g = bat(buffer, *i + j);
				if (g == NULL) break;
				//uint32_t code = line->glyph_info[*glyph + j].code;
				//printf("%c", (code >= 0x20) && (code <= 0x7f) ? (char)code : '?');
				g->color = token_type;
				g->status = *status;
			}
			//printf("]\n");
			*i += match_len;
			*status = row->next_status;
			return;
		}
	}
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
	if (a == NULL) return LEXY_DEFAULT_LINK_OPEN_FN;
	return a->link_fn;
}

static gboolean refresher(editor_t *editor) {
	gtk_widget_queue_draw(editor->drar);
	return FALSE;
}

static void refresher_add(buffer_t *buffer) {
	editor_t *editor = NULL;
	find_editor_for_buffer(buffer, NULL, NULL, &editor);
	if (editor != NULL) g_idle_add((GSourceFunc)refresher, editor);
}

static void *lexy_update_starting_at_thread(void *varg) {
	buffer_t *buffer = (buffer_t *)varg;

	int start_status_index = start_status_for_buffer(buffer);
	if (start_status_index < 0) {
		buffer->lexy_running = 0;
		return NULL;
	}

	pthread_rwlock_rdlock(&(buffer->rwlock));

	int start = buffer->lexy_start;

	//printf("Coloring buffer %p (%s) starting at %d (%d)\n", buffer, buffer->path, start, BSIZE(buffer));

	int status;

	if (start < 0) {
		start = 0;
		status = start_status_index;
	} else {
		/* We always start at the beginning of a line
		The reason is that we need to be sure that we don't start in the middle of a token.
		The beginning of a line is never the beginning of a token because keyword, and regexp matches can not span on multiple lines.
		The only type of token that can span multiple lines is the region match but that will work (because it is a separate state in the state machine)*/

		buffer_move_point_glyph(buffer, &start, MT_ABS, 1);
		if (start > 0) --start;
		my_glyph_info_t *glyph = bat(buffer, start);
		status = (glyph != NULL) ? glyph->status : start_status_index;
		if (status == 0xffff) status = start_status_index;
	}

	//printf("\tActual start %d (status: %d)\n", start, status);
	//printf("Buffer <%s> status: %d start_status_for_buffer %d\n", buffer->path, status, start_status_index);

	int count = 0;

	for (int i = start; i < BSIZE(buffer); ) {
		my_glyph_info_t *g = bat(buffer, i);

		if (g == NULL) break;
		if (g->code == '\n') {
			++count;
		}

		int previ = i;

		if (buffer->release_read_lock) {
			buffer->lexy_running = 2;
			pthread_rwlock_unlock(&(buffer->rwlock));
			refresher_add(buffer);
			return NULL;
		}

		lexy_update_one_token(buffer, &i, &status);

		if (previ == i) ++i;
		if (count > LEXY_LOAD_HOOK_MAX_COUNT) break;

		if (buffer->lexy_quick_exit) {
			if (count > LEXY_QUICK_EXIT_MAX_COUNT) {
				buffer->lexy_running = 2;
				pthread_rwlock_unlock(&(buffer->rwlock));
				refresher_add(buffer);
				return NULL;
			}
		}
	}

	//printf("Lexy finished\n");

	buffer->lexy_running = 0;
	pthread_rwlock_unlock(&(buffer->rwlock));

	refresher_add(buffer);
	return NULL;
}

void lexy_update_resume(buffer_t *buffer) {
	if (buffer->lexy_running != 2) return;
	if (config_intval(&(buffer->config), CFG_LEXY_ENABLED) == 0) return;
	buffer->lexy_quick_exit = false;
	buffer->lexy_running = 1;
	pthread_t thread;
	if (pthread_create(&thread, &lexy_thread_attrs, lexy_update_starting_at_thread, buffer) != 0) {
		buffer->lexy_running = 0;
		perror("Can not start new thread");
	}
}

void lexy_update_starting_at(buffer_t *buffer, int start, bool quick_exit) {
	/* Skip doing updates for +unnamed buffers, they are always temp buffers used for computations */
	if (config_intval(&(buffer->config), CFG_LEXY_ENABLED) == 0) return;
	if (strcmp(buffer->path, "+unnamed") == 0) return;

	if (buffer->lexy_running == 1) {
		// this function runs with the write lock acquired, we can only see lexy_running == 1
		// when the lexy thread is running but hasn't acquired the read lock yet.
		// just update the start
		//printf("%p Continuing at %d %d -> %d\n", buffer, buffer->lexy_start, start, MIN(buffer->lexy_start, start));
		buffer->lexy_start = MIN(buffer->lexy_start, start);
		return;
	} else if (buffer->lexy_running == 2) {
		// the lexy thread was preempted by a buffer update (buffer_replace_selection / buffer_undo)
		// update lexy_start and restart the thread
		//printf("%p Restarting at %d %d -> %d\n", buffer, buffer->lexy_start, start, MIN(buffer->lexy_start, start));
		buffer->lexy_start = MIN(buffer->lexy_start, start);
	} else if (buffer->lexy_running == 0) {
		//printf("%p Starting at %d\n", buffer, start);
		buffer->lexy_start = start;
	}

	if (quick_exit) {
		if (abs(buffer->lexy_start - start) > 1000) {
			quick_exit = false;
		}
	}

	buffer->lexy_running = 1;
	buffer->lexy_quick_exit = quick_exit;
	pthread_t thread;
	if (pthread_create(&thread, &lexy_thread_attrs, lexy_update_starting_at_thread, buffer) != 0) {
		buffer->lexy_running = 0;
		perror("Can not start new thread");
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

		if ((row->match_kind != LM_REGEXP) && (row->match_kind != LM_REGEXP_SPACE)) continue;

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
