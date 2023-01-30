# Minishell-en-C

Proyecto de una minishell escrita en lenguaje C

## Funcionalidades

Esta minishell posee las siguientes funcionalidades:
* Ejecutar un mandato con redirección de entrada, salida y/o error.
* Ejecutar varios mandatos usando pipes y posible redirección de entrada, salida y/o error
* Ejecutar mandatos que reciban de entrada estándar y posible ejecución mediante pipes de otros mandatos
* Ejecutar uno o varios mandatos en background con posible redirección de entrada, salida y/o error
* Mostrar comandos en background mediante el comando `jobs`, distinguiendo entre los que se están ejecutando y los que han sido parados
* Recuperar comandos en background para pasar a ejecutarlos en foreground (incluidos aquellos como cat que reciban de entrada estándar) y recuperar su entrada, salida, etc.
* Finalizar procesos traídos desde background reaccionando a ctrl + c o a ctrl + d en caso de comandos como cat (evitando que se cierre la terminal y haciendo que solo finalice el proceso)
* Controlar que los procesos que estaban en background impriman que han sido finalizados si terminan (como hace por ejemplo bash)
* Ignorar las señales de finalización por teclado (como ctrl + c o ctrl + z)
* Distinguir el directorio actual en el que se está y si forma parte de `$HOME` imprimir ~ en su lugar
* Ejecutar el comando `cd` tanto con rutas absolutas como relativas (incluyendo aquellas que lleven ~ representando `$HOME`)
