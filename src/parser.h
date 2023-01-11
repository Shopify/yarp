#ifndef YARP_PARSER_H
#define YARP_PARSER_H

#include <stdbool.h>
#include "enc/yp_encoding.h"
#include "util/yp_list.h"
#include "ast.h"

// When lexing Ruby source, the lexer has a small amount of state to tell which
// kind of token it is currently lexing. For example, when we find the start of
// a string, the first token that we return is a TOKEN_STRING_BEGIN token. After
// that the lexer is now in the YP_LEX_STRING mode, and will return tokens that
// are found as part of a string.
typedef struct yp_lex_mode {
  enum {
    // This state is used when any given token is being lexed.
    YP_LEX_DEFAULT,

    // This state is used when we're lexing an embdoc (a =begin..=end comment).
    YP_LEX_EMBDOC,

    // This state is used when we're lexing as normal but inside an embedded
    // expression of a string.
    YP_LEX_EMBEXPR,

    // This state is used when we're lexing a variable that is embedded directly
    // inside of a string with the # shorthand.
    YP_LEX_EMBVAR,

    // This state is used when we are lexing a list of tokens, as in a %w word
    // list literal or a %i symbol list literal.
    YP_LEX_LIST,

    // This state is used when a regular expression has been begun and we are
    // looking for the terminator.
    YP_LEX_REGEXP,

    // This state is used when we are lexing a string or a string-like token, as
    // in string content with either quote or an xstring.
    YP_LEX_STRING,

    // This state is used when a symbol has already been begun (e.g., by a
    // colon) and we still need to lex the rest of the symbol.
    YP_LEX_SYMBOL,
  } mode;

  union {
    struct {
      // This is the terminator of the list literal.
      char terminator;

      // Whether or not interpolation is allowed in this list.
      bool interpolation;
    } list;

    struct {
      // This is the terminator of the regular expression.
      char terminator;
    } regexp;

    struct {
      // This is the terminator of the string. It is typically either a single
      // or double quote.
      char terminator;

      // Whether or not interpolation is allowed in this string.
      bool interpolation;
    } string;

    struct {
      // This is the terminator of the symbol.
      char terminator;

      // Whether or not interpolation is allowed in this symbol.
      bool interpolation;
    } symbol;
  } as;

  // The previous lex state so that it knows how to pop.
  struct yp_lex_mode *prev;
} yp_lex_mode_t;

// We pre-allocate a certain number of lex states in order to avoid having to
// call malloc too many times while parsing. You really shouldn't need more than
// this because you only really nest deeply when doing string interpolation.
#define YP_LEX_STACK_SIZE 4

// A forward declaration since our error handler struct accepts a parser for
// each of its function calls.
typedef struct yp_parser yp_parser_t;

// While parsing, we keep track of a stack of contexts. This is helpful for
// error recovery so that we can pop back to a previous context when we hit a
// token that is understood by a parent context but not by the current context.
typedef enum {
  YP_CONTEXT_MAIN,     // the top level context
  YP_CONTEXT_PREEXE,   // a BEGIN block
  YP_CONTEXT_POSTEXE,  // an END block
  YP_CONTEXT_MODULE,   // a module declaration
  YP_CONTEXT_CLASS,    // a class declaration
  YP_CONTEXT_DEF,      // a method definition
  YP_CONTEXT_IF,       // an if statement
  YP_CONTEXT_ELSIF,    // an elsif clause
  YP_CONTEXT_UNLESS,   // an unless statement
  YP_CONTEXT_ELSE,     // an else clause
  YP_CONTEXT_WHILE,    // a while statement
  YP_CONTEXT_UNTIL,    // an until statement
  YP_CONTEXT_EMBEXPR,  // an interpolated expression
  YP_CONTEXT_BEGIN,    // a begin statement
  YP_CONTEXT_SCLASS,   // a singleton class definition
  YP_CONTEXT_FOR,      // a for loop
  YP_CONTEXT_PARENS,   // a parenthesized expression
  YP_CONTEXT_ENSURE,   // an ensure statement
  YP_CONTEXT_RESCUE,   // a rescue statement
} yp_context_t;

// This is a node in a linked list of contexts.
typedef struct yp_context_node {
  yp_context_t context;
  struct yp_context_node *prev;
} yp_context_node_t;

// This is the type of a comment that we've found while parsing.
typedef enum {
  YP_COMMENT_INLINE,
  YP_COMMENT_EMBDOC,
  YP_COMMENT___END__
} yp_comment_type_t;

// This is a node in the linked list of comments that we've found while parsing.
typedef struct yp_comment {
  yp_list_node_t node;
  uint32_t start;
  uint32_t end;
  yp_comment_type_t type;
} yp_comment_t;

// This struct defines the functions necessary to implement the encoding
// interface so we can determine how many bytes the subsequent character takes.
// Each callback should return the number of bytes, or 0 if the next bytes are
// invalid for the encoding and type.
typedef struct {
  size_t (*alpha_char)(const char *c);
  size_t (*alnum_char)(const char *c);
} yp_encoding_t;

// When an encoding is encountered that isn't understood by YARP, we provide
// the ability here to call out to a user-defined function to get an encoding
// struct. If the function returns something that isn't NULL, we set that to
// our encoding and use it to parse identifiers.
typedef yp_encoding_t *(*yp_encoding_decode_callback_t)(const char *name, size_t width);

// This struct represents the overall parser. It contains a reference to the
// source file, as well as pointers that indicate where in the source it's
// currently parsing. It also contains the most recent and current token that
// it's considering.
struct yp_parser {
  struct {
    yp_lex_mode_t *current;                 // the current state of the lexer
    yp_lex_mode_t stack[YP_LEX_STACK_SIZE]; // the stack of lexer states
    size_t index;                           // the current index into the lexer state stack
  } lex_modes;

  const char *start;   // the pointer to the start of the source
  const char *end;     // the pointer to the end of the source
  yp_token_t previous; // the previous token we were considering
  yp_token_t current;  // the current token we're considering

  yp_list_t comment_list;             // the list of comments that have been found while parsing
  yp_list_t error_list;               // the list of errors that have been found while parsing
  yp_node_t *current_scope;           // the current local scope

  yp_context_node_t *current_context; // the current parsing context
  bool recovering; // whether or not we're currently recovering from a syntax error

  // The encoding functions for the current file is attached to the parser as
  // it's parsing so that it can change with a magic comment.
  yp_encoding_t encoding;

  // When an encoding is encountered that isn't understood by YARP, we provide
  // the ability here to call out to a user-defined function to get an encoding
  // struct. If the function returns something that isn't NULL, we set that to
  // our encoding and use it to parse identifiers.
  yp_encoding_decode_callback_t encoding_decode_callback;
};

#endif // YARP_PARSER_H
