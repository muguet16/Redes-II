/**************************************************************************
*	This is a simple echo server.  This demonstrates the steps to set up
*	a streaming server.
**************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/wait.h>
#include "picohttpparser.h"

typedef struct {
    char server_root[256];
    int max_clients;
    int listen_port;
    char server_signature[256];
}ServerConfig;

ServerConfig config;

#define MAXBUF		1024
char buffer[MAXBUF];

const char* obtener_content_type(const char* ruta) {
    if (strstr(ruta, ".html") || strstr(ruta, ".htm")) return "text/html";
    if (strstr(ruta, ".txt")) return "text/plain";
    if (strstr(ruta, ".gif")) return "image/gif";
    if (strstr(ruta, ".jpeg") || strstr(ruta, ".jpg")) return "image/jpeg";
    if (strstr(ruta, ".mpeg") || strstr(ruta, ".mpg")) return "video/mpeg";
    if (strstr(ruta, ".doc") || strstr(ruta, ".docx")) return "application/msword";
    if (strstr(ruta, ".pdf")) return "application/pdf";
    // Tipo por defecto para descargar si no lo reconoce
    return "application/octet-stream"; 
}

void obtener_fecha_http(char *buffer, size_t size, time_t *t) {
    struct tm *tm_info;
    // Si t es NULL, cogemos la hora actual. Si no, cogemos la hora que nos pasen.
    if (t == NULL) {
        time_t ahora = time(NULL);
        tm_info = gmtime(&ahora);
    } else {
        tm_info = gmtime(t);
    }
    // Formateamos la fecha según el estándar RFC 1123
    strftime(buffer, size, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
}

void ejecutar_script(int client_fd, const char *ruta_script, const char *argumentos, const char *interprete) {
    int pipe_in[2];  // Para enviar los argumentos al stdin del script
    int pipe_out[2]; // Para leer el stdout del script

    pipe(pipe_in);
    pipe(pipe_out);

    pid_t pid = fork();

    if (pid == 0) { 
        // --- PROCESO HIJO (Ejecutará el script) ---
        close(pipe_in[1]);  // Cierra el extremo de escritura
        close(pipe_out[0]); // Cierra el extremo de lectura

        // Redirigir la entrada estándar (stdin) y salida estándar (stdout) hacia las tuberías
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);

        // Ejecutar el intérprete (python o php) pasándole la ruta del script
        execlp(interprete, interprete, ruta_script, NULL);
        
        // Si llega aquí, es que execlp falló
        perror("Error al ejecutar el script");
        exit(1);
    } else {
        // --- PROCESO PADRE (El servidor Web) ---
        close(pipe_in[0]);
        close(pipe_out[1]);

        // 1. Le enviamos los argumentos al script por su entrada estándar
        if (argumentos != NULL) {
            write(pipe_in[1], argumentos, strlen(argumentos));
        }
        close(pipe_in[1]); // Hay que cerrarlo para que el script sepa que ya no hay más argumentos

        // 2. Enviamos la línea de estado HTTP y cabeceras completas al cliente
        char status_line[512];
        sprintf(status_line, "HTTP/1.1 200 OK\r\nServer: %s\r\nContent-Type: text/html\r\n\r\n", config.server_signature);
        send(client_fd, status_line, strlen(status_line), 0);

        // 3. Leemos lo que el script imprime por pantalla y se lo pasamos directo al cliente
        char buffer[4096];
        int bytes_leidos;
        while ((bytes_leidos = read(pipe_out[0], buffer, sizeof(buffer))) > 0) {
            send(client_fd, buffer, bytes_leidos, 0);
        }
        close(pipe_out[0]);
        
        // Esperamos a que el script termine completamente
        waitpid(pid, NULL, 0); 
    }
}

int procesarPeticion(int fd){
    char buf[4097]; // Buffer para recibir la petición (un poco más grande que el máximo para detectar overflow)
    int rret, pret;
    size_t buflen = 0, prevbuflen = 0;

    //Variables para PicoHTTPParser
    const char *method, *path;
    size_t method_len, path_len;
    int minor_version;
    struct phr_header headers[100];
    size_t num_headers;

    while(1){
        rret = recv(fd, buf + buflen, sizeof(buf) - buflen, 0);
        if(rret <= 0) return rret; // Error o conexión cerrada por el cliente
        prevbuflen = buflen;
        buflen += rret;

        // --- LA MAGIA: Cortamos el string exactamente donde acaban los datos ---
        buf[buflen] = '\0';

        //intentamos parsear lo que tenemos en el buffer
        num_headers = sizeof(headers) / sizeof(headers[0]);
        pret = phr_parse_request(buf, buflen, &method, &method_len, &path, &path_len, 
                                    &minor_version, headers, &num_headers, prevbuflen);
        
        if(pret > 0){
            break; //Parseo completado con éxito
        }else if(pret == -1){
            return -1; //Error sintáctico en la petición HTTP (se podría enviar un 400 Bad Request aquí)
        }
        //Si pret == -2, la petición es incompleta, seguimos leyendo el while
        if ( buflen == sizeof(buf)) return -1; //Buffer lleno
    }

    char method_str[16], path_str[256];
    sprintf(method_str, "%.*s", (int)method_len, method);
    sprintf(path_str, "%.*s", (int)path_len, path);

    printf("[Petición parseada] Verbo: %s, Ruta: %s\n", method_str, path_str);

    //Lógica de respuesta
    //Solo vamos a procesar GET por ahora
    if (strcmp(method_str, "GET") == 0) {
        char real_path[512];
        char *argumentos = NULL;
        
        // 1. Separar la ruta de los argumentos (si los hay)
        char *interrogacion = strchr(path_str, '?');
        if (interrogacion != NULL) {
            *interrogacion = '\0'; // Cortamos el string de la ruta aquí
            argumentos = interrogacion + 1; // Los argumentos son lo que va después del '?'
        }

        // 2. Construir la ruta física
        if (strcmp(path_str, "/") == 0) {
            sprintf(real_path, "%sindex.html", config.server_root);
        } else {
            sprintf(real_path, "%s%s", config.server_root, path_str + 1);
        }

        // 3. Comprobar si es un script
        if (strstr(real_path, ".py") != NULL) {
            printf("[CGI] Ejecutando script Python: %s con args: %s\n", real_path, argumentos);
            ejecutar_script(fd, real_path, argumentos, "python3"); // O usar "python" según tu sistema
        } 
        else if (strstr(real_path, ".php") != NULL) {
            printf("[CGI] Ejecutando script PHP: %s con args: %s\n", real_path, argumentos);
            ejecutar_script(fd, real_path, argumentos, "php");
        } 
        else {
            // 4. Si no es un script, es un archivo estático normal
            FILE *file = fopen(real_path, "rb");
            if (file) {
                // 1. Obtenemos información del archivo (peso y fecha de modificación)
                struct stat st;
                stat(real_path, &st);
                long content_length = st.st_size;

                // 2. Preparamos las fechas
                char date_header[128];
                obtener_fecha_http(date_header, sizeof(date_header), NULL); // Fecha actual
                
                char modified_header[128];
                obtener_fecha_http(modified_header, sizeof(modified_header), &st.st_mtime); // Fecha del archivo

                const char* content_type = obtener_content_type(real_path);
                
                // 3. Enviamos el 200 OK con TODAS las cabeceras exigidas
                char response[1024];
                sprintf(response, 
                        "HTTP/1.1 200 OK\r\n"
                        "Server: %s\r\n"
                        "Date: %s\r\n"
                        "Last-Modified: %s\r\n"
                        "Content-Length: %ld\r\n"
                        "Content-Type: %s\r\n\r\n", 
                        config.server_signature, date_header, modified_header, content_length, content_type);
                
                send(fd, response, strlen(response), 0);
                
                char filecontent[4096];
                int bytes;
                while ((bytes = fread(filecontent, 1, sizeof(filecontent), file)) > 0) {
                    send(fd, filecontent, bytes, 0);
                }
                fclose(file);
            } else {
                char response[512];
                sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nServer: %s\r\n\r\n<h1>404 Not Found</h1>", 
                        config.server_signature);
                send(fd, response, strlen(response), 0);
            }
        }
    }
    else if (strcmp(method_str, "POST") == 0) {
        char real_path[512];

        //Limpiamos la URL por si vienen variables GET mezcladas
        char *interrogacion = strchr(path_str, '?');
        if (interrogacion != NULL) {
            *interrogacion = '\0'; 
        }

        sprintf(real_path, "%s%s", config.server_root, path_str + 1);

        // En POST, los argumentos van en el cuerpo.
        // El cuerpo empieza justo donde terminan las cabeceras (pret).
        char *argumentos = buf + pret;
        
        // Si no se leyeron argumentos completos en el primer recv, 
        // para la práctica básica bastará con los que haya en el buffer.
        
        if (strstr(real_path, ".py") != NULL) {
            printf("[CGI POST] Ejecutando Python: %s | Args: %s\n", real_path, argumentos);
            ejecutar_script(fd, real_path, argumentos, "python3");
        } 
        else if (strstr(real_path, ".php") != NULL) {
            printf("[CGI POST] Ejecutando PHP: %s | Args: %s\n", real_path, argumentos);
            ejecutar_script(fd, real_path, argumentos, "php");
        } 
        else {
            // Un POST a un archivo que no es script suele ser un error 405
            char response[512];
            sprintf(response, "HTTP/1.1 405 Method Not Allowed\r\nServer: %s\r\n\r\n", config.server_signature);
            send(fd, response, strlen(response), 0);
        }
    } 
    else if (strcmp(method_str, "OPTIONS") == 0) {
        char response[512];
        sprintf(response, "HTTP/1.1 200 OK\r\nAllow: GET, POST, OPTIONS\r\nServer: %s\r\nContent-Length: 0\r\n\r\n", config.server_signature);
        send(fd, response, strlen(response), 0);
    } 
    else {
        // Si piden PUT, DELETE u otro verbo no soportado -> 400 Bad Request
        char response[512];
        sprintf(response, "HTTP/1.1 400 Bad Request\r\nServer: %s\r\n\r\n", config.server_signature);
        send(fd, response, strlen(response), 0);
    }
}

void leer_configuracion(){
    // Leemos la configuración del servidor desde el archivo server.conf
    FILE *config_file = fopen("server.conf", "r");
    if(!config_file){
        perror("Error al abrir el archivo de configuración");
        exit(errno);
    }
    
    char line[512];
    while(fgets(line, sizeof(line), config_file)){
        if(sscanf(line, "server_root = %s", config.server_root) == 1) continue;
        if(sscanf(line, "max_clients = %d", &config.max_clients) == 1) continue;
        if(sscanf(line, "listen_port = %d", &config.listen_port) == 1) continue;
        if(sscanf(line, "server_signature = %s", config.server_signature) == 1) continue;
    }
    
    fclose(config_file);
}

int main(void)
{   int sockfd;
	struct sockaddr_in self;

    leer_configuracion();
	

	// Creamos el socket tipo TCP */
    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
	{
		perror("Error en la creación del socket");
		exit(errno);
	}

	// Inicializamos estructura de dirección y puerto
	bzero(&self, sizeof(self));
	self.sin_family = AF_INET;
	self.sin_port = htons(config.listen_port);
	self.sin_addr.s_addr = INADDR_ANY;

	// Ligamos puerto al socket
    if ( bind(sockfd, (struct sockaddr*)&self, sizeof(self)) != 0 )
	{
		perror("socket--bind");
		exit(errno);
	}

	// OK, listos para escuchar...
	if ( listen(sockfd, 20) != 0 )
	{
		perror("socket--listen");
		exit(errno);
	}
	
	printf("Escuchando en [%s:%d]...\n", inet_ntoa(self.sin_addr), ntohs(self.sin_port));

    for(int i = 0; i < config.max_clients; i++){
        pid_t pid = fork();
        
        if(pid == 0){
            // Proceso hijo (trabajador)
            while (1)
            {	int clientfd;
                struct sockaddr_in client_addr;
                // Usamos socklen_t para evitar warnings en accept()
                socklen_t addrlen = sizeof(client_addr);

                // El hijo se queda bloqueado aquí esperando clientes
                clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &addrlen);
                if(clientfd < 0) continue; // Si hay error, seguimos esperando

                printf("[Proceso %d] Conexión desde [%s:%d]\n", getpid(), inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                // Procesamos la petición del cliente
                procesarPeticion(clientfd);

                // Cerramos la conexióncon el cliente y volvemos al accept()
                close(clientfd);
            }
            exit(0); // Nunca debería llegar aquí por el while(1)
        }
        else if(pid < 0){
            perror("Error al hacer fork");
        }
    }

    // Código del proceso padre
    //El padre no atiende a peticiones, solo espera indefinidamente a que los hijos terminen (lo cual no ocurrirá en este caso) así evita zombies
    while(wait(NULL) > 0);

	close(sockfd);
	return 0;
}