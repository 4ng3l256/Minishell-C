#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include "parser.h"

#define READ_PIPE 0       //extremo de lectura del pipe
#define WRITE_PIPE 1      //extremo de escritura del pipe
#define BUFFER_SIZE 1024  //tamaño del buffer de lectura
#define NUM_JOBS 128      //numero máximo de jobs en background
#define MAX_DIR 256       //tamaño para el comando cd

typedef struct{
  pid_t pid;
  char command[1024];
  int status;
} tjob;

tjob *jobs;               //almacenara los jobs en background (mirar TJob.h para ver variables de tjob)
pid_t main_pgid;          //almacenara el pgid de la shell principal

void spawn_prompt();      //imprime la linea de terminal
void redireccionesOutErr(tline *line);  //establece las redirecciones de salida y error
void childSignalHandler(int sig, siginfo_t *info, void *context); //gestiona las señales que van produciendo los hijos
void formatSignalString(int num, char* command, char* str); //devuelve un string que contiene datos sobre una señal (se vera mas adelante)
void showCurrentJobs(); //muestra los trabajos en background actuales
void fgJob(tline* line);  //devuelve un proceso a foreground
void commandCd(tline* line);  //implementacion del comando cd
int command_not_exist(tline* line); //comprueba si un comando existe o no

int main(void) 
{
  char buff[BUFFER_SIZE]; //variable que guardara el input de usuario
  tline* inputLine;  //variable que contendrá el input del usuario separado segun la libreria parser.h 
  int i, j, pipe_d[2], fdStdin, background_child_status, check_command_result; // variables para bucles, pipes, background y resultados
  pid_t pid;  //variable usada para los pid al hacer fork
  jobs = (tjob *) malloc(NUM_JOBS*sizeof(tjob)); //almacenara los jobs en background
  for (j = 0; j < NUM_JOBS; j++){
    jobs[j].pid = -1; //indica que esta vacio
  }
  
  //esta estructura servirá para gestionar las señales que los hijos vayan produciendo
  struct sigaction accion_hijo;
  memset(&accion_hijo,0,sizeof(accion_hijo)); //la inicializa a 0
  accion_hijo.sa_sigaction = childSignalHandler; //se asigna el handler
  sigemptyset(&accion_hijo.sa_mask); //inicializa el conjunto de señales como vacio
  accion_hijo.sa_flags = SA_RESTART | SA_SIGINFO; //flags que se utilizan para recabar información
  sigaction(SIGCHLD,&accion_hijo,NULL); //esta es la linea que indica que cada vez que se reciba una señal de un hijo se use la estructura


  //se inicializan las señales
  signal(SIGINT,SIG_IGN);   //se ignora ctrl + c
  signal(SIGTSTP,SIG_IGN);  //se ignora ctrl + z
  signal(SIGQUIT,SIG_IGN);  /*se ignora ctrl + \*/
  
  main_pgid = getpid();     //se guarda el pid del proceso de la terminal actual
  spawn_prompt();           //se genera la linea de prompt

  while (fgets(buff, 1024, stdin)) {  //bucle que va leyendo los datos introducidos por teclado
    inputLine = tokenize(buff);       //se formatea la cadena introducida para transformarla al formato tline de parser.h
    if (inputLine->ncommands == 0){   //si no se ha introducido ningun comando se vuelve a mostrar la terminal
      spawn_prompt();
      continue;
    }

    if (strcmp(inputLine->commands[0].argv[0],"exit") == 0){  //si el comando es exit se sale de la terminal y ademas se limpian los procesos pendientes
      for(j = 0; j < NUM_JOBS; j++){
        if(jobs[j].pid!=-1){
          kill(jobs[j].pid,SIGTERM); //se termina el proceso
        }
      }
      free(jobs); //se libera la memoria del almacen de procesos
      exit(0); //se sale del programa
    }
    
    if(strcmp(inputLine->commands[0].argv[0],"jobs") == 0){    //ejecuta el comando jobs para ver los trabajos actuales
      showCurrentJobs();
      spawn_prompt();
    }
    else if(strcmp(inputLine->commands[0].argv[0],"fg") == 0){ //ejecuta el comando fg para mandar un proceso de background a foreground
      fgJob(inputLine);
      spawn_prompt();
    }
    else if(strcmp(inputLine->commands[0].argv[0],"cd") == 0){ //ejecuta el comando cd
      commandCd(inputLine);
      spawn_prompt();
    }
    else{//ejecucion de comandos
      pid = fork();
      if (pid < 0){
        fprintf(stderr,"Fallo el fork()\n");
      }
      else if(pid == 0){ // hijo
        check_command_result = command_not_exist(inputLine);
        if (check_command_result == -1){ //se comprueba que la linea contiene comandos validos
          //se vuelven a poner los valores por defecto de las señales para los comandos
          signal(SIGINT,SIG_DFL);
          signal(SIGTSTP,SIG_DFL);
          signal(SIGQUIT,SIG_DFL);
          i = 0; //ira marcando cual es el comando actual
          if(inputLine->background == 1){
            setpgid(0,0); //en caso de background se le asigna un nuevo ID de proceso de grupo al hijo para separar sus señales y demas parámetros
          }
          if (inputLine->redirect_input != NULL){ //primero se comprueba si hay redireccion de entrada
            fdStdin = open(inputLine->redirect_input, O_RDONLY);
            if (fdStdin < 0){ //la apertura del fichero ha sido incorrecta
              fprintf(stderr, "Fallo al abrir el fichero: %s\n",inputLine->redirect_input);
              exit(-1);
            }
            else{ //la apertura ha sido correcta y se puede mover a la entrada del proximo comando
              dup2(fdStdin, STDIN_FILENO);
              close(fdStdin);
            }
          }
          if(inputLine->ncommands > 1){ //mas de un comando
            pipe(pipe_d);
            pid = fork();
            if (pid < 0){
              fprintf(stderr, "Fallo el fork()\n");
            }
            else if (pid == 0){  //se ejecuta primer comando moviendo el extremo de escritura 
                                 //del pipe a salida estandar para que el proximo comando lo lea
              close(pipe_d[READ_PIPE]); //se cierra extremo de lectura del pipe ya que el comando no lee de ahi
              dup2(pipe_d[WRITE_PIPE],STDOUT_FILENO);
              close(pipe_d[WRITE_PIPE]); //se cierra extremo de escritura una vez movido a STDOUT_FILENO
              execvp(inputLine->commands[i].filename,inputLine->commands[i].argv); //se ejecuta el comando
            }
            else{//se ejecuta el padre para el resto de comandos
              i++;
              close(pipe_d[WRITE_PIPE]);
              while(i < inputLine->ncommands){ //bucle que va enlazando comandos hasta el ultimo
                dup2(pipe_d[READ_PIPE],STDIN_FILENO); //se mueve el extremo de lectura del pipe a entrada 
                                                      //estandar para que el comando lo pueda leer
                close(pipe_d[READ_PIPE]); //se cierra extremo de lectura una vez movido a STDIN_FILENO
                pipe(pipe_d);             //se crea el pipe para el siguiente comando y
                pid = fork();             //se hace fork
                if (pid < 0){
                  printf("Fallo el fork");
                }
                if (pid == 0){ //hijo 
                  close(pipe_d[READ_PIPE]);
                  if (i == inputLine->ncommands - 1){//ultimo comado
                    redireccionesOutErr(inputLine); // se aplican las redirecciones de salida y error ya que es el ultimo
                  }
                  else{
                    dup2(pipe_d[WRITE_PIPE],STDOUT_FILENO); //escribe en el pipe la salida del comando
                  }
                  close(pipe_d[WRITE_PIPE]);
                  execvp(inputLine->commands[i].filename,inputLine->commands[i].argv);	
                }
                else{
                  close(pipe_d[WRITE_PIPE]);
                  waitpid(pid,NULL,0); //espera por los hijos
                }
                i++; //se pasa al siguiente comando
              }
            }
          } //fin mas de un comando
          else { //se ejecuta si solo hay un comando
            redireccionesOutErr(inputLine); //se aplican las redirecciones si las hubiera
            execvp(inputLine->commands[i].filename,inputLine->commands[i].argv); //se ejecuta el comando
          }
        }
        else{ //se ejecuta si alguno de los mandatos introducidos no es valido o no se encuentra
          fprintf(stderr, "%s: No se encuentra el mandato\n",inputLine->commands[check_command_result].argv[0]);
          exit(-1);
        }
      }//end hijo
      else{//padre
        if(inputLine->background == 1){ //si la linea se va a ejecutar en background
          for(j = 0; j < NUM_JOBS; j++){ //se busca el hueco del array de jobs que este libre
            if(jobs[j].pid == -1){
              break;
            }
          }
          jobs[j].pid = pid; //se le asigna el pid del hijo (que sera tambien el pgid del grupo al ser el primer o unico comando)
          strcpy(jobs[j].command,strtok(buff,"\n")); //se guarda la linea introducida por el usuario
          if (inputLine->commands[0].argc == 1){ //si el numero de argumentos del primer comando es 1 hay que comprobar si ha 
                                                 //sido parado por haber querido acceder a stdin (habra recibido SIGTTIN)
            waitpid(jobs[j].pid,&background_child_status,WUNTRACED); //se comprueba si esta stopped
            jobs[j].status = background_child_status; //se guarda su estado dentro del job
          }
          printf("[%d] %d\n",j,pid); //se imprime el pid o pgid de los comandos en background
        }
        else{ //si no es background se espera a que terminen sus hijos
          wait(NULL);
        }
        //se vuelven a ignorar las señales antes de volver a generar la terminal
        signal(SIGINT,SIG_IGN);
        signal(SIGTSTP,SIG_IGN);
        signal(SIGQUIT,SIG_IGN);
        spawn_prompt(); //se vuelve a imprimir la linea de terminal
      }//end padre
    }//end ejecucion comandos
  }//end while
  return 0;
}

void spawn_prompt(){
  //se crean primero 2 arrays en memoria dinamica con un tamaño maximo de 256 caracteres
  char *dir = (char *)malloc(MAX_DIR*sizeof(char));
  char *finalDir = (char *)malloc(MAX_DIR*sizeof(char));
  int home_len, dir_len;
  getcwd(dir,-1); //se obtiene el directorio actual y se guarda en dir
  if(strstr(dir, getenv("HOME")) != NULL){ //compara si el directorio actual es $HOME o un subdirectorio de cualquier nivel
    home_len = strlen(getenv("HOME"));     //longitud del la ruta absoluta de $HOME
    dir_len = strlen(dir);                 //longitud total del directorio actual
    if (home_len != dir_len){              //si las longitudes son distintas (no se esta en el home)
      strncpy(finalDir,dir+home_len, MAX_DIR - home_len); //se copia en finalDir la porcion que no se corresponde con
                                                          //$HOME 
      printf("%s%%msh:~%s > ",getenv("USER"),finalDir);   //se imprime ~ representando $HOME + la porcion de ruta restante
    }
    else{
      printf("%s%%msh:~ > ",getenv("USER")); //si las longitudes son iguales se esta en $HOME y se escribe solo ~
    }
  }
  else{
    printf("%s%%msh:%s > ",getenv("USER"), dir); //en este caso como no se esta en home se escribe la ruta correspondiente
  }
  //se liberan los arrays de memoria dinamica
  free(dir);
  free(finalDir);
}
void redireccionesOutErr(tline *line){ //redirecciones de salida y error
  int fdStdout,fdStderr;
  if (line->redirect_output != NULL){
    fdStdout = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC,0664);
    dup2(fdStdout,STDOUT_FILENO);
    close(fdStdout);
  }
  if (line->redirect_error != NULL){
    fdStderr = open(line->redirect_error, O_WRONLY | O_CREAT | O_TRUNC,0664);
    dup2(fdStderr,STDERR_FILENO);
    close(fdStderr);
  }
}

//command related
int command_not_exist(tline* line){ //se comprueba si el comando existe
  int j;
  for(j = 0; j < line->ncommands; j++){
    if (line->commands[j].filename == NULL){
      return j; //se devuelve el indice del comando que no existe dentro de line
    }
  }
  return -1; //se devuelve -1 indicando que no se ha encontrado un comando que no exista
}
void showCurrentJobs(){ //enseña los jobs en background
  int j;
  for(j = 0; j < NUM_JOBS; j++){
    if(jobs[j].pid != -1){ //los jobs en background seran aquellos del array de jobs que no tengan en valor de pid=-1
      if (WIFSTOPPED(jobs[j].status)){ //se comprueba si esta parado
        printf("[%d]+ Stopped %s\n",j,jobs[j].command);
      }
      else{ //esta running
        printf("[%d]+ Running %s\n",j,jobs[j].command);
      }
    }
  }
}
void fgJob(tline* line){  //devuelve a foreground un proceso background
  //variables iniciales
  int j, lastJob, bg_process_number;
  pid_t bg_pid;
  if(line->commands[0].argc == 1){  //si no hay argumentos se busca el ultimo job
    for(j = 0; j < NUM_JOBS; j++){
      if(jobs[j].pid != -1){
        lastJob = j;
      }
    }
    bg_process_number = lastJob;
  }
  else{
    bg_process_number = atoi(line->commands[0].argv[1]);
    if (jobs[bg_process_number].pid == -1) { //se comprueba si el numero introducido se corresponde con un job valido
      fprintf(stderr, "Error: el numero %d no tiene ningun trabajo asociado\n",bg_process_number);
      return;
    }
  }
  bg_pid = jobs[bg_process_number].pid; //pid del proceso en bg que pasara a estar en foreground
  printf("%s\n",jobs[bg_process_number].command); //se imprime la linea que pasa a ejecutarse en foreground
  tcsetpgrp(STDIN_FILENO,bg_pid);     //se pone al grupo del comando o comandos a ejecutar como grupo foreground
  if (WIFSTOPPED(jobs[bg_process_number].status)){ // el trabajo esta parado ya que intento acceder a stdin 
    killpg(bg_pid,SIGCONT); //se le manda una señal para que se active
  }
  waitpid(bg_pid,NULL,0);   //se espera a que se terminen de ejecutar los comandos (o que se detengan)
  jobs[bg_process_number].pid = -1; //se le asigna -1 al pid del numero de job que se estaba ejecutando para indicar
                                    //que ha finalizado
}
void commandCd(tline* line){  //comando cd
  char *dir = (char*)malloc(MAX_DIR*sizeof(char)); //aqui es donde se guardara el directorio al que hacer cd
  if(line->commands[0].argc == 1){ //si no hay argumentos se hace cd a home
    chdir(getenv("HOME"));
  }
  else{
    if(line->commands[0].argv[1][0] == '~'){  //se comprueba si se ha introducido ruta relativa usando ~ como 
                                              // equivalente de $HOME para transformarlo
      strncpy(dir,getenv("HOME"),MAX_DIR);
      if(strlen(line->commands[0].argv[1]) > 1){
        strncat(dir, line->commands[0].argv[1]+1,MAX_DIR);
      }
    }
    else{ //si no se copia carga directamente la ruta en dir
      strncpy(dir, line->commands[0].argv[1], MAX_DIR);
    }
    if(chdir(dir) != 0){ //si es distinto de 0 se habra producido un error y se imprime
      fprintf(stderr, "No se pudo cambiar de directorio: %s\n",strerror(errno));
    }
  }
  free(dir); //se libera el array de memoria dinamica
}

//signal related
void childSignalHandler(int sig, siginfo_t *info, void *context){
  int j, status;
  char str[BUFFER_SIZE];

  //por si se quiere hacer debug
  if(sig > -1 && context != NULL){
    j = 1;
  }
  for(j = 0; j < NUM_JOBS; j++){  //se busca el numero de job del que ha llegado una señal comparando los pid almacenados en
                                  //cada job con si_pid (que es el pid del proceso que ha originado la señal, es decir el hijo)
    if(jobs[j].pid == info->si_pid){
      break;
    }
  }
  //la siguiente comprobacion es importante ya que podria darse el caso de que se haya reanudado
  //un proceso que estaba stopped debido a haber accedido a stdin por ejemplo, o un proceso al que
  //se le hayan devuelto las señales de ctrl c para pararlo. En ese caso, el control de la terminal
  //lo tendrá el pgid de ese proceso, por lo que hay que devolverle a la terminal principal el control
  //de la terminal antes de que acabe el proceso
  if (tcgetpgrp(STDIN_FILENO) != main_pgid && waitpid(info->si_pid,&status,WNOHANG)==-1){
    signal(SIGTTOU,SIG_IGN);  //es necesario ignorar esta señal ya que cuando se hace tcsetpgrp para devolverle el control a la terminal
                              //principal, como el otro proceso no ha acabado un proceso en background estará intentando acceder a la terminal
                              //en foreground en cuyo caso se envia una señal SIGTTOU por defecto al proceso que lo intenta para detenerlo
    tcsetpgrp(STDIN_FILENO,main_pgid); //se restaura el control al proceso de la terminal principal
    killpg(main_pgid,SIGCONT); //se le manda una señal para que despierte al proceso de la terminal principal
    signal(SIGTTOU,SIG_DFL);

  }
  //en este caso, si_pid sera el pid del proceso hijo que originó la señal, y waitpid tal y 
  //como esta puesto devolvera el pid del hijo en caso de haber acabado por lo que se hace
  //esta comparacion para saber si la señal la origino un hijo que ya ha acabado. Este caso se
  //da cuando acaba un hijo que estaba en background
  if (info->si_pid == waitpid(info->si_pid,&status,WNOHANG) && j < NUM_JOBS){
    formatSignalString(j,jobs[j].command,str);  //se explica abajo
    write(STDOUT_FILENO,str,strlen(str));       //se usa write ya que segun el manual, cuando se implementa
                                                //un signal handler las funciones printf, fprintf y derivadas
                                                //y muchas otras no son seguras de usar por lo que hay que 
                                                //recurrir a write para imprimir
    jobs[j].pid = -1; //se marca el pid del job como -1 para indicar que esta acabado
  }
  return;
}
void formatSignalString(int num, char* command, char* str){ //se formatea una cadena para que aparezca el numero de job
                                                            //done y el comando introducido
  sprintf(str, "[%d]+ Done %s\n",num,command);
}

