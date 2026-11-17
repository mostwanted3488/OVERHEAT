# OVERHEAT

Saturation « un knob » au DSR-50 — par **@BUYORBEALOSER**.
Plus tu montes la chauffe, plus le canon rougit ; à 100 % l'arme surchauffe et tire.

- 5 grains : **THERMAL**, **TUBE**, **TAPE**, **FUZZ**, **GRIT** (shaper suréchantillonné 4×)
- **AUTO GAIN** pour n'entendre que la couleur, pas la différence de volume
- **MIX** et **BYPASS**, tout est automatisable

## Télécharger
Onglet **Releases → latest** : `OVERHEAT-windows.zip` (VST3 64 bits + autonome) et
`OVERHEAT-mac.zip` (VST3 + AU + autonome, universel Apple Silicon / Intel).
Les étapes d'installation sont dans chaque zip.

## Développement
- `Source/OverheatDsp.h` : le moteur (mêmes maths que la preview navigateur)
- `ui/src/template.html` : l'interface ; `python3 tools/build_ui.py` génère `ui/index.html` (embarqué dans le plugin)
- Build : `cmake -B build && cmake --build build --config Release` (JUCE 8 récupéré automatiquement)
- Chaque push sur `main` compile Mac + Windows via GitHub Actions et met à jour la release `latest`.
