#include "yarp.h"

#define STRINGIZE0(expr) #expr
#define STRINGIZE(expr) STRINGIZE0(expr)
#define YP_VERSION_MACRO STRINGIZE(YP_VERSION_MAJOR) "." STRINGIZE(YP_VERSION_MINOR) "." STRINGIZE(YP_VERSION_PATCH)

char* yp_version(void) {
  return YP_VERSION_MACRO;
}

/******************************************************************************/
/* Debugging                                                                  */
/******************************************************************************/

__attribute__((unused)) static const char *
debug_context(yp_context_t context) {
  switch (context) {
    case YP_CONTEXT_BEGIN: return "BEGIN";
    case YP_CONTEXT_CLASS: return "CLASS";
    case YP_CONTEXT_DEF: return "DEF";
    case YP_CONTEXT_ENSURE: return "ENSURE";
    case YP_CONTEXT_ELSE: return "ELSE";
    case YP_CONTEXT_ELSIF: return "ELSIF";
    case YP_CONTEXT_EMBEXPR: return "EMBEXPR";
    case YP_CONTEXT_FOR: return "FOR";
    case YP_CONTEXT_IF: return "IF";
    case YP_CONTEXT_MAIN: return "MAIN";
    case YP_CONTEXT_MODULE: return "MODULE";
    case YP_CONTEXT_PARENS: return "PARENS";
    case YP_CONTEXT_POSTEXE: return "POSTEXE";
    case YP_CONTEXT_PREEXE: return "PREEXE";
    case YP_CONTEXT_SCLASS: return "SCLASS";
    case YP_CONTEXT_UNLESS: return "UNLESS";
    case YP_CONTEXT_UNTIL: return "UNTIL";
    case YP_CONTEXT_WHILE: return "WHILE";
  }
  return NULL;
}

__attribute__((unused)) static void
debug_contexts(yp_parser_t *parser) {
  yp_context_node_t *context_node = parser->current_context;
  printf("CONTEXTS: ");

  if (context_node != NULL) {
    while (context_node != NULL) {
      printf("%s", debug_context(context_node->context));
      context_node = context_node->prev;
      if (context_node != NULL) {
        printf(" <- ");
      }
    }
  } else {
    printf("NONE");
  }

  printf("\n");
}

__attribute__((unused)) static void
debug_node(const char *message, yp_parser_t *parser, yp_node_t *node) {
  yp_buffer_t buffer;
  yp_buffer_init(&buffer);
  yp_prettyprint(parser, node, &buffer);

  printf("%s\n%.*s\n", message, (int) buffer.length, buffer.value);
  yp_buffer_free(&buffer);
}

__attribute__((unused)) static void
debug_lex_mode(yp_parser_t *parser) {
  switch (parser->lex_modes.current->mode) {
    case YP_LEX_DEFAULT: printf("lexing in DEFAULT mode\n"); return;
    case YP_LEX_EMBDOC: printf("lexing in EMBDOC mode\n"); return;
    case YP_LEX_EMBEXPR: printf("lexing in EMBEXPR mode\n"); return;
    case YP_LEX_LIST: printf("lexing in LIST mode\n"); return;
    case YP_LEX_REGEXP: printf("lexing in REGEXP mode\n"); return;
    case YP_LEX_STRING: printf("lexing in STRING mode\n"); return;
    case YP_LEX_SYMBOL: printf("lexing in SYMBOL mode\n"); return;
  }
}

/******************************************************************************/
/* Basic character checks                                                     */
/******************************************************************************/

static inline bool
char_is_binary_number(const char *c) {
  return *c == '0' || *c == '1';
}

static inline bool
char_is_octal_number(const char *c) {
  return *c >= '0' && *c <= '7';
}

static inline bool
char_is_decimal_number(const char *c) {
  return *c >= '0' && *c <= '9';
}

static inline bool
char_is_hexadecimal_number(const char *c) {
  return (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f') || (*c >= 'A' && *c <= 'F');
}

static inline size_t
char_is_identifier_start(yp_parser_t *parser, const char *c) {
  return (*c == '_') ? 1 : parser->encoding.alpha_char(c);
}

static inline size_t
char_is_identifier(yp_parser_t *parser, const char *c) {
  size_t width;
  return (width = parser->encoding.alnum_char(c)) ? width : char_is_identifier_start(parser, c);
}

static inline bool
char_is_non_newline_whitespace(const char *c) {
  return *c == ' ' || *c == '\t' || *c == '\f' || *c == '\r' || *c == '\v';
}

static inline bool
char_is_whitespace(const char *c) {
  return char_is_non_newline_whitespace(c) || *c == '\n';
}

static inline bool
char_is_symbol(const char *c) {
  return *c == '{' || *c == '}' || *c == '(' || *c == ')' || *c == '[' || *c == ']' || *c == '<' || *c == '>' ||
  *c == '!' || *c == '@' || *c == '#' || *c == '$' || *c == '%' || *c == '^' || *c == '&' || *c == '*' ||
  *c == '_' || *c == '+' || *c == '-' || *c == '|' || *c == '\\' || *c == ':' || *c == ';' || *c == '"' ||
  *c == '\'' || *c == ',' || *c == '.';
}

/******************************************************************************/
/* Lexer check helpers                                                        */
/******************************************************************************/

// If the character to be read matches the given value, then returns true and
// advanced the current pointer.
static inline bool
match(yp_parser_t *parser, char value) {
  if (parser->current.end < parser->end && *parser->current.end == value) {
    parser->current.end++;
    return true;
  }
  return false;
}

// Returns the matching character that should be used to terminate a list
// beginning with the given character.
static char
terminator(const char start) {
  switch (start) {
    case '(':
      return ')';
    case '[':
      return ']';
    case '{':
      return '}';
    case '<':
      return '>';
    default:
      return start;
  }
}

/******************************************************************************/
/* Lex mode manipulations                                                     */
/******************************************************************************/

// Push a new lex state onto the stack. If we're still within the pre-allocated
// space of the lex state stack, then we'll just use a new slot. Otherwise we'll
// allocate a new pointer and use that.
static void
lex_mode_push(yp_parser_t *parser, yp_lex_mode_t lex_mode) {
  lex_mode.prev = parser->lex_modes.current;
  parser->lex_modes.index++;

  if (parser->lex_modes.index > YP_LEX_STACK_SIZE - 1) {
    parser->lex_modes.current = (yp_lex_mode_t *) malloc(sizeof(yp_lex_mode_t));
  } else {
    parser->lex_modes.stack[parser->lex_modes.index] = lex_mode;
    parser->lex_modes.current = &parser->lex_modes.stack[parser->lex_modes.index];
  }
}

// Pop the current lex state off the stack. If we're within the pre-allocated
// space of the lex state stack, then we'll just decrement the index. Otherwise
// we'll free the current pointer and use the previous pointer.
static void
lex_mode_pop(yp_parser_t *parser) {
  if (parser->lex_modes.index == 0) {
    parser->lex_modes.current->mode = YP_LEX_DEFAULT;
  } else if (parser->lex_modes.index < YP_LEX_STACK_SIZE) {
    parser->lex_modes.index--;
    parser->lex_modes.current = &parser->lex_modes.stack[parser->lex_modes.index];
  } else {
    parser->lex_modes.index--;
    yp_lex_mode_t *prev = parser->lex_modes.current->prev;
    free(parser->lex_modes.current);
    parser->lex_modes.current = prev;
  }
}

/******************************************************************************/
/* Specific token lexers                                                      */
/******************************************************************************/

static yp_token_type_t
lex_optional_float_suffix(yp_parser_t *parser) {
  yp_token_type_t type = YP_TOKEN_INTEGER;

  // Here we're going to attempt to parse the optional decimal portion of a
  // float. If it's not there, then it's okay and we'll just continue on.
  if (*parser->current.end == '.') {
    if ((parser->current.end + 1 < parser->end) && char_is_decimal_number(parser->current.end + 1)) {
      parser->current.end += 2;
      while (char_is_decimal_number(parser->current.end)) {
        parser->current.end++;
        match(parser, '_');
      }

      type = YP_TOKEN_FLOAT;
    } else {
      // If we had a . and then something else, then it's not a float suffix on
      // a number it's a method call or something else.
      return type;
    }
  }

  // Here we're going to attempt to parse the optional exponent portion of a
  // float. If it's not there, it's okay and we'll just continue on.
  if (match(parser, 'e') || match(parser, 'E')) {
    (void) (match(parser, '+') || match(parser, '-'));

    if (char_is_decimal_number(parser->current.end)) {
      parser->current.end++;
      while (char_is_decimal_number(parser->current.end)) {
        parser->current.end++;
        match(parser, '_');
      }

      type = YP_TOKEN_FLOAT;
    } else {
      return YP_TOKEN_INVALID;
    }
  }

  return type;
}

static yp_token_type_t
lex_numeric_prefix(yp_parser_t *parser) {
  yp_token_type_t type = YP_TOKEN_INTEGER;

  if (parser->current.end[-1] == '0') {
    switch (*parser->current.end) {
      // 0d1111 is a decimal number
      case 'd':
      case 'D':
        if (!char_is_decimal_number(++parser->current.end)) return YP_TOKEN_INVALID;
        while (char_is_decimal_number(parser->current.end)) {
          parser->current.end++;
          match(parser, '_');
        }
        break;

      // 0b1111 is a binary number
      case 'b':
      case 'B':
        if (!char_is_binary_number(++parser->current.end)) return YP_TOKEN_INVALID;
        while (char_is_binary_number(parser->current.end)) {
          parser->current.end++;
          match(parser, '_');
        }
        break;

      // 0o1111 is an octal number
      case 'o':
      case 'O':
        if (!char_is_octal_number(++parser->current.end)) return YP_TOKEN_INVALID;
        // fall through

      // 01111 is an octal number
      case '_':
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
        match(parser, '_');
        while (char_is_octal_number(parser->current.end)) {
          parser->current.end++;
          match(parser, '_');
        }
        break;

      // 0x1111 is a hexadecimal number
      case 'x':
      case 'X':
        if (!char_is_hexadecimal_number(++parser->current.end)) return YP_TOKEN_INVALID;
        while (char_is_hexadecimal_number(parser->current.end)) {
          parser->current.end++;
          match(parser, '_');
        }
        break;

      // 0.xxx is a float
      case '.': {
        type = lex_optional_float_suffix(parser);
        break;
      }

      // 0exxx is a float
      case 'e':
      case 'E': {
        type = lex_optional_float_suffix(parser);
        break;
      }
    }
  } else {
    // If it didn't start with a 0, then we'll lex as far as we can into a
    // decimal number.
    match(parser, '_');
    while (char_is_decimal_number(parser->current.end)) {
      parser->current.end++;
      match(parser, '_');
    }

    // Afterward, we'll lex as far as we can into an optional float suffix.
    type = lex_optional_float_suffix(parser);
  }

  // If the last character that we consumed was an underscore, then this is
  // actually an invalid integer value, and we should return an invalid token.
  if (parser->current.end[-1] == '_') return YP_TOKEN_INVALID;
  return type;
}

static yp_token_type_t
lex_numeric(yp_parser_t *parser) {
  yp_token_type_t type = lex_numeric_prefix(parser);

  if (type != YP_TOKEN_INVALID) {
    if (match(parser, 'r')) type = YP_TOKEN_RATIONAL_NUMBER;
    if (match(parser, 'i')) type = YP_TOKEN_IMAGINARY_NUMBER;
  }

  return type;
}

static yp_token_type_t
lex_global_variable(yp_parser_t *parser) {
  switch (*parser->current.end) {
    case '~':  // $~: match-data
    case '*':  // $*: argv
    case '$':  // $$: pid
    case '?':  // $?: last status
    case '!':  // $!: error string
    case '@':  // $@: error position
    case '/':  // $/: input record separator
    case '\\': // $\: output record separator
    case ';':  // $;: field separator
    case ',':  // $,: output field separator
    case '.':  // $.: last read line number
    case '=':  // $=: ignorecase
    case ':':  // $:: load path
    case '<':  // $<: reading filename
    case '>':  // $>: default output handle
    case '\"': // $": already loaded files
      parser->current.end++;
      return YP_TOKEN_GLOBAL_VARIABLE;

    case '&':  // $&: last match
    case '`':  // $`: string before last match
    case '\'': // $': string after last match
    case '+':  // $+: string matches last paren.
      parser->current.end++;
      return YP_TOKEN_BACK_REFERENCE;

    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      do {
        parser->current.end++;
      } while (char_is_decimal_number(parser->current.end));
      return YP_TOKEN_NTH_REFERENCE;

    default:
      if (char_is_identifier(parser, parser->current.end)) {
        do {
          parser->current.end++;
        } while (char_is_identifier(parser, parser->current.end));
        return YP_TOKEN_GLOBAL_VARIABLE;
      }

      // If we get here, then we have a $ followed by something that isn't
      // recognized as a global variable.
      return YP_TOKEN_INVALID;
  }
}

static yp_token_type_t
lex_identifier(yp_parser_t *parser) {
  // Lex as far as we can into the current identifier.
  size_t width;
  while ((width = char_is_identifier(parser, parser->current.end))) {
    parser->current.end += width;
  }

  // Now cache the length of the identifier so that we can quickly compare it
  // against known keywords.
  width = parser->current.end - parser->current.start;

#define KEYWORD(value, token) \
  if (width == sizeof(value) - 1 && strncmp(parser->current.start, value, sizeof(value) - 1) == 0) \
    return YP_TOKEN_KEYWORD_##token;

  if (parser->current.end < parser->end) {
    // If we're in a position where we can accept a = at the end of an
    // identifier, then we'll optionally accept it.
    if ((parser->previous.type == YP_TOKEN_KEYWORD_DEF || parser->previous.type == YP_TOKEN_DOT) && match(parser, '=')) {
      return YP_TOKEN_IDENTIFIER;
    }

    // Else we'll attempt to extend the identifier by a ! or ?. Then we'll check
    // if we're returning the defined? keyword or just an identifier.
    if ((parser->current.end[1] != '=') && (match(parser, '!') || match(parser, '?'))) {
      width++;

      if (parser->previous.type != YP_TOKEN_DOT) {
        KEYWORD("defined?", DEFINED)
      }

      return YP_TOKEN_IDENTIFIER;
    }
  }

  if (parser->previous.type != YP_TOKEN_DOT) {
    KEYWORD("__ENCODING__", __ENCODING__)
    KEYWORD("__LINE__", __LINE__)
    KEYWORD("__FILE__", __FILE__)
    KEYWORD("alias", ALIAS)
    KEYWORD("and", AND)
    KEYWORD("begin", BEGIN)
    KEYWORD("BEGIN", BEGIN_UPCASE)
    KEYWORD("break", BREAK)
    KEYWORD("case", CASE)
    KEYWORD("class", CLASS)
    KEYWORD("def", DEF)
    KEYWORD("do", DO)
    KEYWORD("else", ELSE)
    KEYWORD("elsif", ELSIF)
    KEYWORD("end", END)
    KEYWORD("END", END_UPCASE)
    KEYWORD("ensure", ENSURE)
    KEYWORD("false", FALSE)
    KEYWORD("for", FOR)
    KEYWORD("if", IF)
    KEYWORD("in", IN)
    KEYWORD("module", MODULE)
    KEYWORD("next", NEXT)
    KEYWORD("nil", NIL)
    KEYWORD("not", NOT)
    KEYWORD("or", OR)
    KEYWORD("redo", REDO)
    KEYWORD("rescue", RESCUE)
    KEYWORD("retry", RETRY)
    KEYWORD("return", RETURN)
    KEYWORD("self", SELF)
    KEYWORD("super", SUPER)
    KEYWORD("then", THEN)
    KEYWORD("true", TRUE)
    KEYWORD("undef", UNDEF)
    KEYWORD("unless", UNLESS)
    KEYWORD("until", UNTIL)
    KEYWORD("when", WHEN)
    KEYWORD("while", WHILE)
    KEYWORD("yield", YIELD)
  }

#undef KEYWORD

  char start = parser->current.start[0];
  return start >= 'A' && start <= 'Z' ? YP_TOKEN_CONSTANT : YP_TOKEN_IDENTIFIER;
}

// Returns true if the current token that the parser is considering is at the
// beginning of a line or the beginning of the source.
static bool
current_token_starts_line(yp_parser_t *parser) {
  return (parser->current.start == parser->start) || (parser->current.start[-1] == '\n');
}

// This is the overall lexer function. It is responsible for advancing both
// parser->current.start and parser->current.end such that they point to the
// beginning and end of the next token. It should return the type of token that
// was found.
static yp_token_type_t
lex_token_type(yp_parser_t *parser) {
  switch (parser->lex_modes.current->mode) {
    case YP_LEX_DEFAULT:
    case YP_LEX_EMBEXPR: {
      // First, we're going to skip past any whitespace at the front of the next
      // token.
      bool chomping = true;
      while (chomping) {
        switch (*parser->current.end) {
          case ' ':
          case '\t':
          case '\f':
          case '\v':
            parser->current.end++;
            break;
          case '\r':
            if (parser->current.end[1] == '\n') {
              chomping = false;
            } else {
              parser->current.end++;
            }
            break;
          default:
            chomping = false;
            break;
        }
      }

      // Next, we'll set to start of this token to be the current end.
      parser->current.start = parser->current.end;

      // Finally, we'll check the current character to determine the next token.
      switch (*parser->current.end++) {
        case '\0':   // NUL or end of script
        case '\004': // ^D
        case '\032': // ^Z
          return YP_TOKEN_EOF;

        case '#': // comments
          while (*parser->current.end != '\n' && *parser->current.end != '\0') {
            parser->current.end++;
          }
          (void) match(parser, '\n');
          return YP_TOKEN_COMMENT;

        case '\r': {
          // The only way to get here is if this is immediately followed by a
          // newline.
          (void) match(parser, '\n');
          return YP_TOKEN_NEWLINE;
        }

        case '\n':
          return YP_TOKEN_NEWLINE;

        // , ( ) ;
        case ',':
          return YP_TOKEN_COMMA;
        case '(':
          return YP_TOKEN_PARENTHESIS_LEFT;
        case ')':
          return YP_TOKEN_PARENTHESIS_RIGHT;
        case ';':
          return YP_TOKEN_SEMICOLON;

        // [ []
        case '[':
          if (parser->previous.type == YP_TOKEN_DOT && match(parser, ']')) {
            return YP_TOKEN_BRACKET_LEFT_RIGHT;
          }
          return YP_TOKEN_BRACKET_LEFT;

        // ]
        case ']':
          return YP_TOKEN_BRACKET_RIGHT;

        // {
        case '{':
          if (parser->previous.type == YP_TOKEN_MINUS_GREATER) return YP_TOKEN_LAMBDA_BEGIN;
          return YP_TOKEN_BRACE_LEFT;

        // }
        case '}':
          if (parser->lex_modes.current->mode == YP_LEX_EMBEXPR) {
            lex_mode_pop(parser);
            return YP_TOKEN_EMBEXPR_END;
          }
          return YP_TOKEN_BRACE_RIGHT;

        // * ** **= *=
        case '*':
          if (match(parser, '*')) return match(parser, '=') ? YP_TOKEN_STAR_STAR_EQUAL : YP_TOKEN_STAR_STAR;
          return match(parser, '=') ? YP_TOKEN_STAR_EQUAL : YP_TOKEN_STAR;

        // ! != !~ !@
        case '!':
          if (match(parser, '=')) return YP_TOKEN_BANG_EQUAL;
          if (match(parser, '~')) return YP_TOKEN_BANG_TILDE;
          if ((parser->previous.type == YP_TOKEN_KEYWORD_DEF || parser->previous.type == YP_TOKEN_DOT) && match(parser, '@'))
            return YP_TOKEN_BANG_AT;
          return YP_TOKEN_BANG;

        // = => =~ == === =begin
        case '=':
          if (current_token_starts_line(parser)) {
            if (strncmp(parser->current.end, "begin\n", 6) == 0) {
              parser->current.end += 6;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_EMBDOC, .term = '\0', .interp = false });
              return YP_TOKEN_EMBDOC_BEGIN;
            }

            if (strncmp(parser->current.end, "begin\r\n", 7) == 0) {
              parser->current.end += 7;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_EMBDOC, .term = '\0', .interp = false });
              return YP_TOKEN_EMBDOC_BEGIN;
            }
          }

          if (match(parser, '>')) return YP_TOKEN_EQUAL_GREATER;
          if (match(parser, '~')) return YP_TOKEN_EQUAL_TILDE;
          if (match(parser, '=')) return match(parser, '=') ? YP_TOKEN_EQUAL_EQUAL_EQUAL : YP_TOKEN_EQUAL_EQUAL;
          return YP_TOKEN_EQUAL;

        // < << <<= <= <=>
        case '<':
          if (match(parser, '<')) {
            if (match(parser, '=')) return YP_TOKEN_LESS_LESS_EQUAL;

            // We don't yet handle heredocs.
            if (match(parser, '-') || match(parser, '~')) return YP_TOKEN_EOF;

            return YP_TOKEN_LESS_LESS;
          }
          if (match(parser, '=')) return match(parser, '>') ? YP_TOKEN_LESS_EQUAL_GREATER : YP_TOKEN_LESS_EQUAL;
          return YP_TOKEN_LESS;

        // > >> >>= >=
        case '>':
          if (match(parser, '>')) return match(parser, '=') ? YP_TOKEN_GREATER_GREATER_EQUAL : YP_TOKEN_GREATER_GREATER;
          return match(parser, '=') ? YP_TOKEN_GREATER_EQUAL : YP_TOKEN_GREATER;

        // double-quoted string literal
        case '"':
          lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_STRING, .term = '"', .interp = true });
          return YP_TOKEN_STRING_BEGIN;

        // xstring literal
        case '`':
          lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_STRING, .term = '`', .interp = true });
          return YP_TOKEN_BACKTICK;

        // single-quoted string literal
        case '\'':
          lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_STRING, .term = '\'', .interp = false });
          return YP_TOKEN_STRING_BEGIN;

        // ? character literal
        case '?':
          if (char_is_identifier(parser, parser->current.end)) {
            parser->current.end++;
            return YP_TOKEN_CHARACTER_LITERAL;
          }
          return YP_TOKEN_QUESTION_MARK;

        // & && &&= &=
        case '&':
          if (match(parser, '&'))
            return match(parser, '=') ? YP_TOKEN_AMPERSAND_AMPERSAND_EQUAL : YP_TOKEN_AMPERSAND_AMPERSAND;
          if (match(parser, '.'))
            return YP_TOKEN_AMPERSAND_DOT;
          return match(parser, '=') ? YP_TOKEN_AMPERSAND_EQUAL : YP_TOKEN_AMPERSAND;

        // | || ||= |=
        case '|':
          if (match(parser, '|')) return match(parser, '=') ? YP_TOKEN_PIPE_PIPE_EQUAL : YP_TOKEN_PIPE_PIPE;
          return match(parser, '=') ? YP_TOKEN_PIPE_EQUAL : YP_TOKEN_PIPE;

        // + += +@
        case '+':
          if (match(parser, '=')) return YP_TOKEN_PLUS_EQUAL;
          if ((parser->previous.type == YP_TOKEN_KEYWORD_DEF || parser->previous.type == YP_TOKEN_DOT) && match(parser, '@')) return YP_TOKEN_PLUS_AT;
          if (char_is_decimal_number(parser->current.end)) return lex_numeric(parser);
          return YP_TOKEN_PLUS;

        // - -= -@
        case '-':
          if (match(parser, '>')) return YP_TOKEN_MINUS_GREATER;
          if (match(parser, '=')) return YP_TOKEN_MINUS_EQUAL;
          if ((parser->previous.type == YP_TOKEN_KEYWORD_DEF || parser->previous.type == YP_TOKEN_DOT) &&
              match(parser, '@'))
            return YP_TOKEN_MINUS_AT;
          return YP_TOKEN_MINUS;

        // . .. ...
        case '.':
          if (!match(parser, '.')) return YP_TOKEN_DOT;
          return match(parser, '.') ? YP_TOKEN_DOT_DOT_DOT : YP_TOKEN_DOT_DOT;

        // integer
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          return lex_numeric(parser);

        // :: symbol
        case ':':
          if (match(parser, ':')) return YP_TOKEN_COLON_COLON;
          if (char_is_identifier(parser, parser->current.end)) {
            lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_SYMBOL, .term = '\0' });
            return YP_TOKEN_SYMBOL_BEGIN;
          } else if ((*parser->current.end == '"') || (*parser->current.end == '\'')) {
            lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_STRING, .term = *parser->current.end, .interp = *parser->current.end == '"' });
            parser->current.end++;
            return YP_TOKEN_SYMBOL_BEGIN;
          }
          return YP_TOKEN_COLON;

        // / /=
        case '/':
          if (match(parser, '=')) return YP_TOKEN_SLASH_EQUAL;
          if (*parser->current.end == ' ') return YP_TOKEN_SLASH;

          lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_REGEXP, .term = '/' });
          return YP_TOKEN_REGEXP_BEGIN;

        // ^ ^=
        case '^':
          return match(parser, '=') ? YP_TOKEN_CARET_EQUAL : YP_TOKEN_CARET;

        // ~ ~@
        case '~':
          if ((parser->previous.type == YP_TOKEN_KEYWORD_DEF || parser->previous.type == YP_TOKEN_DOT) &&
              match(parser, '@'))
            return YP_TOKEN_TILDE_AT;
          return YP_TOKEN_TILDE;

        // TODO
        case '\\':
          return YP_TOKEN_INVALID;

        // % %= %i %I %q %Q %w %W
        case '%':
          switch (*parser->current.end) {
            case '=':
              parser->current.end++;
              return YP_TOKEN_PERCENT_EQUAL;
            case 'i':
              parser->current.end++;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_LIST, .term = terminator(*parser->current.end++), .interp = false });
              return YP_TOKEN_PERCENT_LOWER_I;
            case 'I':
              parser->current.end++;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_LIST, .term = terminator(*parser->current.end++), .interp = true });
              return YP_TOKEN_PERCENT_UPPER_I;
            case 'r':
              parser->current.end++;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_REGEXP, .term = terminator(*parser->current.end++), .interp = true });
              return YP_TOKEN_REGEXP_BEGIN;
            case 'q':
              parser->current.end++;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_STRING, .term = terminator(*parser->current.end++), .interp = false });
              return YP_TOKEN_STRING_BEGIN;
            case 'Q':
              parser->current.end++;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_STRING, .term = terminator(*parser->current.end++), .interp = true });
              return YP_TOKEN_STRING_BEGIN;
            case 's':
              parser->current.end++;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_STRING, .term = terminator(*parser->current.end++), .interp = false });
              return YP_TOKEN_SYMBOL_BEGIN;
            case 'w':
              parser->current.end++;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_LIST, .term = terminator(*parser->current.end++), .interp = false });
              return YP_TOKEN_PERCENT_LOWER_W;
            case 'W':
              parser->current.end++;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_LIST, .term = terminator(*parser->current.end++), .interp = true });
              return YP_TOKEN_PERCENT_UPPER_W;
            case 'x':
              parser->current.end++;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_STRING, .term = terminator(*parser->current.end++), .interp = true });
              return YP_TOKEN_PERCENT_LOWER_X;
            default:
              if (char_is_symbol(parser->current.end)) {
                lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_STRING, .term = terminator(*parser->current.end++), .interp = true });
                return YP_TOKEN_STRING_BEGIN;
              } else {
                return YP_TOKEN_PERCENT;
              }
          }

        // global variable
        case '$':
          return lex_global_variable(parser);

        // instance variable, class variable
        case '@': {
          yp_token_type_t type = match(parser, '@') ? YP_TOKEN_CLASS_VARIABLE : YP_TOKEN_INSTANCE_VARIABLE;
          size_t width;

          if ((width = char_is_identifier_start(parser, parser->current.end))) {
            parser->current.end += width;

            while ((width = char_is_identifier(parser, parser->current.end))) {
              parser->current.end += width;
            }

            return type;
          }

          return YP_TOKEN_INVALID;
        }

        default: {
          // If this isn't the beginning of an identifier, then it's an invalid
          // token as we've exhausted all of the other options.
          size_t width = char_is_identifier_start(parser, parser->current.start);
          if (!width) return YP_TOKEN_INVALID;

          parser->current.end = parser->current.start + width;
          yp_token_type_t type = lex_identifier(parser);

          // If we've hit a __END__ and it was at the start of the line or the
          // start of the file and it is followed by either a \n or a \r\n, then
          // this is the last token of the file.
          if (
            ((parser->current.end - parser->current.start) == 7) &&
            current_token_starts_line(parser) &&
            (strncmp(parser->current.start, "__END__", 7) == 0) &&
            (*parser->current.end == '\n' || (*parser->current.end == '\r' && parser->current.end[1] == '\n'))
          ) {
            parser->current.end = parser->end;
            return YP_TOKEN___END__;
          }

          // If we're lexing in a place that allows labels and we've hit a
          // colon, then we can return a label token.
          if ((parser->current.end[0] == ':') && (parser->current.end[1] != ':')) {
            parser->current.end++;
            return YP_TOKEN_LABEL;
          }

          return type;
        }
      }
    }
    case YP_LEX_EMBDOC: {
      parser->current.start = parser->current.end;

      // If we've hit the end of the embedded documentation then we'll return that token here.
      if (strncmp(parser->current.end, "=end\n", 5) == 0) {
        parser->current.end += 5;
        lex_mode_pop(parser);
        return YP_TOKEN_EMBDOC_END;
      }

      if (strncmp(parser->current.end, "=end\r\n", 6) == 0) {
        parser->current.end += 6;
        lex_mode_pop(parser);
        return YP_TOKEN_EMBDOC_END;
      }

      // Otherwise, we'll parse until the end of the line and return a line of
      // embedded documentation.
      while ((parser->current.end < parser->end) && (*parser->current.end++ != '\n'));

      // If we've still got content, then we'll return a line of embedded
      // documentation.
      if (parser->current.end < parser->end)
        return YP_TOKEN_EMBDOC_LINE;

      return YP_TOKEN_EOF;
    }
    case YP_LEX_LIST: {
      // If there's any whitespace at the start of the list, then we're going to
      // trim it off the beginning and create a new token.
      if (char_is_whitespace(parser->current.end)) {
        parser->current.start = parser->current.end;

        do {
          parser->current.end++;
        } while (char_is_whitespace(parser->current.end));

        return YP_TOKEN_WORDS_SEP;
      }

      // Next, we'll set to start of this token to be the current end.
      parser->current.start = parser->current.end;

      // Lex as far as we can into the word.
      while (parser->current.end < parser->end) {
        // If we've hit whitespace, then we must have received content by now,
        // so we can return an element of the list.
        if (char_is_whitespace(parser->current.end)) {
          return YP_TOKEN_STRING_CONTENT;
        }

        if (*parser->current.end == parser->lex_modes.current->term) {
          // If we've hit the terminator and we've already skipped past content,
          // then we can return a list node.
          if (parser->current.start < parser->current.end) {
            return YP_TOKEN_STRING_CONTENT;
          }

          // Otherwise, switch back to the default state and return the end of
          // the list.
          parser->current.end++;
          lex_mode_pop(parser);
          return YP_TOKEN_STRING_END;
        }

        // If we've hit a #{, then we're at the start of an embedded expression,
        // so we'll switch to the embedded expression lex mode.
        if (parser->lex_modes.current->interp && parser->current.end[0] == '#' && parser->current.end[1] == '{') {
          // If we've already skipped past content, then we need to return that
          // content first before we switch to the embedded expression lex mode.
          if (parser->current.start < parser->current.end) {
            return YP_TOKEN_STRING_CONTENT;
          }

          parser->current.end += 2;
          lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_EMBEXPR });
          return YP_TOKEN_EMBEXPR_BEGIN;
        }

        // Otherwise, just skip past the content as it's part of an element of
        // the list.
        parser->current.end++;
      }

      return YP_TOKEN_EOF;
    }
    case YP_LEX_REGEXP: {
      // First, we'll set to start of this token to be the current end.
      parser->current.start = parser->current.end;

      // If we've hit the end of the string, then we can return to the default
      // state of the lexer and return a string ending token.
      if (match(parser, parser->lex_modes.current->term)) {
        // Since we've hit the terminator of the regular expression, we now need
        // to parse the options.
        bool options = true;
        while (options) {
          switch (*parser->current.end) {
            case 'e':
            case 'i':
            case 'm':
            case 'n':
            case 's':
            case 'u':
            case 'x':
              parser->current.end++;
              break;
            default:
              options = false;
              break;
          }
        }

        lex_mode_pop(parser);
        return YP_TOKEN_REGEXP_END;
      }

      // Otherwise, we'll lex as far as we can into the regular expression. If
      // we hit the end of the regular expression, then we'll return everything
      // up to that point.
      while (parser->current.end < parser->end) {
        // If we hit the terminator, then return this element of the string.
        if (*parser->current.end == parser->lex_modes.current->term) {
          return YP_TOKEN_STRING_CONTENT;
        }

        // If we've hit a #, then check if it's used as the beginning of either
        // an embedded variable or an embedded expression.
        if (*parser->current.end == '#') {
          switch (parser->current.end[1]) {
            case '{':
              // In this case it's the start of an embedded expression.

              // If we have already consumed content, then we need to return
              // that content as string content first.
              if (parser->current.end > parser->current.start) {
                return YP_TOKEN_STRING_CONTENT;
              }

              parser->current.end += 2;
              lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_EMBEXPR });
              return YP_TOKEN_EMBEXPR_BEGIN;
          }
        }

        parser->current.end++;
      }

      return YP_TOKEN_EOF;
    }
    case YP_LEX_STRING: {
      // First, we'll set to start of this token to be the current end.
      parser->current.start = parser->current.end;

      // If we've hit the end of the string, then we can return to the default
      // state of the lexer and return a string ending token.
      if (match(parser, parser->lex_modes.current->term)) {
        lex_mode_pop(parser);
        return YP_TOKEN_STRING_END;
      }

      // Otherwise, we'll lex as far as we can into the string. If we hit the
      // end of the string, then we'll return everything up to that point.
      while (parser->current.end < parser->end) {
        // If we hit the terminator, then return this element of the string.
        if (*parser->current.end == parser->lex_modes.current->term) {
          return YP_TOKEN_STRING_CONTENT;
        }

        switch (*parser->current.end) {
          case '#':
            // If our current lex state allows interpolation and we've hit a #,
            // then check if it's used as the beginning of either an embedded
            // variable or an embedded expression.
            if (parser->lex_modes.current->interp) {
              switch (parser->current.end[1]) {
              case '@':
                // In this case it could be an embedded instance or class
                // variable.
                break;
              case '$':
                // In this case it could be an embedded global variable.
                break;
              case '{':
                // In this case it's the start of an embedded expression.

                // If we have already consumed content, then we need to return
                // that content as string content first.
                if (parser->current.end > parser->current.start) {
                  return YP_TOKEN_STRING_CONTENT;
                }

                parser->current.end += 2;
                lex_mode_push(parser, (yp_lex_mode_t) { .mode = YP_LEX_EMBEXPR });
                return YP_TOKEN_EMBEXPR_BEGIN;
              }
            }
            break;
          case '\\':
            // If we hit an escape, then we need that handle the subsequent
            // character literally.
            parser->current.end++;
        }

        parser->current.end++;
      }

      return YP_TOKEN_EOF;
    }
    case YP_LEX_SYMBOL: {
      // First, we'll set to start of this token to be the current end.
      parser->current.start = parser->current.end;

      // Lex as far as we can into the symbol.
      if (parser->current.end < parser->end) {
        if (char_is_identifier_start(parser, parser->current.end)) {
          parser->current.end++;
          lex_mode_pop(parser);

          yp_token_type_t type = lex_identifier(parser);

          return match(parser, '=') ? YP_TOKEN_IDENTIFIER : type;
        }
      }

      // If we get here then we have the start of a symbol with no content. In
      // that case return an invalid token.
      return YP_TOKEN_INVALID;
    }
  }

  // We shouldn't be able to get here at all, but some compilers can't figure
  // that out, so just returning a value here to make them happy.
  return YP_TOKEN_INVALID;
}

/******************************************************************************/
/* Encoding-related functions                                                 */
/******************************************************************************/

static yp_encoding_t yp_encoding_ascii = {
  .alnum_char = yp_encoding_ascii_alnum_char,
  .alpha_char = yp_encoding_ascii_alpha_char
};

static yp_encoding_t yp_encoding_iso_8859_9 = {
  .alnum_char = yp_encoding_iso_8859_9_alnum_char,
  .alpha_char = yp_encoding_iso_8859_9_alpha_char
};

static yp_encoding_t yp_encoding_utf_8 = {
  .alnum_char = yp_encoding_utf_8_alnum_char,
  .alpha_char = yp_encoding_utf_8_alpha_char
};

// Here we're going to check if this is a "magic" comment, and perform whatever
// actions are necessary for it here.
static void
parser_lex_magic_comments(yp_parser_t *parser) {
  const char *start = parser->current.start + 1;
  while (char_is_non_newline_whitespace(start)) start++;

  if (strncmp(start, "-*-", 3) == 0) {
    start += 3;
    while (char_is_non_newline_whitespace(start)) start++;
  }

  // There is a lot TODO here to make it more accurately reflect encoding
  // parsing, but for now this gets us closer.
  if (strncmp(start, "encoding:", 9) == 0) {
    start += 9;
    while (char_is_non_newline_whitespace(start)) start++;

    const char *end = start;
    while (!char_is_whitespace(end)) end++;
    size_t width = end - start;

    // First, we're going to loop through each of the encodings that we handle
    // explicitly. If we found one that we understand, we'll use that value.
#define ENCODING(value, prebuilt) \
    if (width == sizeof(value) - 1 && strncmp(start, value, sizeof(value) - 1) == 0) { \
      parser->encoding = prebuilt; \
      return; \
    }

    ENCODING("ascii", yp_encoding_ascii);
    ENCODING("iso-8859-9", yp_encoding_iso_8859_9);
    ENCODING("utf-8", yp_encoding_utf_8);
    ENCODING("binary", yp_encoding_ascii);
    ENCODING("us-ascii", yp_encoding_ascii);

#undef ENCODING

    // Otherwise, we're going to call out to a user-defined callback. If they
    // return an encoding struct that we can use, then we'll use that here.
    yp_encoding_t *encoding = parser->encoding_decode_callback(start, width);
    if (encoding != NULL) {
      parser->encoding = *encoding;
      return;
    }

    // If nothing was returned by this point, then we've got an issue because we
    // didn't understand the encoding that the user was trying to use. In this
    // case we'll keep using the default encoding but add an error to the
    // parser to indicate an unsuccessful parse.
    yp_error_list_append(&parser->error_list, "Could not understand the encoding specified in the magic comment.", start - parser->start);
  }
}

/******************************************************************************/
/* Parse functions                                                            */
/******************************************************************************/

// When we are parsing string-like content, we need to unescape the content to
// provide to the consumers of the parser. This function accepts a range of
// characters from the source and unescapes into the provided string.
static yp_node_t *
yp_node_string_node_create_and_unescape(yp_parser_t *parser, const yp_token_t *opening, const yp_token_t *content, const yp_token_t *closing, yp_unescape_type_t unescape_type) {
  yp_node_t *node = yp_node_string_node_create(parser, opening, content, closing);
  yp_unescape(content->start, content->end - content->start, &node->as.string_node.unescaped, unescape_type);
  return node;
}

// Get the next token type and skip over comment tokens.
static void
parser_lex(yp_parser_t *parser) {
  parser->previous = parser->current;
  parser->current.type = lex_token_type(parser);

  while (
    parser->current.type == YP_TOKEN_COMMENT ||
    parser->current.type == YP_TOKEN___END__ ||
    parser->current.type == YP_TOKEN_EMBDOC_BEGIN
  ) {
    // If we found a comment while lexing, then we're going to add it to the
    // list of comments in the file and keep lexing.
    yp_comment_t *comment = malloc(sizeof(yp_comment_t));
    comment->node.start = parser->current.start - parser->start;

    switch (parser->current.type) {
      case YP_TOKEN_COMMENT:
        parser_lex_magic_comments(parser);
        *comment = (yp_comment_t) {
          .node.end = parser->current.end - parser->start,
          .type = YP_COMMENT_INLINE
        };

        parser->current.type = lex_token_type(parser);
        break;
      case YP_TOKEN___END__:
        *comment = (yp_comment_t) {
          .node.end = parser->current.end - parser->start,
          .type = YP_COMMENT___END__
        };

        parser->current.type = lex_token_type(parser);
        break;
      case YP_TOKEN_EMBDOC_BEGIN: {
        // If we found an embedded document, then we need to lex until we find
        // the end of the embedded document.
        do {
          parser->current.type = lex_token_type(parser);
        } while ((parser->current.type != YP_TOKEN_EMBDOC_END) && (parser->current.type != YP_TOKEN_EOF));

        *comment = (yp_comment_t) {
          .node.end = parser->current.end - parser->start,
          .type = YP_COMMENT_EMBDOC
        };

        if (parser->current.type == YP_TOKEN_EOF) {
          yp_error_list_append(&parser->error_list, "Unterminated embdoc", parser->current.start - parser->start);
        } else {
          parser->current.type = lex_token_type(parser);
        }
        break;
      }
      default:
        break;
    }

    yp_list_append(&parser->comment_list, (yp_list_node_t *) comment);
  }
}

static bool
context_terminator(yp_context_t context, yp_token_t *token) {
  switch (context) {
    case YP_CONTEXT_MAIN:
      return token->type == YP_TOKEN_EOF;
    case YP_CONTEXT_PREEXE:
    case YP_CONTEXT_POSTEXE:
      return token->type == YP_TOKEN_BRACE_RIGHT;
    case YP_CONTEXT_MODULE:
    case YP_CONTEXT_CLASS:
    case YP_CONTEXT_DEF:
    case YP_CONTEXT_WHILE:
    case YP_CONTEXT_UNTIL:
    case YP_CONTEXT_ELSE:
    case YP_CONTEXT_SCLASS:
    case YP_CONTEXT_FOR:
    case YP_CONTEXT_ENSURE:
      return token->type == YP_TOKEN_KEYWORD_END;
    case YP_CONTEXT_IF:
    case YP_CONTEXT_UNLESS:
    case YP_CONTEXT_ELSIF:
      return token->type == YP_TOKEN_KEYWORD_ELSE || token->type == YP_TOKEN_KEYWORD_ELSIF || token->type == YP_TOKEN_KEYWORD_END;
    case YP_CONTEXT_BEGIN:
      return token->type == YP_TOKEN_KEYWORD_ENSURE || token->type == YP_TOKEN_KEYWORD_END;
    case YP_CONTEXT_EMBEXPR:
      return token->type == YP_TOKEN_EMBEXPR_END;
    case YP_CONTEXT_PARENS:
      return token->type == YP_TOKEN_PARENTHESIS_RIGHT;
  }

  return false;
}

static bool
context_recoverable(yp_parser_t *parser, yp_token_t *token) {
  yp_context_node_t *context_node = parser->current_context;

  while (context_node != NULL) {
    if (context_terminator(context_node->context, token)) return true;
    context_node = context_node->prev;
  }

  return false;
}

static void
context_push(yp_parser_t *parser, yp_context_t context) {
  yp_context_node_t *context_node = (yp_context_node_t *) malloc(sizeof(yp_context_node_t));
  *context_node = (yp_context_node_t) { .context = context, .prev = NULL };

  if (parser->current_context == NULL) {
    parser->current_context = context_node;
  } else {
    context_node->prev = parser->current_context;
    parser->current_context = context_node;
  }
}

static void
context_pop(yp_parser_t *parser) {
  if (parser->current_context->prev == NULL) {
    free(parser->current_context);
    parser->current_context = NULL;
  } else {
    yp_context_node_t *prev = parser->current_context->prev;
    free(parser->current_context);
    parser->current_context = prev;
  }
}

// These are the various precedence rules. Because we are using a Pratt parser,
// they are named binding power to represent the manner in which nodes are bound
// together in the stack.
typedef enum {
  BINDING_POWER_NONE = 1,
  BINDING_POWER_BRACES,          // braces
  BINDING_POWER_MODIFIER,        // if unless until while
  BINDING_POWER_COMPOSITION,     // and or
  BINDING_POWER_NOT,             // not
  BINDING_POWER_DEFINED,         // defined?
  BINDING_POWER_ASSIGNMENT,      // = += -= *= /= %= &= |= ^= &&= ||= <<= >>= **=
  BINDING_POWER_MODIFIER_RESCUE, // rescue
  BINDING_POWER_TERNARY,         // ?:
  BINDING_POWER_RANGE,           // .. ...
  BINDING_POWER_LOGICAL_OR,      // ||
  BINDING_POWER_LOGICAL_AND,     // &&
  BINDING_POWER_EQUALITY,        // <=> == === != =~ !~
  BINDING_POWER_COMPARISON,      // > >= < <=
  BINDING_POWER_BITWISE_OR,      // | ^
  BINDING_POWER_BITWISE_AND,     // &
  BINDING_POWER_SHIFT,           // << >>
  BINDING_POWER_TERM,            // + -
  BINDING_POWER_FACTOR,          // * / %
  BINDING_POWER_UMINUS,          // unary minus
  BINDING_POWER_EXPONENT,        // **
  BINDING_POWER_UNARY,           // ! ~ +
  BINDING_POWER_INDEX,           // [] []=
  BINDING_POWER_CALL,            // :: .
} binding_power_t;

// This struct represents a set of binding powers used for a given token. They
// are combined in this way to make it easier to represent associativity.
typedef struct {
  binding_power_t left;
  binding_power_t right;
} binding_powers_t;

#define LEFT_ASSOCIATIVE(precedence)                                                                                   \
  { precedence, precedence + 1 }
#define RIGHT_ASSOCIATIVE(precedence)                                                                                  \
  { precedence, precedence }

binding_powers_t binding_powers[YP_TOKEN_MAXIMUM] = {
  // {}
  [YP_TOKEN_BRACE_LEFT] = LEFT_ASSOCIATIVE(BINDING_POWER_BRACES),

  // if unless until while
  [YP_TOKEN_KEYWORD_IF] = LEFT_ASSOCIATIVE(BINDING_POWER_MODIFIER),
  [YP_TOKEN_KEYWORD_UNLESS] = LEFT_ASSOCIATIVE(BINDING_POWER_MODIFIER),
  [YP_TOKEN_KEYWORD_UNTIL] = LEFT_ASSOCIATIVE(BINDING_POWER_MODIFIER),
  [YP_TOKEN_KEYWORD_WHILE] = LEFT_ASSOCIATIVE(BINDING_POWER_MODIFIER),

  // and or
  [YP_TOKEN_KEYWORD_AND] = LEFT_ASSOCIATIVE(BINDING_POWER_COMPOSITION),
  [YP_TOKEN_KEYWORD_OR] = LEFT_ASSOCIATIVE(BINDING_POWER_COMPOSITION),

  // not
  [YP_TOKEN_KEYWORD_NOT] = RIGHT_ASSOCIATIVE(BINDING_POWER_NOT),

  // defined?
  [YP_TOKEN_KEYWORD_DEFINED] = RIGHT_ASSOCIATIVE(BINDING_POWER_DEFINED),

  // &&= &= ^= = >>= <<= -= %= |= += /= *= **=
  [YP_TOKEN_AMPERSAND_AMPERSAND_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_AMPERSAND_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_CARET_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_GREATER_GREATER_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_LESS_LESS_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_MINUS_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_PERCENT_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_PIPE_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_PIPE_PIPE_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_PLUS_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_SLASH_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_STAR_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),
  [YP_TOKEN_STAR_STAR_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_ASSIGNMENT),

  // ?:
  [YP_TOKEN_QUESTION_MARK] = RIGHT_ASSOCIATIVE(BINDING_POWER_TERNARY),

  // .. ...
  [YP_TOKEN_DOT_DOT] = LEFT_ASSOCIATIVE(BINDING_POWER_RANGE),
  [YP_TOKEN_DOT_DOT_DOT] = LEFT_ASSOCIATIVE(BINDING_POWER_RANGE),

  // ||
  [YP_TOKEN_PIPE_PIPE] = LEFT_ASSOCIATIVE(BINDING_POWER_LOGICAL_OR),

  // &&
  [YP_TOKEN_AMPERSAND_AMPERSAND] = LEFT_ASSOCIATIVE(BINDING_POWER_LOGICAL_AND),

  // != !~ == === =~ <=>
  [YP_TOKEN_BANG_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_EQUALITY),
  [YP_TOKEN_BANG_TILDE] = RIGHT_ASSOCIATIVE(BINDING_POWER_EQUALITY),
  [YP_TOKEN_EQUAL_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_EQUALITY),
  [YP_TOKEN_EQUAL_EQUAL_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_EQUALITY),
  [YP_TOKEN_EQUAL_TILDE] = RIGHT_ASSOCIATIVE(BINDING_POWER_EQUALITY),
  [YP_TOKEN_LESS_EQUAL_GREATER] = RIGHT_ASSOCIATIVE(BINDING_POWER_EQUALITY),

  // > >= < <=
  [YP_TOKEN_GREATER] = RIGHT_ASSOCIATIVE(BINDING_POWER_COMPARISON),
  [YP_TOKEN_GREATER_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_COMPARISON),
  [YP_TOKEN_LESS] = RIGHT_ASSOCIATIVE(BINDING_POWER_COMPARISON),
  [YP_TOKEN_LESS_EQUAL] = RIGHT_ASSOCIATIVE(BINDING_POWER_COMPARISON),

  // ^ |
  [YP_TOKEN_CARET] = RIGHT_ASSOCIATIVE(BINDING_POWER_BITWISE_OR),
  [YP_TOKEN_PIPE] = RIGHT_ASSOCIATIVE(BINDING_POWER_BITWISE_OR),

  // &
  [YP_TOKEN_AMPERSAND] = RIGHT_ASSOCIATIVE(BINDING_POWER_BITWISE_AND),

  // >> <<
  [YP_TOKEN_GREATER_GREATER] = RIGHT_ASSOCIATIVE(BINDING_POWER_SHIFT),
  [YP_TOKEN_LESS_LESS] = RIGHT_ASSOCIATIVE(BINDING_POWER_SHIFT),

  // - +
  [YP_TOKEN_MINUS] = LEFT_ASSOCIATIVE(BINDING_POWER_TERM),
  [YP_TOKEN_PLUS] = LEFT_ASSOCIATIVE(BINDING_POWER_TERM),

  // % / *
  [YP_TOKEN_PERCENT] = LEFT_ASSOCIATIVE(BINDING_POWER_FACTOR),
  [YP_TOKEN_SLASH] = LEFT_ASSOCIATIVE(BINDING_POWER_FACTOR),
  [YP_TOKEN_STAR] = LEFT_ASSOCIATIVE(BINDING_POWER_FACTOR),

  // **
  [YP_TOKEN_STAR_STAR] = RIGHT_ASSOCIATIVE(BINDING_POWER_EXPONENT),

  // ! ~
  [YP_TOKEN_BANG] = RIGHT_ASSOCIATIVE(BINDING_POWER_UNARY),
  [YP_TOKEN_TILDE] = RIGHT_ASSOCIATIVE(BINDING_POWER_UNARY),

  // []
  [YP_TOKEN_BRACKET_LEFT_RIGHT] = LEFT_ASSOCIATIVE(BINDING_POWER_INDEX),

  // :: . &.
  [YP_TOKEN_COLON_COLON] = RIGHT_ASSOCIATIVE(BINDING_POWER_CALL),
  [YP_TOKEN_DOT] = RIGHT_ASSOCIATIVE(BINDING_POWER_CALL),
  [YP_TOKEN_AMPERSAND_DOT] = RIGHT_ASSOCIATIVE(BINDING_POWER_CALL)
};

#undef LEFT_ASSOCIATIVE
#undef RIGHT_ASSOCIATIVE

// If the current token is of the specified type, lex forward by one token and
// return true. Otherwise, return false. For example:
//
//     if (accept(parser, YP_TOKEN_COLON)) { ... }
//
static bool
accept(yp_parser_t *parser, yp_token_type_t type) {
  if (parser->current.type == type) {
    parser_lex(parser);
    return true;
  }
  return false;
}

// If the current token is of any of the specified types, lex forward by one
// token and return true. Otherwise, return false. For example:
//
//     if (accept_any(parser, 2, YP_TOKEN_COLON, YP_TOKEN_SEMICOLON)) { ... }
//
static bool
accept_any(yp_parser_t *parser, size_t count, ...) {
  va_list types;
  va_start(types, count);

  for (size_t index = 0; index < count; index++) {
    if (parser->current.type == va_arg(types, yp_token_type_t)) {
      parser_lex(parser);
      va_end(types);
      return true;
    }
  }

  va_end(types);
  return false;
}

// This function indicates that the parser expects a token in a specific
// position. For example, if you're parsing a BEGIN block, you know that a { is
// expected immediately after the keyword. In that case you would call this
// function to indicate that that token should be found.
//
// If we didn't find the token that we were expecting, then we're going to add
// an error to the parser's list of errors (to indicate that the tree is not
// valid) and create an artificial token instead. This allows us to recover from
// the fact that the token isn't present and continue parsing.
static void
expect(yp_parser_t *parser, yp_token_type_t type, const char *message) {
  if (accept(parser, type)) return;

  yp_error_list_append(&parser->error_list, message, parser->previous.end - parser->start);

  parser->previous =
    (yp_token_t) { .type = YP_TOKEN_MISSING, .start = parser->previous.end, .end = parser->previous.end };
}

// In a lot of places in the tree you can have tokens that are not provided but
// that do not cause an error. For example, in a method call without
// parentheses. In these cases we set the token to the "not provided" type. For
// example:
//
//     yp_token_t token;
//     not_provided(&token, parser->previous.end);
//
static inline void
not_provided(yp_token_t *token, const char *location) {
  *token = (yp_token_t) { .type = YP_TOKEN_NOT_PROVIDED, .start = location, .end = location };
}

// This function is used to mark a token as missing. This is used when a token
// is expected in a certain position but is not present. To use it, allocate a
// token on the stack and pass it to this function. For example:
//
//     yp_token_t token;
//     missing(&token, parser->previous.end);
//
static inline void
missing(yp_token_t *token, const char *location) {
  *token = (yp_token_t) { .type = YP_TOKEN_MISSING, .start = location, .end = location };
}

static yp_node_t *
parse_expression(yp_parser_t *parser, binding_power_t binding_power, const char *message);

// Parse a list of targets for assignment. This is used in the case of a for
// loop or a multi-assignment. For example, in the following code:
//
//     for foo, bar in baz
//         ^^^^^^^^
//
// The targets are `foo` and `bar`. This function will either return a single
// target node or a multi-target node.
static yp_node_t *
parse_targets(yp_parser_t *parser, binding_power_t binding_power, const char *message) {
  yp_node_t *first_target = parse_expression(parser, binding_power, message);

  if (parser->current.type != YP_TOKEN_COMMA) {
    return first_target;
  }

  yp_node_t *multi_target = yp_node_multi_target_node_create(parser);
  yp_node_t *target;

  yp_node_list_append(parser, multi_target, &multi_target->as.multi_target_node.targets, first_target);

  while(accept(parser, YP_TOKEN_COMMA)) {
    target = parse_expression(parser, binding_power, message);
    yp_node_list_append(parser, multi_target, &multi_target->as.multi_target_node.targets, target);
  }

  return multi_target;
}

// Parse a list of statements separated by newlines or semicolons.
static yp_node_t *
parse_statements(yp_parser_t *parser, yp_context_t context) {
  context_push(parser, context);
  yp_node_t *statements = yp_node_statements_create(parser);

  while (!context_terminator(context, &parser->current)) {
    yp_node_t *node = parse_expression(parser, BINDING_POWER_NONE, "Expected to be able to parse an expression.");
    yp_node_list_append(parser, statements, &statements->as.statements.body, node);

    // If we're recovering from a syntax error, then we need to stop parsing the
    // statements now.
    if (parser->recovering) {
      // If this is the level of context where the recovery has happened, then
      // we can mark the parser as done recovering.
      if (context_terminator(context, &parser->current)) parser->recovering = false;
      break;
    }

    if (!accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON)) break;
  }

  context_pop(parser);
  return statements;
}

// This is a special out parameter to the parse_arguments function that includes
// opening and closing parentheses in addition to the arguments since it's so
// common.
typedef struct {
  yp_token_t opening;
  yp_node_t *arguments;
  yp_token_t closing;
} yp_arguments_t;

// Parse a list of arguments.
void
parse_arguments(yp_parser_t *parser, yp_node_t *arguments) {
  if (!accept(parser, YP_TOKEN_PARENTHESIS_RIGHT)) {
    while (parser->current.type != YP_TOKEN_EOF) {
      yp_node_t *expression = parse_expression(parser, BINDING_POWER_NONE, "Expected to be able to parse an argument.");
      if (expression->type == YP_NODE_MISSING_NODE) break;

      yp_node_list_append(parser, arguments, &arguments->as.arguments_node.arguments, expression);

      if (accept(parser, YP_TOKEN_PARENTHESIS_RIGHT)) break;
      expect(parser, YP_TOKEN_COMMA, "Expected an ',' to delimit arguments.");
    }
  }
}

// Parse a list of arguments and their surrounding parentheses if they are
// present.
void
parse_arguments_list(yp_parser_t *parser, yp_arguments_t *arguments) {
  if (accept(parser, YP_TOKEN_PARENTHESIS_LEFT)) {
    arguments->opening = parser->previous;

    if (accept(parser, YP_TOKEN_PARENTHESIS_RIGHT)) {
      arguments->arguments = NULL;
      arguments->closing = parser->previous;
    } else {
      arguments->arguments = yp_node_arguments_node_create(parser);
      parse_arguments(parser, arguments->arguments);
      arguments->closing = parser->previous;
    }
  } else {
    not_provided(&arguments->opening, parser->previous.end);

    // TODO: here we should handle arguments without parentheses.
    arguments->arguments = NULL;

    not_provided(&arguments->closing, parser->previous.end);
  }
}

// Parse a list of parameters on a method definition.
static yp_node_t *
parse_parameters(yp_parser_t *parser) {
  yp_node_t *params = yp_node_parameters_node_create(parser, NULL, NULL, NULL);
  bool parsing = true;

  while (parsing) {
    switch (parser->current.type) {
      case YP_TOKEN_AMPERSAND: {
        parser_lex(parser);

        yp_token_t operator = parser->previous;
        yp_token_t name;

        if (accept(parser, YP_TOKEN_IDENTIFIER)) {
          name = parser->previous;
          yp_token_list_append(&parser->current_scope->as.scope.locals, &name);
        } else {
          not_provided(&name, parser->previous.end);
        }

        yp_node_t *param = yp_node_block_parameter_node_create(parser, &operator, &name);
        params->as.parameters_node.block = param;
        if (!accept(parser, YP_TOKEN_COMMA)) parsing = false;
        break;
      }
      case YP_TOKEN_DOT_DOT_DOT: {
        parser_lex(parser);

        yp_node_t *param = yp_node_forwarding_parameter_node_create(parser, &parser->previous);
        params->as.parameters_node.keyword_rest = param;
        if (!accept(parser, YP_TOKEN_COMMA)) parsing = false;
        break;
      }
      case YP_TOKEN_IDENTIFIER: {
        parser_lex(parser);

        yp_token_t name = parser->previous;
        yp_token_list_append(&parser->current_scope->as.scope.locals, &name);

        if (accept(parser, YP_TOKEN_EQUAL)) {
          yp_token_t operator = parser->previous;
          yp_node_t *value = parse_expression(parser, BINDING_POWER_NONE, "Expected to find a default value for the parameter.");

          yp_node_t *param = yp_node_optional_parameter_node_create(parser, &name, &operator, value);
          yp_node_list_append(parser, params, &params->as.parameters_node.optionals, param);

          // If parsing the value of the parameter resulted in error recovery,
          // then we can put a missing node in its place and stop parsing the
          // parameters entirely now.
          if (parser->recovering) return params;
        } else {
          yp_node_t *param = yp_node_required_parameter_node_create(parser, &name);
          yp_node_list_append(parser, params, &params->as.parameters_node.requireds, param);
        }

        if (!accept(parser, YP_TOKEN_COMMA)) parsing = false;
        break;
      }
      case YP_TOKEN_LABEL: {
        parser_lex(parser);

        yp_token_t name = parser->previous;
        yp_token_t local = name;
        local.end -= 1;
        yp_token_list_append(&parser->current_scope->as.scope.locals, &local);

        yp_node_t *param = yp_node_keyword_parameter_node_create(parser, &name);
        yp_node_list_append(parser, params, &params->as.parameters_node.keywords, param);
        if (!accept(parser, YP_TOKEN_COMMA)) parsing = false;
        break;
      }
      case YP_TOKEN_STAR: {
        parser_lex(parser);

        yp_token_t operator = parser->previous;
        yp_token_t name;

        if (accept(parser, YP_TOKEN_IDENTIFIER)) {
          name = parser->previous;
          yp_token_list_append(&parser->current_scope->as.scope.locals, &name);
        } else {
          not_provided(&name, parser->previous.end);
        }

        yp_node_t *param = yp_node_rest_parameter_node_create(parser, &operator, &name);
        params->as.parameters_node.rest = param;
        if (!accept(parser, YP_TOKEN_COMMA)) parsing = false;
        break;
      }
      case YP_TOKEN_STAR_STAR: {
        parser_lex(parser);

        yp_node_t *param;

        if (accept(parser, YP_TOKEN_KEYWORD_NIL)) {
          param = yp_node_no_keywords_parameter_node_create(parser, &parser->previous);
        } else {
          yp_token_t operator = parser->previous;
          yp_token_t name;

          if (accept(parser, YP_TOKEN_IDENTIFIER)) {
            name = parser->previous;
            yp_token_list_append(&parser->current_scope->as.scope.locals, &name);
          } else {
            not_provided(&name, parser->previous.end);
          }

          param = yp_node_keyword_rest_parameter_node_create(parser, &operator, &name);
        }

        params->as.parameters_node.keyword_rest = param;
        if (!accept(parser, YP_TOKEN_COMMA)) parsing = false;
        break;
      }
      default: {
        parsing = false;
        break;
      }
    }
  }

  return params;
}

static inline yp_node_t *
parse_conditional(yp_parser_t *parser, yp_context_t context) {
  yp_token_t keyword = parser->previous;

  yp_node_t *predicate = parse_expression(parser, BINDING_POWER_NONE, "Expected to find a predicate for the conditional.");
  accept_any(parser, 3, YP_TOKEN_KEYWORD_THEN, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

  yp_node_t *statements = parse_statements(parser, context);
  accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

  yp_token_t end_keyword;
  not_provided(&end_keyword, parser->previous.end);

  yp_node_t *parent;
  switch (context) {
    case YP_CONTEXT_IF:
      parent = yp_node_if_node_create(parser, &keyword, predicate, statements, NULL, &end_keyword);
      break;
    case YP_CONTEXT_UNLESS:
      parent = yp_node_unless_node_create(parser, &keyword, predicate, statements, NULL, &end_keyword);
      break;
    default:
      // Should not be able to reach here.
      parent = NULL;
      break;
  }

  yp_node_t *current = parent;

  // Parse any number of elsif clauses. This will form a linked list of if
  // nodes pointing to each other from the top.
  while (parser->current.type == YP_TOKEN_KEYWORD_ELSIF) {
    parser_lex(parser);
    yp_token_t elsif_keyword = parser->previous;

    yp_node_t *predicate = parse_expression(parser, BINDING_POWER_NONE, "Expected to find a predicate for the elsif clause.");
    accept_any(parser, 3, YP_TOKEN_KEYWORD_THEN, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

    yp_node_t *statements = parse_statements(parser, YP_CONTEXT_ELSIF);
    accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

    yp_node_t *elsif = yp_node_if_node_create(parser, &elsif_keyword, predicate, statements, NULL, &end_keyword);
    current->as.if_node.consequent = elsif;
    current = elsif;
  }

  switch (parser->current.type) {
    case YP_TOKEN_KEYWORD_ELSE: {
      parser_lex(parser);
      yp_token_t else_keyword = parser->previous;
      yp_node_t *else_statements = parse_statements(parser, YP_CONTEXT_ELSE);

      accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);
      expect(parser, YP_TOKEN_KEYWORD_END, "Expected `end` to close `else` clause.");

      yp_node_t *else_node = yp_node_else_node_create(parser, &else_keyword, else_statements, &parser->previous);
      current->as.if_node.consequent = else_node;
      parent->as.if_node.end_keyword = parser->previous;
      break;
    }
    case YP_TOKEN_KEYWORD_END: {
      parser_lex(parser);
      parent->as.if_node.end_keyword = parser->previous;
      break;
    }
    default:
      expect(parser, YP_TOKEN_KEYWORD_END, "Expected `end` to close `if` statement.");
      parent->as.if_node.end_keyword = parser->previous;
      break;
  }

  return parent;
}

static yp_node_t*
parse_symbol(yp_parser_t *parser, int mode) {
  yp_token_t opening = parser->previous;

  if (mode == YP_LEX_SYMBOL) {
    yp_token_t symbol = parser->current;
    parser_lex(parser);

    yp_token_t closing;
    not_provided(&closing, parser->previous.end);

    return yp_node_symbol_node_create(parser, &opening, &symbol, &closing);
  }

  if (parser->lex_modes.current->interp) {
    yp_node_t *interpolated = yp_node_interpolated_symbol_node_create(parser, &opening, &opening);

    while (parser->current.type != YP_TOKEN_STRING_END && parser->current.type != YP_TOKEN_EOF) {
      switch (parser->current.type) {
        case YP_TOKEN_STRING_CONTENT: {
          parser_lex(parser);

          yp_token_t string_content_opening;
          not_provided(&string_content_opening, parser->previous.start);

          yp_token_t string_content_closing;
          not_provided(&string_content_closing, parser->previous.end);

          yp_node_list_append(parser, interpolated, &interpolated->as.interpolated_symbol_node.parts, yp_node_string_node_create_and_unescape(parser, &string_content_opening, &parser->previous, &string_content_closing, YP_UNESCAPE_ALL));
          break;
        }
        case YP_TOKEN_EMBEXPR_BEGIN: {
          parser_lex(parser);
          yp_token_t embexpr_opening = parser->previous;
          yp_node_t *statements = parse_statements(parser, YP_CONTEXT_EMBEXPR);
          expect(parser, YP_TOKEN_EMBEXPR_END, "Expected a closing delimiter for an embedded expression.");
          yp_node_list_append(parser, interpolated, &interpolated->as.interpolated_symbol_node.parts, yp_node_string_interpolated_node_create(parser, &embexpr_opening, statements, &parser->previous));
          break;
        }
        default:
          fprintf(stderr, "Could not understand token type %s in an interpolated symbol\n", yp_token_type_to_str(parser->previous.type));
          return NULL;
      }
    }

    expect(parser, YP_TOKEN_STRING_END, "Expected a closing delimiter for an interpolated symbol.");
    interpolated->as.interpolated_symbol_node.closing = parser->previous;
    return interpolated;
  }

  yp_token_t content;
  if (accept(parser, YP_TOKEN_STRING_CONTENT)) {
    content = parser->previous;
  } else {
    content = (yp_token_t) { .type = YP_TOKEN_STRING_CONTENT, .start = parser->previous.end, .end = parser->previous.end };
  }

  expect(parser, YP_TOKEN_STRING_END, "Expected a closing delimiter for a dynamic symbol.");
  return yp_node_symbol_node_create(parser, &opening, &content, &parser->previous);

}

// Parse an argument to alias or undef which can either be a bare word, a
// symbol, or an interpolated symbol.
static inline yp_node_t *
parse_alias_or_undef_argument(yp_parser_t *parser) {
  switch (parser->current.type) {
    case YP_TOKEN_IDENTIFIER: {
      yp_token_t opening;
      not_provided(&opening, parser->current.start);

      yp_token_t closing;
      not_provided(&closing, parser->previous.end);

      parser_lex(parser);
      return yp_node_symbol_node_create(parser, &opening, &parser->previous, &closing);
    }
    case YP_TOKEN_SYMBOL_BEGIN: {
      int mode = parser->lex_modes.current->mode;
      parser_lex(parser);
      return parse_symbol(parser, mode);
    }
    default:
      yp_error_list_append(&parser->error_list, "Expected a bare word or symbol argument.", parser->current.start - parser->start);
      return yp_node_missing_node_create(parser, parser->current.start - parser->start);
  }
}

// Parse an expression that begins with the previous node that we just lexed.
static inline yp_node_t *
parse_expression_prefix(yp_parser_t *parser) {
  yp_token_t recoverable = parser->previous;
  int mode = parser->lex_modes.current->mode;
  parser_lex(parser);

  switch (parser->previous.type) {
    case YP_TOKEN_BRACKET_LEFT: {
      yp_token_t opening = parser->previous;
      yp_node_t *array = yp_node_array_node_create(parser, &opening, &opening);

      while (parser->current.type != YP_TOKEN_BRACKET_RIGHT && parser->current.type != YP_TOKEN_EOF) {
        if (array->as.array_node.elements.size != 0) {
          expect(parser, YP_TOKEN_COMMA, "Expected a separator for the elements in an array.");
        }

        yp_node_t *element = parse_expression(parser, BINDING_POWER_DEFINED, "Expected an element for the array.");
        yp_node_list_append(parser, array, &array->as.array_node.elements, element);
      }

      expect(parser, YP_TOKEN_BRACKET_RIGHT, "Expected a closing bracket for the array.");
      array->as.array_node.closing = parser->previous;

      return array;
    }
    case YP_TOKEN_PARENTHESIS_LEFT: {
      yp_token_t opening = parser->previous;
      yp_node_t *parentheses;

      if (parser->current.type != YP_TOKEN_PARENTHESIS_RIGHT && parser->current.type != YP_TOKEN_EOF) {
        yp_node_t *statements = parse_statements(parser, YP_CONTEXT_PARENS);
        parentheses = yp_node_parentheses_node_create(parser, &opening, statements, &opening);
      } else {
        yp_node_t *statements = yp_node_statements_create(parser);
        parentheses = yp_node_parentheses_node_create(parser, &opening, statements, &opening);
      }

      expect(parser, YP_TOKEN_PARENTHESIS_RIGHT, "Expected a closing parenthesis.");
      parentheses->as.parentheses_node.closing = parser->previous;
      return parentheses;
    }
    case YP_TOKEN_CHARACTER_LITERAL: {
      yp_token_t opening = parser->previous;
      opening.type = YP_TOKEN_STRING_BEGIN;
      opening.end = opening.start + 1;

      yp_token_t content = parser->previous;
      content.type = YP_TOKEN_STRING_CONTENT;
      content.start = content.start + 1;

      yp_token_t closing;
      not_provided(&closing, content.end);

      return yp_node_string_node_create_and_unescape(parser, &opening, &content, &closing, YP_UNESCAPE_NONE);
    }
    case YP_TOKEN_CLASS_VARIABLE:
      return yp_node_class_variable_read_create(parser, &parser->previous);
    case YP_TOKEN_CONSTANT:
      return yp_node_constant_read_create(parser, &parser->previous);
    case YP_TOKEN_FLOAT:
      return yp_node_float_literal_create(parser, &parser->previous);
    case YP_TOKEN_GLOBAL_VARIABLE:
      return yp_node_global_variable_read_create(parser, &parser->previous);
    case YP_TOKEN_IDENTIFIER: {
      if (
        (parser->current.type != YP_TOKEN_PARENTHESIS_LEFT) &&
        (parser->previous.end[-1] != '!') &&
        (parser->previous.end[-1] != '?') &&
        yp_token_list_includes(&parser->current_scope->as.scope.locals, &parser->previous)
      ) {
        return yp_node_local_variable_read_create(parser, &parser->previous);
      }

      yp_token_t message = parser->previous;

      yp_token_t call_operator;
      not_provided(&call_operator, message.start);

      yp_arguments_t arguments;
      parse_arguments_list(parser, &arguments);

      yp_node_t *node = yp_node_call_node_create(parser, NULL, &call_operator, &message, &arguments.opening, arguments.arguments, &arguments.closing);
      yp_string_shared_init(&node->as.call_node.name, message.start, message.end);
      return node;
    }
    case YP_TOKEN_IMAGINARY_NUMBER:
      return yp_node_imaginary_literal_create(parser, &parser->previous);
    case YP_TOKEN_INSTANCE_VARIABLE:
      return yp_node_instance_variable_read_create(parser, &parser->previous);
    case YP_TOKEN_INTEGER:
      return yp_node_integer_literal_create(parser, &parser->previous);
    case YP_TOKEN_KEYWORD_ALIAS: {
      yp_token_t keyword = parser->previous;
      yp_node_t *left = parse_alias_or_undef_argument(parser);
      yp_node_t *right = parse_alias_or_undef_argument(parser);

      return yp_node_alias_node_create(parser, &keyword, left, right);
    }
    case YP_TOKEN_KEYWORD_BEGIN: {
      yp_token_t begin_keyword = parser->previous;
      accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

      yp_node_t *begin_statements = parse_statements(parser, YP_CONTEXT_BEGIN);
      accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

      yp_token_t end_keyword;
      not_provided(&end_keyword, parser->previous.end);

      yp_node_t *begin_node = yp_node_begin_node_create(parser, &begin_keyword, begin_statements, NULL, &end_keyword);

      if (accept(parser, YP_TOKEN_KEYWORD_ENSURE)) {
        yp_token_t ensure_keyword = parser->previous;
        accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

        yp_node_t *ensure_statements = parse_statements(parser, YP_CONTEXT_ENSURE);
        accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

        expect(parser, YP_TOKEN_KEYWORD_END, "Expected `end` to close `ensure` statement.");

        yp_node_t *ensure_node = yp_node_ensure_node_create(
          parser,
          &ensure_keyword,
          ensure_statements,
          &parser->previous
        );

        begin_node->as.begin_node.ensure_clause = ensure_node;
      } else {
        expect(parser, YP_TOKEN_KEYWORD_END, "Expected `end` to close `begin` statement.");
        begin_node->as.begin_node.end_keyword = parser->previous;
      }

      return begin_node;
    }
    case YP_TOKEN_KEYWORD_BEGIN_UPCASE: {
      yp_token_t keyword = parser->previous;
      expect(parser, YP_TOKEN_BRACE_LEFT, "Expected '{' after 'BEGIN'.");
      yp_token_t opening = parser->previous;

      yp_node_t *statements = parse_statements(parser, YP_CONTEXT_PREEXE);
      expect(parser, YP_TOKEN_BRACE_RIGHT, "Expected '}' after 'BEGIN' statements.");
      yp_token_t closing = parser->previous;

      return yp_node_pre_execution_node_create(parser, &keyword, &opening, statements, &closing);
    }
    case YP_TOKEN_KEYWORD_BREAK:
    case YP_TOKEN_KEYWORD_NEXT:
    case YP_TOKEN_KEYWORD_SUPER:
    case YP_TOKEN_KEYWORD_YIELD: {
      yp_token_t keyword = parser->previous;
      yp_arguments_t arguments;
      parse_arguments_list(parser, &arguments);

      switch (keyword.type) {
        case YP_TOKEN_KEYWORD_BREAK:
          return yp_node_break_node_create(parser, &keyword, &arguments.opening, arguments.arguments, &arguments.closing);
        case YP_TOKEN_KEYWORD_NEXT:
          return yp_node_next_node_create(parser, &keyword, &arguments.opening, arguments.arguments, &arguments.closing);
        case YP_TOKEN_KEYWORD_SUPER:
          if (arguments.opening.type == YP_TOKEN_NOT_PROVIDED && arguments.arguments == NULL) {
            return yp_node_forwarding_super_node_create(parser, &keyword);
          } else {
            return yp_node_super_node_create(parser, &keyword, &arguments.opening, arguments.arguments, &arguments.closing);
          }
        case YP_TOKEN_KEYWORD_YIELD:
          return yp_node_yield_node_create(parser, &keyword, &arguments.opening, arguments.arguments, &arguments.closing);
        default:
          // Cannot hit here.
          return NULL;
      }
    }
    case YP_TOKEN_KEYWORD_CLASS: {
      yp_token_t class_keyword = parser->previous;

      if (accept(parser, YP_TOKEN_LESS_LESS)) {
        yp_token_t operator = parser->previous;
        yp_node_t *expression = parse_expression(parser, BINDING_POWER_CALL, "Expected to find an expression after `<<`.");

        accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

        yp_node_t *scope = yp_node_scope_create(parser);
        yp_node_t *parent_scope = parser->current_scope;
        parser->current_scope = scope;

        yp_node_t *statements = parse_statements(parser, YP_CONTEXT_SCLASS);
        expect(parser, YP_TOKEN_KEYWORD_END, "Expected `end` to close `class` statement.");

        parser->current_scope = parent_scope;
        return yp_node_s_class_node_create(parser, scope, &class_keyword, &operator, expression, statements, &parser->previous);
      }

      yp_node_t *name = parse_expression(parser, BINDING_POWER_CALL, "Expected to find a class name after `class`.");
      yp_token_t inheritance_operator;
      yp_node_t *superclass;

      if (accept(parser, YP_TOKEN_LESS)) {
        inheritance_operator = parser->previous;
        superclass = parse_expression(parser, BINDING_POWER_NONE, "Expected to find a superclass after `<`.");
      } else {
        not_provided(&inheritance_operator, parser->previous.end);
        superclass = NULL;
      }

      yp_node_t *scope = yp_node_scope_create(parser);
      yp_node_t *parent_scope = parser->current_scope;
      parser->current_scope = scope;

      yp_node_t *statements = parse_statements(parser, YP_CONTEXT_CLASS);
      expect(parser, YP_TOKEN_KEYWORD_END, "Expected `end` to close `class` statement.");

      parser->current_scope = parent_scope;
      return yp_node_class_node_create(parser, scope, &class_keyword, name, &inheritance_operator, superclass, statements, &parser->previous);
    }
    case YP_TOKEN_KEYWORD_DEF: {
      yp_token_t def_keyword = parser->previous;

      expect(parser, YP_TOKEN_IDENTIFIER, "Expected name of method after `def`.");
      yp_token_t name = parser->previous;

      yp_token_t lparen;
      yp_token_t rparen;

      if (accept(parser, YP_TOKEN_PARENTHESIS_LEFT)) {
        lparen = parser->previous;
      } else {
        not_provided(&lparen, parser->previous.end);
      }

      yp_node_t *parent_scope = parser->current_scope;
      yp_node_t *scope = yp_node_scope_create(parser);
      parser->current_scope = scope;
      yp_node_t *params = parse_parameters(parser);

      if (lparen.type == YP_TOKEN_PARENTHESIS_LEFT) {
        expect(parser, YP_TOKEN_PARENTHESIS_RIGHT, "Expected ')' after left parenthesis.");
        rparen = parser->previous;
      } else {
        not_provided(&rparen, parser->previous.end);
      }

      yp_token_t equal;
      bool endless_definition = accept(parser, YP_TOKEN_EQUAL);

      if (endless_definition) {
        equal = parser->previous;
      } else {
        not_provided(&equal, parser->previous.end);
        accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);
      }

      yp_node_t *statements = parse_statements(parser, YP_CONTEXT_DEF);
      yp_token_t end_keyword;

      if (endless_definition) {
        not_provided(&end_keyword, parser->previous.end);
      } else {
        expect(parser, YP_TOKEN_KEYWORD_END, "Expected `end` to close `def` statement.");
        end_keyword = parser->previous;
      }

      parser->current_scope = parent_scope;
      return yp_node_def_node_create(parser, &def_keyword, &name, &lparen, params, &rparen, &equal, statements, &end_keyword, scope);
    }
    case YP_TOKEN_KEYWORD_DEFINED: {
      yp_token_t keyword = parser->previous;
      yp_token_t lparen;

      if (accept(parser, YP_TOKEN_PARENTHESIS_LEFT)) {
        lparen = parser->previous;
      } else {
        not_provided(&lparen, parser->previous.end);
      }

      yp_node_t *expression = parse_expression(parser, BINDING_POWER_DEFINED, "Expected expression after `defined?`.");
      yp_token_t rparen;

      if (!parser->recovering && lparen.type == YP_TOKEN_PARENTHESIS_LEFT) {
        expect(parser, YP_TOKEN_PARENTHESIS_RIGHT, "Expected ')' after 'defined?' expression.");
        rparen = parser->previous;
      } else {
        not_provided(&rparen, parser->previous.end);
      }

      return yp_node_defined_node_create(parser, &keyword, &lparen, expression, &rparen);
    }
    case YP_TOKEN_KEYWORD_END_UPCASE: {
      yp_token_t keyword = parser->previous;
      expect(parser, YP_TOKEN_BRACE_LEFT, "Expected '{' after 'END'.");
      yp_token_t opening = parser->previous;

      yp_node_t *statements = parse_statements(parser, YP_CONTEXT_POSTEXE);
      expect(parser, YP_TOKEN_BRACE_RIGHT, "Expected '}' after 'END' statements.");
      yp_token_t closing = parser->previous;

      return yp_node_post_execution_node_create(parser, &keyword, &opening, statements, &closing);
    }
    case YP_TOKEN_KEYWORD_FALSE:
      return yp_node_false_node_create(parser, &parser->previous);
    case YP_TOKEN_KEYWORD_FOR: {
      yp_token_t for_keyword = parser->previous;
      yp_node_t *index = parse_targets(parser, BINDING_POWER_INDEX, "Expected index after for.");

      expect(parser, YP_TOKEN_KEYWORD_IN, "Expected keyword in.");
      yp_token_t in_keyword = parser->previous;

      yp_node_t *collection = parse_expression(parser, BINDING_POWER_COMPOSITION, "Expected collection.");

      yp_token_t do_keyword;
      if (accept(parser, YP_TOKEN_KEYWORD_DO)) {
        do_keyword = parser->previous;
      } else {
        not_provided(&do_keyword, parser->previous.start);
      }

      yp_node_t *scope = yp_node_scope_create(parser);
      yp_node_t *parent_scope = parser->current_scope;
      parser->current_scope = scope;

      accept(parser, YP_TOKEN_SEMICOLON);
      accept(parser, YP_TOKEN_NEWLINE);

      yp_node_t *statements = parse_statements(parser, YP_CONTEXT_FOR);
      parser->current_scope = parent_scope;

      expect(parser, YP_TOKEN_KEYWORD_END, "Expected `end` to close for loop.");
      yp_token_t end_keyword = parser->previous;

      return yp_node_for_node_create(parser, &for_keyword, index, &in_keyword, collection, &do_keyword, statements, &end_keyword);
    }
    case YP_TOKEN_KEYWORD_IF:
      return parse_conditional(parser, YP_CONTEXT_IF);
    case YP_TOKEN_KEYWORD_UNDEF: {
      yp_token_t keyword = parser->previous;
      yp_node_t *undef = yp_node_undef_node_create(parser, &keyword);

      yp_node_t *name = parse_alias_or_undef_argument(parser);
      if (name->type == YP_NODE_MISSING_NODE) return undef;

      yp_node_list_append(parser, undef, &undef->as.undef_node.names, name);

      while (accept(parser, YP_TOKEN_COMMA)) {
        name = parse_alias_or_undef_argument(parser);
        if (name->type == YP_NODE_MISSING_NODE) return undef;

        yp_node_list_append(parser, undef, &undef->as.undef_node.names, name);
      }

      return undef;
    }
    case YP_TOKEN_KEYWORD_UNLESS:
      return parse_conditional(parser, YP_CONTEXT_UNLESS);
    case YP_TOKEN_KEYWORD_MODULE: {
      yp_token_t module_keyword = parser->previous;
      yp_node_t *name = parse_expression(parser, BINDING_POWER_NONE, "Expected to find a module name after `module`.");

      // If we can recover from a syntax error that occurred while parsing the
      // name of the module, then we'll handle that here.
      if (parser->recovering) {
        yp_node_t *scope = yp_node_scope_create(parser);
        yp_node_t *statements = yp_node_statements_create(parser);
        yp_token_t end_keyword;
        missing(&end_keyword, parser->previous.end);
        return yp_node_module_node_create(parser, scope, &module_keyword, name, statements, &end_keyword);
      }

      yp_node_t *scope = yp_node_scope_create(parser);
      yp_node_t *parent_scope = parser->current_scope;
      parser->current_scope = scope;

      yp_node_t *statements = parse_statements(parser, YP_CONTEXT_MODULE);
      parser->current_scope = parent_scope;

      expect(parser, YP_TOKEN_KEYWORD_END, "Expected `end` to close `module` statement.");
      return yp_node_module_node_create(parser, scope, &module_keyword, name, statements, &parser->previous);
    }
    case YP_TOKEN_KEYWORD_NIL:
      return yp_node_nil_node_create(parser, &parser->previous);
    case YP_TOKEN_KEYWORD_REDO:
      return yp_node_redo_node_create(parser, &parser->previous);
    case YP_TOKEN_KEYWORD_RETRY:
      return yp_node_retry_node_create(parser, &parser->previous);
    case YP_TOKEN_KEYWORD_SELF:
      return yp_node_self_node_create(parser, &parser->previous);
    case YP_TOKEN_KEYWORD_TRUE:
      return yp_node_true_node_create(parser, &parser->previous);
    case YP_TOKEN_KEYWORD_UNTIL: {
      yp_token_t keyword = parser->previous;

      yp_node_t *predicate = parse_expression(parser, BINDING_POWER_NONE, "Expected predicate expression after `until`.");
      accept_any(parser, 3, YP_TOKEN_KEYWORD_THEN, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

      yp_node_t *statements = parse_statements(parser, YP_CONTEXT_UNTIL);
      accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

      expect(parser, YP_TOKEN_KEYWORD_END, "Expected `end` to close `until` statement.");
      return yp_node_until_node_create(parser, &keyword, predicate, statements);
    }
    case YP_TOKEN_KEYWORD_WHILE: {
      yp_token_t keyword = parser->previous;

      yp_node_t *predicate = parse_expression(parser, BINDING_POWER_NONE, "Expected predicate expression after `while`.");
      accept_any(parser, 3, YP_TOKEN_KEYWORD_THEN, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

      yp_node_t *statements = parse_statements(parser, YP_CONTEXT_WHILE);
      accept_any(parser, 2, YP_TOKEN_NEWLINE, YP_TOKEN_SEMICOLON);

      expect(parser, YP_TOKEN_KEYWORD_END, "Expected `end` to close `while` statement.");
      return yp_node_while_node_create(parser, &keyword, predicate, statements);
    }
    case YP_TOKEN_PERCENT_LOWER_I: {
      yp_token_t opening = parser->previous;
      yp_node_t *array = yp_node_array_node_create(parser, &opening, &opening);

      while (parser->current.type != YP_TOKEN_STRING_END && parser->current.type != YP_TOKEN_EOF) {
        if (array->as.array_node.elements.size == 0) {
          accept(parser, YP_TOKEN_WORDS_SEP);
        } else {
          expect(parser, YP_TOKEN_WORDS_SEP, "Expected a separator for the symbols in a `%i` list.");
        }
        expect(parser, YP_TOKEN_STRING_CONTENT, "Expected a symbol in a `%i` list.");

        yp_token_t opening;
        not_provided(&opening, parser->previous.start);

        yp_token_t closing;
        not_provided(&closing, parser->previous.end);

        yp_node_t *symbol = yp_node_symbol_node_create(parser, &opening, &parser->previous, &closing);
        yp_node_list_append(parser, array, &array->as.array_node.elements, symbol);
      }

      expect(parser, YP_TOKEN_STRING_END, "Expected a closing delimiter for a `%i` list.");
      array->as.array_node.closing = parser->previous;

      return array;
    }
    case YP_TOKEN_PERCENT_UPPER_I: {
      yp_token_t opening = parser->previous;
      yp_node_t *array = yp_node_array_node_create(parser, &opening, &opening);

      while (parser->current.type != YP_TOKEN_STRING_END && parser->current.type != YP_TOKEN_EOF) {
        if (array->as.array_node.elements.size == 0) {
          accept(parser, YP_TOKEN_WORDS_SEP);
        } else {
          expect(parser, YP_TOKEN_WORDS_SEP, "Expected a separator for the symbols in a `%I` list.");
        }

	yp_token_t dynamic_symbol_opening;
	not_provided(&dynamic_symbol_opening, parser->previous.end);
	yp_node_t *interpolated = yp_node_interpolated_symbol_node_create(parser, &dynamic_symbol_opening, &dynamic_symbol_opening);

	while (parser->current.type != YP_TOKEN_WORDS_SEP && parser->current.type != YP_TOKEN_STRING_END && parser->current.type != YP_TOKEN_EOF) {
	  switch (parser->current.type) {
	  case YP_TOKEN_STRING_CONTENT: {
	    parser_lex(parser);

	    yp_token_t string_content_opening;
	    not_provided(&string_content_opening, parser->previous.start);

	    yp_token_t string_content_closing;
	    not_provided(&string_content_closing, parser->previous.end);

	    yp_node_t *symbol_node = yp_node_symbol_node_create(parser, &string_content_opening, &parser->previous, &string_content_closing);

	    if (parser->current.type == YP_TOKEN_EMBEXPR_BEGIN || interpolated->as.interpolated_symbol_node.parts.size > 0) {
	      yp_node_list_append(parser, interpolated, &interpolated->as.interpolated_symbol_node.parts, symbol_node);
	    } else {
	      yp_node_list_append(parser, array, &array->as.array_node.elements, symbol_node);
	    }

	    break;
	  }
	  case YP_TOKEN_EMBEXPR_BEGIN: {
	    parser_lex(parser);
	    yp_token_t embexpr_opening = parser->previous;
	    yp_node_t *statements = parse_statements(parser, YP_CONTEXT_EMBEXPR);
	    expect(parser, YP_TOKEN_EMBEXPR_END, "Expected a closing delimiter for an embedded expression.");
	    yp_node_list_append(parser, interpolated, &interpolated->as.interpolated_symbol_node.parts, yp_node_string_interpolated_node_create(parser, &embexpr_opening, statements, &parser->previous));
	    break;
	  }
	  default:
	    fprintf(stderr, "Could not understand token type %s in an interpolated symbol list\n", yp_token_type_to_str(parser->previous.type));
	    return NULL;
	  }
	}

	if (interpolated->as.interpolated_symbol_node.parts.size > 0) {
	  yp_node_list_append(parser, array, &array->as.array_node.elements, interpolated);
	} else {
	  yp_node_destroy(parser, interpolated);
	}
      }

      expect(parser, YP_TOKEN_STRING_END, "Expected a closing delimiter for a `%I` list.");
      array->as.array_node.closing = parser->previous;

      return array;
    }
    case YP_TOKEN_PERCENT_LOWER_W: {
      yp_token_t opening = parser->previous;
      yp_node_t *array = yp_node_array_node_create(parser, &opening, &opening);

      while (parser->current.type != YP_TOKEN_STRING_END && parser->current.type != YP_TOKEN_EOF) {
        if (array->as.array_node.elements.size == 0) {
          accept(parser, YP_TOKEN_WORDS_SEP);
        } else {
          expect(parser, YP_TOKEN_WORDS_SEP, "Expected a separator for the strings in a `%w` list.");
        }
        expect(parser, YP_TOKEN_STRING_CONTENT, "Expected a string in a `%w` list.");

        yp_token_t opening;
        not_provided(&opening, parser->previous.start);

        yp_token_t closing;
        not_provided(&closing, parser->previous.end);

        yp_node_t *string = yp_node_string_node_create_and_unescape(parser, &opening, &parser->previous, &closing, YP_UNESCAPE_NONE);
        yp_node_list_append(parser, array, &array->as.array_node.elements, string);
      }

      expect(parser, YP_TOKEN_STRING_END, "Expected a closing delimiter for a `%w` list.");
      array->as.array_node.closing = parser->previous;

      return array;
    }
    case YP_TOKEN_PERCENT_UPPER_W: {
      yp_token_t opening = parser->previous;
      yp_node_t *array = yp_node_array_node_create(parser, &opening, &opening);
      yp_node_t *current = NULL;

      while (parser->current.type != YP_TOKEN_STRING_END && parser->current.type != YP_TOKEN_EOF) {
        switch (parser->current.type) {
          case YP_TOKEN_WORDS_SEP: {
            if (current == NULL) {
              // If we hit a separator before we have any content, then we don't
              // need to do anything.
            } else {
              // If we hit a separator after we've hit content, then we need to
              // append that content to the list and reset the current node.
              yp_node_list_append(parser, array, &array->as.array_node.elements, current);
              current = NULL;
            }

            parser_lex(parser);
            break;
          }
          case YP_TOKEN_STRING_CONTENT: {
            parser_lex(parser);

            if (current == NULL) {
              // If we hit content and the current node is NULL, then this is
              // the first string content we've seen. In that case we're going
              // to create a new string node and set that to the current.
              yp_token_t opening;
              not_provided(&opening, parser->previous.start);

              yp_token_t closing;
              not_provided(&closing, parser->previous.end);

              current = yp_node_string_node_create_and_unescape(parser, &opening, &parser->previous, &closing, YP_UNESCAPE_ALL);
            } else if (current->type == YP_NODE_INTERPOLATED_STRING_NODE) {
              // If we hit string content and the current node is an
              // interpolated string, then we need to append the string content
              // to the list of child nodes.
              yp_token_t opening;
              not_provided(&opening, parser->previous.start);

              yp_token_t closing;
              not_provided(&closing, parser->previous.end);

              yp_node_t *next_string = yp_node_string_node_create_and_unescape(parser, &opening, &parser->previous, &closing, YP_UNESCAPE_ALL);
              yp_node_list_append(parser, current, &current->as.interpolated_string_node.parts, next_string);
            }

            break;
          }
          case YP_TOKEN_EMBEXPR_BEGIN: {
            parser_lex(parser);

            if (current == NULL) {
              // If we hit an embedded expression and the current node is NULL,
              // then this is the start of a new string. We'll set the current
              // node to a new interpolated string.
              yp_token_t opening;
              not_provided(&opening, parser->previous.start);

              yp_token_t closing;
              not_provided(&closing, parser->previous.start);

              current = yp_node_interpolated_string_node_create(parser, &opening, &closing);
            } else if (current->type == YP_NODE_STRING_NODE) {
              // If we hit an embedded expression and the current node is a
              // string node, then we'll convert the current into an
              // interpolated string and add the string node to the list of
              // parts.
              yp_token_t opening;
              not_provided(&opening, parser->previous.start);

              yp_token_t closing;
              not_provided(&closing, parser->previous.start);

              yp_node_t *interpolated = yp_node_interpolated_string_node_create(parser, &opening, &closing);
              yp_node_list_append(parser, interpolated, &interpolated->as.interpolated_string_node.parts, current);
              current = interpolated;
            } else if (current->type == YP_NODE_INTERPOLATED_STRING_NODE) {
              // If we hit an embedded expression and the current node is an
              // interpolated string, then we'll just continue on.
            }

            yp_token_t embexpr_opening = parser->previous;
            yp_node_t *statements = parse_statements(parser, YP_CONTEXT_EMBEXPR);
            expect(parser, YP_TOKEN_EMBEXPR_END, "Expected a closing delimiter for an embedded expression.");

            yp_token_t embexpr_closing = parser->previous;
            yp_node_t *interpolated = yp_node_string_interpolated_node_create(parser, &embexpr_opening, statements, &embexpr_closing);
            yp_node_list_append(parser, current, &current->as.interpolated_string_node.parts, interpolated);
            break;
          }
          default:
            expect(parser, YP_TOKEN_STRING_CONTENT, "Expected a string in a `%W` list.");
            parser_lex(parser);
            break;
        }
      }

      // If we have a current node, then we need to append it to the list.
      if (current) {
        yp_node_list_append(parser, array, &array->as.array_node.elements, current);
      }

      expect(parser, YP_TOKEN_STRING_END, "Expected a closing delimiter for a `%W` list.");
      array->as.array_node.closing = parser->previous;
      return array;
    }
    case YP_TOKEN_RATIONAL_NUMBER:
      return yp_node_rational_literal_create(parser, &parser->previous);
    case YP_TOKEN_REGEXP_BEGIN: {
      yp_token_t opening = parser->previous;
      yp_token_t content;

      if (accept(parser, YP_TOKEN_STRING_CONTENT)) {
        content = parser->previous;
      } else {
        content = (yp_token_t) { .type = YP_TOKEN_STRING_CONTENT, .start = parser->previous.end, .end = parser->previous.end };
      }

      expect(parser, YP_TOKEN_REGEXP_END, "Expected a closing delimiter for a regular expression.");
      return yp_node_regular_expression_node_create(parser, &opening, &content, &parser->previous);
    }
    case YP_TOKEN_BANG:
    case YP_TOKEN_TILDE: {
      yp_token_t operator_token = parser->previous;

      yp_token_t lparen;
      not_provided(&lparen, operator_token.end);

      yp_token_t rparen;
      not_provided(&rparen, operator_token.end);

      yp_token_t call_operator;
      not_provided(&call_operator, operator_token.start);

      yp_node_t *receiver = parse_expression(parser, binding_powers[parser->previous.type].right, "Expected a receiver after unary operator.");

      yp_node_t *node = yp_node_call_node_create(parser, receiver, &call_operator, &operator_token, &lparen, NULL, &rparen);
      yp_string_shared_init(&node->as.call_node.name, operator_token.start, operator_token.end);
      return node;
    }
    case YP_TOKEN_MINUS: {
      yp_token_t operator_token = parser->previous;

      yp_token_t lparen;
      not_provided(&lparen, operator_token.end);

      yp_token_t rparen;
      not_provided(&rparen, operator_token.end);

      yp_token_t call_operator;
      not_provided(&call_operator, operator_token.start);

      yp_node_t *receiver = parse_expression(parser, binding_powers[parser->previous.type].right, "Expected a receiver after unary -.");

      yp_node_t *node = yp_node_call_node_create(parser, receiver, &call_operator, &operator_token, &lparen, NULL, &rparen);
      yp_string_constant_init(&node->as.call_node.name, "-@", 2);
      return node;
    }
    case YP_TOKEN_PLUS: {
      yp_token_t operator_token = parser->previous;

      yp_token_t lparen;
      not_provided(&lparen, operator_token.end);

      yp_token_t rparen;
      not_provided(&rparen, operator_token.end);

      yp_token_t call_operator;
      not_provided(&call_operator, operator_token.start);

      yp_node_t *receiver = parse_expression(parser, binding_powers[parser->previous.type].right, "Expected a receiver after unary +.");

      yp_node_t *node = yp_node_call_node_create(parser, receiver, &call_operator, &operator_token, &lparen, NULL, &rparen);
      yp_string_constant_init(&node->as.call_node.name, "+@", 2);
      return node;
    }
    case YP_TOKEN_STRING_BEGIN: {
      yp_token_t opening = parser->previous;

      if (parser->lex_modes.current->interp) {
        yp_node_t *interpolated = yp_node_interpolated_string_node_create(parser, &opening, &opening);

        while (parser->current.type != YP_TOKEN_STRING_END && parser->current.type != YP_TOKEN_EOF) {
          switch (parser->current.type) {
            case YP_TOKEN_STRING_CONTENT: {
              parser_lex(parser);

              yp_token_t string_content_opening;
              not_provided(&string_content_opening, parser->previous.start);

              yp_token_t string_content_closing;
              not_provided(&string_content_closing, parser->previous.end);

              yp_node_t *part = yp_node_string_node_create_and_unescape(parser, &string_content_opening, &parser->previous, &string_content_closing, YP_UNESCAPE_ALL);
              yp_node_list_append(parser, interpolated, &interpolated->as.interpolated_string_node.parts, part);
              break;
            }
            case YP_TOKEN_EMBEXPR_BEGIN: {
              parser_lex(parser);
              yp_token_t embexpr_opening = parser->previous;
              yp_node_t *statements = parse_statements(parser, YP_CONTEXT_EMBEXPR);
              expect(parser, YP_TOKEN_EMBEXPR_END, "Expected a closing delimiter for an embedded expression.");
              yp_node_list_append(parser, interpolated, &interpolated->as.interpolated_string_node.parts, yp_node_string_interpolated_node_create(parser, &embexpr_opening, statements, &parser->previous));
              break;
            }
            default:
              fprintf(stderr, "Could not understand token type %s in an interpolated string\n", yp_token_type_to_str(parser->previous.type));
              return NULL;
          }
        }

        expect(parser, YP_TOKEN_STRING_END, "Expected a closing delimiter for an interpolated string.");
        interpolated->as.interpolated_string_node.closing = parser->previous;
        return interpolated;
      }

      yp_token_t content;
      if (accept(parser, YP_TOKEN_STRING_CONTENT)) {
        content = parser->previous;
      } else {
        content = (yp_token_t) { .type = YP_TOKEN_STRING_CONTENT, .start = parser->previous.end, .end = parser->previous.end };
      }

      expect(parser, YP_TOKEN_STRING_END, "Expected a closing delimiter for a string literal.");
      return yp_node_string_node_create_and_unescape(parser, &opening, &content, &parser->previous, YP_UNESCAPE_MINIMAL);
    }
    case YP_TOKEN_SYMBOL_BEGIN:
      return parse_symbol(parser, mode);
    default:
      if (context_recoverable(parser, &parser->previous)) {
        parser->recovering = true;
      }

      parser->current = parser->previous;
      parser->previous = recoverable;
      return yp_node_missing_node_create(parser, parser->previous.start - parser->start);
  }
}

static inline yp_node_t *
parse_expression_infix(yp_parser_t *parser, yp_node_t *node, binding_power_t binding_power) {
  yp_token_t token = parser->previous;

  switch (token.type) {
    case YP_TOKEN_EQUAL: {
      switch (node->type) {
        case YP_NODE_CLASS_VARIABLE_READ: {
          yp_node_t *value = parse_expression(parser, binding_power, "Expected a value for the class variable after =.");
          yp_node_t *read = node;

          yp_node_t *result = yp_node_class_variable_write_create(parser, &node->as.class_variable_read.name, &token, value);
          yp_node_destroy(parser, read);
          return result;
        }
        case YP_NODE_CONSTANT_PATH_NODE:
        case YP_NODE_CONSTANT_READ: {
          yp_node_t *value = parse_expression(parser, binding_power, "Expected a value for the constant after =.");
          return yp_node_constant_path_write_node_create(parser, node, &token, value);
        }
        case YP_NODE_GLOBAL_VARIABLE_READ: {
          yp_node_t *value = parse_expression(parser, binding_power, "Expected a value for the global variable after =.");
          yp_node_t *read = node;

          yp_node_t *result = yp_node_global_variable_write_create(parser, &node->as.global_variable_read.name, &token, value);
          yp_node_destroy(parser, read);
          return result;
        }
        case YP_NODE_LOCAL_VARIABLE_READ: {
          yp_node_t *value = parse_expression(parser, binding_power, "Expected a value for the local variable after =.");
          yp_node_t *read = node;

          yp_token_t name = node->as.local_variable_read.name;
          yp_token_list_append(&parser->current_scope->as.scope.locals, &name);

          yp_node_t *result = yp_node_local_variable_write_create(parser, &name, &token, value);
          yp_node_destroy(parser, read);
          return result;
        }
        case YP_NODE_INSTANCE_VARIABLE_READ: {
          yp_node_t *value = parse_expression(parser, binding_power, "Expected a value for the instance variable after =.");
          yp_node_t *read = node;

          yp_node_t *result = yp_node_instance_variable_write_create(parser, &node->as.instance_variable_read.name, &token, value);
          yp_node_destroy(parser, read);
          return result;
        }
        case YP_NODE_CALL_NODE: {
          if (node->as.call_node.lparen.type == YP_TOKEN_NOT_PROVIDED && node->as.call_node.arguments == NULL) {
            // If we have no arguments to the call node and we have an equals
            // sign then this is either a method call or a local variable write.
            if (node->as.call_node.receiver == NULL) {
              // When we get here, we have a local varaible write, because it
              // was previously marked as a method call but now we have an =.
              // This looks like:
              //
              //     foo = 1
              //
              // When it was parsed in the prefix position, foo was seen as a
              // method call with no receiver and no arguments. Now we have an
              // =, so we know it's a local variable write.
              yp_node_t *value = parse_expression(parser, binding_power, "Expected a value for the local variable after =.");
              yp_node_t *read = node;

              yp_token_t name = node->as.call_node.message;
              yp_token_list_append(&parser->current_scope->as.scope.locals, &name);

              yp_node_t *result = yp_node_local_variable_write_create(parser, &name, &token, value);
              yp_node_destroy(parser, read);
              return result;
            }

            // When we get here, we have a method call, because it was
            // previously marked as a method call but now we have an =. This
            // looks like:
            //
            //     foo.bar = 1
            //
            // When it was parsed in the prefix position, foo.bar was seen as a
            // method call with no arguments. Now we have an =, so we know it's
            // a method call with an argument.
            yp_token_t lparen;
            not_provided(&lparen, token.end);

            yp_node_t *value = parse_expression(parser, binding_power, "Expected a value for the call after =.");
            yp_node_t *arguments = yp_node_arguments_node_create(parser);
            yp_node_list_append(parser, arguments, &arguments->as.arguments_node.arguments, value);

            yp_token_t rparen;
            not_provided(&rparen, parser->previous.end);

            yp_node_t *next_node = yp_node_call_node_create(
              parser,
              node->as.call_node.receiver,
              &node->as.call_node.call_operator,
              &token,
              &lparen,
              arguments,
              &rparen
            );

            int length = node->as.call_node.message.end - node->as.call_node.message.start;
            yp_string_t *name = &next_node->as.call_node.name;
            yp_string_owned_init(name, malloc(length + 1), length + 1);
            sprintf(name->as.owned.source, "%.*s=", length, node->as.call_node.message.start);

            return next_node;
          }

          // If there are arguments on the call node, then it can't be a method
          // call ending with = or a local variable write, so it must be a
          // syntax error. In this case we'll fall through to our default
          // handling.
        }
        default:
          // In this case we have an = sign, but we don't know what it's for. We
          // need to treat it as an error. For now, we'll mark it as an error
          // and just skip right past it.
          yp_error_list_append(&parser->error_list, "Unexpected `='.", parser->previous.start - parser->start);
          return node;
      }
    }
    case YP_TOKEN_AMPERSAND_AMPERSAND_EQUAL: {
      yp_node_t *value = parse_expression(parser, binding_power, "Expected a value after &&=");
      return yp_node_operator_and_assignment_node_create(parser, node, &token, value);
    }
    case YP_TOKEN_PIPE_PIPE_EQUAL: {
      yp_node_t *value = parse_expression(parser, binding_power, "Expected a value after ||=");
      return yp_node_operator_or_assignment_node_create(parser, node, &token, value);
    }
    case YP_TOKEN_AMPERSAND_EQUAL:
    case YP_TOKEN_CARET_EQUAL:
    case YP_TOKEN_GREATER_GREATER_EQUAL:
    case YP_TOKEN_LESS_LESS_EQUAL:
    case YP_TOKEN_MINUS_EQUAL:
    case YP_TOKEN_PERCENT_EQUAL:
    case YP_TOKEN_PIPE_EQUAL:
    case YP_TOKEN_PLUS_EQUAL:
    case YP_TOKEN_SLASH_EQUAL:
    case YP_TOKEN_STAR_EQUAL:
    case YP_TOKEN_STAR_STAR_EQUAL: {
      yp_node_t *value = parse_expression(parser, binding_power, "Expected a value after the operator.");
      return yp_node_operator_assignment_node_create(parser, node, &token, value);
    }
    case YP_TOKEN_AMPERSAND_AMPERSAND:
    case YP_TOKEN_KEYWORD_AND: {
      yp_node_t *right = parse_expression(parser, binding_power, "Expected a value after the operator.");
      return yp_node_and_node_create(parser, node, &token, right);
    }
    case YP_TOKEN_KEYWORD_OR:
    case YP_TOKEN_PIPE_PIPE: {
      yp_node_t *right = parse_expression(parser, binding_power, "Expected a value after the operator.");
      return yp_node_or_node_create(parser, node, &token, right);
    }
    case YP_TOKEN_BANG_EQUAL:
    case YP_TOKEN_BANG_TILDE:
    case YP_TOKEN_EQUAL_EQUAL:
    case YP_TOKEN_EQUAL_EQUAL_EQUAL:
    case YP_TOKEN_EQUAL_TILDE:
    case YP_TOKEN_LESS_EQUAL_GREATER:
    case YP_TOKEN_GREATER:
    case YP_TOKEN_GREATER_EQUAL:
    case YP_TOKEN_LESS:
    case YP_TOKEN_LESS_EQUAL:
    case YP_TOKEN_CARET:
    case YP_TOKEN_PIPE:
    case YP_TOKEN_AMPERSAND:
    case YP_TOKEN_GREATER_GREATER:
    case YP_TOKEN_LESS_LESS:
    case YP_TOKEN_MINUS:
    case YP_TOKEN_PLUS:
    case YP_TOKEN_PERCENT:
    case YP_TOKEN_SLASH:
    case YP_TOKEN_STAR:
    case YP_TOKEN_STAR_STAR: {
      yp_token_t call_operator;
      not_provided(&call_operator, token.start);

      yp_node_t *arguments = yp_node_arguments_node_create(parser);
      yp_node_t *argument = parse_expression(parser, binding_power, "Expected a value after the operator.");
      yp_node_list_append(parser, arguments, &arguments->as.arguments_node.arguments, argument);

      yp_token_t lparen;
      not_provided(&lparen, token.end);

      yp_token_t rparen;
      not_provided(&rparen, token.end);

      yp_node_t *next_node = yp_node_call_node_create(parser, node, &call_operator, &token, &lparen, arguments, &rparen);
      yp_string_shared_init(&next_node->as.call_node.name, token.start, token.end);
      return next_node;
    }
    case YP_TOKEN_AMPERSAND_DOT:
    case YP_TOKEN_DOT: {
      yp_token_t call_operator = parser->previous;

      // This if statement handles the foo.() syntax.
      if (accept(parser, YP_TOKEN_PARENTHESIS_LEFT)) {
        yp_token_t lparen = parser->previous;
        yp_token_t rparen;

        yp_token_t message;
        not_provided(&message, lparen.start);

        yp_node_t *arguments;
        if (accept(parser, YP_TOKEN_PARENTHESIS_RIGHT)) {
          rparen = parser->previous;
          arguments = NULL;
        } else {
          arguments = yp_node_arguments_node_create(parser);
          parse_arguments(parser, arguments);
          rparen = parser->previous;
        }

        yp_node_t *next_node = yp_node_call_node_create(parser, node, &call_operator, &message, &lparen, arguments, &rparen);
        yp_string_constant_init(&next_node->as.call_node.name, "call", 4);
        return next_node;
      }

      expect(parser, YP_TOKEN_IDENTIFIER, "Expected a method name after '.'");
      yp_token_t message = parser->previous;

      yp_arguments_t arguments;
      parse_arguments_list(parser, &arguments);

      yp_node_t *next_node = yp_node_call_node_create(parser, node, &call_operator, &message, &arguments.opening, arguments.arguments, &arguments.closing);
      yp_string_shared_init(&next_node->as.call_node.name, message.start, message.end);
      return next_node;
    }
    case YP_TOKEN_DOT_DOT:
    case YP_TOKEN_DOT_DOT_DOT: {
      yp_node_t *right = parse_expression(parser, binding_power, "Expected a value after the operator.");
      return yp_node_range_node_create(parser, node, &token, right);
    }
    case YP_TOKEN_KEYWORD_IF: {
      yp_node_t *statements = yp_node_statements_create(parser);
      yp_node_list_append(parser, statements, &statements->as.statements.body, node);

      yp_node_t *predicate = parse_expression(parser, binding_power, "Expected a predicate after 'if'");
      yp_token_t end_keyword;
      not_provided(&end_keyword, parser->previous.end);

      return yp_node_if_node_create(parser, &token, predicate, statements, NULL, &end_keyword);
    }
    case YP_TOKEN_KEYWORD_UNLESS: {
      yp_node_t *statements = yp_node_statements_create(parser);
      yp_node_list_append(parser, statements, &statements->as.statements.body, node);

      yp_node_t *predicate = parse_expression(parser, binding_power, "Expected a predicate after 'unless'");
      yp_token_t end_keyword;
      not_provided(&end_keyword, parser->previous.end);

      return yp_node_unless_node_create(parser, &token, predicate, statements, NULL, &end_keyword);
    }
    case YP_TOKEN_KEYWORD_UNTIL: {
      yp_node_t *statements = yp_node_statements_create(parser);
      yp_node_list_append(parser, statements, &statements->as.statements.body, node);

      yp_node_t *predicate = parse_expression(parser, binding_power, "Expected a predicate after 'until'");
      return yp_node_until_node_create(parser, &token, predicate, statements);
    }
    case YP_TOKEN_KEYWORD_WHILE: {
      yp_node_t *statements = yp_node_statements_create(parser);
      yp_node_list_append(parser, statements, &statements->as.statements.body, node);

      yp_node_t *predicate = parse_expression(parser, binding_power, "Expected a predicate after 'while'");
      return yp_node_while_node_create(parser, &token, predicate, statements);
    }
    case YP_TOKEN_QUESTION_MARK: {
      yp_node_t *true_expression = parse_expression(parser, binding_power, "Expected a value after '?'");

      if (parser->recovering) {
        // If parsing the true expression of this ternary resulted in a syntax
        // error that we can recover from, then we're going to put missing nodes
        // and tokens into the remaining places. We want to be sure to do this
        // before the `expect` function call to make sure it doesn't
        // accidentally move past a ':' token that occurs after the syntax
        // error.
        yp_token_t colon = (yp_token_t) { .type = YP_TOKEN_MISSING, .start = parser->previous.end, .end = parser->previous.end };
        yp_node_t *false_expression = yp_node_missing_node_create(parser, colon.end - parser->start);

        return yp_node_ternary_create(parser, node, &token, true_expression, &colon, false_expression);
      }

      expect(parser, YP_TOKEN_COLON, "Expected ':' after true expression in ternary operator.");

      yp_token_t colon = parser->previous;
      yp_node_t *false_expression = parse_expression(parser, binding_power, "Expected a value after ':'");

      return yp_node_ternary_create(parser, node, &token, true_expression, &colon, false_expression);
    }
    case YP_TOKEN_COLON_COLON: {
      yp_token_t delimiter = parser->previous;

      switch (parser->current.type) {
        case YP_TOKEN_CONSTANT: {
          yp_node_t *child = parse_expression(parser, binding_power, "Expected a value after '::'");
          return yp_node_constant_path_node_create(parser, node, &delimiter, child);
        }
        case YP_TOKEN_IDENTIFIER: {
          yp_node_t *call = parse_expression(parser, binding_power, "Expected a value after '::'");
          call->as.call_node.call_operator = delimiter;
          call->as.call_node.receiver = node;
          return call;
        }
        default: {
          uint32_t position = delimiter.end - parser->start;
          yp_error_list_append(&parser->error_list, "Expected identifier or constant after '::'", position);

          yp_node_t *child = yp_node_missing_node_create(parser, position);
          return yp_node_constant_path_node_create(parser, node, &delimiter, child);
        }
      }
    }
    default:
      return node;
  }
}

// Parse an expression at the given point of the parser using the given binding
// power to parse subsequent chains. If this function finds a syntax error, it
// will append the error message to the parser's error list.
//
// Consumers of this function should always check parser->recovering to
// determine if they need to perform additional cleanup.
static yp_node_t *
parse_expression(yp_parser_t *parser, binding_power_t binding_power, const char *message) {
  yp_token_t recovery = parser->previous;
  yp_node_t *node = parse_expression_prefix(parser);

  // If we found a syntax error, then the type of node returned by
  // parse_expression_prefix is going to be a missing node. In that case we need
  // to add the error message to the parser's error list.
  if (node->type == YP_NODE_MISSING_NODE) {
    yp_error_list_append(&parser->error_list, message, recovery.end - parser->start);
    return node;
  }

  // Otherwise we'll look and see if the next token can be parsed as an infix
  // operator. If it can, then we'll parse it using parse_expression_infix.
  binding_powers_t current_binding_powers;
  while (current_binding_powers = binding_powers[parser->current.type], binding_power <= current_binding_powers.left) {
    parser_lex(parser);
    node = parse_expression_infix(parser, node, current_binding_powers.right);
  }

  return node;
}

static yp_node_t *
parse_program(yp_parser_t *parser) {
  parser_lex(parser);

  yp_node_t *scope = yp_node_scope_create(parser);
  parser->current_scope = scope;

  return yp_node_program_create(parser, scope, parse_statements(parser, YP_CONTEXT_MAIN));
}

/******************************************************************************/
/* External functions                                                         */
/******************************************************************************/

// By default, the parser will not attempt to decode any more encodings than are
// already hard-coded into the parser. This function provides the default
// implementation so that the parser always has a valid function pointer.
static yp_encoding_t *
undecodeable(const char *name, size_t length) {
  return NULL;
}

// Initialize a parser with the given start and end pointers.
__attribute__((__visibility__("default"))) extern void
yp_parser_init(yp_parser_t *parser, const char *source, size_t size) {
  *parser = (yp_parser_t) {
    .lex_modes =
      {
        .index = 0,
        .stack = {{.mode = YP_LEX_DEFAULT}},
        .current = &parser->lex_modes.stack[0],
      },
    .start = source,
    .end = source + size,
    .current = {.start = source, .end = source},
    .current_scope = NULL,
    .current_context = NULL,
    .recovering = false,
    .encoding = yp_encoding_utf_8,
    .encoding_decode_callback = undecodeable
  };

  yp_list_init(&parser->error_list);
  yp_list_init(&parser->comment_list);
}

// Register a callback that will be called when YARP encounters a magic comment
// with an encoding referenced that it doesn't understand. The callback should
// return NULL if it also doesn't understand the encoding or it should return a
// pointer to a yp_encoding_t struct that contains the functions necessary to
// parse identifiers.
__attribute__((__visibility__("default"))) extern void
yp_parser_register_encoding_decode_callback(yp_parser_t *parser, yp_encoding_decode_callback_t callback) {
  parser->encoding_decode_callback = callback;
}

// Free any memory associated with the given parser.
__attribute__((__visibility__("default"))) extern void
yp_parser_free(yp_parser_t *parser) {
  yp_error_list_free(&parser->error_list);
  yp_list_free(&parser->comment_list);
}

// Get the next token type and set its value on the current pointer.
__attribute__((__visibility__("default"))) extern void
yp_lex_token(yp_parser_t *parser) {
  parser->previous = parser->current;
  parser->current.type = lex_token_type(parser);
}

// Parse the Ruby source associated with the given parser and return the tree.
__attribute__((__visibility__("default"))) extern yp_node_t *
yp_parse(yp_parser_t *parser) {
  return parse_program(parser);
}

__attribute__((__visibility__("default"))) extern void
yp_serialize(yp_parser_t *parser, yp_node_t *node, yp_buffer_t *buffer) {
  yp_buffer_append_str(buffer, "YARP", 4);
  yp_buffer_append_u8(buffer, YP_VERSION_MAJOR);
  yp_buffer_append_u8(buffer, YP_VERSION_MINOR);
  yp_buffer_append_u8(buffer, YP_VERSION_PATCH);

  yp_serialize_node(parser, node, buffer);
  yp_buffer_append_str(buffer, "\0", 1);
}

// Parse and serialize the AST represented by the given source to the given
// buffer.
__attribute__((__visibility__("default"))) extern void
yp_parse_serialize(const char *source, size_t size, yp_buffer_t *buffer) {
  yp_parser_t parser;
  yp_parser_init(&parser, source, size);

  yp_node_t *node = yp_parse(&parser);
  yp_serialize(&parser, node, buffer);

  yp_node_destroy(&parser, node);
  yp_parser_free(&parser);
}
