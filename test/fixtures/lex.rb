# This file isn't actually valid Ruby. It's used to exercise the lexer.

&
&&
&&=
&=
!

# If the lexer is in a state where it can accept a method name (either by
# defining a method or by calling a method), it will accept a !@.
def !@() end
foo.!@

!=
!~
{}
[]
^
^=
?a
@@abc
,
..
...

=begin
embdoc
content
=end

"#{abc}"

=
==
===
100 => 100
=~
>
>=
>>
>>=
$~
$*
$$
$?
$!
$@
$/
$\
$;
$,
$.
$=
$:
$<
$>
$"
abc
1i
1ri
0
42.56
0d100
0d100_100
0D100
0D100_100
0b100
0b100_100
0B100
0B100_100
0o100
0o100_100
0O100
0O100_100
0100
0100_100
0x100
0x100_100
0X100
0X100_100
@abc
__ENCODING__
__LINE__
__FILE__
alias
and
begin
BEGIN
break
case
class
def
defined?
do
else
elsif
end
END
ensure
false
for
if
in
module
next
nil
not
or
redo
rescue
retry
return
self
super
then
true
undef
unless
until
when
while
yield
{ label: abc }
<
<=
<=>
<<
<<=
-

# If the lexer is in a state where it can accept a method name (either by
# defining a method or by calling a method), it will accept a -@.
def -@() end
abc.-@

-=
()
100 % 100
100 %= 100
%i[abc def   ghi]
%w[abc def   ghi]
%I[abc def   ghi]
%W[abc def   ghi]
|
|=
100 || 100
||=
+
+=

# If the lexer is in a state where it can accept a method name (either by
# defining a method or by calling a method), it will accept a +@.
def +@() end
abc.+@

?
1r
%r{abc}
;
100 / 100
100 /= 100
*
*=
**
**=

# Lexing strings involves a whole state change. It ends up being at minimum
# three tokens: the beginning, the content, and then end. It gets more
# complicated if you have interpolation.
"abc"
%q[abc]
%Q[abc]

:abc
:ABC
:[]
:self
:@abc
:@@abc
:$abc

~

# If the lexer is in a state where it can accept a method name (either by
# defining a method or by calling a method), it will accept a ~@.
def ~@() end
abc.~@

`abc`

abc&.xyz
