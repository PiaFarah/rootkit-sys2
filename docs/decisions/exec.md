# Exécution

Reprise du TD `module/exec`, adapté au rootkit.

## Pourquoi `call_usermodehelper` et pas un reverse shell ?

Un reverse shell (rediriger stdin/stdout directement vers le socket) était l'alternative évidente. Elle a été écartée parce que le sujet demande stdout, stderr et l'exit code **séparément**, ce qu'un reverse shell classique ne peut pas faire.

`call_usermodehelper` avec redirection vers des fichiers temporaires permet de capturer les trois indépendamment. Le flag `UMH_WAIT_PROC` est indispensable : il bloque le kthread jusqu'à la fin de la commande. Sans lui, les fichiers seraient lus avant que la commande ait fini d'écrire.

## Pourquoi `kmalloc` et pas des buffers sur la pile ?

Les piles kernel sont limitées à 8 Ko. La sortie d'une commande peut largement dépasser ça (`find /`, `cat` d'un gros fichier). Un dépassement de pile kernel provoque un panic immédiat.

`kmalloc` alloue sur le heap kernel, sans cette contrainte. C'est le choix systématique pour tout buffer de taille potentiellement variable dans le kernel.

Pour l'implémentation détaillée : [wlkom.c — execute_and_send_output](../code/wlkom.md)
