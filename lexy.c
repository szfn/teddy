#include "lexy.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <tre/tre.h>

#include "global.h"
#include "interp.h"
#include "treint.h"

int lexy_colors[0xff];

/*
Documentation of TCL interface

The main command is:

 lexydef <lexy name> <lexy state> <transition list> <lexy state> <transition list>...

where <lexy name> and every <lexy state> are arbitrary strings, that must not contain ':', and:

 <transition list> ::= <transition>...
 <transition> ::= <pattern> <lexy state and token type>
 <pattern> ::= any regular expression
 <lexy state and token type> ::= <token type> | <lexy state>:<token type>
 <token type> ::= nothing | keyword | id | comment | string

this expands into:

 {
  lexydef-create <lexy name>
  lexydef-append <lexy name> <lexy state> <pattern> <next lexy state> <token type>
  lexydef-append <lexy name> <lexy state> <pattern> <next lexy state> <token type>
  lexydef-append <lexy name> <lexy state> <pattern> <next lexy state> <token type>
  ...
 }

In essence tokenizers are defined as state machines, lexydef-create creates a new "empty" tokenizer and gives it a name, lexydef-append adds a transition to the state machine of a tokenizer. The transition consists of an initial state (<lexy state>) a <pattern>, that needs to be matched on the input stream for the transition to happen, a final state (<next lexy state>), that indicates the state the tokenizer should go to when the <pattern> is matched in <lexy state> and finally a <token type> which is the <token type> that the string matched by <pattern> will be marked with when this transition triggers.

Finally the command:

  lexyassoc <lexy name> <extension>

Associates one tokenizer with a file extension.
*/

#define LEXY_ROW_NUMBER 1024
#define LEXY_STATUS_NUMBER 0xff
#define LEXY_STATUS_NAME_SIZE 256
#define LEXY_TOKENIZER_NUMBER 0xff
#define LEXY_STATE_BLOCK_SIZE 16
#define LEXY_ASSOCIATION_NUMBER 1024

#define LEXY_LOAD_HOOK_MAX_COUNT 1000

const char *CONTINUATION_STATE = "continuation-state";

struct lexy_row {
	bool enabled;
	bool jump;
	regex_t pattern;
	uint8_t token_type;
	uint8_t next_state;
	uint8_t file_group, lineno_group, colno_group;
};

struct lexy_status_pointer {
	char *status_name;
	size_t index;
};

struct lexy_tokenizer {
	char *tokenizer_name;
	struct lexy_row rows[LEXY_ROW_NUMBER];
	struct lexy_status_pointer status_pointers[LEXY_STATUS_NUMBER];
	const char *link_fn;
	bool verify_file;
};

struct lexy_association {
	char *extension;
	struct lexy_tokenizer *tokenizer;
};

struct lexy_tokenizer lexy_tokenizers[LEXY_TOKENIZER_NUMBER];
struct lexy_association lexy_associations[LEXY_ASSOCIATION_NUMBER];

void lexy_init(void) {
	for (int i = 0; i < LEXY_TOKENIZER_NUMBER; ++i) {
		lexy_tokenizers[i].tokenizer_name = NULL;
		lexy_tokenizers[i].rows[0].enabled = false;
		lexy_tokenizers[i].rows[0].jump = false;
		lexy_tokenizers[i].status_pointers[0].status_name = NULL;
		lexy_tokenizers[i].link_fn = "teddy_intl::link_open";
		lexy_tokenizers[i].verify_file = true;
	}

	for (int i = 0; i < LEXY_ASSOCIATION_NUMBER; ++i) {
		lexy_associations[i].extension = NULL;
	}

	for (int i = 0; i < 0xff; ++i) {
		lexy_colors[i] = 0;
	}
}

static struct lexy_tokenizer *find_tokenizer(const char *tokenizer_name) {
	for (int i = 0; i < LEXY_TOKENIZER_NUMBER; ++i) {
		if (lexy_tokenizers[i].tokenizer_name == NULL) return NULL;
		if (strcmp(lexy_tokenizers[i].tokenizer_name, tokenizer_name) == 0) return lexy_tokenizers+i;
	}
	return NULL;
}

int lexy_create_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if ((argc < 2) || (argc > 4)) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'lexydef-create', usage: 'lexydef-create <lexy name> [<link open function> <verify file existence>]'");
		return TCL_ERROR;
	}

	const char *name = argv[1];

	if (find_tokenizer(name) != NULL) return TCL_OK;

	int i;
	for (i = 0; i < LEXY_TOKENIZER_NUMBER; ++i) {
		if (lexy_tokenizers[i].tokenizer_name == NULL) {
			break;
		}
	}

	if (i >= LEXY_TOKENIZER_NUMBER) {
		Tcl_AddErrorInfo(interp, "Too many tokenizers defined");
		return TCL_ERROR;
	} else {
		//printf("created tokenizer: %s\n", name);
		lexy_tokenizers[i].tokenizer_name = strdup(name);
		alloc_assert(lexy_tokenizers[i].tokenizer_name);

		if (argc == 4) {
			char *t = strdup(argv[2]);
			alloc_assert(t);
			lexy_tokenizers[i].link_fn = t;

			lexy_tokenizers[i].verify_file = atoi(argv[3]);
		}
		return TCL_OK;
	}
}

static int find_state(struct lexy_tokenizer *tokenizer, const char *status_name) {
	for (int i = 0; i < LEXY_STATUS_NUMBER; ++i) {
		if (tokenizer->status_pointers[i].status_name == NULL) return -1;
		if (strcmp(tokenizer->status_pointers[i].status_name, status_name) == 0) return i;
	}
	return -1;
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

static int parse_token_type_name(const char *token_type_name, int *file_group, int *lineno_group, int *colno_group) {
	*file_group = 0; *lineno_group = 0; *colno_group = 0;

	if (strcmp(token_type_name, "nothing") == 0) return 0;
	if (strcmp(token_type_name, "keyword") == 0) return CFG_LEXY_KEYWORD - CFG_LEXY_NOTHING;
	if (strcmp(token_type_name,"id") == 0) return CFG_LEXY_ID - CFG_LEXY_NOTHING;
	if (strcmp(token_type_name, "identifier") == 0) return CFG_LEXY_ID - CFG_LEXY_NOTHING;
	if (strcmp(token_type_name, "comment") == 0) return CFG_LEXY_COMMENT - CFG_LEXY_NOTHING;
	if (strcmp(token_type_name, "string") == 0) return CFG_LEXY_STRING - CFG_LEXY_NOTHING;
	if (strcmp(token_type_name, "literal") == 0) return CFG_LEXY_LITERAL - CFG_LEXY_NOTHING;

	if (strncmp(token_type_name, "file", strlen("file")) == 0) {
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
		return CFG_LEXY_FILE - CFG_LEXY_NOTHING;
	}
	return -1;
}

static int create_new_state(struct lexy_tokenizer *tokenizer, const char *state_name) {
	int next_row_block = 0;
	for (int i = 0; i < LEXY_STATUS_NUMBER; ++i) {
		if (tokenizer->status_pointers[i].status_name == NULL) {
			if (next_row_block + LEXY_STATE_BLOCK_SIZE > LEXY_ROW_NUMBER) {
				// Not enough row space
				return -1;
			}
			tokenizer->status_pointers[i].status_name = strdup(state_name);
			alloc_assert(tokenizer->status_pointers[i].status_name);
			tokenizer->status_pointers[i].index = next_row_block;
			return i;
		} else {
			next_row_block = tokenizer->status_pointers[i].index + LEXY_STATE_BLOCK_SIZE;
		}
	}
	return -1;
}

static struct lexy_row *new_row_for_state(struct lexy_tokenizer *tokenizer, int state) {
	//printf("Searching a row for state: %d\n", state);
	int base = tokenizer->status_pointers[state].index;
	for (int offset = 0; offset < LEXY_STATE_BLOCK_SIZE; ++offset) {
		struct lexy_row *row = tokenizer->rows + base + offset;
		//printf("\toffset: %d\n", offset);
		if (!(row->enabled)) {
			// we found an empty row, we will use this unless it's right at the end of the block, in that
			// case a new block needs to be allocated (the very last row of a block is used to store a pointer)
			if (offset == LEXY_STATE_BLOCK_SIZE-1) {
				//printf("\tLast empty row, filling and creating new state\n");
				int continuation_state = create_new_state(tokenizer, CONTINUATION_STATE);
				if (continuation_state < 0) {
					return NULL;
				}
				row->enabled = true;
				row->jump = true;
				row->next_state = continuation_state;
				row = tokenizer->rows + tokenizer->status_pointers[state].index;
			}
			//printf("\tempty row\n");
			return row;
		}
		if (row->jump) {
			// follow the jump
			//printf("\tFollowing jump, current state now: %d\n", row->next_state);
			base = tokenizer->status_pointers[row->next_state].index;
			offset = -1;
		}
	}
	//printf("FAILED!\n");
	return NULL;
}

int lexy_append_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 6) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'lexydef-append', usage: 'lexydef-append <lexy name> <lexy state> <pattern> <next lexy state> <token type>'");
		return TCL_ERROR;
	}

	const char *tokenizer_name = argv[1];
	const char *start_state_name = argv[2];
	const char *pattern = argv[3];
	const char *next_state_name = argv[4];
	const char *token_type_name = argv[5];

	struct lexy_tokenizer *tokenizer = find_tokenizer(tokenizer_name);
	if (tokenizer == NULL) {
		Tcl_AddErrorInfo(interp, "Couldn't find tokenizer");
		return TCL_ERROR;
	}

	int start_state = find_state(tokenizer, start_state_name);
	if (start_state < 0) start_state = create_new_state(tokenizer, start_state_name);
	if (start_state < 0) {
		Tcl_AddErrorInfo(interp, "Out of state space for tokenizer");
		return TCL_ERROR;
	}

	int next_state = find_state(tokenizer, next_state_name);
	if (next_state < 0) next_state = create_new_state(tokenizer, next_state_name);
	if (next_state < 0) {
		Tcl_AddErrorInfo(interp, "Out of state space for tokenizer");
		return TCL_ERROR;
	}

	int file_group, lineno_group, colno_group;
	int token_type = parse_token_type_name(token_type_name, &file_group, &lineno_group, &colno_group);
	if (token_type < 0) {
		Tcl_AddErrorInfo(interp, "Unknown token type");
		return TCL_ERROR;
	}

	char *fixed_pattern = malloc(sizeof(char) * (strlen(pattern) + strlen("^(?:)") + 1));
	alloc_assert(fixed_pattern);
	strcpy(fixed_pattern, "^(?:");
	strcat(fixed_pattern, pattern);
	strcat(fixed_pattern, ")");

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

	struct lexy_row *new_row = new_row_for_state(tokenizer, start_state);
	if (new_row == NULL) {
		Tcl_AddErrorInfo(interp, "Out of row space");
		return TCL_ERROR;
	}

	new_row->next_state = next_state;
	new_row->token_type = token_type;
	tre_regcomp(&(new_row->pattern), fixed_pattern, REG_EXTENDED);
	new_row->enabled = true;
	new_row->file_group = file_group;
	new_row->lineno_group = lineno_group;
	new_row->colno_group = colno_group;

	free(fixed_pattern);

	return TCL_OK;
}

static void lexy_dump_table(void) {
	printf("Dumping lexy table:\n");
	for (int i = 0; i < LEXY_TOKENIZER_NUMBER; ++i) {
		struct lexy_tokenizer *tokenizer = lexy_tokenizers + i;
		if (tokenizer->tokenizer_name == NULL) {
			printf("End\n");
			break;
		}
		printf("Tokenizer %s:\n", tokenizer->tokenizer_name);
		for (int j = 0; j < LEXY_STATUS_NUMBER; ++j) {
			struct lexy_status_pointer *status_pointer = tokenizer->status_pointers + j;
			if (status_pointer->status_name == NULL) {
				printf("\tEnd\n");
				break;
			}
			printf("\t[%d] Status [%s] beginning at %zd:\n", j, status_pointer->status_name, status_pointer->index);
			for (int offset = 0; offset < LEXY_STATE_BLOCK_SIZE; ++offset) {
				struct lexy_row *row = tokenizer->rows + status_pointer->index + offset;
				if (!(row->enabled)) {
					printf("\t\tEND\n");
					break;
				}
				printf("\t\t[%zd] ", status_pointer->index + offset);
				if (row->jump) {
					printf("JUMP %d\n", row->next_state);
				} else {
					printf("PATTERN-MATCH %d JUMP %d\n", row->token_type, row->next_state);
				}
			}
		}
	}
}

static void lexy_dump_buffer(buffer_t *buffer) {
	for (real_line_t *line = buffer->real_line; line != NULL; line = line->next) {
		uint8_t last_token = 0xff;
		int printed = 0;

		for (int glyph = 0; glyph < line->cap; ++glyph) {
			if (line->glyph_info[glyph].color != last_token) {
				if (last_token != 0xff) printf("]\n");
				printed = 0;
				last_token = line->glyph_info[glyph].color;
				printf("Token %d [", last_token);
			}
			if (printed < 20) {
				uint32_t code = line->glyph_info[glyph].code;
				printf("%c", (code >= 0x20) && (code <= 0x7f) ? (char)code : '?');
				++printed;
				if (printed >= 20) {
					printf("...");
				}
			}
		}

		if (last_token != 0xff) {
			printf("]\n");
			printed = 0;
		}
	}
}

int lexy_dump_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc < 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to lexy_dump, specify \"table\" or \"buffer\"");
		return TCL_ERROR;
	}

	if (strcmp(argv[1], "table") == 0) {
		lexy_dump_table();
		return TCL_OK;
	} else if (strcmp(argv[1], "buffer") == 0) {
		if (interp_context_buffer() == NULL) {
			Tcl_AddErrorInfo(interp, "lexy_dump buffer called when no editor was active");
			return TCL_ERROR;
		}

		lexy_dump_buffer(interp_context_buffer());
		return TCL_OK;
	} else {
		Tcl_AddErrorInfo(interp, "Wrong argument to lexy_dump, specify \"table\" or \"buffer\"");
		return TCL_ERROR;
	}
}

int lexy_assoc_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'lexyassoc': usage 'lexyassoc <lexy-name> <extension>'");
		return TCL_ERROR;
	}

	const char *lexy_name = argv[1];
	const char *extension = argv[2];

	struct lexy_tokenizer *tokenizer = find_tokenizer(lexy_name);
	if (tokenizer == NULL) {
		Tcl_AddErrorInfo(interp, "Cannot find tokenizer");
		return TCL_ERROR;
	}

	for (int i = 0; i < LEXY_ASSOCIATION_NUMBER; ++i) {
		if ((lexy_associations[i].extension == NULL) || (strcmp(lexy_associations[i].extension, extension) == 0)) {
			lexy_associations[i].extension = strdup(extension);
			lexy_associations[i].tokenizer = tokenizer;
			return TCL_OK;
		}
	}

	Tcl_AddErrorInfo(interp, "Out of association space");
	return TCL_ERROR;
}

static struct lexy_tokenizer *tokenizer_from_buffer(buffer_t *buffer) {
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
			return a->tokenizer;
		}
	}
	return NULL;
}

static bool check_file_match(struct lexy_tokenizer *tokenizer, struct lexy_row *row, buffer_t *buffer, real_line_t *line, int glyph, int nmatch, regmatch_t *pmatch) {
	//printf("file_group %d nmatch %d (%d)\n", row->file_group, nmatch, tokenizer->verify_file);
	if (!tokenizer->verify_file) return true;
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

static int lexy_parse_token(struct lexy_tokenizer *tokenizer, int state, const char *text, char **file, char **line, char **col) {
	//printf("Starting match\n");

	int base = tokenizer->status_pointers[state].index;
	for (int offset = 0; offset < LEXY_STATE_BLOCK_SIZE; ++offset) {
		struct lexy_row *row = tokenizer->rows + base + offset;

		if (!(row->enabled)) return CFG_LEXY_NOTHING;

		if (row->jump)	{
			base = tokenizer->status_pointers[row->next_state].index;
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

	struct lexy_tokenizer *tokenizer = tokenizer_from_buffer(interp_context_buffer());
	if (tokenizer != NULL) {
		int state = find_state(tokenizer, argv[1]);
		if (state != -1) {
			r = lexy_parse_token(tokenizer, state, argv[2], &file, &line, &col);
		}
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

static void lexy_update_one_token(buffer_t *buffer, real_line_t *line, int *glyph, struct lexy_tokenizer *tokenizer, int *state) {
	int base = tokenizer->status_pointers[*state].index;
	for (int offset = 0; offset < LEXY_STATE_BLOCK_SIZE; ++offset) {
		struct lexy_row *row = tokenizer->rows + base + offset;
		//printf("\t\toffset = %d base = %d enabled = %d\n", offset, base, row->enabled);
		if (!(row->enabled)) {
			line->glyph_info[*glyph].color = 0;
			++(*glyph);
			return;
		}
		if (row->jump) {
			base = tokenizer->status_pointers[row->next_state].index;
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
				if (!check_file_match(tokenizer, row, buffer, line, *glyph, NMATCH, pmatch)) {
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
			*state = row->next_state;
			return;
		}
	}
}

static void lexy_update_line(buffer_t *buffer, real_line_t *line, struct lexy_tokenizer *tokenizer, int *state) {
	line->lexy_state_start = *state;
	for (int glyph = 0; glyph < line->cap; ) {
		//printf("\tStarting at %d\n", glyph);
		lexy_update_one_token(buffer, line, &glyph, tokenizer, state);
	}
	line->lexy_state_end = *state;
}

void lexy_update_starting_at(buffer_t *buffer, real_line_t *start_line, bool quick_exit) {
	if (start_line == NULL) return;
	struct lexy_tokenizer *tokenizer = tokenizer_from_buffer(buffer);

	//printf("Buffer <%s> tokenizer <%s>\n",  buffer->path, (tokenizer != NULL) ? tokenizer->tokenizer_name : "(null)");

	if (tokenizer == NULL) return;

	int count = 0;
	int state = 0xff;

	real_line_t *line;
	for (line = start_line; line != NULL; line = line->next) {
		if ((line->lexy_state_start != state) || (line->lexy_state_start == 0xff) || (count < 2)) {
			if (state == 0xff) {
				if (line->lexy_state_start == 0xff) line->lexy_state_start = 0;
				state = line->lexy_state_start;
			}
			//printf("\tUpdating line: %p\n", line);
			lexy_update_line(buffer, line, tokenizer, &state);
		} else {
			if (quick_exit) return;
		}

		state = line->lexy_state_end;

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

	for(int count = 0; count < 10; ++count) { // count is here just to prevent this loop from running forever on patological situations
		lexy_update_starting_at(buffer, start_line, true);
		start_line = buffer->lexy_last_update_line;
		if (start_line == NULL) break;
		if (start_line->lineno >= buffer->cursor.line->lineno) break;
	}
}

const char *lexy_get_link_fn(buffer_t *buffer) {
	struct lexy_tokenizer *tokenizer = tokenizer_from_buffer(buffer);
	if (tokenizer == NULL) return "";
	return tokenizer->link_fn;
}

