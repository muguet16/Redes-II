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

#define SERVER_PORT		9999
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

int main(void)
{   int sockfd;
	struct sockaddr_in self;
	

	// Creamos el socket tipo TCP */
    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
	{
		perror("Error en la creación del socket");
		exit(errno);
	}

	// Inicializamos estructura de dirección y puerto
	bzero(&self, sizeof(self));
	self.sin_family = AF_INET;
	self.sin_port = htons(SERVER_PORT);
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

	while (1)
	{	int clientfd;
		struct sockaddr_in client_addr;
		int addrlen=sizeof(client_addr);

		// Aceptamos conexiones
		clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &addrlen);
		printf("Conexión desde [%s:%d]\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        if(fork()==0){
            //hijo procesamos la petición
            procesarPeticion(clientfd);
            exit(0);
        }

		// Cerramos la conexión
		close(clientfd);
	}

	close(sockfd);
	return 0;
}