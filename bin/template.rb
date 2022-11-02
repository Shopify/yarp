#!/usr/bin/env ruby

require "erb"
require "fileutils"
require "yaml"

# This represents a parameter to a node that is itself a node. We pass them as
# references and store them as references.
class NodeParam < Struct.new(:name)
  def param = "yp_node_t *#{name}"
  def rbs_class = "Node"
  def java_type = "Node"
end

# This represents a parameter to a node that is itself a node and can be
# optionally null. We pass them as references and store them as references.
class OptionalNodeParam < Struct.new(:name, :fallback)
  def param = "yp_node_t *#{name}"
  def rbs_class = "Node?"
  def java_type = "Node"
end

# This represents a parameter to a node that is a list of nodes. We pass them as
# references and store them as references.
class NodeListParam < Struct.new(:name)
  def param = nil
  def rbs_class = "Array[Node]"
  def java_type = "Node[]"
end

# This represents a parameter to a node that is a token. We pass them as
# references and store them by copying.
class TokenParam < Struct.new(:name)
  def param = "const yp_token_t *#{name}"
  def rbs_class = "Token"
  def java_type = "Token"
end

# This represents a parameter to a node that is a token that is optional.
class OptionalTokenParam < Struct.new(:name)
  def param = "const yp_token_t *#{name}"
  def rbs_class = "Token?"
  def java_type = "Token"
end

# This represents a parameter to a node that is a list of tokens.
class TokenListParam < Struct.new(:name)
  def param = nil
  def rbs_class = "Array[Token]"
  def java_type = "Token[]"
end

# This represents a parameter to a node that is a string.
class StringParam < Struct.new(:name)
  def param = "yp_string_t *#{name}"
  def rbs_class = "String"
  def java_type = "byte[]"
end

# This class represents a node in the tree, configured by the config.yml file in
# YAML format. It contains information about the name of the node, the various
# child nodes it contains, and how to obtain the location of the node in the
# source.
class NodeType
  attr_reader :name, :type, :human, :params, :location, :location_provided, :comment

  def initialize(config)
    @name = config.fetch("name")

    type = @name.gsub(/(.)([A-Z])/, "\\1_\\2")
    @type = "YP_NODE_#{type.upcase}"
    @human = type.downcase

    @params =
      config.fetch("child_nodes").map do |param|
        name = param.fetch("name")

        case param.fetch("type")
        when "node"
          NodeParam.new(name)
        when "node?"
          OptionalNodeParam.new(name)
        when "node[]"
          NodeListParam.new(name)
        when "string"
          StringParam.new(name)
        when "token"
          TokenParam.new(name)
        when "token?"
          OptionalTokenParam.new(name)
        when "token[]"
          TokenListParam.new(name)
        else
          raise "Unknown param type: #{param["type"].inspect}"
        end
      end

    @location =
      config.fetch("location").then do |location|
        if location == "provided"
          @location_provided = true
          "{ .start = location, .end = location }"
        else
          bounds = location.include?("->") ? location.split("->") : [location, location]
          from, to = bounds.map { |names| names.split("|").map { |name| params.find { |param| param.name == name } } }
          "{ .start = #{start_location_for(from)}, .end = #{end_location_for(to)} }"
        end
      end

    @comment = config.fetch("comment")
  end

  def location_provided?
    @location_provided
  end

  private

  def start_location_for(params)
    case param = params.first
    in NodeParam then "#{param.name}->location.start"
    in OptionalNodeParam then "(#{param.name} == NULL ? #{start_location_for(params.drop(1))} : #{param.name}->location.start)"
    in NodeListParam | TokenListParam then "0"
    in TokenParam then "#{param.name}->start - parser->start"
    in OptionalTokenParam then "(#{param.name} == NULL ? #{start_location_for(params.drop(1))} : #{param.name}->start - parser->start)"
    else
      raise "Unknown param type: #{param.inspect}"
    end
  end

  def end_location_for(params)
    case param = params.first
    in NodeParam then "#{param.name}->location.end"
    in OptionalNodeParam then "(#{param.name} == NULL ? #{end_location_for(params.drop(1))} : #{param.name}->location.end)"
    in NodeListParam | TokenListParam then "0"
    in TokenParam then "#{param.name}->end - parser->start"
    in OptionalTokenParam then "(#{param.name} == NULL ? #{end_location_for(params.drop(1))} : #{param.name}->end - parser->start)"
    else
      raise "Unknown param type: #{param.inspect}"
    end
  end
end

# This represents a token in the lexer. They are configured through the
# config.yml file for now, but this will probably change as we transition to
# storing semantic strings instead of the lexer tokens.
class Token
  attr_reader :name, :value, :comment

  def initialize(config)
    @name = config.fetch("name")
    @value = config["value"]
    @comment = config.fetch("comment")
  end

  def declaration
    output = []
    output << "YP_TOKEN_#{name}"
    output << " = #{value}" if value
    output << ", // #{comment}"
    output.join
  end
end

# This templates out a file using ERB with the given locals. The locals are
# derived from the config.yml file.
def template(name, locals)
  template = File.expand_path("../bin/templates/#{name}.erb", __dir__)
  write_to = File.expand_path("../#{name}", __dir__)

  erb = ERB.new(File.read(template), trim_mode: "-")
  erb.filename = template

  contents = erb.result_with_hash(locals)
  FileUtils.mkdir_p(File.dirname(write_to))
  File.write(write_to, contents)
end

def locals
  config = YAML.load_file(File.expand_path("../config.yml", __dir__))
  {
    nodes: config.fetch("nodes").map { |node| NodeType.new(node) }.sort_by(&:name),
    tokens: config.fetch("tokens").map { |token| Token.new(token) }
  }
end
