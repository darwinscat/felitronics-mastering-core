### session — the max modes' names lose "Maximum"

- The display names of the four max modes (`[terms.loudnessMode]` of the text catalog) are one capitalised word, as
  `manual` «Вручную» "Manual" already was: `maxClean` «Чисто» "Clean", `maxDense` «Плотно» "Dense", `maxExtreme`
  «Экстрим» "Extreme", `maxNuke` «Нюк» "Nuke" (they were «Максимум · чисто» "Maximum · clean" and so on). The group a
  page lists them under is the page's own label; the core has no text for it.
- Every message that takes a mode opens with it and a colon, so a max verdict now reads "Clean: −13.2 LUFS." and the
  floor's log line "Clean: the first landing stopped at …" (facts 608–617); no message's own words change.
- Texts only: the target keys, `LoudnessMode`, the config, the project file, the ABI (17) and every master's sound are
  as they were. A shell that compares a rendered name against the old words must take the new ones.
