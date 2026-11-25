English | [日本語](bugfixes-ja.md)

# Bug fixes

**F1 → Gameplay → Original-game bug fixes** corrects bugs of the original
game, on the US and Japanese discs. It is off by default: off, every one of
them behaves as on the PlayStation. The **Boss camera fix** is a separate
switch, on by default.

Each bug was reproduced in the original game first, and each fix is written
for the port; no patch from another project is applied. A fix marked US is a
bug of the US release only.

## Battles

- **Def UP works.** It sets a status that the original damage formula never
  checks, so it did nothing. It now raises physical defence by half, as Eth
  Def UP does for ether defence.
- **Billy's Ether ammunition.** His two Square attacks use the elements of the
  Ether ammunition he has equipped. The original kept older elements (usually
  wind and fire) whatever was equipped. For a save made before, open Billy's
  equipment or status menu once.
- **Gear accessories in a Gear shop.** Opening the shop's Parts menu no longer
  drops the Ether amplification a Gear gets from its accessories: the shop's
  total shows it and battles use it.
- **The first Deus attack** (**Boss camera fix**). The battle could stall
  during one attack, its camera waiting for a move that never came. The
  pending move now finishes first. Only that encounter's camera changes.

## Field

- **A jump pressed as a random battle comes.** For a moment before a random
  battle the original ignores the jump button, so a jump pressed then was
  lost. Now it happens and the battle starts when you land. One jump per
  battle: jumping can't put a battle off.
- **Looping sounds after the menu.** A field's looping sound, such as the crowd
  in South Bledavik's market, starts again after the menu; the original left
  it silent.
- **Nisan inn, late in the story.** The town's music returns after the rest,
  as in every other inn; the original stayed silent.
- **Kislev, the battlers' quarters.** After the Baptism, Fei's rest lets the
  long sleep music end before the quarters' music starts, as his rest in the
  quarters' bed does.
- **Yggdrasil, Emeralda's first scene.** Her theme plays at its cue. The scene
  before had faded the same theme out, and the original took the silent theme
  for the one playing.
- **Nisan, late in the story.** Loading a save in one of its rooms plays Elly's
  theme at the town's faster speed at once.
- **Aveh desert.** The soldier whose motorcycle Fei takes stays until the
  screen is black; the original removed him while it was still light.
- **The orphanage's garden.** Coming from the world map, the party faces the
  garden, not the gate.

## Menus

- **Names ending in "s"** (US). A character's or Gear's name ending in "s"
  keeps it; the original dropped it ("Feis" became "Fei").
- **The item shop's arrow** (US). It no longer covers Buy, Sell and Exit.
- **EP in the party member menu.** It says EP, as every other menu does,
  not MP.

## Dialogue portraits

- **Holes in ten faces.** 24 see-through pixels in the portraits of the adult
  Emeralda, Id, Vanderkaum, Shakhan, Franz, Yui, Midori, Chief Lee, Alice and
  Timothy are filled black.
- **Miang.** A loose four-pixel strip beside her face is removed.

Only the original pictures are changed: a portrait from a replacement pack or
a modified disc is left as it is, and the extras' Gallery shows the disc's
pictures.

## Looked at, not changed

- **The Nisan shop's early stock** stays as it is: changing it would change
  what can be bought.
- Some reported scene and dialogue glitches need a save at that point of the
  story to check. They stay as they are until they can be reproduced.
