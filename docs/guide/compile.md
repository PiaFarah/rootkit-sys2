# Compiler le module kernel

La compilation doit se faire **sur la VM Victime**, pas sur la machine hôte ni sur la VM Attaquante. C'est une contrainte fondamentale des modules kernel Linux.

---

## Pourquoi compiler sur la VM Victime ?

Le Makefile utilise `/lib/modules/$(uname -r)/build` comme répertoire de build. Ce chemin contient les headers du kernel **actuellement en cours d'exécution** sur la machine. Si vous compilez sur la machine hôte (Arch, kernel 6.x rolling), vous obtenez un `.ko` pour le kernel de l'hôte — incompatible avec le kernel 6.1 LTS de la victime. La compilation échouerait ou produirait un module qui refuse de se charger.

En compilant depuis la VM Victime, les headers correspondent exactement au kernel qui va charger le module.

---

## Étapes

### 1. Copier les sources dans vmshare (depuis l'hôte)

```bash
# Sur la machine hôte
cp -r rootkit/ vmshare/
```

### 2. Se connecter à la VM Victime

```bash
ssh -p 10022 epita@localhost
```

### 3. Compiler

```bash
cd /mnt/vmshare/rootkit
make
```

Sortie attendue :

```
make -C /lib/modules/6.1.0-xx-amd64/build M=/mnt/vmshare/rootkit modules
make[1]: Entering directory '/usr/src/linux-headers-6.1.0-xx-amd64'
  CC [M]  /mnt/vmshare/rootkit/wlkom.o
  MODPOST /mnt/vmshare/rootkit/Module.symvers
  CC [M]  /mnt/vmshare/rootkit/wlkom.mod.o
  LD [M]  /mnt/vmshare/rootkit/wlkom.ko
make[1]: Leaving directory '/usr/src/linux-headers-6.1.0-xx-amd64'
```

Le fichier `wlkom.ko` est créé dans `/mnt/vmshare/rootkit/`.

### 4. Vérifier le module

```bash
modinfo wlkom.ko
```

Sortie attendue :

```
filename:       /mnt/vmshare/rootkit/wlkom.ko
license:        GPL
depends:
retpoline:      Y
name:           wlkom
vermagic:       6.1.0-xx-amd64 SMP preempt mod_unload modversions
parm:           password_hash:FNV-1a 32-bit hash of the C2 password (charp)
parm:           c2_ip:C2 server IP address (default: 192.168.100.10) (charp)
parm:           c2_port:C2 server TCP port (default: 4444) (int)
```

La ligne `vermagic` doit correspondre à `uname -r` sur la victime. C'est ce qui garantit la compatibilité.

---

## Nettoyer les artefacts

```bash
make clean
```

Supprime les fichiers `.o`, `.ko`, `.mod`, etc.

---

## Vérification

- [x] `wlkom.ko` est présent dans `vmshare/rootkit/`
- [x] `modinfo wlkom.ko` affiche les trois paramètres (`password_hash`, `c2_ip`, `c2_port`)
- [x] Le `vermagic` correspond à `uname -r` sur la victime
