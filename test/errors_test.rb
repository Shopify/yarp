# frozen_string_literal: true

require "test_helper"

class ErrorsTest < Test::Unit::TestCase
  include YARP::DSL

  test "constant path with invalid token after" do
    expected = ConstantPathNode(
      ConstantRead(CONSTANT("A")),
      COLON_COLON("::"),
      MissingNode()
    )

    assert_errors expected, "A::$b", ["Expected identifier or constant after '::'"]
  end

  test "module name recoverable" do
    expected = ModuleNode(
      Scope([]),
      KEYWORD_MODULE("module"),
      ConstantRead(CONSTANT("Parent")),
      Statements([
        ModuleNode(
          Scope([]),
          KEYWORD_MODULE("module"),
          MissingNode(),
          Statements([]),
          MISSING("")
        )
      ]),
      KEYWORD_END("end")
    )

    assert_errors expected, "module Parent module end", ["Expected to find a module name after `module`."]
  end

  test "for loops index missing" do
    expected = ForNode(
      KEYWORD_FOR("for"),
      MissingNode(),
      KEYWORD_IN("in"),
      expression("1..10"),
      nil,
      Statements([expression("i")]),
      KEYWORD_END("end"),
    )

    assert_errors expected, "for in 1..10\ni\nend", ["Expected index after for."]
  end

  test "for loops only end" do
    expected = ForNode(
      KEYWORD_FOR("for"),
      MissingNode(),
      MISSING(""),
      MissingNode(),
      nil,
      Statements([]),
      KEYWORD_END("end"),
    )

    assert_errors expected, "for end", ["Expected index after for.", "Expected keyword in.", "Expected collection."]
  end

  test "pre execution missing {" do
    expected = PreExecutionNode(
      KEYWORD_BEGIN_UPCASE("BEGIN"),
      MISSING(""),
      Statements([expression("1")]),
      BRACE_RIGHT("}")
    )

    assert_errors expected, "BEGIN 1 }", ["Expected '{' after 'BEGIN'."]
  end

  test "pre execution context" do
    expected = PreExecutionNode(
      KEYWORD_BEGIN_UPCASE("BEGIN"),
      BRACE_LEFT("{"),
      Statements([
        CallNode(
          expression("1"),
          nil,
          PLUS("+"),
          nil,
          ArgumentsNode([MissingNode()]),
          nil,
          "+"
        )
      ]),
      BRACE_RIGHT("}")
    )

    assert_errors expected, "BEGIN { 1 + }", ["Expected a value after the operator."]
  end

  test "unterminated embdoc" do
    assert_errors expression("1"), "1\n=begin\n", ["Unterminated embdoc"]
  end

  test "unterminated %i list" do
    assert_errors expression("%i["), "%i[", ["Expected a closing delimiter for a `%i` list."]
  end

  test "unterminated %w list" do
    assert_errors expression("%w["), "%w[", ["Expected a closing delimiter for a `%w` list."]
  end

  test "unterminated %W list" do
    assert_errors expression("%W["), "%W[", ["Expected a closing delimiter for a `%W` list."]
  end

  test "unterminated regular expression" do
    assert_errors expression("/hello"), "/hello", ["Expected a closing delimiter for a regular expression."]
  end

  test "unterminated string" do
    assert_errors expression('"hello'), '"hello', ["Expected a closing delimiter for an interpolated string."]
  end

  test "unterminated %s symbol" do
    assert_errors expression("%s[abc"), "%s[abc", ["Expected a closing delimiter for a dynamic symbol."]
  end

  test "unterminated parenthesized expression" do
    assert_errors expression('(1 + 2'), '(1 + 2', ["Expected a closing parenthesis."]
  end

  test "argument forwarding when parent is not forwarding" do
    assert_errors expression('def a(x, y, z); b(...); end'), 'def a(x, y, z); b(...); end', ["unexpected ... when parent method is not forwarding."]
  end

  test "argument forwarding only effects its own internals" do
    assert_errors expression('def a(...); b(...); end; def c(x, y, z); b(...); end'), 'def a(...); b(...); end; def c(x, y, z); b(...); end', ["unexpected ... when parent method is not forwarding."]
  end

  private

  def assert_errors(expected, source, errors)
    result = YARP.parse(source)
    result => YARP::ParseResult[node: YARP::Program[statements: YARP::Statements[body: [*, node]]]]

    assert_equal expected, node
    assert_equal errors, result.errors.map(&:message)
  end

  def expression(source)
    YARP.parse(source) => YARP::ParseResult[node: YARP::Program[statements: YARP::Statements[body: [*, node]]]]
    node
  end
end
