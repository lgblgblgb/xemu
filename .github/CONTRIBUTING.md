# Contribution Guidelines

"Just do it" - you (and even me!) will see what will happens. :)

OK, seriously, some key points though:

* Clearly, any help is welcome!
* Please read this file, and **DELETE** it from the actual PR is github
offers when doing the PR itself at the _end_, and before that, read
further here ...
* Please open an issue ticket first
* Let's discuss things first! Nothing is more irritating than spending
long hours of your time for doing something not even needed, or it's
needed a totally different way. That's waste of your and my time as well.
Let's talk about first! Also about the implementation details, questions,
etc sorted out in advance, as much as possible!
* It's fine if you want to help on small things, like correcting a single
spelling mistake or such, but not as helpful as other tasks. Maybe you
want to be so kind to at least find multiple mistakes like this to target
more of these errors then.
* Refer to the ticket by #NUM in your contribution
* Do not feel offended if your contribution is denied because it's
not suitable for the project because of some LOGICAL reason, including
the fact, that I don't need such a feature at all (but surely the
reason is **never** personal because of the contributor herself/himself).
* Do not feel offended if you are asked to modify things your
contribution to be accepted, even if it's the 32932937297th time on
the very same contribution try of yours :)

## Source code formatting

Sources must have UNIX line endings, not DOS (Windows) or any other.

Identing sources are based on TAB and **not** spaces! One TAB visually means
8 spaces, but should never be inserted as real spaces. Some people consider
all of these "too oldschool" but anyway, these are the rules, period.

I'm personally not picky about the length of a line, having visually 8 spaces
for identation would make very hard to keep 80 character long lines, and
modern UIs + monitors, really, nobody cares. At least not me. I even often go
as far as 200 character long lines or so. However this is not a force at all
to exploit this, if possible, should be more like an exception rather than
rule :)

Source code sample showing other key points as well, without further blah-blah:


    int something ( int a, int b, inc c, int *d )
    {
            if (condition) {
                    func1();
                    func2(a, b, c + 3 * (*d << 2));
            } else {
                    func3();
                    a += 2;
                    b = b + c * 2;
                    c++;
            }
            return a;
    }

### Key points:

Still, the blah-blah:

* block starting "{" always goes to the end of the line it belongs to, expect
  for the situation of start of a function, though in case of very small
  (probably inline) functions, it's possible not to use this exception,
  especially if there are many of these small functions in a row
* space after "," (but not before)
* space before and after "+", "=", "+=", "<<" and so on
* when defining functions, argument list includes spaces around "(" and ")",
  but not when calling them
* otherwise, no spaces before or after "(" and ")"
* `int *d` is the used form, not `int* d` (because the second form is confusing,
  `int* d, e` can seem to have two pointers, while `int *d, e` more clearly
  shows that only `d` is the pointer)
* as usual, exceptions to the rules exist, like: special kind or indenting when
  complex expressions are repeated in more lines, or having a really big
  switch/case block dominating the whole source (like CPU emulation having
  256 case entires ...)
