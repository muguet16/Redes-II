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



int procesarPeticion(int fd){
    char buf[4096];
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
        buflen += rret;

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
    if (strcmp(method_str, "GET") == 0){
        //construimos ruta real: config.server_root + ruta perdida
        char real_path[512];
        //si pide "/", le damos el index.html
        if (strcmp(path_str, "/") == 0){
            sprintf(real_path, "%sindex.html", config.server_root);
        }else{
            sprintf(real_path, "%s%s", config.server_root, path_str + 1); // +1 para eliminar la barra inicial que ya tenemos en server_root
        }

        FILE *file = fopen(real_path, "rb"); //rb para leer binario (imágenes, etc)

        if(file){
            //obtenemos el tipo MIME correcto
            const char* content_type = obtener_content_type(real_path);
            //enviamos cabecera HTTP 200 OKç
            char response[512];
            sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nServer: %s\r\n\r\n", content_type, config.server_signature);
            send(fd, response, strlen(response), 0);

            //enviamos el contenido del archivo
            char filecontent[1024];
            int bytes;
            while((bytes = fread(filecontent, 1, sizeof(filecontent), file)) > 0){
                send(fd, filecontent, bytes, 0);
            }
            fclose(file);
        }else{
            //error 404 not found
            char response[512];
            sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nServer: %s\r\n\r\n<h1>404 Not found</h1>", config.server_signature);
            send(fd, response, strlen(response), 0);
        }
        //Aquí estarán el POST, PUT, etc en el futuro
        return 0;
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