# Contribution Guidelines

"Just do it" - you (and even me!) will see what will happens. :)

OK, seriously, some key points though:

* Clearly, any help is welcome!
* Please open an issue ticket first
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

Identing sources are based on TAB and **not** spaces! One TAB means 8 spaces.
Some people consider that "too oldschool" but anyway, these are the rules,
period.

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
