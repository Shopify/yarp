#include "extension.h"

#include <ruby/encoding.h>

VALUE rb_cYARP;
VALUE rb_cYARPToken;
VALUE rb_cYARPLocation;

VALUE rb_cYARPComment;
VALUE rb_cYARPParseError;
VALUE rb_cYARPParseResult;

VALUE rb_cYARPPack;
VALUE rb_cYARPPackDirective;
VALUE rb_cYARPPackFormat;

static VALUE v3_2_0_symbol;
static VALUE pack_symbol;
static VALUE unpack_symbol;

// Represents a source of Ruby code. It can either be coming from a file or a
// string. If it's a file, it's going to mmap the contents of the file. If it's
// a string it's going to just point to the contents of the string.
typedef struct {
  enum { SOURCE_FILE, SOURCE_STRING } type;
  const char *source;
  off_t size;
} source_t;

// Read the file indicated by the filepath parameter into source and load its
// contents and size into the given source_t.
int
source_file_load(source_t *source, VALUE filepath) {
  // Open the file for reading
  int fd = open(StringValueCStr(filepath), O_RDONLY);
  if (fd == -1) {
    perror("open");
    return 1;
  }

  // Stat the file to get the file size
  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    close(fd);
    perror("fstat");
    return 1;
  }

  // mmap the file descriptor to virtually get the contents
  source->size = sb.st_size;
  source->source = mmap(NULL, source->size, PROT_READ, MAP_PRIVATE, fd, 0);

  close(fd);
  if (source == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  return 0;
}

// Load the contents and size of the given string into the given source_t.
void
source_string_load(source_t *source, VALUE string) {
  *source = (source_t) {
    .type = SOURCE_STRING,
    .source = RSTRING_PTR(string),
    .size = RSTRING_LEN(string),
  };
}

// Free any resources associated with the given source_t.
void
source_file_unload(source_t *source) {
  munmap((void *) source->source, source->size);
}

// Dump the AST corresponding to the given source to a string.
static VALUE
dump_source(source_t *source) {
  yp_parser_t parser;
  yp_parser_init(&parser, source->source, source->size);

  yp_node_t *node = yp_parse(&parser);
  yp_buffer_t buffer;

  yp_buffer_init(&buffer);
  yp_serialize(&parser, node, &buffer);
  VALUE dumped = rb_str_new(buffer.value, buffer.length);

  yp_node_destroy(&parser, node);
  yp_buffer_free(&buffer);
  yp_parser_free(&parser);

  return dumped;
}

// Dump the AST corresponding to the given string to a string.
static VALUE
dump(VALUE self, VALUE string) {
  source_t source;
  source_string_load(&source, string);
  return dump_source(&source);
}

// Dump the AST corresponding to the given file to a string.
static VALUE
dump_file(VALUE self, VALUE filepath) {
  source_t source;
  if (source_file_load(&source, filepath) != 0) return Qnil;

  VALUE value = dump_source(&source);
  source_file_unload(&source);
  return value;
}

// Return an array of tokens corresponding to the given source.
static VALUE
lex_source(source_t *source) {
  yp_parser_t parser;
  yp_parser_init(&parser, source->source, source->size);

  VALUE ary = rb_ary_new();
  for (yp_lex_token(&parser); parser.current.type != YP_TOKEN_EOF; yp_lex_token(&parser)) {
    rb_ary_push(ary, yp_token_new(&parser, &parser.current));
  }

  yp_parser_free(&parser);
  return ary;
}

// Return an array of tokens corresponding to the given string.
static VALUE
lex(VALUE self, VALUE string) {
  source_t source;
  source_string_load(&source, string);
  return lex_source(&source);
}

// Return an array of tokens corresponding to the given file.
static VALUE
lex_file(VALUE self, VALUE filepath) {
  source_t source;
  if (source_file_load(&source, filepath) != 0) return Qnil;

  VALUE value = lex_source(&source);
  source_file_unload(&source);
  return value;
}

static VALUE
parse_source(source_t *source) {
  yp_parser_t parser;
  yp_parser_init(&parser, source->source, source->size);

  yp_node_t *node = yp_parse(&parser);
  VALUE comments = rb_ary_new();
  VALUE errors = rb_ary_new();

  for (yp_comment_t *comment = (yp_comment_t *) parser.comment_list.head; comment != NULL;
       comment = (yp_comment_t *) comment->node.next) {
    VALUE location_argv[] = { LONG2FIX(comment->node.start), LONG2FIX(comment->node.end) };
    VALUE type;

    switch (comment->type) {
      case YP_COMMENT_INLINE:
        type = ID2SYM(rb_intern("inline"));
        break;
      case YP_COMMENT_EMBDOC:
        type = ID2SYM(rb_intern("embdoc"));
        break;
      case YP_COMMENT___END__:
        type = ID2SYM(rb_intern("__END__"));
        break;
      default:
        type = ID2SYM(rb_intern("inline"));
        break;
    }

    VALUE comment_argv[] = { type, rb_class_new_instance(2, location_argv, rb_cYARPLocation) };
    rb_ary_push(comments, rb_class_new_instance(2, comment_argv, rb_cYARPComment));
  }

  for (yp_error_t *error = (yp_error_t *) parser.error_list.head; error != NULL;
       error = (yp_error_t *) error->node.next) {
    VALUE location_argv[] = { LONG2FIX(error->node.start), LONG2FIX(error->node.end) };

    VALUE error_argv[] = { rb_str_new(yp_string_source(&error->message), yp_string_length(&error->message)),
                           rb_class_new_instance(2, location_argv, rb_cYARPLocation) };

    rb_ary_push(errors, rb_class_new_instance(2, error_argv, rb_cYARPParseError));
  }

  VALUE result_argv[] = { yp_node_new(&parser, node), comments, errors };
  VALUE result = rb_class_new_instance(3, result_argv, rb_cYARPParseResult);

  yp_node_destroy(&parser, node);
  yp_parser_free(&parser);

  return result;
}

static VALUE
parse(VALUE self, VALUE string) {
  source_t source;
  source_string_load(&source, string);
  return parse_source(&source);
}

static VALUE
parse_file(VALUE self, VALUE rb_filepath) {
  source_t source;
  if (source_file_load(&source, rb_filepath) != 0) {
    return Qnil;
  }

  VALUE value = parse_source(&source);
  source_file_unload(&source);
  return value;
}

static VALUE
pack_type_to_symbol(yp_pack_type type) {
  switch (type) {
    case YP_PACK_SPACE:
      return ID2SYM(rb_intern("SPACE"));
    case YP_PACK_COMMENT:
      return ID2SYM(rb_intern("COMMENT"));
    case YP_PACK_INTEGER:
      return ID2SYM(rb_intern("INTEGER"));
    case YP_PACK_UTF8:
      return ID2SYM(rb_intern("UTF8"));
    case YP_PACK_BER:
      return ID2SYM(rb_intern("BER"));
    case YP_PACK_FLOAT:
      return ID2SYM(rb_intern("FLOAT"));
    case YP_PACK_STRING_SPACE_PADDED:
      return ID2SYM(rb_intern("STRING_SPACE_PADDED"));
    case YP_PACK_STRING_NULL_PADDED:
      return ID2SYM(rb_intern("STRING_NULL_PADDED"));
    case YP_PACK_STRING_NULL_TERMINATED:
      return ID2SYM(rb_intern("STRING_NULL_TERMINATED"));
    case YP_PACK_STRING_MSB:
      return ID2SYM(rb_intern("STRING_MSB"));
    case YP_PACK_STRING_LSB:
      return ID2SYM(rb_intern("STRING_LSB"));
    case YP_PACK_STRING_HEX_HIGH:
      return ID2SYM(rb_intern("STRING_HEX_HIGH"));
    case YP_PACK_STRING_HEX_LOW:
      return ID2SYM(rb_intern("STRING_HEX_LOW"));
    case YP_PACK_STRING_UU:
      return ID2SYM(rb_intern("STRING_UU"));
    case YP_PACK_STRING_MIME:
      return ID2SYM(rb_intern("STRING_MIME"));
    case YP_PACK_STRING_BASE64:
      return ID2SYM(rb_intern("STRING_BASE64"));
    case YP_PACK_STRING_FIXED:
      return ID2SYM(rb_intern("STRING_FIXED"));
    case YP_PACK_STRING_POINTER:
      return ID2SYM(rb_intern("STRING_POINTER"));
    case YP_PACK_MOVE:
      return ID2SYM(rb_intern("MOVE"));
    case YP_PACK_BACK:
      return ID2SYM(rb_intern("BACK"));
    case YP_PACK_NULL:
      return ID2SYM(rb_intern("NULL"));
    default:
      return Qnil;
  }
}

static VALUE
pack_signed_to_symbol(yp_pack_signed signed_type) {
  switch (signed_type) {
    case YP_PACK_UNSIGNED:
      return ID2SYM(rb_intern("UNSIGNED"));
    case YP_PACK_SIGNED:
      return ID2SYM(rb_intern("SIGNED"));
    case YP_PACK_SIGNED_NA:
      return ID2SYM(rb_intern("SIGNED_NA"));
    default:
      return Qnil;
  }
}

static VALUE
pack_endian_to_symbol(yp_pack_endian endian) {
  switch (endian) {
    case YP_PACK_AGNOSTIC_ENDIAN:
      return ID2SYM(rb_intern("AGNOSTIC_ENDIAN"));
    case YP_PACK_LITTLE_ENDIAN:
      return ID2SYM(rb_intern("LITTLE_ENDIAN"));
    case YP_PACK_BIG_ENDIAN:
      return ID2SYM(rb_intern("BIG_ENDIAN"));
    case YP_PACK_NATIVE_ENDIAN:
      return ID2SYM(rb_intern("NATIVE_ENDIAN"));
    case YP_PACK_ENDIAN_NA:
      return ID2SYM(rb_intern("ENDIAN_NA"));
    default:
      return Qnil;
  }
}

static VALUE
pack_size_to_symbol(yp_pack_size size) {
  switch (size) {
    case YP_PACK_SIZE_SHORT:
      return ID2SYM(rb_intern("SIZE_SHORT"));
    case YP_PACK_SIZE_INT:
      return ID2SYM(rb_intern("SIZE_INT"));
    case YP_PACK_SIZE_LONG:
      return ID2SYM(rb_intern("SIZE_LONG"));
    case YP_PACK_SIZE_LONG_LONG:
      return ID2SYM(rb_intern("SIZE_LONG_LONG"));
    case YP_PACK_SIZE_8:
      return ID2SYM(rb_intern("SIZE_8"));
    case YP_PACK_SIZE_16:
      return ID2SYM(rb_intern("SIZE_16"));
    case YP_PACK_SIZE_32:
      return ID2SYM(rb_intern("SIZE_32"));
    case YP_PACK_SIZE_64:
      return ID2SYM(rb_intern("SIZE_64"));
    case YP_PACK_SIZE_P:
      return ID2SYM(rb_intern("SIZE_P"));
    case YP_PACK_SIZE_NA:
      return ID2SYM(rb_intern("SIZE_NA"));
    default:
      return Qnil;
  }
}

static VALUE
pack_length_type_to_symbol(yp_pack_length_type length_type) {
  switch (length_type) {
    case YP_PACK_LENGTH_FIXED:
      return ID2SYM(rb_intern("LENGTH_FIXED"));
    case YP_PACK_LENGTH_MAX:
      return ID2SYM(rb_intern("LENGTH_MAX"));
    case YP_PACK_LENGTH_RELATIVE:
      return ID2SYM(rb_intern("LENGTH_RELATIVE"));
    case YP_PACK_LENGTH_NA:
      return ID2SYM(rb_intern("LENGTH_NA"));
    default:
      return Qnil;
  }
}

static VALUE
pack_encoding_to_ruby(yp_pack_encoding encoding) {
  int index;
  switch (encoding) {
    case YP_PACK_ENCODING_ASCII_8BIT:
      index = rb_ascii8bit_encindex();
      break;
    case YP_PACK_ENCODING_US_ASCII:
      index = rb_usascii_encindex();
      break;
    case YP_PACK_ENCODING_UTF_8:
      index = rb_utf8_encindex();
      break;
    default:
      return Qnil;
  }
  return rb_enc_from_encoding(rb_enc_from_index(index));
}

static VALUE
pack_parse(VALUE self, VALUE version_symbol, VALUE variant_symbol, VALUE format_string) {
  yp_pack_version version;
  if (version_symbol == v3_2_0_symbol) {
    version = YP_PACK_VERSION_3_2_0;
  } else {
    rb_raise(rb_eArgError, "invalid version");
  }

  yp_pack_variant variant;
  if (variant_symbol == pack_symbol) {
    variant = YP_PACK_VARIANT_PACK;
  } else if (variant_symbol == unpack_symbol) {
    variant = YP_PACK_VARIANT_UNPACK;
  } else {
    rb_raise(rb_eArgError, "invalid variant");
  }

  StringValue(format_string);

  const char *format = RSTRING_PTR(format_string);
  const char *format_end = format + RSTRING_LEN(format_string);
  yp_pack_encoding encoding = YP_PACK_ENCODING_START;

  VALUE directives_array = rb_ary_new();

  while (format < format_end) {
    yp_pack_type type;
    yp_pack_signed signed_type;
    yp_pack_endian endian;
    yp_pack_size size;
    yp_pack_length_type length_type;
    uint64_t length;

    const char *directive_start = format;

    yp_pack_result parse_result = yp_pack_parse(version, variant, &format, format_end, &type, &signed_type, &endian,
                                                &size, &length_type, &length, &encoding);

    const char *directive_end = format;

    switch (parse_result) {
      case YP_PACK_OK:
        break;
      case YP_PACK_ERROR_UNSUPPORTED_DIRECTIVE:
        rb_raise(rb_eArgError, "unsupported directive");
      case YP_PACK_ERROR_UNKNOWN_DIRECTIVE:
        rb_raise(rb_eArgError, "unsupported directive");
      case YP_PACK_ERROR_LENGTH_TOO_BIG:
        rb_raise(rb_eRangeError, "pack length too big");
      case YP_PACK_ERROR_BANG_NOT_ALLOWED:
        rb_raise(rb_eRangeError, "bang not allowed");
      case YP_PACK_ERROR_DOUBLE_ENDIAN:
        rb_raise(rb_eRangeError, "double endian");
      default:
        rb_bug("parse result");
    }

    if (type == YP_PACK_END) {
      break;
    }

    VALUE directive_args[9];
    directive_args[0] = version_symbol;
    directive_args[1] = variant_symbol;
    directive_args[2] = rb_usascii_str_new(directive_start, directive_end - directive_start);
    directive_args[3] = pack_type_to_symbol(type);
    directive_args[4] = pack_signed_to_symbol(signed_type);
    directive_args[5] = pack_endian_to_symbol(endian);
    directive_args[6] = pack_size_to_symbol(size);
    directive_args[7] = pack_length_type_to_symbol(length_type);
    directive_args[8] = (long) LONG2NUM(length);
    rb_ary_push(directives_array, rb_class_new_instance(9, directive_args, rb_cYARPPackDirective));
  }

  VALUE format_args[2];
  format_args[0] = directives_array;
  format_args[1] = pack_encoding_to_ruby(encoding);
  return rb_class_new_instance(2, format_args, rb_cYARPPackFormat);
}

void
Init_yarp(void) {
  if (strcmp(yp_version(), EXPECTED_YARP_VERSION) != 0) {
    rb_raise(rb_eRuntimeError, "The YARP library version (%s) does not match the expected version (%s)", yp_version(),
             EXPECTED_YARP_VERSION);
  }

  rb_cYARP = rb_define_module("YARP");
  rb_cYARPToken = rb_define_class_under(rb_cYARP, "Token", rb_cObject);
  rb_cYARPLocation = rb_define_class_under(rb_cYARP, "Location", rb_cObject);

  rb_cYARPComment = rb_define_class_under(rb_cYARP, "Comment", rb_cObject);
  rb_cYARPParseError = rb_define_class_under(rb_cYARP, "ParseError", rb_cObject);
  rb_cYARPParseResult = rb_define_class_under(rb_cYARP, "ParseResult", rb_cObject);

  rb_define_const(rb_cYARP, "VERSION", rb_sprintf("%d.%d.%d", YP_VERSION_MAJOR, YP_VERSION_MINOR, YP_VERSION_PATCH));

  rb_define_singleton_method(rb_cYARP, "dump", dump, 1);
  rb_define_singleton_method(rb_cYARP, "dump_file", dump_file, 1);

  rb_define_singleton_method(rb_cYARP, "lex", lex, 1);
  rb_define_singleton_method(rb_cYARP, "lex_file", lex_file, 1);

  rb_define_singleton_method(rb_cYARP, "parse", parse, 1);
  rb_define_singleton_method(rb_cYARP, "parse_file", parse_file, 1);

  rb_cYARPPack = rb_define_module_under(rb_cYARP, "Pack");
  rb_cYARPPackDirective = rb_define_class_under(rb_cYARPPack, "Directive", rb_cObject);
  rb_cYARPPackFormat = rb_define_class_under(rb_cYARPPack, "Format", rb_cObject);
  rb_define_singleton_method(rb_cYARPPack, "parse", pack_parse, 3);

  v3_2_0_symbol = ID2SYM(rb_intern("v3_2_0"));
  pack_symbol = ID2SYM(rb_intern("pack"));
  unpack_symbol = ID2SYM(rb_intern("unpack"));
}
