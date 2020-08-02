# Source level documentation

This is a source code documentation of the [Xemu project](https://github.com/lgblgblgb/xemu/), or at least its
initial try :) It's generated with Doxygen. Please see the end of this
page to learn how to generate that for yourself.

# Contents

This section only makes sense if you read this as the part of the
generated Doxygen documentation.

* [Data structures](annotated.html)
* [Data structures index](classes.html)
* [Source files list](files.html)

# TODO

Now let's decorate the source with comments what Doxygen _can_ use to get fancier doc ... :)

# Generating the documentation

You can create/update it from UNIX using the source tree of Xemu:

    make doxygen

The result will be available in directory `buld/doc/doxygen`. Currently
only `html` output is generated. You can modify settings used by Doxygen
via file `build/Doxyfile`.
