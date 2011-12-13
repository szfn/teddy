#include "lexy.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <tre/tre.h>

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

 TODO: add an example here

Finally the command:

  lexyassoc <lexy name> <extension>

Associates one tokenizer with a file extension.
*/

#define LEXY_ROW_NUMBER 1024
#define LEXY_STATUS_NUMBER 0xff
#define LEXY_STATUS_NAME_SIZE 256
#define LEXY_TOKENIZER_NUMBER 0xff
#define LEXY_STATE_BLOCK_SIZE 16

const char *CONTINUATION_STATE = "continuation-state";

struct lexy_row {
	bool enabled;
	bool jump;
	regex_t pattern;
	uint8_t token_type;
	uint8_t next_state;
};

struct lexy_status_pointer {
	char *status_name;
	size_t index;
};

struct lexy_tokenizer {
	char *tokenizer_name;
	struct lexy_row rows[LEXY_ROW_NUMBER];
	struct lexy_status_pointer status_pointers[LEXY_STATUS_NUMBER];
};

struct lexy_tokenizer lexy_tokenizers[LEXY_TOKENIZER_NUMBER];

void lexy_init(void) {
	for (int i = 0; i < LEXY_TOKENIZER_NUMBER; ++i) {
		lexy_tokenizers[i].tokenizer_name = NULL;
		lexy_tokenizers[i].rows[0].enabled = false;
		lexy_tokenizers[i].rows[0].jump = false;
		lexy_tokenizers[i].status_pointers[0].status_name = NULL;
	}
}

int lexy_create_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to 'lexydef-create', usage: 'lexydef-create <lexy name>'");
		return TCL_ERROR;
	}

	const char *name = argv[1];

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
		lexy_tokenizers[i].tokenizer_name = strdup(name);
		if (!(lexy_tokenizers[i].tokenizer_name)) {
			perror("Out of memory");
			exit(EXIT_FAILURE);
		}
		return TCL_OK;
	}
}

static struct lexy_tokenizer *find_tokenizer(const char *tokenizer_name) {
	for (int i = 0; i < LEXY_TOKENIZER_NUMBER; ++i) {
		if (lexy_tokenizers[i].tokenizer_name == NULL) return NULL;
		if (strcmp(lexy_tokenizers[i].tokenizer_name, tokenizer_name) == 0) return lexy_tokenizers+i;
	}
	return NULL;
}

static int find_state(struct lexy_tokenizer *tokenizer, const char *status_name) {
	for (int i = 0; i < LEXY_STATUS_NUMBER; ++i) {
		if (tokenizer->status_pointers[i].status_name == NULL) return -1;
		if (strcmp(tokenizer->status_pointers[i].status_name, status_name) == 0) return i;
	}
	return -1;
}

static int parse_token_type_name(const char *token_type_name) {
	if (strcmp(token_type_name, "nothing") == 0) return L_NOTHING;
	if (strcmp(token_type_name, "keyword") == 0) return L_KEYWORD;
	if (strcmp(token_type_name,"id") == 0) return L_ID;
	if (strcmp(token_type_name, "identifier") == 0) return L_ID;
	if (strcmp(token_type_name, "comment") == 0) return L_COMMENT;
	if (strcmp(token_type_name, "string") == 0) return L_STRING;
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
			if (!(tokenizer->status_pointers[i].status_name)) {
				perror("Out of memory");
				exit(EXIT_FAILURE);
			}
			tokenizer->status_pointers[i].index = next_row_block;
			return i;
		} else {
			next_row_block = tokenizer->status_pointers[i].index + LEXY_STATE_BLOCK_SIZE;
		}
	}
	return -1;
}

static struct lexy_row *new_row_for_state(struct lexy_tokenizer *tokenizer, int state) {
	int base = tokenizer->status_pointers[state].index;
	for (int offset = 0; offset < LEXY_STATE_BLOCK_SIZE; ++offset) {
		struct lexy_row *row = tokenizer->rows + base + offset;
		if (!(row->enabled)) {
			// we found an empty row, we will use this unless it's right at the end of the block, in that
			// case a new block needs to be allocated (the very last row of a block is used to store a pointer)
			if (offset == LEXY_STATE_BLOCK_SIZE) {
				int continuation_state = create_new_state(tokenizer, CONTINUATION_STATE);
				if (continuation_state < 0) {
					return NULL;
				}
				row->enabled = true;
				row->jump = true;
				row->next_state = continuation_state;
				row = tokenizer->rows + tokenizer->status_pointers[state].index;
			}
			return row;
		}
		if (row->jump) {
			// follow the jump
			base = tokenizer->status_pointers[row->next_state].index;
			offset = -1;
		}
	}
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
	if (next_state < 0) {
		Tcl_AddErrorInfo(interp, "Couldn't find next state name");
		return TCL_ERROR;
	}

	int token_type = parse_token_type_name(token_type_name);
	if (token_type < 0) {
		Tcl_AddErrorInfo(interp, "Unknown token type");
		return TCL_ERROR;
	}

	regex_t compiled_pattern;
	int r = tre_regcomp(&compiled_pattern, pattern, REG_EXTENDED);
	if (r != REG_OK) {
#define REGERROR_BUF_SIZE 512
		char buf[REGERROR_BUF_SIZE];
		tre_regerror(r, &compiled_pattern, buf, REGERROR_BUF_SIZE);
		char *msg;
		asprintf(&msg, "Syntax error in regular expression [%s]: %s\n", pattern, buf);
		if (msg == NULL) {
			perror("Out of memory");
			exit(EXIT_FAILURE);
		}
		Tcl_AddErrorInfo(interp, msg);
		free(msg);
		return TCL_ERROR;
	}
	tre_regfree(&compiled_pattern);

	struct lexy_row *new_row = new_row_for_state(tokenizer, start_state);

	new_row->next_state = next_state;
	new_row->token_type = token_type;
	tre_regcomp(&(new_row->pattern), pattern, REG_EXTENDED);

	return TCL_OK;
}

int lexy_dump_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	//TODO; implement
	return TCL_OK;
}

int lexy_assoc_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	//TODO: implement
	return TCL_OK;
}
