# Contribution Guidelines

## Checklist

* Make sure you contribute your **own** code, not someone's other code,
and not something's other code.
* This also means: **no AI generated code is allowed**. If I like AI, I
can prompt an AI myself as well to generate code, then your PR would be
meaningless. The point here is to have code written by _you_ as a human
effort, more than just giving a prompt, what literally everybody can do.
* This also means that code written by another person shouldn't be submitted
as a PR by you.
* Open an issue ticket **first** on github, and let's discuss everything.
I wouldn't like you to waste your time and energy to do a PR what I can't
accept. So let's discuss it first, before you do any significant work.
Only make significant effort, if we discussed everything, maybe also through
the development of your contribution. I don't want to waste your time. It
would be irritating that you spent hours of your life to make a great
contribution which I may find improper. So, in nutshell: again, let's discuss
first to avoid any of this in advance :)
* Never do PR against the `master` branch, it won't be ever accepted! When
you opened the required issue, discuss it with the project owner, which
branch is suitable for a given PR (it's usually - _but not always_ - `next` or `dev`).
* Refer the issue ticket number (where we had our discussion) in your PR too,
in the standard github format, like `#num` where the `num` is the issue number.
* Do not feel offended if your contribution is denied because it's
not suitable for the project because of some reason, including the fact,
that I don't need such a feature at all (but surely the reason is **never**
personal because of the contributor herself/himself).
* Do not feel offended if you are asked to modify things in your
contribution to be accepted, even if it's the 32932937297th time on
the very same contribution try of yours :)
* It's fine if you want to help on small things, like correcting a single
spelling mistake or such, but there are not as important as other tasks.
Maybe you want to be so kind to at least find multiple mistakes like this
to target more of these errors then. But these are usually not proper
PR material to correct a single word which has nothing to do with software's
features/bugs or anything. So these are classified as "cosmetic minor change".

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

### Key points about formatting

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
