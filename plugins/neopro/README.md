# PS4 Pro Mode (NEOP00001)

Plugin etaHEN unique qui fusionne deux projets :

| Projet | Licence | Rôle dans le plugin |
|---|---|---|
| [illusionyy/ps-patch-system](https://github.com/illusionyy/ps-patch-system) | Apache-2.0 | Hook de `SceShellCore`, serveur de patchs, moteur de patchs XML (`third_party/pspatch`) |
| [gasb07/NeoEnabler](https://github.com/gasb07/NeoEnabler) | GPL-3.0 | Shim de lancement qui active le bit « PS4 Pro » + patchs XML par jeu (`third_party/neo`, `xml/`) |
| [zeux/pugixml](https://github.com/zeux/pugixml) | MIT | Lecture des fichiers XML (`third_party/pugixml`) |

Le plugin complet est distribué sous **GPL-3.0** (voir `LICENSE`).

## Fonctionnement

1. Au démarrage, les 51 fichiers XML de NeoEnabler (embarqués dans l'ELF) sont fusionnés dans `/data/user/patches/xml_orbis/`.
2. Le moteur ps-patch-system installe son hook dans `SceShellCore` et lance son serveur (port 23916) dans le processus du plugin.
3. Le shim NeoEnabler est chaîné sur ce hook : au lancement d'un jeu PS4 (`CUSA…`), il met le bit PS4 Pro dans l'attribut du jeu.
4. Deux modes (page de réglages du plugin) :
   * **Jeux avec patch activé** (défaut) : seuls les jeux dont un patch « PS4 Pro » est activé démarrent en mode Pro (interrupteur par jeu).
   * **Tous les jeux PS4** : le bit Pro est forcé pour tous les titres. Beaucoup de jeux ont besoin de leur patch XML pour démarrer correctement en Pro.

Si `SceShellCore` redémarre, le hook est réinstallé automatiquement.

## Limites

* Firmwares : 4.00 à 13.60 (le hook utilise les motifs de ps-patch-system / NeoEnabler, testé par NeoEnabler surtout en 13.20).
* Le mode mémoire 8 Go de NeoEnabler et le réglage des fréquences GPU/CPU ne sont **pas** repris ici.
* Journal : `/data/etaHEN/NEOP00001.log`.

## Compilation

Compilé par `scripts/build.sh` (phase 2h). Les en-têtes de shellcode de ps-patch-system
(`third_party/pspatch/patch/shellcore_onNewProcess.h`, `patch-server/usermode_prx_load.h`,
`patch-server/videoout_120hz.h`) sont générés par `host/shellcode_gen.cpp` (keystone + zydis) et commités.
