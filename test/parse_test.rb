# frozen_string_literal: true

require "test_helper"

class ParseTest < Test::Unit::TestCase
  include YARP::DSL

  test "empty string" do
    YARP.parse("") => YARP::ParseResult[node: YARP::Program[statements: YARP::Statements[body: []]]]
  end

  test "comment inline" do
    YARP.parse("# comment") => YARP::ParseResult[comments: [YARP::Comment[type: :inline]]]
  end

  test "comment __END__" do
    source = <<~RUBY
      __END__
      comment
    RUBY

    YARP.parse(source) => YARP::ParseResult[comments: [YARP::Comment[type: :__END__]]]
  end

  test "comment embedded document" do
    source = <<~RUBY
      =begin
      comment
      =end
    RUBY

    YARP.parse(source) => YARP::ParseResult[comments: [YARP::Comment[type: :embdoc]]]
  end

  test "alias bare" do
    expected = AliasNode(
      KEYWORD_ALIAS("alias"),
      SymbolNode(nil, IDENTIFIER("foo"), nil),
      SymbolNode(nil, IDENTIFIER("bar"), nil)
    )

    assert_parses expected, "alias foo bar"
  end

  test "alias symbols" do
    expected = AliasNode(
      KEYWORD_ALIAS("alias"),
      SymbolNode(SYMBOL_BEGIN(":"), IDENTIFIER("foo"), nil),
      SymbolNode(SYMBOL_BEGIN(":"), IDENTIFIER("bar"), nil)
    )

    assert_parses expected, "alias :foo :bar"
  end

  test "and keyword" do
    assert_parses AndNode(expression("1"), KEYWORD_AND("and"), expression("2")), "1 and 2"
  end

  test "and operator" do
    assert_parses AndNode(expression("1"), AMPERSAND_AMPERSAND("&&"), expression("2")), "1 && 2"
  end

  test "array literal" do
    expected = ArrayNode(
      BRACKET_LEFT("["),
      [
        IntegerLiteral(INTEGER("1")),
        FloatLiteral(FLOAT("1.0")),
        RationalLiteral(RATIONAL_NUMBER("1r")),
        ImaginaryLiteral(IMAGINARY_NUMBER("1i"))
      ],
      BRACKET_RIGHT("]")
    )

    assert_parses expected, "[1, 1.0, 1r, 1i]"
  end

  test "array literal empty" do
    assert_parses ArrayNode(BRACKET_LEFT("["), [], BRACKET_RIGHT("]")), "[]"
  end

  test "binary !=" do
    assert_parses CallNode(expression("1"), nil, BANG_EQUAL("!="), nil, ArgumentsNode([expression("2")]), nil, "!="), "1 != 2"
  end

  test "binary !~" do
    assert_parses CallNode(expression("1"), nil, BANG_TILDE("!~"), nil, ArgumentsNode([expression("2")]), nil, "!~"), "1 !~ 2"
  end

  test "binary ==" do
    assert_parses CallNode(expression("1"), nil, EQUAL_EQUAL("=="), nil, ArgumentsNode([expression("2")]), nil, "=="), "1 == 2"
  end

  test "binary ===" do
    assert_parses CallNode(expression("1"), nil, EQUAL_EQUAL_EQUAL("==="), nil, ArgumentsNode([expression("2")]), nil, "==="), "1 === 2"
  end

  test "binary =~" do
    assert_parses CallNode(expression("1"), nil, EQUAL_TILDE("=~"), nil, ArgumentsNode([expression("2")]), nil, "=~"), "1 =~ 2"
  end

  test "binary <=>" do
    assert_parses CallNode(expression("1"), nil, LESS_EQUAL_GREATER("<=>"), nil, ArgumentsNode([expression("2")]), nil, "<=>"), "1 <=> 2"
  end

  test "binary >" do
    assert_parses CallNode(expression("1"), nil, GREATER(">"), nil, ArgumentsNode([expression("2")]), nil, ">"), "1 > 2"
  end

  test "binary >=" do
    assert_parses CallNode(expression("1"), nil, GREATER_EQUAL(">="), nil, ArgumentsNode([expression("2")]), nil, ">="), "1 >= 2"
  end

  test "binary <" do
    assert_parses CallNode(expression("1"), nil, LESS("<"), nil, ArgumentsNode([expression("2")]), nil, "<"), "1 < 2"
  end

  test "binary <=" do
    assert_parses CallNode(expression("1"), nil, LESS_EQUAL("<="), nil, ArgumentsNode([expression("2")]), nil, "<="), "1 <= 2"
  end

  test "binary ^" do
    assert_parses CallNode(expression("1"), nil, CARET("^"), nil, ArgumentsNode([expression("2")]), nil, "^"), "1 ^ 2"
  end

  test "binary |" do
    assert_parses CallNode(expression("1"), nil, PIPE("|"), nil, ArgumentsNode([expression("2")]), nil, "|"), "1 | 2"
  end

  test "binary &" do
    assert_parses CallNode(expression("1"), nil, AMPERSAND("&"), nil, ArgumentsNode([expression("2")]), nil, "&"), "1 & 2"
  end

  test "binary >>" do
    assert_parses CallNode(expression("1"), nil, GREATER_GREATER(">>"), nil, ArgumentsNode([expression("2")]), nil, ">>"), "1 >> 2"
  end

  test "binary <<" do
    assert_parses CallNode(expression("1"), nil, LESS_LESS("<<"), nil, ArgumentsNode([expression("2")]), nil, "<<"), "1 << 2"
  end

  test "binary -" do
    assert_parses CallNode(expression("1"), nil, MINUS("-"), nil, ArgumentsNode([expression("2")]), nil, "-"), "1 - 2"
  end

  test "binary +" do
    assert_parses CallNode(expression("1"), nil, PLUS("+"), nil, ArgumentsNode([expression("2")]), nil, "+"), "1 + 2"
  end

  test "binary %" do
    assert_parses CallNode(expression("1"), nil, PERCENT("%"), nil, ArgumentsNode([expression("2")]), nil, "%"), "1 % 2"
  end

  test "binary /" do
    assert_parses CallNode(expression("1"), nil, SLASH("/"), nil, ArgumentsNode([expression("2")]), nil, "/"), "1 / 2"
  end

  test "binary *" do
    assert_parses CallNode(expression("1"), nil, STAR("*"), nil, ArgumentsNode([expression("2")]), nil, "*"), "1 * 2"
  end

  test "binary **" do
    assert_parses CallNode(expression("1"), nil, STAR_STAR("**"), nil, ArgumentsNode([expression("2")]), nil, "**"), "1**2"
  end

  test "break" do
    assert_parses BreakNode(KEYWORD_BREAK("break"), nil, nil, nil), "break"
  end

  test "break()" do
    assert_parses BreakNode(KEYWORD_BREAK("break"), PARENTHESIS_LEFT("("), nil, PARENTHESIS_RIGHT(")")), "break()"
  end

  test "break(1)" do
    assert_parses BreakNode(KEYWORD_BREAK("break"), PARENTHESIS_LEFT("("), ArgumentsNode([expression("1")]), PARENTHESIS_RIGHT(")")), "break(1)"
  end

  test "break(1, 2, 3)" do
    assert_parses BreakNode(KEYWORD_BREAK("break"), PARENTHESIS_LEFT("("), ArgumentsNode([expression("1"), expression("2"), expression("3")]), PARENTHESIS_RIGHT(")")), "break(1, 2, 3)"
  end

  test "call with ? identifier" do
    assert_parses CallNode(nil, nil, IDENTIFIER("a?"), nil, nil, nil, "a?"), "a?"
  end

  test "call with ! identifier" do
    assert_parses CallNode(nil, nil, IDENTIFIER("a!"), nil, nil, nil, "a!"), "a!"
  end

  test "call with ::" do
    expected = CallNode(
      expression("a"),
      COLON_COLON("::"),
      IDENTIFIER("b"),
      nil,
      nil,
      nil,
      "b"
    )

    assert_parses expected, "a::b"
  end

  test "call with .() shorthand" do
    expected = CallNode(
      CallNode(nil, nil, IDENTIFIER("a"), nil, nil, nil, "a"),
      DOT("."),
      NOT_PROVIDED(""),
      PARENTHESIS_LEFT("("),
      nil,
      PARENTHESIS_RIGHT(")"),
      "call"
    )

    assert_parses expected, "a.()"
  end

  test "call with .() shorthand and arguments" do
    expected = CallNode(
      expression("a"),
      DOT("."),
      NOT_PROVIDED(""),
      PARENTHESIS_LEFT("("),
      ArgumentsNode([expression("1"), expression("2"), expression("3")]),
      PARENTHESIS_RIGHT(")"),
      "call"
    )

    assert_parses expected, "a.(1, 2, 3)"
  end

  test "call with no parentheses or arguments" do
    expected = CallNode(
      CallNode(
        expression("a"),
        DOT("."),
        IDENTIFIER("b"),
        nil,
        nil,
        nil,
        "b"
      ),
      DOT("."),
      IDENTIFIER("c"),
      nil,
      nil,
      nil,
      "c"
    )

    assert_parses expected, "a.b.c"
  end

  test "call with parentheses and no arguments" do
    expected = CallNode(
      nil,
      nil,
      IDENTIFIER("a"),
      PARENTHESIS_LEFT("("),
      nil,
      PARENTHESIS_RIGHT(")"),
      "a"
    )

    assert_parses expected, "a()"
  end

  test "call with parentheses and arguments" do
    expected = CallNode(
      nil,
      nil,
      IDENTIFIER("a"),
      PARENTHESIS_LEFT("("),
      ArgumentsNode([expression("b"), expression("c")]),
      PARENTHESIS_RIGHT(")"),
      "a"
    )

    assert_parses expected, "a(b, c)"
  end

  test "call nested with parentheses and no arguments" do
    expected = CallNode(
      expression("a"),
      DOT("."),
      IDENTIFIER("b"),
      PARENTHESIS_LEFT("("),
      nil,
      PARENTHESIS_RIGHT(")"),
      "b"
    )

    assert_parses expected, "a.b()"
  end

  test "call nested with parentheses and arguments" do
    expected = CallNode(
      expression("a"),
      DOT("."),
      IDENTIFIER("b"),
      PARENTHESIS_LEFT("("),
      ArgumentsNode([expression("c"), expression("d")]),
      PARENTHESIS_RIGHT(")"),
      "b"
    )

    assert_parses expected, "a.b(c, d)"
  end

  test "character literal" do
    assert_parses StringNode(STRING_BEGIN("?"), STRING_CONTENT("a"), nil), "?a"
  end

  test "class" do
    expected = ClassNode(
      Scope([IDENTIFIER("a")]),
      KEYWORD_CLASS("class"),
      ConstantRead(CONSTANT("A")),
      nil,
      nil,
      Statements([
        LocalVariableWrite(
          IDENTIFIER("a"),
          EQUAL("="),
          IntegerLiteral(INTEGER("1"))
        )
      ]),
      KEYWORD_END("end")
    )

    assert_parses expected, "class A a = 1 end"
  end

  test "class with superclass" do
    expected = ClassNode(
      Scope([IDENTIFIER("a")]),
      KEYWORD_CLASS("class"),
      ConstantRead(CONSTANT("A")),
      LESS("<"),
      ConstantRead(CONSTANT("B")),
      Statements([
        LocalVariableWrite(
          IDENTIFIER("a"),
          EQUAL("="),
          IntegerLiteral(INTEGER("1"))
        )
      ]),
      KEYWORD_END("end")
    )

    assert_parses expected, "class A < B a = 1 end"
  end

  test "class variable read" do
    assert_parses ClassVariableRead(CLASS_VARIABLE("@@abc")), "@@abc"
  end

  test "class variable write" do
    assert_parses ClassVariableWrite(CLASS_VARIABLE("@@abc"), EQUAL("="), expression("1")), "@@abc = 1"
  end

  test "constant path" do
    assert_parses ConstantPathNode(ConstantRead(CONSTANT("A")), COLON_COLON("::"), ConstantRead(CONSTANT("B"))), "A::B"
  end

  test "constant path with multiple levels" do
    expected = ConstantPathNode(
      ConstantRead(CONSTANT("A")),
      COLON_COLON("::"),
      ConstantPathNode(
        ConstantRead(CONSTANT("B")),
        COLON_COLON("::"),
        ConstantRead(CONSTANT("C"))
      )
    )

    assert_parses expected, "A::B::C"
  end

  test "constant path with variable parent" do
    expected = ConstantPathNode(
      CallNode(nil, nil, IDENTIFIER("a"), nil, nil, nil, "a"),
      COLON_COLON("::"),
      ConstantRead(CONSTANT("B"))
    )

    assert_parses expected, "a::B"
  end

  test "constant read" do
    assert_parses ConstantRead(CONSTANT("ABC")), "ABC"
  end

  test "constant path write single" do
    assert_parses ConstantPathWriteNode(ConstantRead(CONSTANT("A")), EQUAL("="), expression("1")), "A = 1"
  end

  test "constant path write multiple" do
    expected = ConstantPathWriteNode(
      ConstantPathNode(ConstantRead(CONSTANT("A")), COLON_COLON("::"), ConstantRead(CONSTANT("B"))),
      EQUAL("="),
      expression("1")
    )

    assert_parses expected, "A::B = 1"
  end

  test "def without parentheses" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode([], [], nil, [], nil, nil),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([])
    )

    assert_parses expected, "def a\nend"
  end

  test "def with parentheses" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      PARENTHESIS_LEFT("("),
      ParametersNode([], [], nil, [], nil, nil),
      PARENTHESIS_RIGHT(")"),
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([])
    )

    assert_parses expected, "def a()\nend"
  end

  test "def with scope" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode([], [], nil, [], nil, nil),
      nil,
      nil,
      Statements([expression("b = 1")]),
      KEYWORD_END("end"),
      Scope([IDENTIFIER("b")])
    )

    assert_parses expected, "def a\nb = 1\nend"
  end

  test "def with required parameter" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode([RequiredParameterNode(IDENTIFIER("b"))], [], nil, [], nil, nil),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([IDENTIFIER("b")])
    )

    assert_parses expected, "def a b\nend"
  end

  test "def with multiple required parameters" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode(
        [
          RequiredParameterNode(IDENTIFIER("b")),
          RequiredParameterNode(IDENTIFIER("c")),
          RequiredParameterNode(IDENTIFIER("d"))
        ],
        [],
        nil,
        [],
        nil,
        nil
      ),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([IDENTIFIER("b"), IDENTIFIER("c"), IDENTIFIER("d")])
    )

    assert_parses expected, "def a b, c, d\nend"
  end

  test "def with required and optional parameters" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode(
        [RequiredParameterNode(IDENTIFIER("b"))],
        [OptionalParameterNode(IDENTIFIER("c"), EQUAL("="), expression("2"))],
        nil,
        [],
        nil,
        nil
      ),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([IDENTIFIER("b"), IDENTIFIER("c")])
    )

    assert_parses expected, "def a b, c = 2\nend"
  end

  test "def with optional parameters" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode(
        [],
        [
          OptionalParameterNode(IDENTIFIER("b"), EQUAL("="), expression("1")),
          OptionalParameterNode(IDENTIFIER("c"), EQUAL("="), expression("2"))
        ],
        nil,
        [],
        nil,
        nil
      ),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([IDENTIFIER("b"), IDENTIFIER("c")])
    )

    assert_parses expected, "def a b = 1, c = 2\nend"
  end

  test "def with rest parameter" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode([], [], RestParameterNode(STAR("*"), IDENTIFIER("b")), [], nil, nil),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([IDENTIFIER("b")])
    )

    assert_parses expected, "def a *b\nend"
  end

  test "def with rest parameter without name" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode([], [], RestParameterNode(STAR("*"), nil), [], nil, nil),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([])
    )

    assert_parses expected, "def a *\nend"
  end

  test "def with keyword rest parameter" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode([], [], nil, [], KeywordRestParameterNode(STAR_STAR("**"), IDENTIFIER("b")), nil),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([IDENTIFIER("b")])
    )

    assert_parses expected, "def a **b\nend"
  end

  test "def with keyword rest parameter without name" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode([], [], nil, [], KeywordRestParameterNode(STAR_STAR("**"), nil), nil),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([])
    )

    assert_parses expected, "def a **\nend"
  end

  test "def with forwarding parameter" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode([], [], nil, [], ForwardingParameterNode(DOT_DOT_DOT("...")), nil),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([])
    )

    assert_parses expected, "def a ...\nend"
  end

  test "def with block parameter" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode([], [], nil, [], nil, BlockParameterNode(AMPERSAND("&"), IDENTIFIER("b"))),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([IDENTIFIER("b")])
    )

    assert_parses expected, "def a &b\nend"
  end

  test "def with block parameter without name" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("a"),
      nil,
      ParametersNode([], [], nil, [], nil, BlockParameterNode(AMPERSAND("&"), nil)),
      nil,
      nil,
      Statements([]),
      KEYWORD_END("end"),
      Scope([])
    )

    assert_parses expected, "def a &\nend"
  end

  test "defined? without parentheses" do
    assert_parses DefinedNode(KEYWORD_DEFINED("defined?"), nil, expression("1"), nil), "defined? 1"
  end

  test "defined? with parentheses" do
    assert_parses DefinedNode(KEYWORD_DEFINED("defined?"), PARENTHESIS_LEFT("("), expression("1"), PARENTHESIS_RIGHT(")")), "defined?(1)"
  end

  test "defined? binding power" do
    expected =
      AndNode(
        DefinedNode(KEYWORD_DEFINED("defined?"), nil, expression("1"), nil),
        KEYWORD_AND("and"),
        DefinedNode(KEYWORD_DEFINED("defined?"), nil, expression("2"), nil)
      )

    assert_parses expected, "defined? 1 and defined? 2"
  end

  test "false" do
    assert_parses FalseNode(KEYWORD_FALSE("false")), "false"
  end

  test "float" do
    assert_parses FloatLiteral(FLOAT("1.0")), "1.0"
  end

  test "global variable read" do
    assert_parses GlobalVariableRead(GLOBAL_VARIABLE("$abc")), "$abc"
  end

  test "global variable write" do
    assert_parses GlobalVariableWrite(GLOBAL_VARIABLE("$abc"), EQUAL("="), expression("1")), "$abc = 1"
  end

  test "identifier" do
    assert_parses CallNode(nil, nil, IDENTIFIER("a"), nil, nil, nil, "a"), "a"
  end

  test "if" do
    assert_parses IfNode(KEYWORD_IF("if"), expression("true"), Statements([expression("1")]), nil, KEYWORD_END("end")), "if true; 1; end"
  end

  test "if modifier" do
    assert_parses IfNode(KEYWORD_IF("if"), expression("true"), Statements([expression("1")]), nil, nil), "1 if true"
  end

  test "if else" do
    expected = IfNode(
      KEYWORD_IF("if"),
      expression("true"),
      Statements([expression("1")]),
      ElseNode(
        KEYWORD_ELSE("else"),
        Statements([expression("2")]),
        KEYWORD_END("end")
      ),
      KEYWORD_END("end")
    )

    assert_parses expected, "if true\n1 else 2 end"
  end

  test "if elsif" do
    expected = IfNode(
      KEYWORD_IF("if"),
      TrueNode(KEYWORD_TRUE("true")),
      Statements([TrueNode(KEYWORD_TRUE("true"))]),
      IfNode(
        KEYWORD_ELSIF("elsif"),
        FalseNode(KEYWORD_FALSE("false")),
        Statements([FalseNode(KEYWORD_FALSE("false"))]),
        IfNode(
          KEYWORD_ELSIF("elsif"),
          NilNode(KEYWORD_NIL("nil")),
          Statements([NilNode(KEYWORD_NIL("nil"))]),
          ElseNode(
            KEYWORD_ELSE("else"),
            Statements([SelfNode(KEYWORD_SELF("self"))]),
            KEYWORD_END("end")
          ),
          nil
        ),
        nil
      ),
      KEYWORD_END("end")
    )

    assert_parses expected, "if true then true elsif false then false elsif nil then nil else self end"
  end

  test "imaginary" do
    assert_parses ImaginaryLiteral(IMAGINARY_NUMBER("1i")), "1i"
  end

  test "instance variable read" do
    assert_parses InstanceVariableRead(INSTANCE_VARIABLE("@abc")), "@abc"
  end

  test "instance variable write" do
    assert_parses InstanceVariableWrite(INSTANCE_VARIABLE("@abc"), EQUAL("="), expression("1")), "@abc = 1"
  end

  test "local variable write" do
    assert_parses LocalVariableWrite(IDENTIFIER("abc"), EQUAL("="), expression("1")), "abc = 1"
  end

  test "module" do
    expected = ModuleNode(
      Scope([IDENTIFIER("a")]),
      KEYWORD_MODULE("module"),
      ConstantRead(CONSTANT("A")),
      Statements([
        LocalVariableWrite(
          IDENTIFIER("a"),
          EQUAL("="),
          IntegerLiteral(INTEGER("1"))
        )
      ]),
      KEYWORD_END("end")
    )

    assert_parses expected, "module A a = 1 end"
  end

  test "next" do
    assert_parses NextNode(KEYWORD_NEXT("next"), nil, nil, nil), "next"
  end

  test "next()" do
    assert_parses NextNode(KEYWORD_NEXT("next"), PARENTHESIS_LEFT("("), nil, PARENTHESIS_RIGHT(")")), "next()"
  end

  test "next(1)" do
    assert_parses NextNode(KEYWORD_NEXT("next"), PARENTHESIS_LEFT("("), ArgumentsNode([expression("1")]), PARENTHESIS_RIGHT(")")), "next(1)"
  end

  test "next(1, 2, 3)" do
    assert_parses NextNode(KEYWORD_NEXT("next"), PARENTHESIS_LEFT("("), ArgumentsNode([expression("1"), expression("2"), expression("3")]), PARENTHESIS_RIGHT(")")), "next(1, 2, 3)"
  end

  test "nil" do
    assert_parses NilNode(KEYWORD_NIL("nil")), "nil"
  end

  test "or keyword" do
    assert_parses OrNode(expression("1"), KEYWORD_OR("or"), expression("2")), "1 or 2"
  end

  test "or operator" do
    assert_parses OrNode(expression("1"), PIPE_PIPE("||"), expression("2")), "1 || 2"
  end

  test "operator and assignment" do
    assert_parses OperatorAndAssignmentNode(expression("a"), AMPERSAND_AMPERSAND_EQUAL("&&="), expression("b")), "a &&= b"
  end

  test "operator or assignment" do
    assert_parses OperatorOrAssignmentNode(expression("a"), PIPE_PIPE_EQUAL("||="), expression("b")), "a ||= b"
  end

  test "operator assignment" do
    assert_parses OperatorAssignmentNode(expression("a"), PLUS_EQUAL("+="), expression("b")), "a += b"
  end

  test "post execution" do
    assert_parses PostExecutionNode(KEYWORD_END_UPCASE("END"), BRACE_LEFT("{"), Statements([expression("1")]), BRACE_RIGHT("}")), "END { 1 }"
  end

  test "pre execution" do
    assert_parses PreExecutionNode(KEYWORD_BEGIN_UPCASE("BEGIN"), BRACE_LEFT("{"), Statements([expression("1")]), BRACE_RIGHT("}")), "BEGIN { 1 }"
  end

  test "range inclusive" do
    assert_parses RangeNode(expression("1"), DOT_DOT(".."), expression("2")), "1..2"
  end

  test "range exclusive" do
    assert_parses RangeNode(expression("1"), DOT_DOT_DOT("..."), expression("2")), "1...2"
  end

  test "rational" do
    assert_parses RationalLiteral(RATIONAL_NUMBER("1r")), "1r"
  end

  test "redo" do
    assert_parses RedoNode(KEYWORD_REDO("redo")), "redo"
  end

  test "regular expression, /, no interpolation" do
    assert_parses RegularExpressionNode(REGEXP_BEGIN("/"), STRING_CONTENT("abc"), REGEXP_END("/i")), "/abc/i"
  end

  test "retry" do
    assert_parses RetryNode(KEYWORD_RETRY("retry")), "retry"
  end

  test "self" do
    assert_parses SelfNode(KEYWORD_SELF("self")), "self"
  end

  test "string empty" do
    assert_parses StringNode(STRING_BEGIN("'"), STRING_CONTENT(""), STRING_END("'")), "''"
  end

  test "string interpolation disallowed" do
    assert_parses StringNode(STRING_BEGIN("'"), STRING_CONTENT("abc"), STRING_END("'")), "'abc'"
  end

  test "string interpolation allowed, but not used" do
    expected = InterpolatedStringNode(
      STRING_BEGIN("\""),
      [StringNode(nil, STRING_CONTENT("abc"), nil)],
      STRING_END("\"")
    )

    assert_parses expected, "\"abc\""
  end

  test "string interpolation allowed, sed" do
    expected = InterpolatedStringNode(
      STRING_BEGIN("\""),
      [
        StringNode(nil, STRING_CONTENT("aaa "), nil),
        StringInterpolatedNode(
          EMBEXPR_BEGIN("\#{"),
          Statements([CallNode(nil, nil, IDENTIFIER("bbb"), nil, nil, nil, "bbb")]),
          EMBEXPR_END("}")
        ),
        StringNode(nil, STRING_CONTENT(" ccc"), nil)
      ],
      STRING_END("\"")
    )

    assert_parses expected, "\"aaa \#{bbb} ccc\""
  end

  test "string list" do
    expected = ArrayNode(
      PERCENT_LOWER_W("%w["),
      [
        StringNode(nil, STRING_CONTENT("a"), nil),
        StringNode(nil, STRING_CONTENT("b"), nil),
        StringNode(nil, STRING_CONTENT("c"), nil)
      ],
      STRING_END("]")
    )

    assert_parses expected, "%w[a b c]"
  end

  test "string list with interpolation allowed but not used" do
    expected = ArrayNode(
      PERCENT_UPPER_W("%W["),
      [
        StringNode(nil, STRING_CONTENT("a"), nil),
        StringNode(nil, STRING_CONTENT("b"), nil),
        StringNode(nil, STRING_CONTENT("c"), nil)
      ],
      STRING_END("]")
    )

    assert_parses expected, "%W[a b c]"
  end

  test "string list with interpolation allowed and used" do
    expected = ArrayNode(
      PERCENT_UPPER_W("%W["),
      [
        StringNode(nil, STRING_CONTENT("a"), nil),
        InterpolatedStringNode(
          nil,
          [
            StringNode(nil, STRING_CONTENT("b"), nil),
            StringInterpolatedNode(
              EMBEXPR_BEGIN("\#{"),
              Statements([expression("c")]),
              EMBEXPR_END("}")
            ),
            StringNode(nil, STRING_CONTENT("d"), nil)
          ],
          nil
        ),
        StringNode(nil, STRING_CONTENT("e"), nil)
      ],
      STRING_END("]")
    )

    assert_parses expected, "%W[a b\#{c}d e]"
  end

  test "super" do
    assert_parses ForwardingSuperNode(KEYWORD_SUPER("super")), "super"
  end

  test "super()" do
    assert_parses SuperNode(KEYWORD_SUPER("super"), PARENTHESIS_LEFT("("), nil, PARENTHESIS_RIGHT(")")), "super()"
  end

  test "super(1)" do
    assert_parses SuperNode(KEYWORD_SUPER("super"), PARENTHESIS_LEFT("("), ArgumentsNode([expression("1")]), PARENTHESIS_RIGHT(")")), "super(1)"
  end

  test "super(1, 2, 3)" do
    assert_parses SuperNode(KEYWORD_SUPER("super"), PARENTHESIS_LEFT("("), ArgumentsNode([expression("1"), expression("2"), expression("3")]), PARENTHESIS_RIGHT(")")), "super(1, 2, 3)"
  end

  test "symbol" do
    assert_parses SymbolNode(SYMBOL_BEGIN(":"), IDENTIFIER("a"), nil), ":a"
  end

  test "keyword symbol" do
    assert_parses SymbolNode(SYMBOL_BEGIN(":"), KEYWORD_SELF("self"), nil), ":self"
  end

  test "constant symbol" do
    assert_parses SymbolNode(SYMBOL_BEGIN(":"), CONSTANT("Foo"), nil), ":Foo"
  end

  test "intance variable symbol" do
    assert_parses SymbolNode(SYMBOL_BEGIN(":"), INSTANCE_VARIABLE("@foo"), nil), ":@foo"
  end

  test "class variable symbol" do
    assert_parses SymbolNode(SYMBOL_BEGIN(":"), CLASS_VARIABLE("@@foo"), nil), ":@@foo"
  end

  test "operator symbol" do
    assert_parses SymbolNode(SYMBOL_BEGIN(":"), PLUS("+"), nil), ":+"
    assert_parses SymbolNode(SYMBOL_BEGIN(":"), BRACKET_LEFT_RIGHT("[]"), nil), ":[]"
  end

  test "global variable symbol" do
    assert_parses SymbolNode(SYMBOL_BEGIN(":"), GLOBAL_VARIABLE("$foo"), nil), ":$foo"
  end

  test "symbol list" do
    expected = ArrayNode(
      PERCENT_LOWER_I("%i["),
      [
        SymbolNode(nil, STRING_CONTENT("a"), nil),
        SymbolNode(nil, STRING_CONTENT("b"), nil),
        SymbolNode(nil, STRING_CONTENT("c"), nil)
      ],
      STRING_END("]")
    )

    assert_parses expected, "%i[a b c]"
  end

  test "dynamic symbol" do
    expected = SymbolNode(SYMBOL_BEGIN(":'"), STRING_CONTENT("abc"), STRING_END("'"))
    assert_parses expected, ":'abc'"
  end

  test "dynamic symbol with interpolation" do
    expected = InterpolatedSymbolNode(
      SYMBOL_BEGIN(":\""),
      [StringNode(nil, STRING_CONTENT("abc"), nil),
       StringInterpolatedNode(
         EMBEXPR_BEGIN("\#{"),
         Statements([expression("1")]),
         EMBEXPR_END("}")
       )],
      STRING_END("\"")
    )
    assert_parses expected, ":\"abc\#{1}\""
  end

  test "alias with dynamic symbol" do
    expected = AliasNode(
      KEYWORD_ALIAS("alias"),
      SymbolNode(SYMBOL_BEGIN(":'"), STRING_CONTENT("abc"), STRING_END("'")),
      SymbolNode(SYMBOL_BEGIN(":'"), STRING_CONTENT("def"), STRING_END("'"))
    )
    assert_parses expected, "alias :'abc' :'def'"
  end

  test "alias with dynamic symbol with interpolation" do
    expected = AliasNode(
      KEYWORD_ALIAS("alias"),
      InterpolatedSymbolNode(
        SYMBOL_BEGIN(":\""),
        [StringNode(nil, STRING_CONTENT("abc"), nil),
         StringInterpolatedNode(
           EMBEXPR_BEGIN("\#{"),
           Statements([expression("1")]),
           EMBEXPR_END("}")
         )],
        STRING_END("\"")
      ),
      SymbolNode(SYMBOL_BEGIN(":'"), STRING_CONTENT("def"), STRING_END("'"))
    )
    assert_parses expected, "alias :\"abc\#{1}\" :'def'"
  end

  test "undef with dynamic symbols" do
    expected = UndefNode(
      KEYWORD_UNDEF("undef"),
      [SymbolNode(SYMBOL_BEGIN(":'"), STRING_CONTENT("abc"), STRING_END("'"))]
    )
    assert_parses expected, "undef :'abc'"
  end

  test "undef with dynamic symbols with interpolation" do
    expected = UndefNode(
      KEYWORD_UNDEF("undef"),
      [InterpolatedSymbolNode(
         SYMBOL_BEGIN(":\""),
         [StringNode(nil, STRING_CONTENT("abc"), nil),
          StringInterpolatedNode(
            EMBEXPR_BEGIN("\#{"),
            Statements([expression("1")]),
            EMBEXPR_END("}")
          )],
         STRING_END("\"")
       )]
    )
    assert_parses expected, "undef :\"abc\#{1}\""
  end

  test "ternary" do
    expected = Ternary(
      CallNode(nil, nil, IDENTIFIER("a"), nil, nil, nil, "a"),
      QUESTION_MARK("?"),
      CallNode(nil, nil, IDENTIFIER("b"), nil, nil, nil, "b"),
      COLON(":"),
      CallNode(nil, nil, IDENTIFIER("c"), nil, nil, nil, "c")
    )

    assert_parses expected, "a ? b : c"
  end

  test "true" do
    assert_parses TrueNode(KEYWORD_TRUE("true")), "true"
  end

  test "unary !" do
    assert_parses CallNode(expression("1"), nil, BANG("!"), nil, nil, nil, "!"), "!1"
  end

  test "unary -" do
    assert_parses CallNode(expression("1"), nil, MINUS("-"), nil, nil, nil, "-@"), "-1"
  end

  test "unary ~" do
    assert_parses CallNode(expression("1"), nil, TILDE("~"), nil, nil, nil, "~"), "~1"
  end

  test "undef bare" do
    assert_parses UndefNode(KEYWORD_UNDEF("undef"), [SymbolNode(nil, IDENTIFIER("a"), nil)]), "undef a"
  end

  test "undef bare, multiple" do
    assert_parses UndefNode(KEYWORD_UNDEF("undef"), [SymbolNode(nil, IDENTIFIER("a"), nil), SymbolNode(nil, IDENTIFIER("b"), nil)]), "undef a, b"
  end

  test "undef symbol" do
    assert_parses UndefNode(KEYWORD_UNDEF("undef"), [SymbolNode(SYMBOL_BEGIN(":"), IDENTIFIER("a"), nil)]), "undef :a"
  end

  test "undef symbol, multiple" do
    expected = UndefNode(
      KEYWORD_UNDEF("undef"),
      [
        SymbolNode(SYMBOL_BEGIN(":"), IDENTIFIER("a"), nil),
        SymbolNode(SYMBOL_BEGIN(":"), IDENTIFIER("b"), nil),
        SymbolNode(SYMBOL_BEGIN(":"), IDENTIFIER("c"), nil)
      ]
    )

    assert_parses expected, "undef :a, :b, :c"
  end

  test "unless" do
    assert_parses UnlessNode(KEYWORD_UNLESS("unless"), expression("true"), Statements([expression("1")]), nil, KEYWORD_END("end")), "unless true; 1; end"
  end

  test "unless modifier" do
    assert_parses UnlessNode(KEYWORD_UNLESS("unless"), expression("true"), Statements([expression("1")]), nil, nil), "1 unless true"
  end

  test "unless else" do
    expected = UnlessNode(
      KEYWORD_UNLESS("unless"),
      expression("true"),
      Statements([expression("1")]),
      ElseNode(
        KEYWORD_ELSE("else"),
        Statements([expression("2")]),
        KEYWORD_END("end")
      ),
      KEYWORD_END("end")
    )

    assert_parses expected, "unless true\n1 else 2 end"
  end

  test "unless elsif" do
    expected = UnlessNode(
      KEYWORD_UNLESS("unless"),
      TrueNode(KEYWORD_TRUE("true")),
      Statements([TrueNode(KEYWORD_TRUE("true"))]),
      IfNode(
        KEYWORD_ELSIF("elsif"),
        FalseNode(KEYWORD_FALSE("false")),
        Statements([FalseNode(KEYWORD_FALSE("false"))]),
        IfNode(
          KEYWORD_ELSIF("elsif"),
          NilNode(KEYWORD_NIL("nil")),
          Statements([NilNode(KEYWORD_NIL("nil"))]),
          ElseNode(
            KEYWORD_ELSE("else"),
            Statements([SelfNode(KEYWORD_SELF("self"))]),
            KEYWORD_END("end")
          ),
          nil
        ),
        nil
      ),
      KEYWORD_END("end")
    )

    assert_parses expected, "unless true then true elsif false then false elsif nil then nil else self end"
  end

  test "until" do
    assert_parses UntilNode(KEYWORD_UNTIL("until"), expression("true"), Statements([expression("1")])), "until true; 1; end"
  end

  test "until modifier" do
    assert_parses UntilNode(KEYWORD_UNTIL("until"), expression("true"), Statements([expression("1")])), "1 until true"
  end

  test "while" do
    assert_parses WhileNode(KEYWORD_WHILE("while"), expression("true"), Statements([expression("1")])), "while true; 1; end"
  end

  test "while modifier" do
    assert_parses WhileNode(KEYWORD_WHILE("while"), expression("true"), Statements([expression("1")])), "1 while true"
  end

  test "yield" do
    assert_parses YieldNode(KEYWORD_YIELD("yield"), nil, nil, nil), "yield"
  end

  test "yield()" do
    assert_parses YieldNode(KEYWORD_YIELD("yield"), PARENTHESIS_LEFT("("), nil, PARENTHESIS_RIGHT(")")), "yield()"
  end

  test "yield(1)" do
    assert_parses YieldNode(KEYWORD_YIELD("yield"), PARENTHESIS_LEFT("("), ArgumentsNode([expression("1")]), PARENTHESIS_RIGHT(")")), "yield(1)"
  end

  test "yield(1, 2, 3)" do
    assert_parses YieldNode(KEYWORD_YIELD("yield"), PARENTHESIS_LEFT("("), ArgumentsNode([expression("1"), expression("2"), expression("3")]), PARENTHESIS_RIGHT(")")), "yield(1, 2, 3)"
  end

  test "TERM < FACTOR" do
    expected = CallNode(
      expression("1"),
      nil,
      PLUS("+"),
      nil,
      ArgumentsNode([
        CallNode(expression("2"), nil, STAR("*"), nil, ArgumentsNode([expression("3")]), nil, "*")
      ]),
      nil,
      "+"
    )

    assert_parses expected, "1 + 2 * 3"
  end

  test "FACTOR < EXPONENT" do
    expected = CallNode(
      expression("1"),
      nil,
      STAR("*"),
      nil,
      ArgumentsNode([
        CallNode(expression("2"), nil, STAR_STAR("**"), nil, ArgumentsNode([expression("3")]), nil, "**")
      ]),
      nil,
      "*"
    )

    assert_parses expected, "1 * 2 ** 3"
  end

  test "FACTOR > TERM" do
    expected = CallNode(
      CallNode(expression("1"), nil, STAR("*"), nil, ArgumentsNode([expression("2")]), nil, "*"),
      nil,
      PLUS("+"),
      nil,
      ArgumentsNode([expression("3")]),
      nil,
      "+"
    )

    assert_parses expected, "1 * 2 + 3"
  end

  test "MODIFIER left associativity" do
    expected = IfNode(
      KEYWORD_IF("if"),
      expression("c"),
      Statements([IfNode(KEYWORD_IF("if"), expression("b"), Statements([expression("a")]), nil, nil)]),
      nil,
      nil
    )

    assert_parses expected, "a if b if c"
  end

  test "begin statements" do
    expected = BeginNode(
      KEYWORD_BEGIN("begin"),
      Statements([expression("a")]),
      KEYWORD_END("end"),
    )

    assert_parses expected, "begin\na\nend"
    assert_parses expected, "begin; a; end"
    assert_parses expected, "begin a\n end"
    assert_parses expected, "begin a; end"
  end

  test "endless method definition without arguments" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("foo"),
      nil,
      ParametersNode([], [], nil, [], nil, nil),
      nil,
      EQUAL("="),
      Statements([expression("123")]),
      nil,
      Scope([])
    )

    assert_parses expected, "def foo = 123"
  end

  test "endless method definition with arguments" do
    expected = DefNode(
      KEYWORD_DEF("def"),
      IDENTIFIER("foo"),
      PARENTHESIS_LEFT("("),
      ParametersNode([RequiredParameterNode(IDENTIFIER("bar"))], [], nil, [], nil, nil),
      PARENTHESIS_RIGHT(")"),
      EQUAL("="),
      Statements([expression("123")]),
      nil,
      Scope([IDENTIFIER("bar")])
    )

    assert_parses expected, "def foo(bar) = 123"
  end

  test "singleton class defintion" do
    expected = SClassNode(
      Scope([]),
      KEYWORD_CLASS("class"),
      LESS_LESS("<<"),
      expression("self"),
      Statements([]),
      KEYWORD_END("end"),
    )

    assert_parses expected, "class << self\nend"
    assert_parses expected, "class << self;end"
  end

  test "singleton class definition with method invocation for rhs" do
    expected = SClassNode(
      Scope([]),
      KEYWORD_CLASS("class"),
      LESS_LESS("<<"),
      CallNode(
        CallNode(nil, nil, IDENTIFIER("foo"), nil, nil, nil, "foo"),
        DOT("."),
        IDENTIFIER("bar"),
        nil,
        nil,
        nil,
        "bar"
      ),
      Statements([]),
      KEYWORD_END("end"),
    )

    assert_parses expected, "class << foo.bar\nend"
    assert_parses expected, "class << foo.bar;end"
  end

  test "singleton class defintion with statement" do
    expected = SClassNode(
      Scope([]),
      KEYWORD_CLASS("class"),
      LESS_LESS("<<"),
      expression("self"),
      Statements(
        [CallNode(
          IntegerLiteral(INTEGER("1")),
          nil,
          PLUS("+"),
          nil,
          ArgumentsNode([IntegerLiteral(INTEGER("2"))]),
          nil,
          "+"
        )]
      ),
      KEYWORD_END("end"),
    )

    assert_parses expected, "class << self\n1 + 2\nend"
    assert_parses expected, "class << self;1 + 2;end"
  end

  test "for loop" do
    expected = ForNode(
      KEYWORD_FOR("for"),
      expression("i"),
      KEYWORD_IN("in"),
      expression("1..10"),
      nil,
      Statements([expression("i")]),
      KEYWORD_END("end"),
    )

    assert_parses expected, "for i in 1..10\ni\nend"
  end

  test "for loop with do keyword" do
    expected = ForNode(
      KEYWORD_FOR("for"),
      expression("i"),
      KEYWORD_IN("in"),
      expression("1..10"),
      KEYWORD_DO("do"),
      Statements([expression("i")]),
      KEYWORD_END("end"),
    )

    assert_parses expected, "for i in 1..10 do\ni\nend"
  end

  test "for loop no newlines" do
    expected = ForNode(
      KEYWORD_FOR("for"),
      expression("i"),
      KEYWORD_IN("in"),
      expression("1..10"),
      nil,
      Statements([expression("i")]),
      KEYWORD_END("end"),
    )

    assert_parses expected, "for i in 1..10 i end"
  end

  test "for loop with semicolons" do
    expected = ForNode(
      KEYWORD_FOR("for"),
      expression("i"),
      KEYWORD_IN("in"),
      expression("1..10"),
      nil,
      Statements([expression("i")]),
      KEYWORD_END("end"),
    )

    assert_parses expected, "for i in 1..10; i; end"
  end

  test "for loop with 2 indexes" do
    expected = ForNode(
      KEYWORD_FOR("for"),
      MultiTargetNode([
        expression("i"),
        expression("j"),
      ]),
      KEYWORD_IN("in"),
      expression("1..10"),
      nil,
      Statements([expression("i")]),
      KEYWORD_END("end"),
    )

    assert_parses expected, "for i,j in 1..10\ni\nend"
  end

  test "for loop with 3 indexes" do
    expected = ForNode(
      KEYWORD_FOR("for"),
      MultiTargetNode([
        expression("i"),
        expression("j"),
        expression("k"),
      ]),
      KEYWORD_IN("in"),
      expression("1..10"),
      nil,
      Statements([expression("i")]),
      KEYWORD_END("end"),
    )

    assert_parses expected, "for i,j,k in 1..10\ni\nend"
  end

  private

  def assert_serializes(expected, source)
    YARP.load(source, YARP.dump(source)) => YARP::Program[statements: YARP::Statements[body: [*, node]]]
    assert_equal expected, node
  end

  def assert_parses(expected, source)
    assert_equal expected, expression(source)
    assert_serializes expected, source

    YARP.lex_ripper(source).zip(YARP.lex_compat(source)).each do |(ripper, yarp)|
      assert_equal ripper[0...-1], yarp[0...-1]
    end
  end

  def expression(source)
    YARP.parse(source) => YARP::ParseResult[
      node: YARP::Program[statements: YARP::Statements[body: [*, node]]],
      errors: []
    ]

    node
  end
end
