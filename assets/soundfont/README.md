# assets/soundfont

The General MIDI SoundFont that plays the music (the songs are the MIDI files of the user's
GRP). The Makefile converts it to `soundfont.sf64` (`audioconv64`); `tools/pack_rom.py
--soundfont FILE.sf2` packs another one instead.

`SC-55-SoundFont-v1.2a1.sf2`: samples of the Roland SC-55 Sound Canvas, the General MIDI
module of the 1990s. What its INFO chunk says about it:

| Field | Value |
|---|---|
| Name | SC-55 SoundFont v1.2a.sf2 |
| Product | Roland SC-55 SoundFont |
| Copyright | Roland Corporation (C) 1991 |
| Comments | Fixed by Triaxis |
| Engineer | SynthFont Viena |
| Made with | Polyphone, 12/18/2020 |

It comes with no license.

The N64 synthesizer (libdragon's sf64) ignores the SoundFont's filters and modulation LFOs
(`audioconv64` lists them when converting): some instruments sound brighter or flatter than
on a PC.
