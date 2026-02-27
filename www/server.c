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

typedef struct {
    char server_root[256];
    int max_clients;
    int listen_port;
    char server_signature[256];
}ServerConfig;

ServerConfig config;

#define MAXBUF		1024
char buffer[MAXBUF];

int procesarPeticion(int fd){
    //Procesa la petición http
    char filename[512];
    char *pos;
    FILE *file;
    int bytes;
    char filecontent[4096];
    
    recv(fd, buffer, MAXBUF, 0);
    printf("%s\n", buffer);
    
    // Extraemos el nombre del archivo de la petición
    // Formato: GET /archivo.html HTTP/1.1
    pos = strchr(buffer, '/');
    if(pos){
        sscanf(pos, "%s", filename);
        // Eliminamos el espacio y lo que viene después (HTTP/1.1)
        pos = strchr(filename, ' ');
        if(pos) *pos = '\0';
    }
    
    // Abrimos el archivo (sin la barra inicial)
    file = fopen(filename + 1, "r");
    
    if(file){
        // Archivo encontrado - enviamos respuesta 200 OK
        char response[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
        send(fd, response, strlen(response), 0);
        
        // Enviamos el contenido del archivo
        while((bytes = fread(filecontent, 1, sizeof(filecontent), file)) > 0){
            send(fd, filecontent, bytes, 0);
        }
        fclose(file);
    } else {
        // Archivo no encontrado - enviamos 404
        char response[] = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<h1>404 Not Found</h1>";
        send(fd, response, strlen(response), 0);
    }
    
    return 0;
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