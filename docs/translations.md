# Playing with translated text

The US download plays in English. Start the game from your original US
discs and select a translation as its text source. Italian and Spanish can
use patched images directly. French setup depends on your build.

## Set up a translation in v2026.11.17

For a translation that works directly from patched images, such as Italian or Spanish, follow these steps. French uses the pack setup below.

1. Apply the translation team's patch to **copies** of both US disc images.
   Keep your original pair selected in disc setup.
2. Open **F1 → System → Language and text → Text images**. Enter the patched
   Disc 1 and Disc 2 image paths and select **Use** for each.
3. Choose **Text image** for **Dialogue**, **Battle text**, **Game menus**
   and **Names, items**. For menu and battle labels drawn as pictures,
   enable **Pictures with text too**. Reopen the game menu or enter a new
   battle to see those labels.

The **Language** row in the same section switches F1 and Extras between
English and Japanese in the stock download. To read
Japanese game text in the US build, supply your Japanese discs and choose
**Japanese** in the game text rows. You can also choose an installed text
pack by name. **Disc** restores the running game's own text for that row.
Japanese battle voices and movie audio have separate settings under
**F1 → Audio**. [Audio settings](gameplay.md#controls-and-audio)

### French with Atelier Traduction 1.1

1. Apply Atelier’s 1.1 patch to copies of both US discs. Keep the
   `Patched` folder that the patcher produces. Its `Undub_patched`
   folder also works for text. [Atelier’s patch and instructions](https://ateliertraduction.forumgaming.fr/t51-mise-a-jour-de-xenogears-en-v1-1)
2. Save both Python files in the same folder. Run the helper with the
   Original US game executable, your original US Disc 1 and the patcher’s output folder.
   Replace the example paths with yours. Allow about 1.5 GB of temporary
   space. The helper checks both patched images and uses the game to
   export a personal pack with the French text, font and battle labels.
   [French pack helper](https://raw.githubusercontent.com/EtherAura/xenogears-pc/main/tools/atelier_text_pack.py) · [Disc reader (xenofs.py)](https://raw.githubusercontent.com/EtherAura/xenogears-pc/main/tools/xenofs.py)

   ```sh
   python3 atelier_text_pack.py --game "/path/to/bin/xenogears" --retail "/path/to/original Disc 1.cue" --french "/path/to/Patched"
   ```

3. Launch the game from your original US discs. In **F1 → System →
   Language and text**, choose **Atelier Français 1.1** for Dialogue,
   Battle text, Game menus and Names/items. Enable **Pictures with text
   too** for menus and battles. On builds with the single game-language
   selector, choose **Atelier Français 1.1** once.

If your French wheel still says **Attack**, move the old `atelier-fr` folder
outside your packs directory, run the updated helper and start a new battle.

Selecting the French BINs directly in v2026.11.17 leaves Fei speaking
English. The patch stores extra data in unused file-table slots, which
the release reads as invalid file entries. The helper accounts for those
slots in temporary copies. [Direct-image result](img/translations/field14-fr.png)

## Which translations work?

These results are from the **Original engine in the Linux x86-64 download
of v2026.11.17**. Testing covered dialogue and a status screen on each disc,
plus a Disc 1 battle. Windows, Enhanced, movie subtitles and a complete
playthrough were outside this test. [Tested release](https://github.com/EtherAura/xenogears-pc/releases/tag/v2026.11.17)

| Translation | Result on this build |
|---|---|
| Italian · SadNES cITy 1.2.1 | Both discs showed Italian dialogue and equipment text. The battle showed **Attacco** and **Tempo**. Some status labels stayed in English. [Patch and instructions](https://www.sadnescity.it/traduzioni/xeno/xeno.php) |
| Spanish · Romhack Hispano v1 | The text-and-graphics edition showed Spanish dialogue, menu labels and equipment names on both discs. The battle command read **Atacar**. Some status labels stayed in English. [Patch and instructions](https://xenogears.romhackhispano.org/traduccion.html) |
| English · Perfect Works Build 0.11.4 | Use your own images patched with the **`story_mode_script`** preset for the tested text option. Revised English narration appeared on Disc 2. All six `all_*` presets failed verification on both discs. The `bug_fixes` and `story_mode` images passed verification, but their dialogue was untested. Selecting a text image leaves gameplay rules under the control of the PC engine. [Perfect Works release](https://github.com/PWBuild-Team/Perfect_Works_Build/releases/tag/0.11.4) |
| French · Atelier Traduction 1.1 | The personal pack showed French dialogue and equipment text on both discs. With the updated helper, the Disc 1 battle wheel reads **Attaque** and the gauge reads **Temps**. [French setup](#french-with-atelier-traduction-11) |
| Brazilian Portuguese · TransXeno | Compatibility with the team's translation is **unverified**. In a separate two-line Portuguese test pack, accented dialogue appeared when the pack supplied the missing glyphs. With the stock US font, both lines fell back to English. [TransXeno's translation screenshots](https://transxeno.blogspot.com/2010/06/novas-imagens-da-traducao.html) |

## Screenshot comparisons

The PC screenshots use the published Linux Original build, with each scene captured at the same frame across languages. The PS1 battle screenshot shows the same command wheel from Atelier’s patched disc. The Disc 2 narration contains a story spoiler.

### Disc 1 dialogue

Fei says “Whew! That about does it...” in US retail and “Fiou ! Ça devrait suffire...” with the Atelier pack.

| US retail | Atelier Français 1.1 pack |
|---|---|
| ![US retail, Fei’s dialogue](img/translations/field14-us.png) | ![Fei says “Fiou ! Ça devrait suffire...” in French](img/translations/field14-fr-pack.png) |

| SadNES cITy Italian | Romhack Hispano Spanish |
|---|---|
| ![SadNES cITy Italian, Fei’s dialogue](img/translations/field14-it.png) | ![Romhack Hispano Spanish, Fei’s dialogue](img/translations/field14-es.png) |

### Battle and status

On the Disc 1 battle wheel, the French command **Attaque** and the **Temps** gauge match Atelier’s patched Disc 1 running in Beetle PSX. The Italian command reads **Attacco** and the Spanish command reads **Atacar**.

| Atelier pack on PC v2026.11.17 | Atelier patched Disc 1 on PS1 |
|---|---|
| ![PC battle wheel showing Attaque with Temps and PA above it](img/translations/battle-fr-wheel.png) | ![Atelier’s PS1 battle wheel showing Attaque, Temps and PA](img/translations/battle-fr-ps1.png) |

| Italian battle commands | Spanish battle commands |
|---|---|
| ![Italian battle commands, battle](img/translations/battle-it.png) | ![Spanish battle commands, battle](img/translations/battle-es.png) |

| Italian status screen | Spanish status screen | French status screen |
|---|---|---|
| ![Italian status screen](img/translations/status-it.png) | ![Spanish status screen](img/translations/status-es.png) | ![French status screen with Force, Défense and Tenue de Kung Fu](img/translations/status-fr-pack.png) |

### Disc 2 narration

The same moment in the narration shows a wording change with the Perfect Works `story_mode_script` preset and translated lines in French, Italian and Spanish.

| US retail | Atelier Français 1.1 pack | Perfect Works English |
|---|---|---|
| ![US retail, Disc 2 narration](img/translations/field618-us.png) | ![French Disc 2 narration beginning “J’étais impuissant...”](img/translations/field618-fr-pack.png) | ![Perfect Works English, Disc 2 narration](img/translations/field618-pw-story.png) |

| SadNES cITy Italian | Romhack Hispano Spanish |
|---|---|
| ![SadNES cITy Italian, Disc 2 narration](img/translations/field618-it.png) | ![Romhack Hispano Spanish, Disc 2 narration](img/translations/field618-es.png) |

### Brazilian Portuguese font check

Both runs use the same two-line diagnostic pack. The stock US font falls back to English. Supplying the accented glyphs makes the Portuguese line visible. TransXeno itself was untested.

| Stock US font | Pack with accented glyphs |
|---|---|
| ![Stock US font, Fei’s dialogue](img/translations/field14-pt-demo.png) | ![Pack with accented glyphs, Fei’s dialogue](img/translations/field14-pt-font.png) |

## Troubleshooting

If a line stays in English, check the status under its game text row.
The port keeps the running disc's text when it cannot read the source or
when the replacement does not fit the scene's script. On Linux, you can
check both patched images with this command.

```sh
xenogears --verify-discs "/path/to/patched-disc-1.cue" "/path/to/patched-disc-2.cue"
```

A successful check confirms the images' identities and file tables. To
confirm the translation, open a scene with a known wording change.
