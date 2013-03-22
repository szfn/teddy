#include "plumb.h"

#include <string.h>

#include "global.h"
#include "lexy.h"
#include "interp.h"

#define MAX_PLUMB_RULES 4086

enum cond_subject {
	CS_TYPE = 0,
	CS_OBUF_NAME,
	CS_TEXT,
	CS_TOKEN_TYPE,
	CS_FILE,
	CS_LINENO,
	CS_COLNO,
	CS_ACTION,
	CS_MAX,
};

const char *subject_table[] = {
	/*CS_TYPE*/ "type",
	/*CS_OBUF_NAME*/ "bufname",
	/*CS_TEXT*/ "text",
	/*CS_TOKEN_TYPE*/ "toktype",
	/*CS_FILE*/ "file",
	/*CS_LINENO*/ "line",
	/*CS_COLNO*/ "col",
};

enum cond_op {
	CO_NONE = 0,
	CO_IS,
	CO_ISNOT,
	CO_MATCH,
	CO_MATCHNOT,
	CO_MAX,
};

const char *op_table[] = {
	/*CO_NONE*/ "",
	/*CO_IS*/ "is",
	/*CO_ISNOT*/ "isnot",
	/*CO_MATCH*/ "match",
	/*CO_MATCHNOT*/ "matchnot",
};

struct plumb_rule_t {
	enum cond_subject subject;
	enum cond_op op;
	char *arg;
} plumb_rules[MAX_PLUMB_RULES];

struct plumb_target_t {
	buffer_t *buffer;
	const char *type;
	const char *bufname;
	const char *text;
	const char *toktype;
	char *file;
	char *lineno;
	char *colno;
};


static int num_plumb_rules = 0;

enum plumb_resolve_state {
	PRS_MATCHING = 0,
	PRS_MATCH_FAILED,
};

static bool precond_match(struct plumb_target_t *tgt, struct plumb_rule_t *rule) {
	const char *fld = NULL;

	switch (rule->subject) {
	case CS_TYPE:
		fld = tgt->type;
		break;
	case CS_OBUF_NAME:
		fld = tgt->bufname;
		break;
	case CS_TEXT:
		fld = tgt->text;
		break;
	case CS_TOKEN_TYPE:
		fld = tgt->toktype;
		break;
	case CS_FILE:
		fld = tgt->file;
		break;
	case CS_LINENO:
		fld = tgt->lineno;
		break;
	case CS_COLNO:
		fld = tgt->colno;
		break;
	default:
		return false;
	}

	if (fld == NULL) return false;

	switch (rule->op) {
	case CO_IS:
		return (strcmp(fld, rule->arg) == 0);
	case CO_ISNOT:
		return (strcmp(fld, rule->arg) != 0);
	case CO_MATCH:
	case CO_MATCHNOT: {
		regex_t pat;
		int r = tre_regcomp(&pat, rule->arg, REG_EXTENDED);
		if (r != REG_OK) {
			return false;
		}
#define NMATCH 10
		regmatch_t pmatch[NMATCH];
		r = tre_regexec(&pat, fld, NMATCH, pmatch, 0);
		tre_regfree(&pat);
		if (rule->op == CO_MATCH) {
			return r == REG_OK;
		}
		return r != REG_OK;
	}
	default:
		return false;
	}

	return true;
}

static bool run_rule(struct plumb_target_t *target, struct plumb_rule_t *rule) {
	Tcl_SetVar(interp, "type", target->type, 0);
	Tcl_SetVar(interp, "bufname", target->bufname, 0);
	Tcl_SetVar(interp, "text", target->text, 0);
	Tcl_SetVar(interp, "toktype", target->toktype, 0);
	Tcl_SetVar(interp, "linkfile", target->file, 0);
	Tcl_SetVar(interp, "line", target->lineno, 0);
	Tcl_SetVar(interp, "col", target->colno, 0);

	int code = interp_eval(NULL, target->buffer, rule->arg, false, false);
	Tcl_ResetResult(interp);
	if (code == TCL_ERROR) return false;

	return true;
}

void plumb(buffer_t *buffer, bool islink, const char *text) {
	struct plumb_target_t target;

	target.buffer = buffer;
	target.type = islink ? "link" : "selection";
	if (buffer != NULL) {
		target.bufname =  buffer->path;
	} else {
		target.bufname = "";
	}
	target.text = text;

	int lexy_state = islink ? lexy_start_status_for_buffer(buffer) : lexy_find_status("filesearch/0");

	target.file = NULL;
	target.lineno = NULL;
	target.colno = NULL;

	if (lexy_state >= 0) {
		int token = lexy_parse_token(lexy_state, text, &(target.file), &(target.lineno), &(target.colno));
		target.toktype = deparse_token_type_name(token);
		if (target.file == NULL) target.file = strdup(text);
		if (target.lineno == NULL) target.lineno = strdup("");
		if (target.colno == NULL) target.colno = strdup("");
	} else {
		target.toktype = "nothing";
		target.file = strdup(text);
		target.lineno = strdup("");
		target.colno = strdup("");
	}


	enum plumb_resolve_state state = PRS_MATCHING;
	for (int i = 0; i < num_plumb_rules; ++i) {
		struct plumb_rule_t *rule = plumb_rules + i;
		switch (state) {
			case PRS_MATCHING:
				if (rule->subject == CS_ACTION) {
					if (run_rule(&target, rule)) {
						goto plumb_done;
					}
					// else we keep trying
				} else {
					if (!precond_match(&target, rule)) {
						state = PRS_MATCH_FAILED;
					}
				}
				break;

			case PRS_MATCH_FAILED:
				// skip to next rule
				if (rule->subject == CS_ACTION) {
					state = PRS_MATCHING;
				}
				break;
		}
	}

plumb_done:
	free(target.file);
	free(target.lineno);
	free(target.colno);
	return;
}

int teddy_plumb_command_plumb(Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 3) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to \"teddy::plumb plumb\"");
		return TCL_ERROR;
	}

	plumb(interp_context_buffer(), false, argv[2]);
	return TCL_OK;
}

static bool add_precondition(char *precln) {
	char *sp;

	char *subjstr = strtok_r(precln, " \t", &sp);
	if (subjstr == NULL) return true;
	if (strcmp(subjstr, "") == 0) return true;

	char *opstr = strtok_r(NULL, " \t", &sp);
	if (opstr == NULL) return false;

	char *argstr = strtok_r(NULL, "", &sp);
	if (argstr == NULL) return false;

	enum cond_subject subj;
	for (subj = 0; subj < CS_MAX; ++subj) {
		if (strcmp(subjstr, subject_table[subj]) == 0) break;
	}
	if (subj >= CS_MAX) return false;

	enum cond_op op;
	for (op = 0; op < CO_MAX; ++op) {
		if (strcmp(opstr, op_table[op]) == 0) break;
	}
	if (op >= CO_MAX) return false;
	if (op == CO_NONE) return false;

	struct plumb_rule_t *rule = plumb_rules + (num_plumb_rules++);
	rule->subject = subj;
	rule->op = op;
	rule->arg = strdup(argstr);
	alloc_assert(rule->arg);

	return true;
}

static bool add_preconditions(char *prec) {
	char *sp, *tok;
	for (tok = strtok_r(prec, "\n", &sp); tok != NULL; tok = strtok_r(NULL, "\n", &sp)) {
		if (!add_precondition(tok)) {
			return false;
		}
	}
	return true;
}

static void add_body(const char *body) {
	struct plumb_rule_t *rule = plumb_rules + (num_plumb_rules++);
	rule->arg = strdup(body);
	alloc_assert(rule->arg);
	rule->subject = CS_ACTION;
	rule->op = CO_NONE;
}

int teddy_plumb_command_addrule(Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc != 5) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to \"teddy::plumb addrule\"");
		return TCL_ERROR;
	}

	const char *doarg = argv[3];

	if (strcmp(doarg, "do") != 0) {
		Tcl_AddErrorInfo(interp, "Wrong argument to \"teddy::plumb addrule\" (first argument should be followed by \"do\")");
		return TCL_ERROR;
	}

	int start = num_plumb_rules;

	char *prec = strdup(argv[2]);

	if (!add_preconditions(prec)) {
		for (int i = start; i < num_plumb_rules; ++i) {
			if (plumb_rules[i].arg != NULL) {
				free(plumb_rules[i].arg);
			}
		}
		num_plumb_rules = start;
		Tcl_AddErrorInfo(interp, "Could not add rule");
		return TCL_ERROR;
	} else {
		add_body(argv[4]);
	}

	free(prec);

	return TCL_OK;
}

int teddy_plumb_command(ClientData client_data, Tcl_Interp *interp, int argc, const char *argv[]) {
	if (argc < 2) {
		Tcl_AddErrorInfo(interp, "Wrong number of arguments to \"teddy::plumb\"");
		return TCL_ERROR;
	}

	if (strcmp(argv[1], "plumb") == 0) {
		return teddy_plumb_command_plumb(interp, argc, argv);
	} else if (strcmp(argv[1], "addrule") == 0) {
		return teddy_plumb_command_addrule(interp, argc, argv);
	}

	Tcl_AddErrorInfo(interp, "Wrong argument to \"teddy::plumb\"");
	return TCL_ERROR;
}

