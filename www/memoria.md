# Memoria Técnica - Práctica 1: Servidor Web (Redes II)

## 1. Introducción
El presente documento detalla el diseño, arquitectura y desarrollo de un servidor web concurrente escrito en C, correspondiente a la Práctica 1 de la asignatura de Redes de Comunicación II. 

El objetivo principal ha sido construir un servidor HTTP/1.1 robusto y funcional desde cero, capaz de procesar peticiones concurrentes, servir contenido estático (texto, imágenes, vídeos) con sus correspondientes tipos MIME y cabeceras, y ejecutar contenido dinámico (CGI) mediante scripts en Python y PHP. Todo ello respetando estándares de ingeniería de software, empaquetado mediante *Makefiles*, estructuración modular y control estricto de memoria (garantizando cero fugas según las pruebas de Valgrind).

---

## 2. Documentos de Diseño

### 2.1. Especificación de Requisitos
El servidor debe cumplir con las siguientes funcionalidades principales:
* **Configuración externa:** Lectura de un fichero `server.conf` al arranque para definir el puerto de escucha, el directorio raíz (`server_root`), la firma del servidor y el número de clientes concurrentes (tamaño del pool de procesos).
* **Concurrencia:** Capacidad para atender múltiples peticiones simultáneas de forma paralela y segura frente a ataques DoS.
* **Protocolo HTTP:** Soporte completo para los métodos `GET`, `POST` y `OPTIONS`.
* **Contenido Estático:** Lectura y envío de ficheros binarios y de texto plano, incluyendo la inyección de cabeceras obligatorias (`Date`, `Last-Modified`, `Content-Length`, `Content-Type`).
* **Contenido Dinámico (CGI):** Ejecución de scripts externos (`.py`, `.php`) inyectando los argumentos (tanto provenientes de variables GET en la URL como del cuerpo en POST) a través de la entrada estándar (`stdin`) y capturando su salida.

### 2.2. Casos de Uso Principales
1. **Petición de Recurso Estático:** El cliente (navegador web) solicita `/media/img1.jpg`. El servidor parsea la petición, verifica la existencia del fichero en disco, calcula su tamaño y fecha de modificación mediante `stat()`, y devuelve un código de estado `200 OK` adjuntando el binario del archivo.
2. **Petición de Ejecución CGI (POST):** El cliente envía un formulario con datos (ej. `temperatura=30`) a `/scripts/temperatura.py`. El servidor recorta el cuerpo del mensaje, crea un proceso hijo, le inyecta los datos por una tubería al `stdin` del intérprete de Python, lee el HTML resultante por otra tubería de salida y lo envía de vuelta al cliente.
3. **Petición Malformada o Recurso No Encontrado:** El cliente solicita un recurso inexistente (`/kjhdsfkjhdk.html`) o utiliza un verbo no soportado (como `PUT`). El servidor captura el error de forma segura y devuelve un `404 Not Found` o un `400 Bad Request` respectivamente, manteniendo el proceso vivo para procesar futuras conexiones.

---

## 3. Desarrollo Técnico y Decisiones de Diseño

### 3.1. Estructura del Proyecto y Modularidad
Para mantener un código limpio, modular y cumplir estrictamente con la normativa de entrega, el código fuente se ha reorganizado en la siguiente estructura de directorios:
* `src/`: Contiene el código principal del servidor (`server.c`).
* `srclib/`: Contiene los fuentes de librerías de terceros (`picohttpparser.c`).
* `includes/`: Ficheros de cabeceras y definiciones (`picohttpparser.h`).
* `lib/`: Destino de las librerías compiladas estáticas (`.a`).
* `obj/`: Directorio temporal para los ficheros objeto (`.o`) generados durante la compilación.
* `www/`: Directorio raíz del servidor con los ficheros web a servir, subcarpetas de medios y el archivo `server.conf`.
* `scripts/`: Scripts CGI de prueba desarrollados en Python y PHP.

### 3.2. Paralelización: Pool de Procesos (Pre-forking)
**Decisión:** Se ha implementado una arquitectura de *Pre-forking* en lugar de un modelo de "un hilo por petición" o la creación de un proceso dinámico de un solo uso por cada conexión entrante.

**Justificación:** Realizar una llamada al sistema `fork()` consume recursos de la máquina. Si el servidor sufre un pico masivo de tráfico, generar procesos instantáneamente colapsaría la memoria operativa. Al arrancar un *pool* estático de $N$ trabajadores (cuyo número se extrae del fichero de configuración `.conf`) en el momento de inicialización, el coste de creación se asume una sola vez. Los trabajadores bloquean simultáneamente en la llamada `accept()`, y el sistema operativo se encarga de despertar a uno solo de forma segura y eficiente cuando llega una nueva conexión TCP.

### 3.3. Parseo HTTP: Integración de PicoHTTPParser
**Decisión:** Se ha delegado la interpretación de las peticiones HTTP a la micro-librería externa `PicoHTTPParser`.

**Justificación:** El estándar HTTP es complejo de procesar manualmente en C de forma totalmente segura. Implementar un parseador propio apoyándose en funciones como `strtok` o `sscanf` eleva drásticamente el riesgo de vulnerabilidades (desbordamiento de buffer o *Segmentation Faults*). PicoHTTPParser está altamente optimizada, previene accesos indebidos a memoria, e identifica con precisión milimétrica la longitud exacta de las cabeceras. Esto último resulta vital para localizar dónde empieza exactamente el cuerpo de los datos en peticiones `POST`.

### 3.4. Ejecución de CGI (Contenido Dinámico)
**Decisión:** Unificar el paso de argumentos mediante la Entrada Estándar (`STDIN`) usando tuberías inter-proceso (*pipes*), unificando el diseño para peticiones `GET` y `POST`.

**Justificación:** Esta abstracción simplifica la lógica del servidor. El proceso padre (nuestro servidor web) abre dos *pipes* y lanza un `fork()`. El proceso hijo redirige su `STDIN` y `STDOUT` hacia las tuberías usando `dup2()` y luego reemplaza su imagen de ejecución con `execlp()` llamando al intérprete correspondiente (`python3` o `php`). El padre escribe los argumentos limpios en la tubería de entrada del script, cierra el extremo de escritura para enviar la señal `EOF`, lee el resultado generado (HTML) de la tubería de salida y lo reenvía al socket del cliente.

### 3.5. Makefile y Librerías Propias
El proceso de construcción (`build`) está completamente automatizado a través de un `Makefile` principal con características avanzadas:
* **Generación de Librería Estática:** El código responsable del parseo HTTP no se compila directamente junto al código principal del servidor. El Makefile posee una regla específica que toma el código de `srclib/`, genera su código objeto en `obj/` y lo empaqueta usando el comando `ar rcs` en una librería estática ubicada en `lib/libpico.a`.
* **Parámetros/Etiquetas (Tags):**
  * `make all`: Crea los directorios estructurados si no existen, compila la librería y finalmente genera el binario enlazando estáticamente dicha librería mediante las banderas `-L./lib -lpico`.
  * `make clean`: Borra de forma segura el ejecutable y vacía el contenido de los directorios `obj/` y `lib/`, dejando el entorno preparado para la entrega en el repositorio Git sin incluir binarios residuales.

---

## 4. Conclusiones

### 4.1. Conclusiones Técnicas
La implementación de un servidor y el protocolo HTTP/1.1 desde cero exige una precisión muy elevada en el manejo de memoria y *strings* en C. El uso estricto del retorno de carro y salto de línea (`\r\n`) para la separación de metadatos, así como la inyección calculada del carácter nulo `\0` tras la lectura de datos mediante `recv()`, han sido factores críticos para evitar el arrastre de "basura" en los buffers entre diferentes conexiones. 

El análisis final de estrés ejecutado con la herramienta *Valgrind* (`--leak-check=full`) confirma que el diseño y la arquitectura final son robustos, arrojando un resultado de cero bytes de memoria perdida (`definitely lost: 0`). Esto garantiza que la gestión dinámica de memoria es correcta y el programa está preparado para mantenerse en ejecución de forma ininterrumpida.

### 4.2. Conclusiones Personales
* **Aprendizaje:** El desarrollo de esta primera práctica ha afianzado los conocimientos sobre programación a bajo nivel en Linux y el ciclo de vida completo de un *socket* TCP de servidor (bind, listen, accept). Adicionalmente, implementar un motor CGI básico nos ha permitido asimilar de forma práctica la intercomunicación de procesos a través de llamadas al sistema como `pipe` y `dup2`.
* **Dificultades:** La mayor barrera técnica consistió en el diseño bidireccional de las tuberías para el CGI. Sincronizar correctamente los procesos padre e hijo, prestando especial atención al orden de cierre de los descriptores de fichero para evitar escenarios de interbloqueo (*deadlocks* o procesos zombi), requirió varias fases de rediseño y depuración. El uso de la herramienta de línea de comandos `curl` fue imprescindible para depurar errores de formato HTTP que los navegadores web modernos tendían a enmascarar u omitir.
* **Sugerencias:** En futuras iteraciones, la adición de conexiones persistentes (cabecera `Connection: Keep-Alive`) supondría una mejora sustancial en el rendimiento, permitiendo que el navegador del cliente descargue todos los recursos accesorios de una misma web a través de un único socket TCP, reduciendo drásticamente el *overhead* de la red.