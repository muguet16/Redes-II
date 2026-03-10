# Arquitectura y Desarrollo del Servidor Web en C

Este documento detalla el funcionamiento interno, los conceptos de sistemas operativos utilizados y las fases de construcción del servidor web HTTP/1.1 desarrollado para la práctica de Redes de Comunicación II.

---

## 1. Esquema de Funcionamiento del Servidor

El siguiente esquema representa el ciclo de vida de la ejecución del servidor y cómo fluye la información desde que se arranca el programa hasta que se atiende a un cliente.

```text
=======================================================================
                        [ PROCESO PADRE ]
=======================================================================
       |
       |-- 1. Lee configuración (server.conf)
       |-- 2. Abre el Socket (socket)
       |-- 3. Asigna el puerto 8080 (bind)
       |-- 4. Escucha conexiones (listen)
       |
       |-- 5. CREACIÓN DEL POOL (Pre-forking)
       |      Bucle for (i=0; i < max_clients):
       |
       +------------+------------+------------+
       |            |            |            |
  [ HIJO 1 ]   [ HIJO 2 ]   [ HIJO 3 ]   [ HIJO N ]
       |            |            |            |
       v            v            v            v
    (accept)     (accept)     (accept)     (accept)  <-- Todos bloqueados durmiendo

=======================================================================
                        [ LLEGA UN CLIENTE ]
=======================================================================
       |
       |-- El Sistema Operativo despierta a UN solo proceso (Ej: HIJO 1).
       |-- HIJO 1 lee los datos de la red (recv).
       |
       |-- PARSEO HTTP (PicoHTTPParser):
       |   Extrae Verbo (GET/POST), Ruta y Cabeceras.
       |
       +-- ¿Qué ha pedido el cliente?
           |
           |--- A) RECURSO ESTÁTICO (html, jpg, mp4)
           |        |-- Comprueba si existe y obtiene tamaño/fecha (stat).
           |        |-- Envía cabeceras (200 OK, Content-Type, etc).
           |        |-- Lee del disco y envía por el socket (send).
           |
           |--- B) RECURSO DINÁMICO (Script .py / .php)
                    |-- Crea tuberías de comunicación (pipe).
                    |-- Se clona a sí mismo (fork).
                    |
                    |-- [ SUB-HIJO (Script) ]
                    |     |-- Enchufa tuberías a STDIN/STDOUT (dup2).
                    |     |-- Se transforma en Python (execlp).
                    |     |-- Genera HTML y muere.
                    |
                    |-- [ HIJO 1 (Servidor) ]
                          |-- Le envía argumentos al script por la tubería.
                          |-- Lee el HTML de la tubería.
                          |-- Envía el HTML al cliente (send).

=======================================================================
                        [ FIN DE PETICIÓN ]
=======================================================================
       |
       |-- HIJO 1 cierra el socket del cliente (close).
       |-- HIJO 1 vuelve arriba y se bloquea de nuevo en (accept).
```


## 2. Conceptos Clave de Sistemas Operativos

Para entender la arquitectura concurrente y la ejecución de contenido dinámico (CGI) del servidor, es vital comprender las siguientes herramientas y llamadas al sistema que proporciona Linux:

### 2.1. Procesos y Clonación (`fork`)
La llamada al sistema `fork()` permite a un programa crear una copia exacta de sí mismo en la memoria RAM. 
* A partir del momento en que se ejecuta, coexisten dos procesos de forma concurrente: el **Padre** y el **Hijo**.
* Tienen las mismas variables y los mismos archivos abiertos en el instante exacto de la clonación, pero **sus memorias son independientes**. Si el proceso Hijo modifica una variable, el Padre no se ve afectado.
* En nuestra arquitectura, `fork()` se utiliza en dos momentos: para crear el *pool* de trabajadores al arrancar el servidor y para aislar la ejecución de los scripts CGI.

### 2.2. Tuberías (`pipe`)
Un `pipe` es un canal de comunicación unidireccional entre dos procesos del sistema. Al crear una tubería, el sistema operativo devuelve un array con dos descriptores de fichero:
* `pipefd[0]`: El extremo de **lectura**.
* `pipefd[1]`: El extremo de **escritura**.

Lo que un proceso escribe en el extremo `1`, el otro lo puede leer por el extremo `0`. Son una estructura esencial porque los procesos tienen su memoria aislada y no pueden intercambiar información de forma directa.

### 2.3. Redirección (`dup2`)
Todos los programas en Linux nacen con tres canales abiertos por defecto: Entrada Estándar (`STDIN`), Salida Estándar (`STDOUT`) y Error Estándar (`STDERR`).
* **`dup2(viejo, nuevo)`:** Permite desenchufar un canal estándar y conectarlo a otro destino. En el módulo CGI, usamos `dup2` para redirigir el `STDOUT` y el `STDIN` del proceso hijo hacia nuestras tuberías, engañando al proceso para que lea y escriba en ellas en lugar de en la terminal.

### 2.4. La Metamorfosis de Procesos (`execlp`)
La familia de funciones `exec` (y en concreto `execlp`) es el motor que permite ejecutar lenguajes externos desde nuestro código en C. Su comportamiento es radical: **destruye todo el código en C del proceso actual, borra su memoria, y reemplaza su "cuerpo" entero con el código de otro programa distinto**.

La anatomía de la llamada `execlp("python3", "python3", ruta_script, NULL);` se desglosa de la siguiente manera:
* **`exec` (Execute):** Ordena la sustitución del proceso actual.
* **`l` (List):** Indica que los argumentos del nuevo programa se pasan como una lista separada por comas, finalizando obligatoriamente con un puntero `NULL`.
* **`p` (Path):** Le dice al sistema operativo que busque el programa (en este caso, `python3` o `php`) utilizando la variable de entorno PATH, sin necesidad de escribir la ruta absoluta (como `/usr/bin/python3`).

**Comportamiento clave en el servidor:**
1. **Herencia de tuberías:** Al mutar, el proceso pierde su código original pero **conserva los descriptores de fichero abiertos**. Cuando el nuevo script de Python o PHP intenta leer del teclado o imprimir en pantalla, en realidad lo está haciendo a través de las tuberías que enlazamos previamente con `dup2`, permitiendo que el servidor en C tome el control de las entradas y salidas.
2. **Punto de no retorno:** Si `execlp` tiene éxito, el código en C original desaparece, por lo que ninguna línea de código situada debajo de esta función llegará a ejecutarse jamás. Si el flujo del programa llega a la siguiente línea (típicamente un `perror`), significa que el sistema operativo no encontró el intérprete solicitado y la metamorfosis falló.
---

## 3. Fases de Desarrollo Explicadas

A continuación, se detalla la evolución técnica del proyecto a lo largo de sus distintas fases de implementación:

### Fase 1: Configuración Externa
* **Objetivo:** Evitar valores fijos ("hardcodeados") en el código fuente para permitir la reconfiguración del servidor sin necesidad de recompilación.
* **Desarrollo:** Se implementó una función para parsear el fichero de texto `server.conf`. Utilizando la función `sscanf` de C con el comodín `%[^\n]`, se extraen parámetros clave como el puerto de escucha, la ruta raíz (`./`) y la firma del servidor, inyectándolos en una estructura de configuración global antes de levantar los sockets de red.
* **Código Clave:**
  ```c
  // Lectura de la configuración línea a línea
  if(sscanf(line, "server_signature = %[^\n]", config.server_signature) == 1) continue;

* **Problemas y Soluciones**
   **Problema:** Truncamiento de la firma del servidor. Al usar %s, si el archivo decía MiServidorWeb 1.0, el servidor solo guardaba MiServidorWeb porque %s se detiene al encontrar un espacio en blanco.

   **Solución:** Se sustituyó %s por la expresión `%[^\n]`, que indica al escáner que debe leer todo el texto hasta encontrar un salto de línea.


### Fase 2: Arquitectura Pre-forking (Protección DoS)
* **Objetivo:** Atender decenas de conexiones concurrentes sin agotar la memoria de la máquina (mitigación de ataques de Denegación de Servicio).
* **Desarrollo:** En lugar del modelo *fork-por-petición* (ineficiente y vulnerable frente a picos de tráfico), se implementó un **Pool de Procesos Estático**. El servidor inicializa un bucle que crea $N$ procesos trabajadores de antemano. Todos ellos quedan bloqueados de forma pasiva en la llamada `accept()`. El kernel de Linux se encarga de balancear la carga de forma segura, despertando a un solo trabajador libre cuando entra una nueva conexión TCP.
* **Código Clave:**
  ```c
  // Generación del pool de procesos trabajadores
   for (int i = 0; i < config.max_clients; i++) {
       if (fork() == 0) {
              // Código del trabajador (HIJO)
              while(1) {
                     int client_fd = accept(sockfd, ...); // Todos esperan aquí
                     procesarPeticion(client_fd);
                     close(client_fd);
              }
       }
   }
       wait(NULL); // El padre espera infinitamente

* **Problemas y soluciones:**
   **Problema:** Error Address already in use. Al cerrar el servidor bruscamente con Ctrl+C y volver a arrancarlo, Linux mantenía el puerto 8080 bloqueado por seguridad en estado TIME_WAIT.

   **Solución:** Se configuró el socket con la opción SO_REUSEADDR usando setsockopt() justo antes de hacer el bind(), obligando al sistema operativo a liberar el puerto instantáneamente.


### Fase 3: Parseo HTTP
* **Objetivo:** Interpretar y validar las peticiones del cliente de forma rápida y segura.
* **Desarrollo:** Se descartó realizar el parseo manualmente mediante funciones estándar de C (`strtok`, `strstr`) debido a su alta vulnerabilidad frente a peticiones malformadas o intencionadamente erróneas (*Buffer Overflows*). Se integró la micro-librería `PicoHTTPParser`, capaz de trocear la cadena HTTP a alta velocidad, aislando el Método (Verbo), la Ruta (Path) y las Cabeceras, e indicando el byte exacto donde comienza el cuerpo del mensaje.

* **Código Clave:**
  ```c
  // Parseo de la petición HTTP con PicoHTTPParser
   pret = phr_parse_request(buf, buflen, &method, &method_len, &path, &path_len, ...);

* **Problemas y Soluciones**
   **Problema:** Errores 404 Not Found sistemáticos al inicio. El servidor intentaba buscar los recursos en www/www/index.html.

   **Solución:** Ocurría por una desalineación entre el directorio de ejecución y la variable server_root. Se corrigió modificando el fichero de configuración para usar la ruta relativa actual (server_root = ./).


### Fase 4: Despacho de Contenido Estático
* **Objetivo:** Servir archivos del disco duro (HTML, JPEG, MP4) cumpliendo con los estándares HTTP/1.1.
* **Desarrollo:** 1. Se extrae la extensión del archivo solicitado para asignar dinámicamente la cabecera `Content-Type`.
  2. Se utiliza la llamada al sistema `stat()` para inspeccionar el archivo en disco antes de abrirlo, obteniendo su tamaño exacto en bytes y su última fecha de modificación, inyectándolos respectivamente en las cabeceras `Content-Length` y `Last-Modified`.
  3. Se lee el fichero binario en bloques iterativos y se transfiere por el socket mediante la función `send()`.

* **Código Clave:**
  ```c
  // Obtención de metadatos del archivo para las cabeceras
   struct stat st;
   stat(real_path, &st);
   obtener_fecha_http(modified_header, sizeof(modified_header), &st.st_mtime);

   // Construcción de la respuesta
   sprintf(response, "HTTP/1.1 200 OK\r\nDate: %s\r\nLast-Modified: %s\r\nContent-Length: %ld\r\nContent-Type: %s\r\n\r\n", ...);


### Fase 5: Ejecución de Contenido Dinámico (CGI) y Verbos
* **Objetivo:** Soportar peticiones `POST`, responder al verbo `OPTIONS` y ejecutar código de servidor en lenguajes de scripting externos.
* **Desarrollo:** Se programó una capa de compatibilidad CGI unificada para `GET` y `POST`:
  * Para peticiones **GET**, se aísla la cadena de consulta separando la ruta a partir del carácter `?`.
  * Para peticiones **POST**, se captura el cuerpo del mensaje calculando el offset exacto de los bytes ocupados por las cabeceras previas.
  * En ambos flujos, el proceso trabajador abre dos *pipes*, ejecuta un `fork`, y redirige los canales estándar del proceso hijo mediante `dup2` hacia las tuberías antes de invocar al intérprete (`php` o `python3`) mediante `execlp`. El proceso padre escribe asíncronamente los argumentos limpios en la tubería de entrada del script, lee la salida HTML procesada por la tubería inversa y la vuelca directamente hacia el socket del cliente.

   ```text
  [Cliente Web] <=======(SOCKET)=======> [Servidor en C]
                                             |      ^
                                  (pipe_in)  |      |  (pipe_out)
                                             v      |
                                        [ Script Python ]

* **Código Clave:**
  ```c
   // Aislamiento de argumentos según el verbo
   char *interrogacion = strchr(path_str, '?'); // Para GET
   char *argumentos = buf + pret;               // Para POST (pret = fin de cabeceras)

   // Inyección de tuberías en el proceso CGI
   dup2(pipe_in[0], STDIN_FILENO);   // STDIN lee del pipe de entrada
   dup2(pipe_out[1], STDOUT_FILENO); // STDOUT escribe al pipe de salida
   execlp("python3", "python3", ruta_script, NULL);

* **Problemas y Soluciones**
   **Problema1:** Comportamiento errático y "basura" en la memoria. Al hacer peticiones POST cortas después de peticiones GET muy largas, se colaban restos de texto de conexiones anteriores al final de la variable de argumentos.

   **Solución1:** Se protegió la lectura del socket añadiendo dinámicamente el carácter nulo (buf[buflen] = '\0';) para truncar la cadena exactamente donde terminaban los datos recibidos. También se restó -1 al tamaño máximo en recv para evitar un Buffer Overflow.

   **Problema2:** Error Header without colon reportado por el cliente de terminal curl.

   **Solución2:** El protocolo HTTP exige una línea en blanco para separar cabeceras del cuerpo. Se corrigió añadiendo explícitamente la secuencia \r\n\r\n entre el 200 OK del servidor y la salida HTML generada por el script Python.

   **Problema3:** Pantallas en blanco al enviar formularios mixtos (POST hacia un script con ? en la URL).

   **Solución3:** Se añadió el recorte preventivo del símbolo ? también dentro de la rama lógica del verbo POST, evitando que execlp intentara ejecutar un fichero con un nombre de archivo inexistente.