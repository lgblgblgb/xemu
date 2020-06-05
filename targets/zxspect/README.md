# ZX Spectrum emulator try ...

Since I've never had a ZX Spectrum (still, I always wanted to own that, but it
never happened unfortunately), I've decided to try to learn something about it.
I guess, the best way is trying to emulate it, as always, in my case :-)

Currently, only 48K spectrum is planned to be emulated, with the absolutely
minimum features.

*WARNING*: currently this emulator is simply unusable, even the display is
totally wrong, not to mention the timing, and the ability to load something
at least, also the lack of sound, and so on ...

Please note, that I almost know nothing about this machine, and I try to do
write an emulator by reading specifications and descriptions on this machine.
For at least a small amount accuracy, I'm trying to emulate contended memory
in a sane way at least, though available documents on the ULA are really
confusing.

For example, documents state that 64 lines (224 T-states for each) passes
till the first pixel is displayed. But as far as I see it cannot be, as
the left border is still there, so 64 lines + T-states for 48 pixels
(24 T-states) it should be ... Also I have very similar problems trying to
implement the floating bus by the correct time ULA reading screen/attribute
memory. Again documents cannot be interpreted by me in the sane way, as it
seems it's an impossible situation: ULA reads memory _after_ it already
displayed pixel from that info? WTF??

I'm trying to use these resources:

* http://www.worldofspectrum.org/faq/reference/48kreference.htm
* http://faqwiki.zxnet.co.uk/wiki/Contended_memory
* http://faqwiki.zxnet.co.uk/wiki/Contended_I/O
* http://faqwiki.zxnet.co.uk/wiki/Floating_bus
* http://faqwiki.zxnet.co.uk/wiki/Spectrum_Video_Modes
* http://faqwiki.zxnet.co.uk/wiki/ULAplus

Please help me, with better - error free - resources, since these seem
to describe impossible situations. For example:

http://faqwiki.zxnet.co.uk/wiki/Floating_bus mentions that ULA reads
screen memory at 0x4000 at T-state (after interrupt) 14338. However this
http://www.worldofspectrum.org/faq/reference/48kreference.htm states
that the first pixel is displayed at T-state 14336. You see the problem,
it's impossible to display it, when it's two T-states in the future to
even read that information ...

Support for ULAplus is planned, though I should learn first, how the
original ULA works exactly at T-state precision. :-)

One of the reason of this project, that I have the crazy goal to try
to write a ZX Spectrum emulator for MEGA65 (!!) even if it cannot be
cycle exact, well, far from it.
