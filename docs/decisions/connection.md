# Connexion

Reprise du TD `module/network`, adapté au rootkit.

## Pourquoi un kthread dédié et pas un workqueue ?

Un workqueue partage un pool de workers système. `kernel_recvmsg` est bloquant par conception : quand le rootkit attend une commande du C2, il reste bloqué indéfiniment sur la lecture. Bloquer un worker du pool impacterait tout le reste du kernel qui l'utilise.

Un kthread dédié est fait pour ça : il peut bloquer aussi longtemps qu'il veut sans perturber quoi que ce soit. C'est le choix idiomatique pour une tâche réseau persistante dans le kernel.

---

## Pourquoi cet ordre précis au `rmmod` ?

Au `rmmod`, le thread peut être bloqué sur `kernel_recvmsg`. L'ordre des opérations dans `wlkom_exit` est critique :

1. `kernel_sock_shutdown` : force `recvmsg` à retourner une erreur, débloque le thread
2. `kthread_stop` : attend que le thread se termine proprement
3. `sock_release` : libère le socket

Inverser 1 et 2 causerait un blocage indéfini : `kthread_stop` attend que le thread sorte, mais le thread ne peut pas sortir de `recvmsg` tout seul. Faire 3 avant 2 libérerait un socket encore en cours d'utilisation par le thread, corruption mémoire garantie.

→ Implémentation détaillée : [wlkom.c — connection_thread et wlkom_exit](../code/wlkom.md)
